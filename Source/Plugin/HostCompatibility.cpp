#include "HostCompatibility.h"

HostCompatibility::HostInfo HostCompatibility::createHostInfo(HostType type) {
    HostInfo info;
    info.type = type;

    switch (type) {
        case HostType::StudioOne:
            info.name = "Studio One";
            info.supportsARA = true;
            info.notes = "ARA supported. Use ARA mode for best integration.";
            break;

        case HostType::Cubase:
            info.name = "Cubase";
            info.supportsARA = true;
            info.notes = "ARA supported.";
            break;

        case HostType::LogicPro:
            info.name = "Logic Pro";
            info.supportsARA = true;
            info.notes = "ARA supported (AU format).";
            break;

        case HostType::ProTools:
            info.name = "Pro Tools";
            info.supportsARA = true;
            info.notes = "ARA supported (AAX format).";
            break;

        case HostType::Reaper:
            info.name = "REAPER";
            info.supportsARA = true;
            info.notes = "ARA supported.";
            break;

        case HostType::Nuendo:
            info.name = "Nuendo";
            info.supportsARA = true;
            info.notes = "ARA supported.";
            break;

        case HostType::FLStudio:
            info.name = "FL Studio";
            info.supportsARA = false;
            info.requiresSpecialHandling = true;
            info.notes = "No ARA support. Uses auto-capture mode.";
            break;

        case HostType::AbletonLive:
            info.name = "Ableton Live";
            info.supportsARA = false;
            info.notes = "No ARA support. Uses auto-capture mode.";
            break;

        case HostType::Bitwig:
            info.name = "Bitwig Studio";
            info.supportsARA = false;
            info.notes = "No ARA support. Uses auto-capture mode.";
            break;

        case HostType::Unknown:
        default:
            info.name = "";
            info.supportsARA = false;
            info.notes = "Unknown host.";
            break;
    }

    return info;
}

HostCompatibility::HostType HostCompatibility::detectHostType(juce::AudioProcessor* processor) {
    if (!processor)
        return HostType::Unknown;

    // AAX is Pro Tools
#if JucePlugin_Build_AAX
    return HostType::ProTools;
#endif

    // For VST3/AU, we can't reliably detect the host from wrapper type alone
    // Return Unknown and let ARA detection handle it
    return HostType::Unknown;
}

HostCompatibility::HostInfo HostCompatibility::detectHost(juce::AudioProcessor* processor) {
    auto type = detectHostType(processor);

#if JucePlugin_Enable_ARA
    // If ARA is active but host is unknown, just mark as ARA-capable
    if (type == HostType::Unknown && processor) {
        if (auto* editor = processor->getActiveEditor()) {
            if (auto* araEditor = dynamic_cast<juce::AudioProcessorEditorARAExtension*>(editor)) {
                if (auto* editorView = araEditor->getARAEditorView()) {
                    if (editorView->getDocumentController()) {
                        HostInfo info;
                        info.type = HostType::Unknown;
                        info.supportsARA = true;
                        info.notes = "ARA mode active.";
                        return info;
                    }
                }
            }
        }
    }
#endif

    return createHostInfo(type);
}

juce::String HostCompatibility::getRecommendedMode(const HostInfo& info) {
    return info.supportsARA ? "ARA Mode (Recommended)" : "Non-ARA Mode (Auto-capture)";
}
