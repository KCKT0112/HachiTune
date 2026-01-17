#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "Vocoder.h"
#include <atomic>
#include <memory>
#include <thread>

/**
 * Real-time pitch correction processor
 * Pre-computes processed audio in background, provides real-time playback
 */
class RealtimePitchProcessor {
public:
    RealtimePitchProcessor();
    ~RealtimePitchProcessor();

    void setProject(Project* proj);
    void setVocoder(Vocoder* voc);
    void prepareToPlay(double sampleRate, int samplesPerBlock);

    /**
     * Process audio block
     * @return true if processed audio was used, false if passthrough
     */
    bool processBlock(juce::AudioBuffer<float>& input,
                      juce::AudioBuffer<float>& output,
                      const juce::AudioPlayHead::PositionInfo* positionInfo);

    /**
     * Trigger re-computation (call when project data changes)
     */
    void invalidate();

    bool isReady() const { return ready.load(); }
    double getPosition() const { return position.load(); }
    void setPosition(double positionSeconds) { position.store(positionSeconds); }

private:
    void startComputation();
    void computeInBackground();

    Project* project = nullptr;
    Vocoder* vocoder = nullptr;
    double sampleRate = 44100.0;

    juce::AudioBuffer<float> processedBuffer;
    std::atomic<bool> ready{false};
    std::atomic<bool> computing{false};
    std::atomic<bool> cancelCompute{false};
    std::atomic<double> position{0.0};

    juce::CriticalSection bufferLock;
    std::unique_ptr<std::thread> computeThread;
};
