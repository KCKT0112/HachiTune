#pragma once

#include "../Audio/PitchDetectorType.h"
#include "../JuceHeader.h"
#include "../Utils/Constants.h"
#include "Main/SettingsManager.h"
#include "StyledComponents.h"
#include <functional>

enum class Language; // Forward declaration

/**
 * Settings dialog for application configuration.
 * Includes device selection for ONNX inference.
 */
class SettingsComponent : public juce::Component,
                          public juce::ComboBox::Listener,
                          public juce::ChangeListener,
                          public juce::Timer {
public:
  SettingsComponent(SettingsManager *settingsManager,
                    juce::AudioDeviceManager *audioDeviceManager = nullptr);
  ~SettingsComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  // ComboBox::Listener
  void comboBoxChanged(juce::ComboBox *comboBox) override;

  // ChangeListener
  void changeListenerCallback(juce::ChangeBroadcaster *source) override;

  // Timer
  void timerCallback() override;

  // Get current settings
  juce::String getSelectedDevice() const { return currentDevice; }
  int getGPUDeviceId() const { return gpuDeviceId; }
  PitchDetectorType getPitchDetectorType() const { return pitchDetectorType; }

  // Plugin mode (disables audio device settings)
  bool isPluginMode() const { return pluginMode; }

  // Callbacks
  std::function<void()> onSettingsChanged;
  std::function<void()> onLanguageChanged;
  std::function<void(PitchDetectorType)> onPitchDetectorChanged;
  std::function<bool()> canChangeDevice;

  // Load/save settings
  void loadSettings();
  void saveSettings();

  // Get available execution providers
  static juce::StringArray getAvailableDevices();

private:
  class SettingsLookAndFeel : public DarkLookAndFeel {
  public:
    juce::Font getTextButtonFont(juce::TextButton &, int) override {
      return AppFont::getFont(15.0f);
    }

    juce::Font getLabelFont(juce::Label &) override {
      return AppFont::getFont(15.0f);
    }

    juce::Font getComboBoxFont(juce::ComboBox &) override {
      return AppFont::getFont(15.0f);
    }

    juce::Font getPopupMenuFont() override { return AppFont::getFont(15.0f); }
  };

  enum class SettingsTab { General, Audio };

  void updateDeviceList();
  void updateGPUDeviceList(const juce::String &deviceType);
  void updateAudioDeviceTypes();
  void updateAudioOutputDevices(bool force = false);
  void updateSampleRates();
  void updateBufferSizes();
  void applyAudioSettings();
  void setActiveTab(SettingsTab tab);
  void updateTabButtonStyles();
  void updateTabVisibility();
  bool shouldShowGpuDeviceList() const;

  bool pluginMode = false;
  juce::AudioDeviceManager *deviceManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  SettingsLookAndFeel settingsLookAndFeel;

  juce::Label titleLabel;
  juce::Label generalSectionLabel;

  juce::Label languageLabel;
  StyledComboBox languageComboBox;

  juce::Label uiScaleLabel;
  StyledComboBox uiScaleComboBox;

  juce::Label deviceLabel;
  StyledComboBox deviceComboBox;
  juce::Label gpuDeviceLabel;
  StyledComboBox gpuDeviceComboBox;

  juce::Label pitchDetectorLabel;
  StyledComboBox pitchDetectorComboBox;

  juce::Label infoLabel;

  // Audio device settings (standalone mode only)
  juce::Label audioSectionLabel;
  juce::Label audioDeviceTypeLabel;
  StyledComboBox audioDeviceTypeComboBox;
  juce::Array<juce::AudioIODeviceType *> audioDeviceTypeOrder;
  juce::Label audioOutputLabel;
  StyledComboBox audioOutputComboBox;
  juce::Label sampleRateLabel;
  StyledComboBox sampleRateComboBox;
  juce::Label bufferSizeLabel;
  StyledComboBox bufferSizeComboBox;
  juce::Label outputChannelsLabel;
  StyledComboBox outputChannelsComboBox;

  juce::StringArray cachedOutputDevices;
  juce::String cachedOutputDeviceName;
  juce::String cachedDeviceTypeName;

  juce::String currentDevice = "CPU";
  bool hasLoadedSettings = false;
  int gpuDeviceId = 0;
  juce::String lastConfirmedDevice = "CPU";
  int lastConfirmedGpuDeviceId = 0;
  PitchDetectorType pitchDetectorType = PitchDetectorType::RMVPE;
  SettingsTab activeTab = SettingsTab::General;
  juce::TextButton generalTabButton;
  juce::TextButton audioTabButton;
  juce::Rectangle<int> cardBounds;
  juce::Rectangle<int> sidebarBounds;
  juce::Array<int> separatorYs;
  float cornerRadius = 10.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

/**
 * Settings overlay panel (in-window modal).
 */
class SettingsOverlay : public juce::Component {
public:
  SettingsOverlay(SettingsManager *settingsManager,
                  juce::AudioDeviceManager *audioDeviceManager = nullptr);
  ~SettingsOverlay() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;
  bool keyPressed(const juce::KeyPress &key) override;

  SettingsComponent *getSettingsComponent() { return settingsComponent.get(); }

  std::function<void()> onClose;

private:
  void closeIfPossible();

  std::unique_ptr<SettingsComponent> settingsComponent;
  juce::TextButton closeButton{"X"};
  juce::Rectangle<int> contentBounds;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

/**
 * Settings dialog window.
 */
class SettingsDialog : public juce::DialogWindow {
public:
  SettingsDialog(SettingsManager *settingsManager,
                 juce::AudioDeviceManager *audioDeviceManager = nullptr);
  ~SettingsDialog() override = default;

  void closeButtonPressed() override;
  void paint(juce::Graphics &g) override;

  SettingsComponent *getSettingsComponent() { return settingsComponent.get(); }

private:
  std::unique_ptr<SettingsComponent> settingsComponent;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
