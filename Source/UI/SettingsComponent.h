#pragma once

#include "../JuceHeader.h"
#include <functional>

/**
 * Settings dialog for application configuration.
 * Includes device selection for ONNX inference.
 */
class SettingsComponent : public juce::Component,
                          public juce::ComboBox::Listener
{
public:
    SettingsComponent();
    ~SettingsComponent() override = default;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // ComboBox::Listener
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    
    // Get current settings
    juce::String getSelectedDevice() const { return currentDevice; }
    int getNumThreads() const { return numThreads; }
    
    // Callbacks
    std::function<void()> onSettingsChanged;
    
    // Load/save settings
    void loadSettings();
    void saveSettings();
    
    // Get available execution providers
    static juce::StringArray getAvailableDevices();
    
private:
    void updateDeviceList();
    
    juce::Label titleLabel;
    juce::Label deviceLabel;
    juce::ComboBox deviceComboBox;
    juce::Label threadsLabel;
    juce::Slider threadsSlider;
    juce::Label threadsValueLabel;
    
    juce::Label infoLabel;
    
    juce::String currentDevice = "CPU";
    int numThreads = 0;  // 0 = auto (use all cores)
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

/**
 * Settings dialog window.
 */
class SettingsDialog : public juce::DialogWindow
{
public:
    SettingsDialog();
    ~SettingsDialog() override = default;
    
    void closeButtonPressed() override;
    
    SettingsComponent* getSettingsComponent() { return &settingsComponent; }
    
private:
    SettingsComponent settingsComponent;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
