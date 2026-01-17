#include "IncrementalSynthesizer.h"

IncrementalSynthesizer::IncrementalSynthesizer() = default;

IncrementalSynthesizer::~IncrementalSynthesizer() {
    cancel();
}

void IncrementalSynthesizer::cancel() {
    if (cancelFlag)
        cancelFlag->store(true);
}

std::pair<int, int> IncrementalSynthesizer::expandToSilenceBoundaries(int dirtyStart, int dirtyEnd) {
    if (!project) return {dirtyStart, dirtyEnd};

    auto& voicedMask = project->getAudioData().voicedMask;
    const int totalFrames = static_cast<int>(voicedMask.size());

    if (totalFrames == 0) return {dirtyStart, dirtyEnd};

    const int minSilenceFrames = 5;  // Minimum silence gap to consider as boundary

    // Helper: check if frame is voiced
    auto isVoiced = [&](int i) {
        return i >= 0 && i < totalFrames && voicedMask[i];
    };

    // Expand start backwards to find silence boundary
    int expandedStart = dirtyStart;
    int silenceCount = 0;
    for (int i = dirtyStart - 1; i >= 0; --i) {
        if (!isVoiced(i)) {
            silenceCount++;
            if (silenceCount >= minSilenceFrames) {
                // Found silence boundary
                expandedStart = i + silenceCount;
                break;
            }
        } else {
            silenceCount = 0;
            expandedStart = i;
        }
    }
    if (expandedStart > dirtyStart) expandedStart = dirtyStart;
    if (silenceCount < minSilenceFrames && expandedStart > 0) {
        // Didn't find silence, expand to beginning
        expandedStart = 0;
    }

    // Expand end forwards to find silence boundary
    int expandedEnd = dirtyEnd;
    silenceCount = 0;
    for (int i = dirtyEnd; i < totalFrames; ++i) {
        if (!isVoiced(i)) {
            silenceCount++;
            if (silenceCount >= minSilenceFrames) {
                // Found silence boundary
                expandedEnd = i - silenceCount + 1;
                break;
            }
        } else {
            silenceCount = 0;
            expandedEnd = i + 1;
        }
    }
    if (expandedEnd < dirtyEnd) expandedEnd = dirtyEnd;
    if (silenceCount < minSilenceFrames && expandedEnd < totalFrames) {
        // Didn't find silence, expand to end
        expandedEnd = totalFrames;
    }

    DBG("expandToSilenceBoundaries: [" << dirtyStart << ", " << dirtyEnd <<
        "] -> [" << expandedStart << ", " << expandedEnd << "]");

    return {expandedStart, expandedEnd};
}

void IncrementalSynthesizer::synthesizeRegion(ProgressCallback onProgress,
                                               CompleteCallback onComplete) {
    if (!project || !vocoder) {
        if (onComplete) onComplete(false);
        return;
    }

    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
        if (onComplete) onComplete(false);
        return;
    }

    if (!vocoder->isLoaded()) {
        if (onComplete) onComplete(false);
        return;
    }

    // Check for dirty regions
    if (!project->hasDirtyNotes() && !project->hasF0DirtyRange()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
    if (dirtyStart < 0 || dirtyEnd < 0) {
        if (onComplete) onComplete(false);
        return;
    }

    // Expand to silence boundaries (no padding, no crossfade)
    auto [startFrame, endFrame] = expandToSilenceBoundaries(dirtyStart, dirtyEnd);

    // Clamp to valid range
    startFrame = std::max(0, startFrame);
    endFrame = std::min(static_cast<int>(audioData.melSpectrogram.size()), endFrame);

    if (startFrame >= endFrame) {
        if (onComplete) onComplete(false);
        return;
    }

    // Extract mel spectrogram range
    std::vector<std::vector<float>> melRange(
        audioData.melSpectrogram.begin() + startFrame,
        audioData.melSpectrogram.begin() + endFrame);

    // Get adjusted F0 for range
    std::vector<float> adjustedF0Range = project->getAdjustedF0ForRange(startFrame, endFrame);

    if (melRange.empty() || adjustedF0Range.empty()) {
        if (onComplete) onComplete(false);
        return;
    }

    if (onProgress) onProgress("Synthesizing...");

    // Cancel previous job
    if (cancelFlag)
        cancelFlag->store(true);
    cancelFlag = std::make_shared<std::atomic<bool>>(false);
    uint64_t currentJobId = ++jobId;

    isBusy = true;

    int hopSize = vocoder->getHopSize();
    int capturedStartFrame = startFrame;

    // Capture for lambda
    auto capturedCancelFlag = cancelFlag;
    auto capturedProject = project;

    DBG("synthesizeRegion: frames [" << startFrame << ", " << endFrame << "]");

    // Run vocoder inference asynchronously
    vocoder->inferAsync(
        melRange, adjustedF0Range,
        [this, capturedCancelFlag, capturedProject, capturedStartFrame, hopSize,
         currentJobId, onComplete](std::vector<float> synthesizedAudio) {

            // Check if cancelled or superseded
            if (capturedCancelFlag->load() || currentJobId != jobId.load()) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            if (synthesizedAudio.empty()) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            auto& audioData = capturedProject->getAudioData();
            int totalSamples = audioData.waveform.getNumSamples();
            int numChannels = audioData.waveform.getNumChannels();

            int startSample = capturedStartFrame * hopSize;
            int samplesToReplace = static_cast<int>(synthesizedAudio.size());
            samplesToReplace = std::min(samplesToReplace, totalSamples - startSample);

            if (samplesToReplace <= 0) {
                isBusy = false;
                if (onComplete) onComplete(false);
                return;
            }

            // Direct replacement - no crossfade
            for (int i = 0; i < samplesToReplace; ++i) {
                int dstIdx = startSample + i;
                float srcVal = synthesizedAudio[i];
                for (int ch = 0; ch < numChannels; ++ch) {
                    float* dstCh = audioData.waveform.getWritePointer(ch);
                    dstCh[dstIdx] = srcVal;
                }
            }

            DBG("synthesizeRegion: replaced " << samplesToReplace << " samples at " << startSample);

            // Clear dirty flags
            capturedProject->clearAllDirty();

            isBusy = false;
            if (onComplete) onComplete(true);
        });
}
