#pragma once

#include "../JuceHeader.h"

/**
 * Manages DPI scaling for the application.
 * Provides automatic DPI detection and user-configurable scaling options.
 */
class DPIScaleManager : public juce::DeletedAtShutdown
{
public:
    /**
     * Listener interface for components that need to respond to scale changes.
     */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void scaleFactorChanged(float newScale) = 0;
    };

    /**
     * Available scaling presets.
     */
    enum class ScalePreset
    {
        Scale100 = 0,  // 100%
        Scale125 = 1,  // 125%
        Scale150 = 2,  // 150%
        Scale200 = 3,  // 200%
        Auto = 4       // Automatic (system DPI)
    };

    static DPIScaleManager& getInstance()
    {
        if (instance == nullptr)
            instance = new DPIScaleManager();
        return *instance;
    }

    /**
     * Initialize the DPI scale manager.
     * Should be called once at application startup.
     */
    void initialize()
    {
        // Detect system DPI
        systemScaleFactor = juce::Desktop::getInstance().getGlobalScaleFactor();

        // Load user preference
        loadScalePreference();

        // Apply the scale
        applyScale();
    }

    /**
     * Get the current effective scale factor.
     */
    float getScaleFactor() const { return currentScaleFactor; }

    /**
     * Get the system-detected scale factor.
     */
    float getSystemScaleFactor() const { return systemScaleFactor; }

    /**
     * Set the scale preset.
     */
    void setScalePreset(ScalePreset preset)
    {
        if (currentPreset != preset)
        {
            currentPreset = preset;
            saveScalePreference();
            applyScale();
            notifyListeners();
        }
    }

    /**
     * Get the current scale preset.
     */
    ScalePreset getScalePreset() const { return currentPreset; }

    /**
     * Convert a preset to its scale factor value.
     */
    static float presetToScaleFactor(ScalePreset preset)
    {
        switch (preset)
        {
            case ScalePreset::Scale100: return 1.0f;
            case ScalePreset::Scale125: return 1.25f;
            case ScalePreset::Scale150: return 1.5f;
            case ScalePreset::Scale200: return 2.0f;
            case ScalePreset::Auto:
            default:
                return juce::Desktop::getInstance().getGlobalScaleFactor();
        }
    }

    /**
     * Get a scaled value based on the current scale factor.
     */
    float scale(float value) const { return value * currentScaleFactor; }
    int scaleInt(int value) const { return juce::roundToInt(value * currentScaleFactor); }

    /**
     * Add a listener to be notified of scale changes.
     */
    void addListener(Listener* listener)
    {
        listeners.add(listener);
    }

    /**
     * Remove a listener.
     */
    void removeListener(Listener* listener)
    {
        listeners.remove(listener);
    }

private:
    DPIScaleManager() = default;
    ~DPIScaleManager() { clearSingletonInstance(); }

    void applyScale()
    {
        currentScaleFactor = presetToScaleFactor(currentPreset);
        juce::Desktop::getInstance().setGlobalScaleFactor(currentScaleFactor);
    }

    void loadScalePreference()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "HachiTune";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";

        juce::PropertiesFile props(options);
        int presetValue = props.getIntValue("dpiScalePreset", static_cast<int>(ScalePreset::Auto));
        currentPreset = static_cast<ScalePreset>(presetValue);
    }

    void saveScalePreference()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "HachiTune";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";

        juce::PropertiesFile props(options);
        props.setValue("dpiScalePreset", static_cast<int>(currentPreset));
        props.saveIfNeeded();
    }

    void notifyListeners()
    {
        listeners.call([this](Listener& l) { l.scaleFactorChanged(currentScaleFactor); });
    }

    void clearSingletonInstance()
    {
        if (instance == this)
            instance = nullptr;
    }

    static DPIScaleManager* instance;
    juce::ListenerList<Listener> listeners;

    float systemScaleFactor = 1.0f;
    float currentScaleFactor = 1.0f;
    ScalePreset currentPreset = ScalePreset::Auto;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DPIScaleManager)
};

// Static instance declaration
inline DPIScaleManager* DPIScaleManager::instance = nullptr;
