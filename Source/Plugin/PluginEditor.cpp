#include "PluginEditor.h"
#include "HostCompatibility.h"

#if JucePlugin_Enable_ARA
#include "ARADocumentController.h"
#endif

PitchEditorAudioProcessorEditor::PitchEditorAudioProcessorEditor(PitchEditorAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
#if JucePlugin_Enable_ARA
    , AudioProcessorEditorARAExtension(&p)
#endif
{
    addAndMakeVisible(mainComponent);
    audioProcessor.setMainComponent(&mainComponent);

#if JucePlugin_Enable_ARA
    setupARAMode();
#else
    setupNonARAMode();
#endif

    setupCallbacks();

    setSize(1400, 900);
    setResizable(true, true);
}

PitchEditorAudioProcessorEditor::~PitchEditorAudioProcessorEditor() {
    audioProcessor.setMainComponent(nullptr);
}

void PitchEditorAudioProcessorEditor::setupARAMode() {
#if JucePlugin_Enable_ARA
    auto* editorView = getARAEditorView();
    if (!editorView) {
        setupNonARAMode();
        return;
    }

    auto* docController = editorView->getDocumentController();
    if (!docController) {
        setupNonARAMode();
        return;
    }

    auto* pitchDocController = juce::ARADocumentControllerSpecialisation::
        getSpecialisedDocumentController<PitchEditorDocumentController>(docController);

    if (!pitchDocController) {
        setupNonARAMode();
        return;
    }

    // Connect ARA controller to UI
    pitchDocController->setMainComponent(&mainComponent);
    pitchDocController->setRealtimeProcessor(&audioProcessor.getRealtimeProcessor());

    // Setup re-analyze callback
    mainComponent.onReanalyzeRequested = [pitchDocController]() {
        pitchDocController->reanalyze();
    };

    // Check for existing audio sources
    auto* juceDocument = docController->getDocument();
    if (juceDocument) {
        auto& audioSources = juceDocument->getAudioSources<juce::ARAAudioSource>();

        if (!audioSources.empty()) {
            // Process first audio source
            auto* source = audioSources.front();
            if (source && source->getSampleCount() > 0) {
                juce::ARAAudioSourceReader reader(source);
                int numSamples = static_cast<int>(source->getSampleCount());
                int numChannels = source->getChannelCount();
                double sampleRate = source->getSampleRate();

                juce::AudioBuffer<float> buffer(numChannels, numSamples);
                if (reader.read(&buffer, 0, numSamples, 0, true, true)) {
                    mainComponent.getToolbar().setStatusMessage("ARA Mode - Analyzing...");
                    mainComponent.setHostAudio(buffer, sampleRate);
                    return;
                }
            }
        }

        // No audio yet
        auto hostInfo = audioProcessor.getHostInfo();
        juce::String status = hostInfo.name.isEmpty() ? "ARA Mode" : hostInfo.name + " - ARA Mode";
        status += " - Waiting for audio...";
        mainComponent.getToolbar().setStatusMessage(status);
    }
#endif
}

void PitchEditorAudioProcessorEditor::setupNonARAMode() {
    auto hostInfo = audioProcessor.getHostInfo();
    juce::String status;

    if (hostInfo.type != HostCompatibility::HostType::Unknown) {
        status = hostInfo.name;
        if (hostInfo.supportsARA)
            status += " - Non-ARA (ARA Available)";
        else
            status += " - Non-ARA Mode";
    } else {
        status = "Non-ARA Mode - Auto-capture";
    }

    mainComponent.getToolbar().setStatusMessage(status);
}

void PitchEditorAudioProcessorEditor::setupCallbacks() {
    // When project data changes (analysis complete or synthesis complete)
    mainComponent.onProjectDataChanged = [this]() {
        if (mainComponent.getVocoder())
            audioProcessor.getRealtimeProcessor().setVocoder(mainComponent.getVocoder());

        if (mainComponent.getProject())
            audioProcessor.getRealtimeProcessor().setProject(mainComponent.getProject());

        audioProcessor.getRealtimeProcessor().invalidate();

        // Update status
        auto hostInfo = audioProcessor.getHostInfo();
        juce::String status = hostInfo.name.isEmpty() ? "" : hostInfo.name + " - ";
        status += audioProcessor.isARAModeActive() ? "ARA Mode - Ready" : "Ready";
        mainComponent.getToolbar().setStatusMessage(status);
    };

    // onPitchEditFinished is handled by onProjectDataChanged (called after async synthesis completes)
    // No need for separate callback here
}

void PitchEditorAudioProcessorEditor::paint(juce::Graphics&) {
    // MainComponent handles all painting
}

void PitchEditorAudioProcessorEditor::resized() {
    mainComponent.setBounds(getLocalBounds());
}
