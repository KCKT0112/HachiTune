#include "RealtimePitchProcessor.h"
#include <algorithm>
#include <cmath>

RealtimePitchProcessor::RealtimePitchProcessor() = default;

RealtimePitchProcessor::~RealtimePitchProcessor() {
    cancelCompute = true;
    if (computeThread && computeThread->joinable())
        computeThread->join();
}

void RealtimePitchProcessor::setProject(Project* proj) {
    {
        const juce::ScopedLock sl(bufferLock);
        project = proj;
    }
    invalidate();
}

void RealtimePitchProcessor::setVocoder(Vocoder* voc) {
    {
        const juce::ScopedLock sl(bufferLock);
        vocoder = voc;
    }
    invalidate();
}

void RealtimePitchProcessor::prepareToPlay(double sr, int) {
    sampleRate = sr;
    position.store(0.0);
}

bool RealtimePitchProcessor::processBlock(juce::AudioBuffer<float>& input,
                                           juce::AudioBuffer<float>& output,
                                           const juce::AudioPlayHead::PositionInfo* posInfo) {
    // Get position from host (don't store - let host control position)
    double pos = 0.0;
    if (posInfo) {
        if (auto time = posInfo->getTimeInSamples())
            pos = static_cast<double>(*time) / sampleRate;
        else if (auto time = posInfo->getTimeInSeconds())
            pos = *time;
    }
    position.store(pos);

    // Passthrough if not ready
    if (!ready.load()) {
        output.makeCopyOf(input);
        return false;
    }

    const int numSamples = output.getNumSamples();
    const int numChannels = output.getNumChannels();
    auto posSamples = static_cast<juce::int64>(pos * sampleRate);

    // Copy from processed buffer
    {
        const juce::ScopedLock sl(bufferLock);

        if (processedBuffer.getNumSamples() == 0) {
            output.makeCopyOf(input);
            return false;
        }

        int available = processedBuffer.getNumSamples() - static_cast<int>(posSamples);
        if (posSamples < 0 || available <= 0) {
            output.makeCopyOf(input);
            return false;
        }

        int toCopy = std::min(numSamples, available);
        int channelsToCopy = std::min(numChannels, processedBuffer.getNumChannels());

        for (int ch = 0; ch < channelsToCopy; ++ch) {
            output.copyFrom(ch, 0, processedBuffer, ch, static_cast<int>(posSamples), toCopy);
            if (toCopy < numSamples)
                output.clear(ch, toCopy, numSamples - toCopy);
        }

        for (int ch = channelsToCopy; ch < numChannels; ++ch)
            output.clear(ch, 0, numSamples);
    }

    return true;
}

void RealtimePitchProcessor::invalidate() {
    ready = false;

    DBG("RealtimePitchProcessor::invalidate() called");

    if (!project) {
        DBG("  -> Skipped: project is null");
        return;
    }

    auto& audioData = project->getAudioData();
    if (audioData.waveform.getNumSamples() == 0) {
        DBG("  -> Skipped: waveform is empty");
        return;
    }

    // Use the already-synthesized waveform from project (updated by resynthesizeIncremental)
    // This avoids duplicate synthesis and ensures consistency with standalone mode
    const int srcSampleRate = audioData.sampleRate;
    const int dstSampleRate = static_cast<int>(sampleRate);

    DBG("  -> srcSampleRate=" << srcSampleRate << ", dstSampleRate=" << dstSampleRate);

    if (srcSampleRate == dstSampleRate || srcSampleRate <= 0) {
        // No resampling needed
        const juce::ScopedLock sl(bufferLock);
        processedBuffer.makeCopyOf(audioData.waveform);
        ready = true;
        DBG("  -> Using project waveform directly, samples=" << processedBuffer.getNumSamples());
    } else {
        // Resample to host sample rate
        const double ratio = static_cast<double>(srcSampleRate) / dstSampleRate;
        const int srcSamples = audioData.waveform.getNumSamples();
        const int dstSamples = static_cast<int>(srcSamples / ratio);
        const int numChannels = audioData.waveform.getNumChannels();

        juce::AudioBuffer<float> resampled(numChannels, dstSamples);

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* src = audioData.waveform.getReadPointer(ch);
            float* dst = resampled.getWritePointer(ch);

            for (int i = 0; i < dstSamples; ++i) {
                const double srcPos = i * ratio;
                const int srcIndex = static_cast<int>(srcPos);
                const double frac = srcPos - srcIndex;

                if (srcIndex + 1 < srcSamples)
                    dst[i] = static_cast<float>(src[srcIndex] * (1.0 - frac) + src[srcIndex + 1] * frac);
                else if (srcIndex < srcSamples)
                    dst[i] = src[srcIndex];
                else
                    dst[i] = 0.0f;
            }
        }

        const juce::ScopedLock sl(bufferLock);
        processedBuffer = std::move(resampled);
        ready = true;
        DBG("  -> Resampled from " << srcSamples << " to " << dstSamples << " samples");
    }
}

void RealtimePitchProcessor::startComputation() {
    // Cancel previous computation
    cancelCompute = true;
    if (computeThread && computeThread->joinable()) {
        // Don't block - start new thread that waits for old one
        auto oldThread = std::move(computeThread);
        cancelCompute = false;
        computing = true;

        computeThread = std::make_unique<std::thread>([this, old = std::move(oldThread)]() mutable {
            if (old && old->joinable())
                old->join();
            if (!cancelCompute.load())
                computeInBackground();
        });
    } else {
        cancelCompute = false;
        computing = true;
        computeThread = std::make_unique<std::thread>([this]() { computeInBackground(); });
    }
}

void RealtimePitchProcessor::computeInBackground() {
    DBG("RealtimePitchProcessor::computeInBackground() started");

    if (!project || !vocoder || !vocoder->isLoaded()) {
        DBG("  -> Aborted: project/vocoder not ready");
        computing = false;
        return;
    }

    auto& audioData = project->getAudioData();
    if (audioData.melSpectrogram.empty()) {
        DBG("  -> Aborted: melSpectrogram empty");
        computing = false;
        return;
    }

    auto adjustedF0 = project->getAdjustedF0();
    DBG("  -> adjustedF0 size=" << adjustedF0.size() << ", melSpec size=" << audioData.melSpectrogram.size());

    if (adjustedF0.empty() || adjustedF0.size() != audioData.melSpectrogram.size()) {
        DBG("  -> Aborted: F0 size mismatch");
        computing = false;
        return;
    }

    if (cancelCompute.load()) {
        DBG("  -> Cancelled before synthesis");
        computing = false;
        return;
    }

    // Synthesize
    DBG("  -> Starting vocoder synthesis...");
    std::vector<float> synthesized;
    try {
        synthesized = vocoder->infer(audioData.melSpectrogram, adjustedF0);
    } catch (...) {
        DBG("  -> Vocoder exception!");
        computing = false;
        return;
    }

    DBG("  -> Synthesized " << synthesized.size() << " samples");

    if (cancelCompute.load() || synthesized.empty()) {
        DBG("  -> Cancelled or empty result");
        computing = false;
        return;
    }

    // Create output buffer
    int numChannels = audioData.waveform.getNumChannels();
    int numSamples = static_cast<int>(synthesized.size());

    juce::AudioBuffer<float> output(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            output.setSample(ch, i, synthesized[i]);

    // Apply volume
    float volumeDb = project->getVolume();
    if (volumeDb != 0.0f)
        output.applyGain(std::pow(10.0f, volumeDb / 20.0f));

    // Update buffer directly (with lock)
    if (!cancelCompute.load()) {
        const juce::ScopedLock sl(bufferLock);
        processedBuffer = std::move(output);
        ready = true;
        DBG("  -> Buffer updated, ready=true, samples=" << processedBuffer.getNumSamples());
    }
    computing = false;
}
