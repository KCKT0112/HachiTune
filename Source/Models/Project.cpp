#include "Project.h"
#include "../Utils/Constants.h"
#include "../Utils/PitchCurveProcessor.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float twoPi = 6.2831853071795864769f;
}

Project::Project()
{
}

bool Project::saveToFile(const juce::File& file) const
{
    auto xml = toXml();
    if (!xml)
        return false;
    return xml->writeTo(file);
}

std::unique_ptr<juce::XmlElement> Project::toXml() const
{
    auto root = std::make_unique<juce::XmlElement>("HachiTuneProject");
    root->setAttribute("version", 1);
    root->setAttribute("name", name);
    root->setAttribute("audioPath", filePath.getFullPathName());
    root->setAttribute("sampleRate", audioData.sampleRate);
    root->setAttribute("globalPitchOffset", globalPitchOffset);
    root->setAttribute("formantShift", formantShift);
    root->setAttribute("volume", volume);

    // Notes
    auto* notesElem = root->createNewChildElement("Notes");
    for (const auto& note : notes)
    {
        auto* n = notesElem->createNewChildElement("Note");
        n->setAttribute("startFrame", note.getStartFrame());
        n->setAttribute("endFrame", note.getEndFrame());
        n->setAttribute("midiNote", note.getMidiNote());
        n->setAttribute("pitchOffset", note.getPitchOffset());

        n->setAttribute("vibratoEnabled", note.isVibratoEnabled() ? 1 : 0);
        n->setAttribute("vibratoRateHz", note.getVibratoRateHz());
        n->setAttribute("vibratoDepthSemitones", note.getVibratoDepthSemitones());
        n->setAttribute("vibratoPhaseRadians", note.getVibratoPhaseRadians());

        // Lyric and phoneme
        if (note.hasLyric())
            n->setAttribute("lyric", note.getLyric());
        if (note.hasPhoneme())
            n->setAttribute("phoneme", note.getPhoneme());
    }

    // F0
    auto* f0Elem = root->createNewChildElement("F0");
    {
        juce::StringArray parts;
        parts.ensureStorageAllocated(static_cast<int>(audioData.f0.size()));
        for (float v : audioData.f0)
            parts.add(juce::String(v, 6));
        f0Elem->addTextElement(parts.joinIntoString(" "));
    }

    // BasePitch (MIDI)
    auto* baseElem = root->createNewChildElement("BasePitch");
    {
        juce::StringArray parts;
        parts.ensureStorageAllocated(static_cast<int>(audioData.basePitch.size()));
        for (float v : audioData.basePitch)
            parts.add(juce::String(v, 6));
        baseElem->addTextElement(parts.joinIntoString(" "));
    }

    // DeltaPitch (MIDI)
    auto* deltaElem = root->createNewChildElement("DeltaPitch");
    {
        juce::StringArray parts;
        parts.ensureStorageAllocated(static_cast<int>(audioData.deltaPitch.size()));
        for (float v : audioData.deltaPitch)
            parts.add(juce::String(v, 6));
        deltaElem->addTextElement(parts.joinIntoString(" "));
    }

    // VoicedMask
    auto* voicedElem = root->createNewChildElement("VoicedMask");
    {
        juce::String mask;
        mask.preallocateBytes(static_cast<size_t>(audioData.voicedMask.size()));
        for (bool b : audioData.voicedMask)
            mask << (b ? '1' : '0');
        voicedElem->addTextElement(mask);
    }

    return root;
}

bool Project::fromXml(const juce::XmlElement& xml)
{
    // Support both old and new project format names for backwards compatibility
    if (xml.getTagName() != "HachiTuneProject" && xml.getTagName() != "PitchEditorProject")
        return false;

    name = xml.getStringAttribute("name", "Untitled");
    filePath = juce::File(xml.getStringAttribute("audioPath"));
    audioData.sampleRate = xml.getIntAttribute("sampleRate", 44100);
    globalPitchOffset = static_cast<float>(xml.getDoubleAttribute("globalPitchOffset", 0.0));
    formantShift = static_cast<float>(xml.getDoubleAttribute("formantShift", 0.0));
    volume = static_cast<float>(xml.getDoubleAttribute("volume", 0.0));

    // Notes
    notes.clear();
    if (auto* notesElem = xml.getChildByName("Notes"))
    {
        for (auto* n = notesElem->getFirstChildElement(); n != nullptr; n = n->getNextElement())
        {
            if (n->getTagName() == "Note")
            {
                Note note;
                note.setStartFrame(n->getIntAttribute("startFrame", 0));
                note.setEndFrame(n->getIntAttribute("endFrame", 0));
                note.setMidiNote(static_cast<float>(n->getDoubleAttribute("midiNote", 60.0)));
                note.setPitchOffset(static_cast<float>(n->getDoubleAttribute("pitchOffset", 0.0)));
                note.setVibratoEnabled(n->getIntAttribute("vibratoEnabled", 0) != 0);
                note.setVibratoRateHz(static_cast<float>(n->getDoubleAttribute("vibratoRateHz", 5.0)));
                note.setVibratoDepthSemitones(static_cast<float>(n->getDoubleAttribute("vibratoDepthSemitones", 0.0)));
                note.setVibratoPhaseRadians(static_cast<float>(n->getDoubleAttribute("vibratoPhaseRadians", 0.0)));
                notes.push_back(std::move(note));
            }
        }
    }

    // F0
    audioData.f0.clear();
    if (auto* f0Elem = xml.getChildByName("F0"))
    {
        juce::String f0Text = f0Elem->getAllSubText();
        juce::StringArray parts;
        parts.addTokens(f0Text, " ", "");
        audioData.f0.reserve(static_cast<size_t>(parts.size()));
        for (const auto& p : parts)
        {
            if (p.isNotEmpty())
                audioData.f0.push_back(p.getFloatValue());
        }
        // Initialize baseF0 from loaded f0
        audioData.baseF0 = audioData.f0;
    }

    // BasePitch (MIDI)
    audioData.basePitch.clear();
    if (auto* baseElem = xml.getChildByName("BasePitch"))
    {
        juce::String baseText = baseElem->getAllSubText();
        juce::StringArray parts;
        parts.addTokens(baseText, " ", "");
        audioData.basePitch.reserve(static_cast<size_t>(parts.size()));
        for (const auto& p : parts)
        {
            if (p.isNotEmpty())
                audioData.basePitch.push_back(p.getFloatValue());
        }
    }

    // DeltaPitch (MIDI)
    audioData.deltaPitch.clear();
    if (auto* deltaElem = xml.getChildByName("DeltaPitch"))
    {
        juce::String deltaText = deltaElem->getAllSubText();
        juce::StringArray parts;
        parts.addTokens(deltaText, " ", "");
        audioData.deltaPitch.reserve(static_cast<size_t>(parts.size()));
        for (const auto& p : parts)
        {
            if (p.isNotEmpty())
                audioData.deltaPitch.push_back(p.getFloatValue());
        }
    }

    // VoicedMask
    audioData.voicedMask.clear();
    if (auto* voicedElem = xml.getChildByName("VoicedMask"))
    {
        juce::String mask = voicedElem->getAllSubText();
        audioData.voicedMask.reserve(static_cast<size_t>(mask.length()));
        for (int i = 0; i < mask.length(); ++i)
            audioData.voicedMask.push_back(mask[i] == '1');
    }

    // Build dense curves if missing or misaligned
    const bool needsCurveRebuild = audioData.basePitch.empty() ||
                                   audioData.deltaPitch.empty() ||
                                   audioData.basePitch.size() != audioData.f0.size() ||
                                   audioData.deltaPitch.size() != audioData.f0.size();

    if (needsCurveRebuild && !audioData.f0.empty())
    {
        auto dense = PitchCurveProcessor::interpolateWithUvMask(audioData.f0, audioData.voicedMask);
        audioData.f0 = dense;
        PitchCurveProcessor::rebuildCurvesFromSource(*this, audioData.f0);
    }
    else if (!audioData.basePitch.empty() && !audioData.deltaPitch.empty() && audioData.f0.empty())
    {
        // Compose f0 if only curves were stored
        PitchCurveProcessor::composeF0InPlace(*this, /*applyUvMask=*/false);
    }

    modified = false;
    return true;
}

Note* Project::getNoteAtFrame(int frame)
{
    for (auto& note : notes)
    {
        if (note.containsFrame(frame))
            return &note;
    }
    return nullptr;
}

std::vector<Note*> Project::getNotesInRange(int startFrame, int endFrame)
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.getStartFrame() < endFrame && note.getEndFrame() > startFrame)
            result.push_back(&note);
    }
    return result;
}

std::vector<Note*> Project::getSelectedNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isSelected())
            result.push_back(&note);
    }
    return result;
}

bool Project::removeNoteByStartFrame(int startFrame)
{
    for (auto it = notes.begin(); it != notes.end(); ++it)
    {
        if (it->getStartFrame() == startFrame)
        {
            notes.erase(it);
            return true;
        }
    }
    return false;
}

void Project::deselectAllNotes()
{
    for (auto& note : notes)
        note.setSelected(false);
}

std::vector<Note*> Project::getDirtyNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isDirty())
            result.push_back(&note);
    }
    return result;
}

void Project::clearAllDirty()
{
    for (auto& note : notes)
        note.clearDirty();
    // Also clear F0 dirty range
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
}

bool Project::hasDirtyNotes() const
{
    for (const auto& note : notes)
    {
        if (note.isDirty())
            return true;
    }
    return false;
}

void Project::setF0DirtyRange(int startFrame, int endFrame)
{
    if (f0DirtyStart < 0 || startFrame < f0DirtyStart)
        f0DirtyStart = startFrame;
    if (f0DirtyEnd < 0 || endFrame > f0DirtyEnd)
        f0DirtyEnd = endFrame;
}

void Project::clearF0DirtyRange()
{
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
}

bool Project::hasF0DirtyRange() const
{
    return f0DirtyStart >= 0 && f0DirtyEnd >= 0;
}

std::pair<int, int> Project::getF0DirtyRange() const
{
    return {f0DirtyStart, f0DirtyEnd};
}

std::pair<int, int> Project::getDirtyFrameRange() const
{
    int minStart = -1;
    int maxEnd = -1;
    
    // Check dirty notes
    for (const auto& note : notes)
    {
        if (note.isDirty())
        {
            if (minStart < 0 || note.getStartFrame() < minStart)
                minStart = note.getStartFrame();
            if (maxEnd < 0 || note.getEndFrame() > maxEnd)
                maxEnd = note.getEndFrame();
        }
    }
    
    // Also include F0 dirty range from Draw mode edits
    if (f0DirtyStart >= 0)
    {
        if (minStart < 0 || f0DirtyStart < minStart)
            minStart = f0DirtyStart;
    }
    if (f0DirtyEnd >= 0)
    {
        if (maxEnd < 0 || f0DirtyEnd > maxEnd)
            maxEnd = f0DirtyEnd;
    }
    
    return {minStart, maxEnd};
}

std::vector<float> Project::getAdjustedF0() const
{
    if (audioData.basePitch.empty() || audioData.deltaPitch.empty())
        return {};

    // Compose base + delta with UV applied and global offset
    std::vector<float> adjustedF0 = PitchCurveProcessor::composeF0(*this,
                                                                   /*applyUvMask=*/true,
                                                                   globalPitchOffset);

    // Apply vibrato per note on top of composed curve
    for (const auto& note : notes)
    {
        const bool hasVibrato = note.isVibratoEnabled() &&
                                note.getVibratoDepthSemitones() > 0.0001f &&
                                note.getVibratoRateHz() > 0.0001f;
        if (!hasVibrato)
            continue;

        const int start = std::max(0, note.getStartFrame());
        const int end = std::min(note.getEndFrame(), static_cast<int>(adjustedF0.size()));

        for (int i = start; i < end; ++i)
        {
            if (i < static_cast<int>(audioData.voicedMask.size()) && !audioData.voicedMask[i])
                continue;

            float vib = note.getVibratoDepthSemitones() *
                        std::sin(twoPi * note.getVibratoRateHz() * framesToSeconds(i - start) +
                                 note.getVibratoPhaseRadians());
            adjustedF0[static_cast<size_t>(i)] *= std::pow(2.0f, vib / 12.0f);
        }
    }

    return adjustedF0;
}

std::vector<float> Project::getAdjustedF0ForRange(int startFrame, int endFrame) const
{
    if (audioData.basePitch.empty() || audioData.deltaPitch.empty())
        return {};

    // Clamp range
    startFrame = std::max(0, startFrame);
    endFrame = std::min(endFrame, static_cast<int>(audioData.basePitch.size()));

    if (startFrame >= endFrame)
        return {};

    const int rangeSize = endFrame - startFrame;
    std::vector<float> adjustedF0(static_cast<size_t>(rangeSize), 0.0f);

    for (int i = 0; i < rangeSize; ++i)
    {
        const int globalIdx = startFrame + i;
        const float base = audioData.basePitch[static_cast<size_t>(globalIdx)];
        const float delta = (globalIdx < static_cast<int>(audioData.deltaPitch.size()))
                                ? audioData.deltaPitch[static_cast<size_t>(globalIdx)]
                                : 0.0f;
        float midi = base + delta + globalPitchOffset;
        float freq = midiToFreq(midi);
        if (globalIdx < static_cast<int>(audioData.voicedMask.size()) && !audioData.voicedMask[globalIdx])
            freq = 0.0f;
        adjustedF0[static_cast<size_t>(i)] = freq;
    }

    // Apply vibrato for overlapping notes
    for (const auto& note : notes)
    {
        const bool hasVibrato = note.isVibratoEnabled() &&
                                note.getVibratoDepthSemitones() > 0.0001f &&
                                note.getVibratoRateHz() > 0.0001f;
        if (!hasVibrato)
            continue;

        const int overlapStart = std::max(note.getStartFrame(), startFrame);
        const int overlapEnd = std::min(note.getEndFrame(), endFrame);
        for (int frame = overlapStart; frame < overlapEnd; ++frame)
        {
            const int localIdx = frame - startFrame;
            if (frame < static_cast<int>(audioData.voicedMask.size()) && !audioData.voicedMask[frame])
                continue;

            float vib = note.getVibratoDepthSemitones() *
                        std::sin(twoPi * note.getVibratoRateHz() * framesToSeconds(frame - note.getStartFrame()) +
                                 note.getVibratoPhaseRadians());
            adjustedF0[static_cast<size_t>(localIdx)] *= std::pow(2.0f, vib / 12.0f);
        }
    }

    return adjustedF0;
}
