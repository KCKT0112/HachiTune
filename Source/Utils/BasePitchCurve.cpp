#include "BasePitchCurve.h"
#include <algorithm>

// Local constants (to avoid JUCE dependency from Constants.h)
namespace {
    constexpr int SAMPLE_RATE = 44100;
    constexpr int HOP_SIZE = 512;
    constexpr int MIDI_A4 = 69;
    constexpr float FREQ_A4 = 440.0f;

    inline float midiToFreq(float midi) {
        return FREQ_A4 * std::pow(2.0f, (midi - MIDI_A4) / 12.0f);
    }

    inline float freqToMidi(float freq) {
        if (freq <= 0.0f) return 0.0f;
        return 12.0f * std::log2(freq / FREQ_A4) + MIDI_A4;
    }
}

std::vector<double> BasePitchCurve::createCosineKernel()
{
    std::vector<double> kernel(KERNEL_SIZE);
    double sum = 0.0;

    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        const double time = 0.001 * (i - KERNEL_SIZE / 2);  // Time offset in seconds
        kernel[i] = std::cos(M_PI * time / SMOOTH_WINDOW);
        sum += kernel[i];
    }

    // Normalize
    for (auto& value : kernel)
        value /= sum;

    return kernel;
}

std::vector<float> BasePitchCurve::generateForNote(int startFrame, int endFrame, float midiNote, int totalFrames)
{
    std::vector<NoteSegment> notes = {{startFrame, endFrame, midiNote}};
    return generateForNotes(notes, totalFrames);
}

std::vector<float> BasePitchCurve::generateForNotes(const std::vector<NoteSegment>& notes, int totalFrames)
{
    if (notes.empty() || totalFrames <= 0)
        return {};

    // Convert frames to milliseconds (at ~86 fps, each frame is ~11.6ms)
    // We'll work at 1ms resolution for smoothing, then resample
    double msPerFrame = 1000.0 * HOP_SIZE / SAMPLE_RATE;  // ~11.6ms

    // Calculate total duration in seconds, add padding for convolution kernel
    double lastNoteEndSec = notes.back().endFrame * msPerFrame / 1000.0;
    int totalMs = static_cast<int>(std::round(1000.0 * (lastNoteEndSec + SMOOTH_WINDOW))) + 1;

    // Create initial step function values at 1ms resolution
    // This matches ds-editor-lite's BasePitchCurve::Convolve algorithm
    std::vector<double> initValues(totalMs, 0.0);
    
    // Convert note segments from frames to seconds for step function
    struct NoteInSeconds {
        double start;
        double end;
        float midiNote;
    };
    std::vector<NoteInSeconds> noteArray;
    for (const auto& note : notes)
    {
        double startSec = note.startFrame * msPerFrame / 1000.0;
        double endSec = note.endFrame * msPerFrame / 1000.0;
        noteArray.push_back({startSec, endSec, note.midiNote});
    }
    
    if (noteArray.empty())
        return {};
    
    // Create step function: each millisecond gets the semitone value of its note
    // At note boundaries, switch at the midpoint (matching ds-editor-lite)
    int noteIndex = 0;
    for (int i = 0; i < totalMs; ++i)
    {
        const double time = 0.001 * i;  // Time in seconds
        
        // Assign current note's semitone value
        initValues[i] = noteArray[noteIndex].midiNote;
        
        // Check if we should advance to next note at midpoint
        // Switch at: 0.5 * (current_note_end + next_note_start)
        if (noteIndex < static_cast<int>(noteArray.size()) - 1 &&
            time > 0.5 * (noteArray[noteIndex].end + noteArray[noteIndex + 1].start))
        {
            noteIndex++;
        }
    }

    // Apply cosine kernel convolution
    auto kernel = createCosineKernel();
    std::vector<double> smoothedMs(totalMs, 0.0);

    for (int i = 0; i < totalMs; ++i)
    {
        for (int j = 0; j < KERNEL_SIZE; ++j)
        {
            int srcIdx = std::clamp(i - KERNEL_SIZE / 2 + j, 0, totalMs - 1);
            smoothedMs[i] += initValues[srcIdx] * kernel[j];
        }
    }

    // Resample back to frame resolution
    std::vector<float> result(totalFrames);
    for (int frame = 0; frame < totalFrames; ++frame)
    {
        double ms = frame * msPerFrame;
        int msIdx = static_cast<int>(ms);
        double frac = ms - msIdx;

        if (msIdx + 1 < totalMs)
            result[frame] = static_cast<float>(smoothedMs[msIdx] * (1.0 - frac) + smoothedMs[msIdx + 1] * frac);
        else if (msIdx < totalMs)
            result[frame] = static_cast<float>(smoothedMs[msIdx]);
        else
            result[frame] = static_cast<float>(smoothedMs.back());
    }

    return result;
}

std::vector<float> BasePitchCurve::calculateDeltaPitch(const std::vector<float>& f0Values,
                                                        const std::vector<float>& basePitch,
                                                        int startFrame)
{
    std::vector<float> deltaPitch(f0Values.size(), 0.0f);

    for (size_t i = 0; i < f0Values.size(); ++i)
    {
        int globalFrame = startFrame + static_cast<int>(i);
        if (globalFrame < 0 || globalFrame >= static_cast<int>(basePitch.size()))
            continue;

        float f0 = f0Values[i];
        if (f0 > 0.0f)
        {
            // Convert F0 to MIDI
            float f0Midi = freqToMidi(f0);
            // Delta = actual - base
            deltaPitch[i] = f0Midi - basePitch[globalFrame];
        }
    }

    return deltaPitch;
}

std::vector<float> BasePitchCurve::applyBasePitchChange(const std::vector<float>& deltaPitch,
                                                         float newBaseMidi,
                                                         int numFrames)
{
    std::vector<float> newF0(numFrames, 0.0f);

    for (int i = 0; i < numFrames && i < static_cast<int>(deltaPitch.size()); ++i)
    {
        // New MIDI = new base + delta
        float newMidi = newBaseMidi + deltaPitch[i];
        // Convert back to Hz
        newF0[i] = midiToFreq(newMidi);
    }

    return newF0;
}
