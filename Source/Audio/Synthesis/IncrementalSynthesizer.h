#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Project.h"
#include "../Vocoder.h"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/**
 * Handles audio synthesis for edited regions.
 * Uses vocoder to resynthesize dirty (modified) portions of audio.
 * Expands dirty region to nearest silence boundaries for clean cuts.
 */
class IncrementalSynthesizer {
public:
    using ProgressCallback = std::function<void(const juce::String& message)>;
    using CompleteCallback = std::function<void(bool success)>;

    IncrementalSynthesizer();
    ~IncrementalSynthesizer();

    void setVocoder(Vocoder* v) { vocoder = v; }
    void setProject(Project* p) { project = p; }

    /**
     * Synthesize the dirty region.
     * - Finds dirty frame range from project
     * - Expands to nearest silence boundaries
     * - Synthesizes entire region (no padding, no crossfade)
     * - Direct replacement of samples
     */
    void synthesizeRegion(ProgressCallback onProgress, CompleteCallback onComplete);

    // Cancel ongoing synthesis
    void cancel();

    // Check if synthesis is in progress
    bool isSynthesizing() const { return isBusy.load(); }

private:
    /**
     * Expand dirty range to nearest silence boundaries.
     * Searches backwards and forwards to find silence gaps (>= 5 frames).
     */
    std::pair<int, int> expandToSilenceBoundaries(int dirtyStart, int dirtyEnd);

    Vocoder* vocoder = nullptr;
    Project* project = nullptr;

    std::shared_ptr<std::atomic<bool>> cancelFlag;
    std::atomic<uint64_t> jobId{0};
    std::atomic<bool> isBusy{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IncrementalSynthesizer)
};
