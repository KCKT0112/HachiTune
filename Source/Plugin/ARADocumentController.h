#pragma once

#include "../Audio/RealtimePitchProcessor.h"
#include "../JuceHeader.h"

#if JucePlugin_Enable_ARA

class MainComponent;
class PitchEditorDocumentController;

/**
 * ARA Playback Renderer
 * Reads audio from ARA sources and applies pitch correction
 */
class PitchEditorPlaybackRenderer : public juce::ARAPlaybackRenderer {
public:
    using ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay(double sampleRateIn, int maxBlockSize, int numChannelsIn,
                       juce::AudioProcessor::ProcessingPrecision,
                       AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;
    bool processBlock(juce::AudioBuffer<float>& buffer,
                      juce::AudioProcessor::Realtime realtime,
                      const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

private:
    bool readFromARARegions(juce::AudioBuffer<float>& buffer,
                            juce::int64 timeInSamples, int numSamples);
    PitchEditorDocumentController* getDocController() const;

    std::map<juce::ARAAudioSource*, std::unique_ptr<juce::ARAAudioSourceReader>> readers;
    std::unique_ptr<juce::AudioBuffer<float>> tempBuffer;
    double sampleRate = 44100.0;
    int numChannels = 2;
};

/**
 * ARA Document Controller
 * Manages ARA document lifecycle and audio source analysis
 */
class PitchEditorDocumentController : public juce::ARADocumentControllerSpecialisation {
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    void didAddAudioSourceToDocument(juce::ARADocument* doc, juce::ARAAudioSource* audioSource) override;
    void reanalyze();

    void setMainComponent(MainComponent* mc) { mainComponent = mc; }
    MainComponent* getMainComponent() const { return mainComponent; }

    void setRealtimeProcessor(RealtimePitchProcessor* processor) { realtimeProcessor = processor; }
    RealtimePitchProcessor* getRealtimeProcessor() const { return realtimeProcessor; }

protected:
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    bool doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                    const juce::ARARestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToStream(juce::ARAOutputStream& output,
                                const juce::ARAStoreObjectsFilter* filter) noexcept override;

private:
    void processAudioSource(juce::ARAAudioSource* source);

    MainComponent* mainComponent = nullptr;
    juce::ARAAudioSource* currentAudioSource = nullptr;
    RealtimePitchProcessor* realtimeProcessor = nullptr;
};

#endif // JucePlugin_Enable_ARA
