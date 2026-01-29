#include "SettingsComponent.h"
#include "../Utils/Constants.h"
#include "../Utils/DPIScaleManager.h"
#include "../Utils/Localization.h"

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#ifdef _WIN32
#include <dxgi1_2.h>
#include <windows.h>
#endif

namespace {
#ifdef _WIN32
juce::StringArray getDxgiAdapterNames() {
  juce::StringArray names;
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void **>(&factory))) ||
      factory == nullptr) {
    return names;
  }

  for (UINT i = 0;; ++i) {
    IDXGIAdapter1 *adapter = nullptr;
    const auto hr = factory->EnumAdapters1(i, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(hr) || adapter == nullptr)
      continue;

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
        names.add(juce::String(desc.Description));
      }
    }
    adapter->Release();
  }

  factory->Release();
  return names;
}
#endif
} // namespace

//==============================================================================
// SettingsComponent
//==============================================================================

SettingsComponent::SettingsComponent(
    SettingsManager *settingsMgr, juce::AudioDeviceManager *audioDeviceManager)
    : deviceManager(audioDeviceManager),
      pluginMode(audioDeviceManager == nullptr), settingsManager(settingsMgr) {
  // Allow transparent corners for rounded window styling
  setOpaque(false);

  auto configureRowLabel = [](juce::Label &label) {
    label.setColour(juce::Label::textColourId, juce::Colour(0xFFD6D6DE));
    label.setFont(AppFont::getFont(15.0f));
    label.setJustificationType(juce::Justification::centredLeft);
  };

  // Title
  titleLabel.setText(TR("settings.title"), juce::dontSendNotification);
  titleLabel.setFont(AppFont::getBoldFont(20.0f));
  titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFF0F0F4));
  addAndMakeVisible(titleLabel);

  auto configureTabButton = [this](juce::TextButton &button) {
    button.setClickingTogglesState(false);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    button.setLookAndFeel(&settingsLookAndFeel);
  };

  // Tabs
  generalTabButton.setButtonText(TR("settings.general"));
  configureTabButton(generalTabButton);
  generalTabButton.onClick = [this]() { setActiveTab(SettingsTab::General); };
  addAndMakeVisible(generalTabButton);

  audioTabButton.setButtonText(TR("settings.audio"));
  configureTabButton(audioTabButton);
  audioTabButton.onClick = [this]() { setActiveTab(SettingsTab::Audio); };
  addAndMakeVisible(audioTabButton);

  // General section label
  generalSectionLabel.setText(TR("settings.general"),
                              juce::dontSendNotification);
  generalSectionLabel.setFont(AppFont::getBoldFont(15.0f));
  generalSectionLabel.setColour(juce::Label::textColourId,
                                juce::Colour(0xFFB8B8C2));
  addAndMakeVisible(generalSectionLabel);

  // Language selection
  languageLabel.setText(TR("settings.language"), juce::dontSendNotification);
  configureRowLabel(languageLabel);
  addAndMakeVisible(languageLabel);

  // Add "Auto" option first
  languageComboBox.addItem(TR("lang.auto"), 1);
  // Add available languages dynamically
  const auto &langs = Localization::getInstance().getAvailableLanguages();
  for (int i = 0; i < static_cast<int>(langs.size()); ++i)
    languageComboBox.addItem(langs[i].nativeName, i + 2); // IDs start at 2
  languageComboBox.addListener(this);
  languageComboBox.setLookAndFeel(&settingsLookAndFeel);
  addAndMakeVisible(languageComboBox);

  // UI Scale selection
  uiScaleLabel.setText(TR("settings.ui_scale"), juce::dontSendNotification);
  configureRowLabel(uiScaleLabel);
  addAndMakeVisible(uiScaleLabel);

  uiScaleComboBox.addItem(TR("settings.ui_scale.auto"), 1);
  uiScaleComboBox.addItem("100%", 2);
  uiScaleComboBox.addItem("125%", 3);
  uiScaleComboBox.addItem("150%", 4);
  uiScaleComboBox.addItem("200%", 5);
  uiScaleComboBox.addListener(this);
  uiScaleComboBox.setLookAndFeel(&settingsLookAndFeel);
  addAndMakeVisible(uiScaleComboBox);

  // Device selection
  deviceLabel.setText(TR("settings.device"), juce::dontSendNotification);
  configureRowLabel(deviceLabel);
  addAndMakeVisible(deviceLabel);

  deviceComboBox.addListener(this);
  deviceComboBox.setLookAndFeel(&settingsLookAndFeel);
  addAndMakeVisible(deviceComboBox);

  // GPU device ID selection
  gpuDeviceLabel.setText(TR("settings.gpu_device"), juce::dontSendNotification);
  configureRowLabel(gpuDeviceLabel);
  addAndMakeVisible(gpuDeviceLabel);

  // GPU device list will be populated dynamically based on available devices
  gpuDeviceComboBox.addListener(this);
  gpuDeviceComboBox.setLookAndFeel(&settingsLookAndFeel);
  addAndMakeVisible(gpuDeviceComboBox);
  gpuDeviceLabel.setVisible(false);
  gpuDeviceComboBox.setVisible(false);

  // Pitch detector selection
  pitchDetectorLabel.setText(TR("settings.pitch_detector"),
                             juce::dontSendNotification);
  configureRowLabel(pitchDetectorLabel);
  addAndMakeVisible(pitchDetectorLabel);

  pitchDetectorComboBox.addItem("RMVPE", 1);
  pitchDetectorComboBox.addItem("FCPE", 2);
  pitchDetectorComboBox.setSelectedId(
      1, juce::dontSendNotification); // Default to RMVPE
  pitchDetectorComboBox.addListener(this);
  pitchDetectorComboBox.setLookAndFeel(&settingsLookAndFeel);
  addAndMakeVisible(pitchDetectorComboBox);

  // Info label
  infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9A9AA6));
  infoLabel.setFont(AppFont::getFont(13.0f));
  infoLabel.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(infoLabel);

  // Audio device settings (standalone mode only)
  if (!pluginMode && deviceManager != nullptr) {
    deviceManager->addChangeListener(this);

    audioSectionLabel.setText(TR("settings.audio"), juce::dontSendNotification);
    audioSectionLabel.setFont(AppFont::getBoldFont(15.0f));
    audioSectionLabel.setColour(juce::Label::textColourId,
                                juce::Colour(0xFFB8B8C2));
    addAndMakeVisible(audioSectionLabel);

    // Audio device type (driver)
    audioDeviceTypeLabel.setText(TR("settings.audio_driver"),
                                 juce::dontSendNotification);
    configureRowLabel(audioDeviceTypeLabel);
    addAndMakeVisible(audioDeviceTypeLabel);
    audioDeviceTypeComboBox.addListener(this);
    audioDeviceTypeComboBox.setLookAndFeel(&settingsLookAndFeel);
    addAndMakeVisible(audioDeviceTypeComboBox);

    // Output device
    audioOutputLabel.setText(TR("settings.audio_output"),
                             juce::dontSendNotification);
    configureRowLabel(audioOutputLabel);
    addAndMakeVisible(audioOutputLabel);
    audioOutputComboBox.addListener(this);
    audioOutputComboBox.setLookAndFeel(&settingsLookAndFeel);
    addAndMakeVisible(audioOutputComboBox);

    // Sample rate
    sampleRateLabel.setText(TR("settings.sample_rate"),
                            juce::dontSendNotification);
    configureRowLabel(sampleRateLabel);
    addAndMakeVisible(sampleRateLabel);
    sampleRateComboBox.addListener(this);
    sampleRateComboBox.setLookAndFeel(&settingsLookAndFeel);
    addAndMakeVisible(sampleRateComboBox);

    // Buffer size
    bufferSizeLabel.setText(TR("settings.buffer_size"),
                            juce::dontSendNotification);
    configureRowLabel(bufferSizeLabel);
    addAndMakeVisible(bufferSizeLabel);
    bufferSizeComboBox.addListener(this);
    bufferSizeComboBox.setLookAndFeel(&settingsLookAndFeel);
    addAndMakeVisible(bufferSizeComboBox);

    // Output channels
    outputChannelsLabel.setText(TR("settings.output_channels"),
                                juce::dontSendNotification);
    configureRowLabel(outputChannelsLabel);
    addAndMakeVisible(outputChannelsLabel);
    outputChannelsComboBox.addItem(TR("settings.mono"), 1);
    outputChannelsComboBox.addItem(TR("settings.stereo"), 2);
    outputChannelsComboBox.setSelectedId(2, juce::dontSendNotification);
    outputChannelsComboBox.addListener(this);
    outputChannelsComboBox.setLookAndFeel(&settingsLookAndFeel);
    addAndMakeVisible(outputChannelsComboBox);

    updateAudioDeviceTypes();
    startTimer(2000);
  }

  // Load saved settings
  loadSettings();
  updateDeviceList();

  updateTabButtonStyles();
  updateTabVisibility();

  // Set size based on mode
  if (pluginMode)
    setSize(720, 420);
  else
    setSize(820, 620);

  // Setup Kiwi constraint layout
  setupConstraints();
}

void SettingsComponent::setupConstraints() {
  // Create variables for container (this component)
  containerVars = layout.createComponentVars("container");

  // Create variables for sidebar and tabs
  sidebarVars = layout.createComponentVars("sidebar");
  generalTabButtonVars = layout.createComponentVars("generalTabButton");
  audioTabButtonVars = layout.createComponentVars("audioTabButton");

  // Create variables for content area
  contentVars = layout.createComponentVars("content");
  titleVars = layout.createComponentVars("title");
  cardVars = layout.createComponentVars("card");

  // Container constraints (will be updated in resized())
  containerWidthConstraint = containerVars.width == 800.0;
  containerHeightConstraint = containerVars.height == 600.0;
  layout.addConstraint(containerVars.left == 0.0);
  layout.addConstraint(containerVars.top == 0.0);
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);

  // Sidebar constraints (140px width, 16px from left, 16px from top/bottom)
  layout.addConstraint(sidebarVars.left == containerVars.left + 16.0);
  layout.addConstraint(sidebarVars.top == containerVars.top + 16.0);
  layout.addConstraint(sidebarVars.width == 140.0);
  layout.addConstraint(sidebarVars.bottom == containerVars.bottom - 16.0);

  // Tab buttons (inside sidebar, 10px padding, 32px height, 6px gap)
  layout.addConstraint(generalTabButtonVars.left == sidebarVars.left + 10.0);
  layout.addConstraint(generalTabButtonVars.top == sidebarVars.top + 10.0);
  layout.addConstraint(generalTabButtonVars.right == sidebarVars.right - 10.0);
  layout.addConstraint(generalTabButtonVars.height == 32.0);

  layout.addConstraint(audioTabButtonVars.left == sidebarVars.left + 10.0);
  layout.addConstraint(audioTabButtonVars.top == generalTabButtonVars.bottom + 6.0);
  layout.addConstraint(audioTabButtonVars.right == sidebarVars.right - 10.0);
  layout.addConstraint(audioTabButtonVars.height == 32.0);

  // Title area (34px height, 10px gap from sidebar, 6px gap below)
  layout.addConstraint(titleVars.left == sidebarVars.right + 10.0 + 16.0);
  layout.addConstraint(titleVars.top == containerVars.top + 16.0);
  layout.addConstraint(titleVars.right == containerVars.right - 16.0);
  layout.addConstraint(titleVars.height == 34.0);

  // Card bounds (fills remaining space)
  layout.addConstraint(cardVars.left == sidebarVars.right + 10.0 + 16.0);
  layout.addConstraint(cardVars.top == titleVars.bottom + 6.0);
  layout.addConstraint(cardVars.right == containerVars.right - 16.0);
  layout.addConstraint(cardVars.bottom == containerVars.bottom - 16.0);

  // Content area (inside card, 16px horizontal, 12px vertical padding)
  layout.addConstraint(contentVars.left == cardVars.left + 16.0);
  layout.addConstraint(contentVars.top == cardVars.top + 12.0);
  layout.addConstraint(contentVars.right == cardVars.right - 16.0);
  layout.addConstraint(contentVars.bottom == cardVars.bottom - 12.0);
}

void SettingsComponent::updateLayoutConstraints() {
  // Remove old size constraints
  layout.removeConstraint(containerWidthConstraint);
  layout.removeConstraint(containerHeightConstraint);

  // Create new size constraints with current dimensions
  containerWidthConstraint = containerVars.width == static_cast<double>(getWidth());
  containerHeightConstraint = containerVars.height == static_cast<double>(getHeight());

  // Add new constraints
  layout.addConstraint(containerWidthConstraint);
  layout.addConstraint(containerHeightConstraint);
}

void SettingsComponent::layoutGeneralTab() {
  // Note: We don't need to explicitly remove old constraints
  // The layout manager will handle constraint updates
  currentRowVars.clear();

  const double rowHeight = 32.0;
  const double rowGap = 8.0;
  const double labelWidth = 150.0;
  const double controlWidth = 190.0;
  const double sectionHeight = 20.0;
  const double sectionGap = 10.0;

  double currentY = contentVars.top.value();

  // Section label
  auto sectionVars = layout.createComponentVars("generalSection");
  layout.addConstraint(sectionVars.left == contentVars.left);
  layout.addConstraint(sectionVars.top == currentY);
  layout.addConstraint(sectionVars.width == contentVars.width);
  layout.addConstraint(sectionVars.height == sectionHeight);
  layout.applyComponentVars(&generalSectionLabel, sectionVars);
  currentY += sectionHeight + sectionGap;

  // Helper to create a row
  auto createRow = [&](juce::Label &label, juce::Component &control) {
    RowVars row;
    row.label = layout.createComponentVars("row_label_" + juce::String(currentRowVars.size()));
    row.control = layout.createComponentVars("row_control_" + juce::String(currentRowVars.size()));

    layout.addConstraint(row.label.left == contentVars.left);
    layout.addConstraint(row.label.top == currentY);
    layout.addConstraint(row.label.width == labelWidth);
    layout.addConstraint(row.label.height == rowHeight);

    layout.addConstraint(row.control.right == contentVars.right);
    layout.addConstraint(row.control.top == currentY + 2.0);
    layout.addConstraint(row.control.width == controlWidth);
    layout.addConstraint(row.control.height == rowHeight - 4.0);

    layout.applyComponentVars(&label, row.label);
    layout.applyComponentVars(&control, row.control);

    currentRowVars.push_back(row);
    currentY += rowHeight + rowGap;
  };

  // Layout rows
  createRow(languageLabel, languageComboBox);
  createRow(uiScaleLabel, uiScaleComboBox);
  createRow(deviceLabel, deviceComboBox);

  if (gpuDeviceLabel.isVisible()) {
    createRow(gpuDeviceLabel, gpuDeviceComboBox);
  }

  createRow(pitchDetectorLabel, pitchDetectorComboBox);

  // Info label (56px height, 12px gap)
  auto infoVars = layout.createComponentVars("infoLabel");
  layout.addConstraint(infoVars.left == contentVars.left);
  layout.addConstraint(infoVars.top == currentY);
  layout.addConstraint(infoVars.width == contentVars.width);
  layout.addConstraint(infoVars.height == 56.0);
  layout.applyComponentVars(&infoLabel, infoVars);
}

void SettingsComponent::layoutAudioTab() {
  // Note: We don't need to explicitly remove old constraints
  // The layout manager will handle constraint updates
  currentRowVars.clear();

  const double rowHeight = 32.0;
  const double rowGap = 8.0;
  const double labelWidth = 150.0;
  const double controlWidth = 190.0;
  const double sectionHeight = 20.0;
  const double sectionGap = 10.0;

  double currentY = contentVars.top.value();

  // Section label
  auto sectionVars = layout.createComponentVars("audioSection");
  layout.addConstraint(sectionVars.left == contentVars.left);
  layout.addConstraint(sectionVars.top == currentY);
  layout.addConstraint(sectionVars.width == contentVars.width);
  layout.addConstraint(sectionVars.height == sectionHeight);
  layout.applyComponentVars(&audioSectionLabel, sectionVars);
  currentY += sectionHeight + sectionGap;

  // Helper to create a row
  auto createRow = [&](juce::Label &label, juce::Component &control) {
    RowVars row;
    row.label = layout.createComponentVars("row_label_" + juce::String(currentRowVars.size()));
    row.control = layout.createComponentVars("row_control_" + juce::String(currentRowVars.size()));

    layout.addConstraint(row.label.left == contentVars.left);
    layout.addConstraint(row.label.top == currentY);
    layout.addConstraint(row.label.width == labelWidth);
    layout.addConstraint(row.label.height == rowHeight);

    layout.addConstraint(row.control.right == contentVars.right);
    layout.addConstraint(row.control.top == currentY + 2.0);
    layout.addConstraint(row.control.width == controlWidth);
    layout.addConstraint(row.control.height == rowHeight - 4.0);

    layout.applyComponentVars(&label, row.label);
    layout.applyComponentVars(&control, row.control);

    currentRowVars.push_back(row);
    currentY += rowHeight + rowGap;
  };

  // Layout rows
  createRow(audioDeviceTypeLabel, audioDeviceTypeComboBox);
  createRow(audioOutputLabel, audioOutputComboBox);
  createRow(sampleRateLabel, sampleRateComboBox);
  createRow(bufferSizeLabel, bufferSizeComboBox);
  createRow(outputChannelsLabel, outputChannelsComboBox);
}

SettingsComponent::~SettingsComponent() {
  stopTimer();
  if (!pluginMode && deviceManager != nullptr)
    deviceManager->removeChangeListener(this);
  generalTabButton.setLookAndFeel(nullptr);
  audioTabButton.setLookAndFeel(nullptr);
  languageComboBox.setLookAndFeel(nullptr);
  deviceComboBox.setLookAndFeel(nullptr);
  gpuDeviceComboBox.setLookAndFeel(nullptr);
  pitchDetectorComboBox.setLookAndFeel(nullptr);
  audioDeviceTypeComboBox.setLookAndFeel(nullptr);
  audioOutputComboBox.setLookAndFeel(nullptr);
  sampleRateComboBox.setLookAndFeel(nullptr);
  bufferSizeComboBox.setLookAndFeel(nullptr);
  outputChannelsComboBox.setLookAndFeel(nullptr);
}

void SettingsComponent::changeListenerCallback(
    juce::ChangeBroadcaster *source) {
  if (source == deviceManager)
    updateAudioOutputDevices(true);
}

void SettingsComponent::timerCallback() {
  if (!pluginMode && deviceManager != nullptr)
    updateAudioOutputDevices(false);
}

void SettingsComponent::paint(juce::Graphics &g) {
  juce::Path rounded;
  rounded.addRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);
  g.reduceClipRegion(rounded);

  g.setColour(juce::Colour(0xFF25252E));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);

  if (!sidebarBounds.isEmpty()) {
    g.setColour(juce::Colour(0xFF1F1F27));
    g.fillRect(sidebarBounds);
    g.setColour(juce::Colour(0xFF34343E));
    g.drawLine((float)sidebarBounds.getRight(), (float)sidebarBounds.getY(),
               (float)sidebarBounds.getRight(),
               (float)sidebarBounds.getBottom(), 1.0f);
  }

  if (!cardBounds.isEmpty()) {
    g.setColour(juce::Colour(0xFF31313B));
    g.fillRoundedRectangle(cardBounds.toFloat(), 8.0f);

    g.setColour(juce::Colour(0xFF40404A));
    g.drawRoundedRectangle(cardBounds.toFloat().reduced(0.5f), 8.0f, 1.0f);

    g.setColour(juce::Colour(0xFF3A3A45));
    for (int i = 0; i < separatorYs.size(); ++i) {
      int y = separatorYs[i];
      g.drawLine((float)cardBounds.getX() + 14.0f, (float)y,
                 (float)cardBounds.getRight() - 14.0f, (float)y, 1.0f);
    }
  }
}

void SettingsComponent::resized() {
  separatorYs.clear();

  // Update container size constraints
  updateLayoutConstraints();

  // Apply layout to fixed components
  layout.applyComponentVars(&generalTabButton, generalTabButtonVars);
  layout.applyComponentVars(&audioTabButton, audioTabButtonVars);
  layout.applyComponentVars(&titleLabel, titleVars);

  // Update bounds for painting (extract from solved variables)
  sidebarBounds = juce::Rectangle<int>(
    static_cast<int>(sidebarVars.left.value()),
    static_cast<int>(sidebarVars.top.value()),
    static_cast<int>(sidebarVars.width.value()),
    static_cast<int>(sidebarVars.height.value())
  );

  cardBounds = juce::Rectangle<int>(
    static_cast<int>(cardVars.left.value()),
    static_cast<int>(cardVars.top.value()),
    static_cast<int>(cardVars.width.value()),
    static_cast<int>(cardVars.height.value())
  );

  // Layout active tab content
  if (activeTab == SettingsTab::General) {
    layoutGeneralTab();
    // Add separator line position
    separatorYs.add(static_cast<int>(contentVars.top.value() + 20.0 + 6.0));
  } else if (!pluginMode && deviceManager != nullptr &&
             activeTab == SettingsTab::Audio) {
    layoutAudioTab();
    // Add separator line position
    separatorYs.add(static_cast<int>(contentVars.top.value() + 20.0 + 6.0));
  }
}

void SettingsComponent::comboBoxChanged(juce::ComboBox *comboBox) {
  if (comboBox == &languageComboBox) {
    int selectedId = languageComboBox.getSelectedId();
    if (selectedId == 1) {
      // Auto - detect system language
      Localization::detectSystemLanguage();
    } else if (selectedId >= 2) {
      // Get language code from index
      const auto &langs = Localization::getInstance().getAvailableLanguages();
      int langIndex = selectedId - 2;
      if (langIndex < static_cast<int>(langs.size()))
        Localization::getInstance().setLanguage(langs[langIndex].code);
    }
    saveSettings();

    if (onLanguageChanged)
      onLanguageChanged();
  } else if (comboBox == &uiScaleComboBox) {
    int selectedId = uiScaleComboBox.getSelectedId();
    DPIScaleManager::ScalePreset preset;
    switch (selectedId) {
      case 1: preset = DPIScaleManager::ScalePreset::Auto; break;
      case 2: preset = DPIScaleManager::ScalePreset::Scale100; break;
      case 3: preset = DPIScaleManager::ScalePreset::Scale125; break;
      case 4: preset = DPIScaleManager::ScalePreset::Scale150; break;
      case 5: preset = DPIScaleManager::ScalePreset::Scale200; break;
      default: preset = DPIScaleManager::ScalePreset::Auto; break;
    }
    DPIScaleManager::getInstance().setScalePreset(preset);
    saveSettings();
  } else if (comboBox == &deviceComboBox) {
    if (canChangeDevice && !canChangeDevice()) {
      for (int i = 0; i < deviceComboBox.getNumItems(); ++i) {
        if (deviceComboBox.getItemText(i) == lastConfirmedDevice) {
          deviceComboBox.setSelectedItemIndex(i,
                                              juce::dontSendNotification);
          break;
        }
      }
      currentDevice = lastConfirmedDevice;
      updateGPUDeviceList(currentDevice);
      gpuDeviceComboBox.setSelectedId(lastConfirmedGpuDeviceId + 1,
                                      juce::dontSendNotification);
      infoLabel.setText(
          "Inference in progress. Stop it to switch device.",
          juce::dontSendNotification);
      updateTabVisibility();
      resized();
      return;
    }

    currentDevice = deviceComboBox.getText();

    // Show/hide GPU device selector based on device type
    bool showGpuDeviceList = shouldShowGpuDeviceList();
    if (showGpuDeviceList) {
      // Update GPU device list for the selected device type
      updateGPUDeviceList(currentDevice);
    }
    updateTabVisibility();
    resized();

    saveSettings();

    // Update info label
    if (currentDevice == "CPU") {
      infoLabel.setText(TR("settings.cpu_desc"), juce::dontSendNotification);
    } else if (currentDevice == "CUDA") {
      infoLabel.setText(TR("settings.cuda_desc"), juce::dontSendNotification);
    } else if (currentDevice == "DirectML") {
      infoLabel.setText(TR("settings.directml_desc"),
                        juce::dontSendNotification);
    } else if (currentDevice == "CoreML") {
      infoLabel.setText(TR("settings.coreml_desc"), juce::dontSendNotification);
    }

    if (onSettingsChanged)
      onSettingsChanged();

    lastConfirmedDevice = currentDevice;
    lastConfirmedGpuDeviceId = gpuDeviceId;
  } else if (comboBox == &gpuDeviceComboBox) {
    if (canChangeDevice && !canChangeDevice()) {
      gpuDeviceComboBox.setSelectedId(lastConfirmedGpuDeviceId + 1,
                                      juce::dontSendNotification);
      infoLabel.setText(
          "Inference in progress. Stop it to switch device.",
          juce::dontSendNotification);
      return;
    }
    gpuDeviceId = gpuDeviceComboBox.getSelectedId() - 1;
    saveSettings();
    if (onSettingsChanged)
      onSettingsChanged();
    lastConfirmedGpuDeviceId = gpuDeviceId;
  } else if (comboBox == &pitchDetectorComboBox) {
    int selectedId = pitchDetectorComboBox.getSelectedId();
    if (selectedId == 1)
      pitchDetectorType = PitchDetectorType::RMVPE;
    else if (selectedId == 2)
      pitchDetectorType = PitchDetectorType::FCPE;

    saveSettings();

    if (onPitchDetectorChanged)
      onPitchDetectorChanged(pitchDetectorType);
  } else if (comboBox == &audioDeviceTypeComboBox) {
    int idx = audioDeviceTypeComboBox.getSelectedId() - 1;
    if (idx >= 0 && idx < audioDeviceTypeOrder.size()) {
      deviceManager->setCurrentAudioDeviceType(
          audioDeviceTypeOrder.getReference(idx)->getTypeName(), true);
      updateAudioOutputDevices(true);
    }
  } else if (comboBox == &audioOutputComboBox) {
    applyAudioSettings();
    updateSampleRates();
    updateBufferSizes();
  } else if (comboBox == &sampleRateComboBox ||
             comboBox == &bufferSizeComboBox ||
             comboBox == &outputChannelsComboBox) {
    applyAudioSettings();
  }
}

bool SettingsComponent::shouldShowGpuDeviceList() const {
  return currentDevice == "CUDA" || currentDevice == "DirectML";
}

void SettingsComponent::setActiveTab(SettingsTab tab) {
  if (activeTab == tab)
    return;

  activeTab = tab;
  updateTabButtonStyles();
  updateTabVisibility();
  resized();
  repaint();
}

void SettingsComponent::updateTabButtonStyles() {
  auto applyStyle = [&](juce::TextButton &button, bool isActive) {
    if (isActive) {
      button.setColour(juce::TextButton::buttonColourId,
                       juce::Colour(APP_COLOR_PRIMARY));
      button.setColour(juce::TextButton::textColourOffId,
                       juce::Colours::white);
    } else {
      button.setColour(juce::TextButton::buttonColourId,
                       juce::Colour(0xFF2B2B34));
      button.setColour(juce::TextButton::textColourOffId,
                       juce::Colour(0xFFC6C6D0));
    }
  };

  applyStyle(generalTabButton, activeTab == SettingsTab::General);
  applyStyle(audioTabButton, activeTab == SettingsTab::Audio);
}

void SettingsComponent::updateTabVisibility() {
  const bool showGeneral = (activeTab == SettingsTab::General);
  const bool showAudio =
      (!pluginMode && deviceManager != nullptr &&
       activeTab == SettingsTab::Audio);
  const bool showGpuDeviceList = shouldShowGpuDeviceList();

  generalSectionLabel.setVisible(showGeneral);
  languageLabel.setVisible(showGeneral);
  languageComboBox.setVisible(showGeneral);
  deviceLabel.setVisible(showGeneral);
  deviceComboBox.setVisible(showGeneral);
  gpuDeviceLabel.setVisible(showGeneral && showGpuDeviceList);
  gpuDeviceComboBox.setVisible(showGeneral && showGpuDeviceList);
  pitchDetectorLabel.setVisible(showGeneral);
  pitchDetectorComboBox.setVisible(showGeneral);
  infoLabel.setVisible(showGeneral);

  audioSectionLabel.setVisible(showAudio);
  audioDeviceTypeLabel.setVisible(showAudio);
  audioDeviceTypeComboBox.setVisible(showAudio);
  audioOutputLabel.setVisible(showAudio);
  audioOutputComboBox.setVisible(showAudio);
  sampleRateLabel.setVisible(showAudio);
  sampleRateComboBox.setVisible(showAudio);
  bufferSizeLabel.setVisible(showAudio);
  bufferSizeComboBox.setVisible(showAudio);
  outputChannelsLabel.setVisible(showAudio);
  outputChannelsComboBox.setVisible(showAudio);

  audioTabButton.setVisible(!pluginMode && deviceManager != nullptr);

  if (pluginMode || deviceManager == nullptr)
    setActiveTab(SettingsTab::General);
}

void SettingsComponent::updateDeviceList() {
  deviceComboBox.clear();

  auto devices = getAvailableDevices();
  int selectedIndex = 0;

  // Auto-select based on compile-time flags (first run only)
  if (!hasLoadedSettings && currentDevice == "CPU") {
#ifdef USE_DIRECTML
    // If DirectML is compiled in, default to DirectML
    for (int i = 0; i < devices.size(); ++i) {
      if (devices[i] == "DirectML") {
        selectedIndex = i;
        currentDevice = devices[i];
        DBG("Auto-selecting DirectML (compiled in)");
        break;
      }
    }
#elif defined(USE_CUDA)
    // If CUDA is compiled in, default to CUDA
    for (int i = 0; i < devices.size(); ++i) {
      if (devices[i] == "CUDA") {
        selectedIndex = i;
        currentDevice = devices[i];
        DBG("Auto-selecting CUDA (compiled in)");
        break;
      }
    }
#else
    // No GPU compiled in, stay on CPU
    DBG("No GPU provider compiled in, using CPU");
#endif
  }

  for (int i = 0; i < devices.size(); ++i) {
    deviceComboBox.addItem(devices[i], i + 1);
    if (devices[i] == currentDevice)
      selectedIndex = i;
  }

  deviceComboBox.setSelectedItemIndex(selectedIndex,
                                      juce::dontSendNotification);

  // Update info for initially selected device
  comboBoxChanged(&deviceComboBox);
}

void SettingsComponent::updateGPUDeviceList(const juce::String &deviceType) {
  gpuDeviceComboBox.clear();

  if (deviceType == "CPU") {
    // No GPU devices for CPU
    return;
  }

#ifdef HAVE_ONNXRUNTIME
  if (deviceType == "CUDA") {
#ifdef USE_CUDA
    int deviceCount = 0;
    bool devicesDetected = false;
    juce::StringArray cudaDeviceNames;

    // Try to load CUDA runtime library to get actual device count and names
#ifdef _WIN32
    const char *cudaDllNames[] = {
        "cudart64_12.dll", // CUDA 12.x
        "cudart64_11.dll", // CUDA 11.x
        "cudart64_10.dll", // CUDA 10.x
        "cudart64.dll"     // Generic
    };

    HMODULE cudaLib = nullptr;
    for (const char *dllName : cudaDllNames) {
      cudaLib = LoadLibraryA(dllName);
      if (cudaLib) {
        DBG("Loaded CUDA runtime: " + juce::String(dllName));
        break;
      }
    }

    if (cudaLib) {
      typedef int (*cudaGetDeviceCountFunc)(int *);
      typedef int (*cudaGetDevicePropertiesFunc)(void *, int);

      auto cudaGetDeviceCount =
          (cudaGetDeviceCountFunc)GetProcAddress(cudaLib, "cudaGetDeviceCount");

      if (cudaGetDeviceCount) {
        int result = cudaGetDeviceCount(&deviceCount);
        if (result == 0 && deviceCount > 0) {
          DBG("CUDA device count: " + juce::String(deviceCount));

          // Try to get device properties for names
          auto cudaGetDeviceProperties =
              (cudaGetDevicePropertiesFunc)GetProcAddress(
                  cudaLib, "cudaGetDeviceProperties");

          for (int deviceId = 0; deviceId < deviceCount; ++deviceId) {
            juce::String deviceName = "GPU " + juce::String(deviceId);

            // Try to get device name from properties
            if (cudaGetDeviceProperties) {
              // Allocate full cudaDeviceProp structure (it's large, ~1KB)
              // We can't use the actual struct without CUDA headers, so
              // allocate enough space
              char propBuffer[2048]; // Large enough for cudaDeviceProp
              memset(propBuffer, 0, sizeof(propBuffer));

              if (cudaGetDeviceProperties(propBuffer, deviceId) == 0) {
                // Device name is at the start of the structure
                char *name = propBuffer;
                if (name[0] != '\0') {
                  deviceName = juce::String(name);
                  DBG("CUDA device " + juce::String(deviceId) + ": " +
                      deviceName);
                }
              }
            }

            cudaDeviceNames.add(deviceName + " (CUDA)");
          }
          devicesDetected = true;
        } else {
          DBG("cudaGetDeviceCount failed or returned 0 devices");
        }
      }
      FreeLibrary(cudaLib);
    } else {
      DBG("Failed to load CUDA runtime library");
    }
#endif

    if (devicesDetected && cudaDeviceNames.size() > 0) {
      for (int i = 0; i < cudaDeviceNames.size(); ++i)
        gpuDeviceComboBox.addItem(cudaDeviceNames[i], i + 1);
    } else {
#ifdef _WIN32
      auto dxgiNames = getDxgiAdapterNames();
      if (dxgiNames.size() > 0) {
        for (int i = 0; i < dxgiNames.size(); ++i)
          gpuDeviceComboBox.addItem(dxgiNames[i] + " (DXGI)", i + 1);
      }
#endif
    }

    // If no devices detected, add default
    if (gpuDeviceComboBox.getNumItems() == 0) {
      gpuDeviceComboBox.addItem("GPU 0 (CUDA)", 1);
      DBG("No CUDA devices detected, using default GPU 0");
    }
#else
    // CUDA not compiled in, but provider is available
    // This shouldn't happen, but add default option
    gpuDeviceComboBox.addItem("GPU 0 (CUDA)", 1);
    DBG("CUDA provider available but USE_CUDA not defined");
#endif
  } else if (deviceType == "DirectML") {
#ifdef USE_DIRECTML
    bool addedFromDxgi = false;
#ifdef _WIN32
    auto dxgiNames = getDxgiAdapterNames();
    if (dxgiNames.size() > 0) {
      for (int i = 0; i < dxgiNames.size(); ++i)
        gpuDeviceComboBox.addItem(dxgiNames[i] + " (DirectML)", i + 1);
      addedFromDxgi = true;
    }
#endif
    if (!addedFromDxgi) {
      // DirectML fallback: provide a small default list
      for (int deviceId = 0; deviceId < 4; ++deviceId) {
        gpuDeviceComboBox.addItem(
            "GPU " + juce::String(deviceId) + " (DirectML)", deviceId + 1);
      }
    }
#else
    // DirectML not compiled in
    gpuDeviceComboBox.addItem("GPU 0 (DirectML)", 1);
    DBG("DirectML provider available but USE_DIRECTML not defined");
#endif
  } else {
    // Other GPU providers (CoreML, TensorRT) - use default device
    gpuDeviceComboBox.addItem(TR("settings.default_gpu"), 1);
  }

  // Set default selection
  if (gpuDeviceComboBox.getNumItems() > 0) {
    // Try to restore saved selection, or use first device
    int savedId = gpuDeviceId + 1;
    if (savedId > 0 && savedId <= gpuDeviceComboBox.getNumItems())
      gpuDeviceComboBox.setSelectedId(savedId, juce::dontSendNotification);
    else
      gpuDeviceComboBox.setSelectedId(1, juce::dontSendNotification);
  }
#endif
}

juce::StringArray SettingsComponent::getAvailableDevices() {
  juce::StringArray devices;

  // CPU is always available
  devices.add("CPU");

#ifdef HAVE_ONNXRUNTIME
  try {
    // Get providers that are compiled into the ONNX Runtime library
    auto availableProviders = Ort::GetAvailableProviders();

    // Check which providers are available
    bool hasCuda = false, hasDml = false, hasCoreML = false,
         hasTensorRT = false;

    DBG("=== ONNX Runtime Provider Detection ===");
    DBG("Total providers found: " + juce::String(availableProviders.size()));
    DBG("Available ONNX Runtime providers:");
    for (const auto &provider : availableProviders) {
      juce::String providerStr(provider);
      DBG("  - " + providerStr);

      if (providerStr == "CUDAExecutionProvider")
        hasCuda = true;
      else if (providerStr == "DmlExecutionProvider")
        hasDml = true;
      else if (providerStr == "CoreMLExecutionProvider")
        hasCoreML = true;
      else if (providerStr == "TensorrtExecutionProvider")
        hasTensorRT = true;
    }

    // Add available GPU providers based on compile-time flags
    // DML and CUDA are mutually exclusive
#ifdef USE_DIRECTML
    if (hasDml) {
      devices.add("DirectML");
      DBG("DirectML provider: ENABLED");
    }
#elif defined(USE_CUDA)
    if (hasCuda) {
      devices.add("CUDA");
      DBG("CUDA provider: ENABLED");
    }
#else
    // No GPU compiled in, but show available providers for information
    if (hasCuda) {
      devices.add("CUDA");
      DBG("CUDA provider: AVAILABLE (not compiled in)");
    }
    if (hasDml) {
      devices.add("DirectML");
      DBG("DirectML provider: AVAILABLE (not compiled in)");
    }
#endif
    if (hasCoreML) {
      devices.add("CoreML");
      DBG("CoreML provider: ENABLED");
    }
    if (hasTensorRT) {
      devices.add("TensorRT");
      DBG("TensorRT provider: ENABLED");
    }

    // If no GPU providers found, show info about how to enable them
    if (!hasCuda && !hasDml && !hasCoreML && !hasTensorRT) {
      DBG("WARNING: No GPU execution providers available in this ONNX Runtime "
          "build.");
      DBG("This appears to be a CPU-only build of ONNX Runtime.");
      DBG("To enable GPU acceleration:");
      DBG("  - Windows DirectML: Use onnxruntime-directml package");
      DBG("  - NVIDIA CUDA: Use onnxruntime-gpu package (requires CUDA "
          "toolkit)");
    }
  } catch (const std::exception &e) {
    DBG("ERROR: Failed to get ONNX Runtime providers: " +
        juce::String(e.what()));
    DBG("Falling back to CPU-only mode.");
  }
#else
  DBG("WARNING: HAVE_ONNXRUNTIME not defined - only CPU available");
  DBG("To enable GPU support, ensure ONNX Runtime is properly configured in "
      "CMakeLists.txt");
#endif

  DBG("Final device list: " + devices.joinIntoString(", "));
  return devices;
}

void SettingsComponent::loadSettings() {
  const auto &langs = Localization::getInstance().getAvailableLanguages();

  if (settingsManager)
    settingsManager->loadConfig();

  if (settingsManager) {
    currentDevice = settingsManager->getDevice();
    gpuDeviceId = settingsManager->getGPUDeviceId();
    pitchDetectorType = settingsManager->getPitchDetectorType();

    auto langCode = settingsManager->getLanguage();
    if (langCode == "auto") {
      Localization::detectSystemLanguage();
      languageComboBox.setSelectedId(1, juce::dontSendNotification);
    } else {
      Localization::getInstance().setLanguage(langCode);
      for (int i = 0; i < static_cast<int>(langs.size()); ++i) {
        if (langs[i].code == langCode) {
          languageComboBox.setSelectedId(i + 2, juce::dontSendNotification);
          break;
        }
      }
    }
  }

  // Load UI scale setting
  auto scalePreset = DPIScaleManager::getInstance().getScalePreset();
  int scaleId = 1; // Default to Auto
  switch (scalePreset) {
    case DPIScaleManager::ScalePreset::Auto: scaleId = 1; break;
    case DPIScaleManager::ScalePreset::Scale100: scaleId = 2; break;
    case DPIScaleManager::ScalePreset::Scale125: scaleId = 3; break;
    case DPIScaleManager::ScalePreset::Scale150: scaleId = 4; break;
    case DPIScaleManager::ScalePreset::Scale200: scaleId = 5; break;
  }
  uiScaleComboBox.setSelectedId(scaleId, juce::dontSendNotification);

  // Update the ComboBox selection to match loaded settings
  for (int i = 0; i < deviceComboBox.getNumItems(); ++i) {
    if (deviceComboBox.getItemText(i) == currentDevice) {
      deviceComboBox.setSelectedItemIndex(i, juce::dontSendNotification);
      break;
    }
  }

  // Update GPU device ID and visibility
  bool showGpuDeviceList =
      (currentDevice == "CUDA" || currentDevice == "DirectML");
  if (showGpuDeviceList) {
    // Update GPU device list for the loaded device type
    updateGPUDeviceList(currentDevice);
  }
  gpuDeviceComboBox.setSelectedId(gpuDeviceId + 1, juce::dontSendNotification);
  gpuDeviceLabel.setVisible(showGpuDeviceList);
  gpuDeviceComboBox.setVisible(showGpuDeviceList);

  // Update pitch detector combo box
  if (pitchDetectorType == PitchDetectorType::RMVPE)
    pitchDetectorComboBox.setSelectedId(1, juce::dontSendNotification);
  else if (pitchDetectorType == PitchDetectorType::FCPE)
    pitchDetectorComboBox.setSelectedId(2, juce::dontSendNotification);

  hasLoadedSettings = true;
  lastConfirmedDevice = currentDevice;
  lastConfirmedGpuDeviceId = gpuDeviceId;
}

void SettingsComponent::saveSettings() {
  // Don't save if combo box not initialized yet
  if (languageComboBox.getSelectedId() == 0)
    return;

  // Save language code
  int langId = languageComboBox.getSelectedId();
  juce::String langCode = "auto";
  if (langId >= 2) {
    const auto &langs = Localization::getInstance().getAvailableLanguages();
    int langIndex = langId - 2;
    if (langIndex < static_cast<int>(langs.size()))
      langCode = langs[langIndex].code;
  }

  if (settingsManager) {
    settingsManager->setDevice(currentDevice);
    settingsManager->setGPUDeviceId(gpuDeviceId);
    settingsManager->setPitchDetectorType(pitchDetectorType);
    settingsManager->setLanguage(langCode);
    settingsManager->saveConfig();
  }
}

void SettingsComponent::updateAudioDeviceTypes() {
  if (deviceManager == nullptr)
    return;

  audioDeviceTypeComboBox.clear();
  audioDeviceTypeOrder.clear();

  auto &types = deviceManager->getAvailableDeviceTypes();
  juce::AudioIODeviceType *asioType = nullptr;
  for (int i = 0; i < types.size(); ++i) {
    if (types[i]->getTypeName() == "ASIO") {
      asioType = types[i];
    } else {
      audioDeviceTypeOrder.add(types[i]);
    }
  }

  if (asioType != nullptr)
    audioDeviceTypeOrder.insert(0, asioType);

  for (int i = 0; i < audioDeviceTypeOrder.size(); ++i)
    audioDeviceTypeComboBox.addItem(
        audioDeviceTypeOrder[i]->getTypeName(), i + 1);

  if (auto *currentType = deviceManager->getCurrentDeviceTypeObject()) {
    for (int i = 0; i < audioDeviceTypeOrder.size(); ++i) {
      if (audioDeviceTypeOrder[i] == currentType) {
        audioDeviceTypeComboBox.setSelectedId(i + 1,
                                              juce::dontSendNotification);
        break;
      }
    }
  }
  updateAudioOutputDevices(true);
}

void SettingsComponent::updateAudioOutputDevices(bool force) {
  if (deviceManager == nullptr)
    return;

  if (auto *currentType = deviceManager->getCurrentDeviceTypeObject()) {
    currentType->scanForDevices();
    auto devices = currentType->getDeviceNames(false); // false = output devices
    juce::String currentName;
    if (auto *audioDevice = deviceManager->getCurrentAudioDevice())
      currentName = audioDevice->getName();

    if (!force && devices == cachedOutputDevices &&
        currentName == cachedOutputDeviceName &&
        currentType->getTypeName() == cachedDeviceTypeName) {
      return;
    }

    cachedOutputDevices = devices;
    cachedOutputDeviceName = currentName;
    cachedDeviceTypeName = currentType->getTypeName();

    audioOutputComboBox.clear();
    for (int i = 0; i < devices.size(); ++i)
      audioOutputComboBox.addItem(devices[i], i + 1);

    for (int i = 0; i < devices.size(); ++i) {
      if (devices[i] == currentName) {
        audioOutputComboBox.setSelectedId(i + 1, juce::dontSendNotification);
        break;
      }
    }

    if (audioOutputComboBox.getSelectedId() == 0 && devices.size() > 0)
      audioOutputComboBox.setSelectedId(1, juce::dontSendNotification);
  }
  updateSampleRates();
  updateBufferSizes();
}

void SettingsComponent::updateSampleRates() {
  if (deviceManager == nullptr)
    return;

  sampleRateComboBox.clear();
  if (auto *device = deviceManager->getCurrentAudioDevice()) {
    auto rates = device->getAvailableSampleRates();
    double currentRate = device->getCurrentSampleRate();
    for (int i = 0; i < rates.size(); ++i) {
      sampleRateComboBox.addItem(
          juce::String(static_cast<int>(rates[i])) + " Hz", i + 1);
      if (std::abs(rates[i] - currentRate) < 1.0)
        sampleRateComboBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
  }
}

void SettingsComponent::updateBufferSizes() {
  if (deviceManager == nullptr)
    return;

  bufferSizeComboBox.clear();
  if (auto *device = deviceManager->getCurrentAudioDevice()) {
    auto sizes = device->getAvailableBufferSizes();
    int currentSize = device->getCurrentBufferSizeSamples();
    for (int i = 0; i < sizes.size(); ++i) {
      bufferSizeComboBox.addItem(juce::String(sizes[i]) + " samples", i + 1);
      if (sizes[i] == currentSize)
        bufferSizeComboBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
  }
}

void SettingsComponent::applyAudioSettings() {
  if (deviceManager == nullptr)
    return;

  auto setup = deviceManager->getAudioDeviceSetup();

  // Get selected output device
  if (auto *currentType = deviceManager->getCurrentDeviceTypeObject()) {
    auto devices = currentType->getDeviceNames(false);
    int outputIdx = audioOutputComboBox.getSelectedId() - 1;
    if (outputIdx >= 0 && outputIdx < devices.size())
      setup.outputDeviceName = devices[outputIdx];
  }

  // Get selected sample rate
  if (auto *device = deviceManager->getCurrentAudioDevice()) {
    auto rates = device->getAvailableSampleRates();
    int rateIdx = sampleRateComboBox.getSelectedId() - 1;
    if (rateIdx >= 0 && rateIdx < rates.size())
      setup.sampleRate = rates[rateIdx];

    auto sizes = device->getAvailableBufferSizes();
    int sizeIdx = bufferSizeComboBox.getSelectedId() - 1;
    if (sizeIdx >= 0 && sizeIdx < sizes.size())
      setup.bufferSize = sizes[sizeIdx];
  }

  // Output channels
  int channels = outputChannelsComboBox.getSelectedId();
  setup.outputChannels.setRange(0, channels, true);

  deviceManager->setAudioDeviceSetup(setup, true);
}

//==============================================================================
// SettingsOverlay
//==============================================================================

SettingsOverlay::SettingsOverlay(SettingsManager *settingsManager,
                                 juce::AudioDeviceManager *audioDeviceManager) {
  setOpaque(false);
  setInterceptsMouseClicks(true, true);
  setWantsKeyboardFocus(true);

  settingsComponent =
      std::make_unique<SettingsComponent>(settingsManager, audioDeviceManager);
  addAndMakeVisible(settingsComponent.get());

  closeButton.setColour(juce::TextButton::buttonColourId,
                        juce::Colour(0xFF3A3A45));
  closeButton.setColour(juce::TextButton::textColourOffId,
                        juce::Colour(0xFFD6D6DE));
  closeButton.setColour(juce::TextButton::buttonOnColourId,
                        juce::Colour(0xFF4A4A55));
  closeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
  closeButton.setLookAndFeel(&DarkLookAndFeel::getInstance());
  closeButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  closeButton.onClick = [this]() { closeIfPossible(); };
  addAndMakeVisible(closeButton);
}

SettingsOverlay::~SettingsOverlay() { closeButton.setLookAndFeel(nullptr); }

void SettingsOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xB0000000));

  if (!contentBounds.isEmpty()) {
    juce::DropShadow shadow(juce::Colour(0x90000000), 18, {0, 10});
    shadow.drawForRectangle(g, contentBounds);
  }
}

void SettingsOverlay::resized() {
  auto bounds = getLocalBounds();

  if (settingsComponent != nullptr) {
    const int preferredWidth = settingsComponent->getWidth();
    const int preferredHeight = settingsComponent->getHeight();
    const int maxWidth = juce::jmax(420, bounds.getWidth() - 80);
    const int maxHeight = juce::jmax(320, bounds.getHeight() - 80);
    const int contentWidth = juce::jmin(preferredWidth, maxWidth);
    const int contentHeight = juce::jmin(preferredHeight, maxHeight);
    contentBounds = juce::Rectangle<int>(0, 0, contentWidth, contentHeight)
                        .withCentre(bounds.getCentre());
    settingsComponent->setBounds(contentBounds);

    const int buttonSize = 24;
    closeButton.setBounds(contentBounds.getRight() - buttonSize - 10,
                          contentBounds.getY() + 8, buttonSize, buttonSize);
  }
}

void SettingsOverlay::mouseDown(const juce::MouseEvent &e) {
  if (!contentBounds.contains(e.getPosition()))
    closeIfPossible();
}

bool SettingsOverlay::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    closeIfPossible();
    return true;
  }
  return false;
}

void SettingsOverlay::closeIfPossible() {
  if (onClose)
    onClose();
}

//==============================================================================
// SettingsDialog
//==============================================================================

SettingsDialog::SettingsDialog(SettingsManager *settingsManager,
                               juce::AudioDeviceManager *audioDeviceManager)
    : DialogWindow("Settings", juce::Colour(APP_COLOR_BACKGROUND), true) {
  // Set opaque before any other operations - this must be done first
  setOpaque(true);

  // Create and configure content component
  settingsComponent =
      std::make_unique<SettingsComponent>(settingsManager, audioDeviceManager);

  // Ensure content component is also opaque before setting it
  if (settingsComponent)
    settingsComponent->setOpaque(true);

  // Set content before native title bar
  setContentOwned(settingsComponent.get(), false);

  // Now set native title bar after content is set and opaque
  setUsingNativeTitleBar(true);

  // Set window properties
  setResizable(false, false);

  int dialogWidth = 460;
  int dialogHeight = audioDeviceManager != nullptr ? 600 : 320;
  if (settingsComponent != nullptr) {
    dialogWidth = settingsComponent->getWidth();
    dialogHeight = settingsComponent->getHeight();
  }
  centreWithSize(dialogWidth, dialogHeight);
}

void SettingsDialog::closeButtonPressed() { setVisible(false); }

void SettingsDialog::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(APP_COLOR_BACKGROUND));
}
