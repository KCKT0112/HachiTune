#include "PluginProcessor.h"
#include "../UI/MainComponent.h"
#include "PluginEditor.h"

PitchEditorAudioProcessor::PitchEditorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

PitchEditorAudioProcessor::~PitchEditorAudioProcessor() = default;

const juce::String PitchEditorAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool PitchEditorAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PitchEditorAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PitchEditorAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

void PitchEditorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    hostSampleRate = sampleRate;
    realtimeProcessor.prepareToPlay(sampleRate, samplesPerBlock);

#if JucePlugin_Enable_ARA
    prepareToPlayForARA(sampleRate, samplesPerBlock,
                        getMainBusNumOutputChannels(), getProcessingPrecision());
#endif

    // Pre-allocate capture buffer for non-ARA mode
    int maxSamples = static_cast<int>(sampleRate * MAX_CAPTURE_SECONDS);
    captureBuffer.setSize(getMainBusNumOutputChannels(), maxSamples);
    captureBuffer.clear();
    capturePosition = 0;
    captureState = CaptureState::WaitingForAudio;
}

void PitchEditorAudioProcessor::releaseResources() {
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

#if !JucePlugin_PreferredChannelConfigurations
bool PitchEditorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}
#endif

bool PitchEditorAudioProcessor::isARAModeActive() const {
#if JucePlugin_Enable_ARA
    if (auto* editor = getActiveEditor()) {
        if (auto* araEditor = dynamic_cast<juce::AudioProcessorEditorARAExtension*>(editor)) {
            if (auto* editorView = araEditor->getARAEditorView()) {
                return editorView->getDocumentController() != nullptr;
            }
        }
    }
#endif
    return false;
}

HostCompatibility::HostInfo PitchEditorAudioProcessor::getHostInfo() const {
    return HostCompatibility::detectHost(const_cast<PitchEditorAudioProcessor*>(this));
}

juce::String PitchEditorAudioProcessor::getHostStatusMessage() const {
    auto hostInfo = getHostInfo();
    bool araActive = isARAModeActive();

    if (hostInfo.type != HostCompatibility::HostType::Unknown) {
        if (araActive)
            return hostInfo.name + " - ARA Mode";
        if (hostInfo.supportsARA)
            return hostInfo.name + " - Non-ARA (ARA Available)";
        return hostInfo.name + " - Non-ARA Mode";
    }
    return araActive ? "ARA Mode" : "Non-ARA Mode";
}

void PitchEditorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

#if JucePlugin_Enable_ARA
    // ARA mode: let ARA renderer handle audio
    if (processBlockForARA(buffer, isRealtime(), getPlayHead()))
        return;
#endif

    // Non-ARA mode
    juce::AudioPlayHead::PositionInfo posInfo;
    if (auto* playHead = getPlayHead()) {
        if (auto info = playHead->getPosition())
            posInfo = *info;
    }

    processNonARAMode(buffer, posInfo);
}

void PitchEditorAudioProcessor::processNonARAMode(juce::AudioBuffer<float>& buffer,
                                                   const juce::AudioPlayHead::PositionInfo& posInfo) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Check if we have analyzed project ready for real-time processing
    bool hasProject = mainComponent && mainComponent->getProject() &&
                      mainComponent->getProject()->getAudioData().waveform.getNumSamples() > 0 &&
                      !mainComponent->getProject()->getAudioData().f0.empty();

    if (hasProject && realtimeProcessor.isReady()) {
        // Real-time pitch correction mode
        juce::AudioBuffer<float> outputBuffer(numChannels, numSamples);
        if (realtimeProcessor.processBlock(buffer, outputBuffer, &posInfo)) {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.copyFrom(ch, 0, outputBuffer, ch, 0, numSamples);
        }
        return;
    }

    // Capture mode
    CaptureState state = captureState.load();

    if (state == CaptureState::WaitingForAudio) {
        // Detect audio input
        float maxLevel = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                maxLevel = std::max(maxLevel, std::abs(data[i]));
        }

        if (maxLevel > AUDIO_THRESHOLD) {
            captureState = CaptureState::Capturing;
            capturePosition = 0;
            state = CaptureState::Capturing;
        }
    }

    if (state == CaptureState::Capturing) {
        // Capture audio
        int spaceLeft = captureBuffer.getNumSamples() - capturePosition;
        int toCopy = std::min(numSamples, spaceLeft);

        for (int ch = 0; ch < std::min(numChannels, captureBuffer.getNumChannels()); ++ch)
            captureBuffer.copyFrom(ch, capturePosition, buffer, ch, 0, toCopy);

        capturePosition += toCopy;

        // Auto-stop after 30 seconds or buffer full
        int autoStopSamples = static_cast<int>(hostSampleRate * 30);
        if (capturePosition >= autoStopSamples || capturePosition >= captureBuffer.getNumSamples())
            finishCapture();
    }

    // Passthrough during capture
}

void PitchEditorAudioProcessor::finishCapture() {
    if (capturePosition < static_cast<int>(hostSampleRate * 0.5))
        return; // Too short

    captureState = CaptureState::Complete;

    // Trim buffer
    juce::AudioBuffer<float> trimmed;
    trimmed.setSize(captureBuffer.getNumChannels(), capturePosition);
    for (int ch = 0; ch < captureBuffer.getNumChannels(); ++ch)
        trimmed.copyFrom(ch, 0, captureBuffer, ch, 0, capturePosition);

    // Send to MainComponent for analysis
    double sr = hostSampleRate;
    juce::MessageManager::callAsync([this, trimmed, sr]() {
        if (mainComponent) {
            mainComponent->getToolbar().setStatusMessage("Analyzing...");
            mainComponent->setHostAudio(trimmed, sr);
        }
    });
}

void PitchEditorAudioProcessor::startCapture() {
    captureBuffer.clear();
    capturePosition = 0;
    captureState = CaptureState::Capturing;
}

void PitchEditorAudioProcessor::stopCapture() {
    if (captureState == CaptureState::Capturing)
        finishCapture();
}

void PitchEditorAudioProcessor::setMainComponent(MainComponent* mc) {
    mainComponent = mc;
    if (mc) {
        realtimeProcessor.setProject(mc->getProject());
        realtimeProcessor.setVocoder(mc->getVocoder());
    } else {
        realtimeProcessor.setProject(nullptr);
        realtimeProcessor.setVocoder(nullptr);
    }
}

juce::AudioProcessorEditor* PitchEditorAudioProcessor::createEditor() {
    return new PitchEditorAudioProcessorEditor(*this);
}

void PitchEditorAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (mainComponent && mainComponent->getProject()) {
        if (auto xml = mainComponent->getProject()->toXml())
            copyXmlToBinary(*xml, destData);
    }
}

void PitchEditorAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (mainComponent && mainComponent->getProject())
            mainComponent->getProject()->fromXml(*xml);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PitchEditorAudioProcessor();
}

#if JucePlugin_Enable_ARA
#include "ARADocumentController.h"

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory() {
    return juce::ARADocumentControllerSpecialisation::createARAFactory<PitchEditorDocumentController>();
}
#endif
