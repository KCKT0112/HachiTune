#include "PitchCurveProcessor.h"
#include "BasePitchCurve.h"
#include "CurveResampler.h"
#include "PitchToolOperations.h"
#include "../Utils/Constants.h"
#include <algorithm>
#include <cmath>

namespace
{
    inline float safeFreqToMidi(float freq)
    {
        if (freq <= 0.0f)
            return 0.0f;
        return freqToMidi(freq);
    }

    void ensureSizes(AudioData& audioData, int totalFrames)
    {
        if (totalFrames <= 0)
            return;

        if (audioData.basePitch.size() != static_cast<size_t>(totalFrames))
            audioData.basePitch.assign(static_cast<size_t>(totalFrames), 0.0f);
        if (audioData.deltaPitch.size() != static_cast<size_t>(totalFrames))
            audioData.deltaPitch.assign(static_cast<size_t>(totalFrames), 0.0f);
    }

    std::vector<float> getNoteSourceDelta(const Note& note)
    {
        const auto& rawSourceData = note.hasOriginalDeltaPitch()
            ? note.getOriginalDeltaPitch()
            : note.getDeltaPitch();
        if (rawSourceData.empty())
            return {};

        const int numFrames = note.getDurationFrames();
        if (numFrames <= 0)
            return {};

        if (static_cast<int>(rawSourceData.size()) == numFrames)
            return rawSourceData;

        return CurveResampler::resampleLinear(rawSourceData, numFrames);
    }

    float clamp01(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    float lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    float evaluateCubicBezier1D(float p0,
                                float p1,
                                float p2,
                                float p3,
                                float t)
    {
        const float oneMinusT = 1.0f - t;
        return p0 * oneMinusT * oneMinusT * oneMinusT +
               3.0f * p1 * oneMinusT * oneMinusT * t +
               3.0f * p2 * oneMinusT * t * t +
               p3 * t * t * t;
    }

    float solveCubicBezierTForX(float controlX1,
                                float controlX2,
                                float endX,
                                float x)
    {
        if (endX <= 0.0f)
            return 0.0f;

        const float clampedX = juce::jlimit(0.0f, endX, x);
        float lower = 0.0f;
        float upper = 1.0f;

        for (int i = 0; i < 18; ++i)
        {
            const float mid = 0.5f * (lower + upper);
            const float midX = evaluateCubicBezier1D(
                0.0f, controlX1, controlX2, endX, mid);
            if (midX < clampedX)
                lower = mid;
            else
                upper = mid;
        }

        return 0.5f * (lower + upper);
    }

    float evaluateBoundaryEaseBezier(float startValue,
                                     float endValue,
                                     float boundaryX,
                                     float endX,
                                     float x)
    {
        const float clampedBoundaryX = juce::jlimit(0.0f, endX, boundaryX);
        const float t = solveCubicBezierTForX(
            clampedBoundaryX, clampedBoundaryX, endX, x);

        // Using repeated start/end Y control points gives a zero-slope ease
        // at both note-side anchors, which is closer to Melodyne's smoothing
        // shape than a simple quadratic fall.
        return evaluateCubicBezier1D(
            startValue, startValue, endValue, endValue, t);
    }

    float getNoteCenterMidi(const Note& note)
    {
        // Boundary smoothing is defined against the note center line visible
        // to the user, not the globally smoothed dense base-pitch array.
        return note.getAdjustedMidiNote();
    }

    struct BoundarySmoothingSegment
    {
        std::vector<int> frames;
        std::vector<float> idealMidiValues;
        std::vector<float> weights;
    };

    std::vector<float> composeRawDeltaFromNotes(const Project& project,
                                                const std::vector<float>& basePitch,
                                                int totalFrames)
    {
        std::vector<float> denseDelta(static_cast<size_t>(totalFrames), 0.0f);

        for (const auto& note : project.getNotes())
        {
            if (note.isRest())
                continue;

            const int startFrame = note.getStartFrame();
            const auto sourceData = getNoteSourceDelta(note);
            const int numFrames = static_cast<int>(sourceData.size());
            if (numFrames <= 0)
                continue;

            auto transformedDelta = PitchToolOperations::applyAllTransformations(
                sourceData,
                note.getTiltLeft(),
                note.getTiltRight(),
                note.getVarianceScale());

            const float dScale = note.getDeltaScale();
            const float dOffset = note.getDeltaOffset();
            if (std::abs(dScale - 1.0f) > 0.0001f ||
                std::abs(dOffset) > 0.0001f)
            {
                for (auto& value : transformedDelta)
                    value = value * dScale + dOffset;
            }

            for (int i = 0; i < numFrames &&
                            i < static_cast<int>(transformedDelta.size()); ++i)
            {
                const int globalFrame = startFrame + i;
                if (globalFrame >= 0 && globalFrame < totalFrames)
                {
                    denseDelta[static_cast<size_t>(globalFrame)] =
                        transformedDelta[static_cast<size_t>(i)];
                }
            }
        }

        return denseDelta;
    }

    std::vector<BoundarySmoothingSegment> buildBoundarySmoothingSegments(
        const Project& project,
        const std::vector<float>& basePitch)
    {
        const int totalFrames = static_cast<int>(basePitch.size());
        if (totalFrames <= 0)
        {
            return {};
        }

        std::vector<const Note*> sortedNotes;
        sortedNotes.reserve(project.getNotes().size());
        for (const auto& note : project.getNotes())
        {
            if (!note.isRest())
                sortedNotes.push_back(&note);
        }

        std::sort(sortedNotes.begin(), sortedNotes.end(),
                  [](const Note* a, const Note* b)
                  {
                      if (a->getStartFrame() != b->getStartFrame())
                          return a->getStartFrame() < b->getStartFrame();
                      return a->getEndFrame() < b->getEndFrame();
                  });

        if (sortedNotes.size() < 2)
            return {};

        std::vector<BoundarySmoothingSegment> segments;

        for (size_t i = 0; i + 1 < sortedNotes.size(); ++i)
        {
            const Note& leftNote = *sortedNotes[i];
            const Note& rightNote = *sortedNotes[i + 1];

            const int sharedFrames =
                std::max(leftNote.getSmoothRightFrames(),
                         rightNote.getSmoothLeftFrames());
            if (sharedFrames <= 0)
                continue;

            const int leftExtent =
                std::min(sharedFrames, leftNote.getDurationFrames());
            const int rightExtent =
                std::min(sharedFrames, rightNote.getDurationFrames());
            if (leftExtent <= 0 || rightExtent <= 0)
                continue;

            const int leftStartFrame = leftNote.getEndFrame() - leftExtent;
            const int leftBoundaryFrame = leftNote.getEndFrame() - 1;
            const int rightBoundaryFrame = rightNote.getStartFrame();
            const int rightEndFrame =
                rightNote.getStartFrame() + rightExtent - 1;

            if (leftStartFrame < 0 || rightEndFrame >= totalFrames ||
                leftBoundaryFrame < 0 || rightBoundaryFrame >= totalFrames)
            {
                continue;
            }

            const int spanFrames = leftExtent + rightExtent;
            if (spanFrames < 2)
                continue;

            // The ideal connection should slide on the note center line, so
            // max smoothing still anchors to the note's own base pitch instead
            // of inheriting neighboring transition curvature from basePitch.
            const float leftCenterMidi = getNoteCenterMidi(leftNote);
            const float rightCenterMidi = getNoteCenterMidi(rightNote);
            const float startMidi = leftCenterMidi;
            const float endMidi = rightCenterMidi;
            const float controlX =
                static_cast<float>(leftExtent) - 0.5f;
            const float endX =
                static_cast<float>(spanFrames - 1);

            BoundarySmoothingSegment segment;
            segment.frames.reserve(static_cast<size_t>(spanFrames));
            segment.idealMidiValues.reserve(static_cast<size_t>(spanFrames));
            segment.weights.reserve(static_cast<size_t>(spanFrames));

            for (int localFrame = 0; localFrame < spanFrames; ++localFrame)
            {
                const int globalFrame = localFrame < leftExtent
                    ? leftStartFrame + localFrame
                    : rightBoundaryFrame + (localFrame - leftExtent);
                if (globalFrame < 0 || globalFrame >= totalFrames)
                    continue;

                const float idealMidi = evaluateBoundaryEaseBezier(
                    startMidi,
                    endMidi,
                    controlX,
                    endX,
                    static_cast<float>(localFrame));

                float weight = 0.0f;
                if (localFrame < leftExtent)
                {
                    weight = leftExtent <= 1
                        ? 1.0f
                        : static_cast<float>(localFrame) /
                              static_cast<float>(leftExtent - 1);
                }
                else
                {
                    const int rightLocal = localFrame - leftExtent;
                    weight = rightExtent <= 1
                        ? 1.0f
                        : 1.0f - static_cast<float>(rightLocal) /
                                     static_cast<float>(rightExtent - 1);
                }

                weight = clamp01(weight);
                if (weight <= 0.0f)
                    continue;

                segment.frames.push_back(globalFrame);
                segment.idealMidiValues.push_back(idealMidi);
                segment.weights.push_back(weight);
            }

            if (!segment.frames.empty())
                segments.push_back(std::move(segment));
        }

        return segments;
    }

    void applyBoundarySmoothing(Project& project,
                                const std::vector<float>& rawDelta)
    {
        auto& audioData = project.getAudioData();
        const int totalFrames = audioData.getNumFrames();
        if (totalFrames <= 0 ||
            static_cast<int>(rawDelta.size()) != totalFrames)
        {
            return;
        }

        audioData.deltaPitch = rawDelta;

        const auto segments = buildBoundarySmoothingSegments(
            project, audioData.basePitch);
        if (segments.empty())
            return;

        std::vector<float> idealDeltaSum(static_cast<size_t>(totalFrames), 0.0f);
        std::vector<float> weightSum(static_cast<size_t>(totalFrames), 0.0f);

        for (const auto& segment : segments)
        {
            for (size_t i = 0; i < segment.frames.size(); ++i)
            {
                const int frame = segment.frames[i];
                const float idealDelta =
                    segment.idealMidiValues[i] -
                    audioData.basePitch[static_cast<size_t>(frame)];
                const float weight = segment.weights[i];
                idealDeltaSum[static_cast<size_t>(frame)] +=
                    idealDelta * weight;
                weightSum[static_cast<size_t>(frame)] += weight;
            }
        }

        for (int frame = 0; frame < totalFrames; ++frame)
        {
            const float weight = weightSum[static_cast<size_t>(frame)];
            if (weight <= 0.0f)
                continue;

            const float idealDelta =
                idealDeltaSum[static_cast<size_t>(frame)] / weight;
            const float blend = std::min(weight, 1.0f);
            audioData.deltaPitch[static_cast<size_t>(frame)] =
                lerp(rawDelta[static_cast<size_t>(frame)],
                     idealDelta,
                     blend);
        }
    }

    std::vector<BasePitchCurve::NoteSegment> collectNoteSegments(const std::vector<Note>& notes)
    {
        std::vector<BasePitchCurve::NoteSegment> segments;
        segments.reserve(notes.size());

        for (const auto& note : notes)
        {
            if (note.isRest())
                continue;

            BasePitchCurve::NoteSegment seg;
            seg.startFrame = note.getStartFrame();
            seg.endFrame = note.getEndFrame();
            // Base pitch already includes per-note offset
            seg.midiNote = note.getMidiNote() + note.getPitchOffset()
                         - (note.getTiltLeft() + note.getTiltRight()) / 2.0f;
            segments.push_back(seg);
        }

        // Ensure segments are sorted by start frame for stable generation
        std::sort(segments.begin(), segments.end(),
                  [](const auto& a, const auto& b) { return a.startFrame < b.startFrame; });
        return segments;
    }
} // namespace

namespace PitchCurveProcessor
{
    std::vector<Note*> collectDependentNotes(Project& project,
                                             const std::vector<Note*>& seedNotes)
    {
        std::vector<Note*> dependentNotes;
        auto& allNotes = project.getNotes();

        auto addUnique = [&dependentNotes](Note* note)
        {
            if (!note || note->isRest())
                return;

            if (std::find(dependentNotes.begin(), dependentNotes.end(), note) ==
                dependentNotes.end())
            {
                dependentNotes.push_back(note);
            }
        };

        for (auto* seedNote : seedNotes)
        {
            if (!seedNote || seedNote->isRest())
                continue;

            addUnique(seedNote);

            auto it = std::find_if(
                allNotes.begin(), allNotes.end(),
                [seedNote](const Note& candidate)
                {
                    return &candidate == seedNote;
                });
            if (it == allNotes.end())
                continue;

            auto prevIt = it;
            while (prevIt != allNotes.begin())
            {
                --prevIt;
                if (!prevIt->isRest())
                {
                    addUnique(&*prevIt);
                    break;
                }
            }

            auto nextIt = it;
            ++nextIt;
            while (nextIt != allNotes.end())
            {
                if (!nextIt->isRest())
                {
                    addUnique(&*nextIt);
                    break;
                }
                ++nextIt;
            }
        }

        return dependentNotes;
    }

    std::vector<SmoothingDebugSegment> collectIdealSmoothingDebugSegments(
        const Project& project)
    {
        const auto& audioData = project.getAudioData();
        const int totalFrames = audioData.getNumFrames();
        if (totalFrames <= 0 ||
            static_cast<int>(audioData.basePitch.size()) != totalFrames)
        {
            return {};
        }

        const auto segments = buildBoundarySmoothingSegments(
            project, audioData.basePitch);

        std::vector<SmoothingDebugSegment> debugSegments;
        debugSegments.reserve(segments.size());
        for (const auto& segment : segments)
        {
            SmoothingDebugSegment debugSegment;
            debugSegment.frames = segment.frames;
            debugSegment.idealMidiValues = segment.idealMidiValues;
            debugSegments.push_back(std::move(debugSegment));
        }
        return debugSegments;
    }

    std::vector<float> interpolateWithUvMask(const std::vector<float>& pitchHz,
                                             const std::vector<bool>& uvMask)
    {
        if (pitchHz.empty())
            return {};

        const int n = static_cast<int>(pitchHz.size());
        std::vector<float> dense(pitchHz);

        auto isVoicedFrame = [&](int i) -> bool {
            if (i < 0 || i >= n)
                return false;

            const bool hasPitch = pitchHz[static_cast<size_t>(i)] > 0.0f;
            if (!hasPitch)
                return false;

            // If uvMask is provided, respect it (out-of-range treated as unvoiced).
            // If uvMask is missing, fall back to pitch presence.
            if (!uvMask.empty())
                return i < static_cast<int>(uvMask.size()) && uvMask[static_cast<size_t>(i)];
            return true;
        };

        int nextVoiced = -1;
        auto findNext = [&](int idx) -> int {
            for (int i = idx; i < n; ++i)
            {
                if (isVoicedFrame(i))
                    return i;
            }
            return -1;
        };

        int lastVoiced = -1;
        nextVoiced = findNext(0);

        for (int i = 0; i < n; ++i)
        {
            const bool voiced = isVoicedFrame(i);
            if (voiced)
            {
                lastVoiced = i;
                if (i == nextVoiced)
                    nextVoiced = findNext(i + 1);
                continue;
            }

            // Update next voiced lazily
            if (nextVoiced != -1 && nextVoiced < i)
                nextVoiced = findNext(i + 1);

            float prevVal = (lastVoiced >= 0) ? dense[static_cast<size_t>(lastVoiced)] : 0.0f;
            float nextVal = (nextVoiced >= 0) ? dense[static_cast<size_t>(nextVoiced)] : 0.0f;

            if (prevVal <= 0.0f && nextVal <= 0.0f)
            {
                dense[static_cast<size_t>(i)] = 0.0f;
                continue;
            }

            if (prevVal <= 0.0f)
            {
                dense[static_cast<size_t>(i)] = nextVal;
                continue;
            }
            if (nextVal <= 0.0f)
            {
                dense[static_cast<size_t>(i)] = prevVal;
                continue;
            }

            const float t = (nextVoiced > i) ? static_cast<float>(i - lastVoiced) /
                                               static_cast<float>(nextVoiced - lastVoiced)
                                             : 0.0f;
            const float logA = std::log(prevVal);
            const float logB = std::log(nextVal);
            dense[static_cast<size_t>(i)] = std::exp(logA * (1.0f - t) + logB * t);
        }

        return dense;
    }

    void rebuildCurvesFromSource(Project& project,
                                 const std::vector<float>& sourcePitchHz)
    {
        auto& audioData = project.getAudioData();
        const int totalFrames = static_cast<int>(sourcePitchHz.size());
        ensureSizes(audioData, totalFrames);

        // Ensure delta is built from a dense F0 source that includes UV
        // head/tail fill (same behavior expectation as F0 interpolation).
        std::vector<float> denseSource = sourcePitchHz;
        if (!audioData.voicedMask.empty() &&
            audioData.voicedMask.size() == sourcePitchHz.size())
        {
            denseSource = interpolateWithUvMask(sourcePitchHz, audioData.voicedMask);
        }

        auto segments = collectNoteSegments(project.getNotes());
        if (!segments.empty())
        {
            audioData.basePitch = BasePitchCurve::generateForNotes(segments, totalFrames);
        }

        if (audioData.basePitch.size() != static_cast<size_t>(totalFrames))
        {
            // Fallback: derive base from source pitch directly
            audioData.basePitch.assign(static_cast<size_t>(totalFrames), 0.0f);
            for (int i = 0; i < totalFrames; ++i)
                audioData.basePitch[static_cast<size_t>(i)] = safeFreqToMidi(denseSource[i]);
        }

        // Dense delta: midi(source) - base
        audioData.deltaPitch.assign(static_cast<size_t>(totalFrames), 0.0f);
        for (int i = 0; i < totalFrames; ++i)
        {
            const float base = audioData.basePitch[static_cast<size_t>(i)];
            const float midi = safeFreqToMidi(denseSource[i]);
            audioData.deltaPitch[static_cast<size_t>(i)] = midi - base;
        }

        // Initialize originalDeltaPitch in each note from computed deltaPitch
        // This preserves the pristine pitch curve for non-destructive transformations
        for (auto& note : project.getNotes())
        {
            if (note.isRest()) continue;

            const int startFrame = note.getStartFrame();
            const int endFrame = note.getEndFrame();
            const int numFrames = endFrame - startFrame;

            if (numFrames <= 0) continue;

            std::vector<float> origDelta(static_cast<size_t>(numFrames));
            for (int i = 0; i < numFrames; ++i)
            {
                const int globalIdx = startFrame + i;
                if (globalIdx >= 0 && globalIdx < totalFrames)
                    origDelta[static_cast<size_t>(i)] = audioData.deltaPitch[static_cast<size_t>(globalIdx)];
            }
            note.setOriginalDeltaPitch(std::move(origDelta));
        }

        // Cache base F0 (Hz) for backwards compatibility
        audioData.baseF0.resize(static_cast<size_t>(totalFrames));
        for (int i = 0; i < totalFrames; ++i)
            audioData.baseF0[static_cast<size_t>(i)] = midiToFreq(audioData.basePitch[static_cast<size_t>(i)]);

        composeF0InPlace(project, /*applyUvMask=*/false);
    }

    void rebuildBaseFromNotes(Project& project)
    {
        auto& audioData = project.getAudioData();
        const int totalFrames = audioData.getNumFrames();
        ensureSizes(audioData, totalFrames);

        auto segments = collectNoteSegments(project.getNotes());
        if (!segments.empty())
        {
            audioData.basePitch = BasePitchCurve::generateForNotes(segments, totalFrames);
        }

        if (audioData.basePitch.size() != static_cast<size_t>(totalFrames))
        {
            audioData.basePitch.assign(static_cast<size_t>(totalFrames), 0.0f);
        }

        const auto rawDelta = composeRawDeltaFromNotes(
            project, audioData.basePitch, totalFrames);
        applyBoundarySmoothing(project, rawDelta);

        // Update cached baseF0
        audioData.baseF0.resize(static_cast<size_t>(totalFrames));
        for (int i = 0; i < totalFrames; ++i)
            audioData.baseF0[static_cast<size_t>(i)] = midiToFreq(audioData.basePitch[static_cast<size_t>(i)]);

        composeF0InPlace(project, /*applyUvMask=*/false);
    }

    void rebuildBaseFromNotesForDrag(Project& project, const std::vector<Note*>& affectedNotes)
    {
        (void) affectedNotes;
        rebuildBaseFromNotes(project);
    }

    void rebuildDeltaForNotes(Project& project, const std::vector<Note*>& affectedNotes)
    {
        (void) affectedNotes;
        rebuildBaseFromNotes(project);
    }

    std::vector<float> composeF0(const Project& project,
                                 bool applyUvMask,
                                 float globalPitchOffset)
    {
        const auto& audioData = project.getAudioData();
        const int totalFrames = static_cast<int>(audioData.basePitch.size());
        std::vector<float> result(static_cast<size_t>(totalFrames), 0.0f);

        for (int i = 0; i < totalFrames; ++i)
        {
            bool isVoiced = (i < static_cast<int>(audioData.voicedMask.size())) ? audioData.voicedMask[i] : true;
            if (applyUvMask && !isVoiced)
                continue;

            const float base = audioData.basePitch[static_cast<size_t>(i)];
            const float delta = (i < static_cast<int>(audioData.deltaPitch.size()))
                                    ? audioData.deltaPitch[static_cast<size_t>(i)]
                                    : 0.0f;
            const float midi = base + delta + globalPitchOffset;
            result[static_cast<size_t>(i)] = midiToFreq(midi);
        }

        return result;
    }

    void composeF0InPlace(Project& project,
                          bool applyUvMask,
                          float globalPitchOffset)
    {
        auto composed = composeF0(project, applyUvMask, globalPitchOffset);
        auto& audioData = project.getAudioData();
        audioData.f0 = std::move(composed);
    }
} // namespace PitchCurveProcessor
