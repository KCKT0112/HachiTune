#pragma once

#include "../Audio/RealtimePitchProcessor.h"
#include "../JuceHeader.h"
#include "HostCompatibility.h"
#include <atomic>

class MainComponent;

/**
 * HachiTune Audio Processor
 *
 * Supports two modes like Melodyne:
 * 1. ARA Mode: Direct audio access via ARA protocol (Studio One, Cubase, Logic, etc.)
 * 2. Non-ARA Mode: Auto-capture and process (FL Studio, Ableton, etc.)
 */
class PitchEditorAudioProcessor : public juce::AudioProcessor
#if JucePlugin_Enable_ARA
    , public juce::AudioProcessorARAExtension
#endif
{
public:
    PitchEditorAudioProcessor();
    ~PitchEditorAudioProcessor() override;

    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

#if !JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Mode detection
    bool isARAModeActive() const;
    HostCompatibility::HostInfo getHostInfo() const;
    juce::String getHostStatusMessage() const;

    // Editor connection
    void setMainComponent(MainComponent* mc);
    MainComponent* getMainComponent() const { return mainComponent; }

    // Real-time processor access
    RealtimePitchProcessor& getRealtimeProcessor() { return realtimeProcessor; }
    double getHostSampleRate() const { return hostSampleRate; }

    // Non-ARA mode: capture control
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return captureState == CaptureState::Capturing; }
    bool hasCapturedAudio() const { return captureState == CaptureState::Complete; }

private:
    // Capture state machine for non-ARA mode
    enum class CaptureState { Idle, WaitingForAudio, Capturing, Complete };

    void processNonARAMode(juce::AudioBuffer<float>& buffer,
                           const juce::AudioPlayHead::PositionInfo& posInfo);
    void finishCapture();

    RealtimePitchProcessor realtimeProcessor;
    MainComponent* mainComponent = nullptr;
    double hostSampleRate = 44100.0;

    // Non-ARA capture
    std::atomic<CaptureState> captureState{CaptureState::Idle};
    juce::AudioBuffer<float> captureBuffer;
    int capturePosition = 0;
    static constexpr int MAX_CAPTURE_SECONDS = 300; // 5 minutes max
    static constexpr float AUDIO_THRESHOLD = 0.001f; // -60dB

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchEditorAudioProcessor)
};
