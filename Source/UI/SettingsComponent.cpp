#include "SettingsComponent.h"
#include "../Utils/Constants.h"
#include <thread>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

//==============================================================================
// SettingsComponent
//==============================================================================

SettingsComponent::SettingsComponent()
{
    // Title
    titleLabel.setText("Settings", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);
    
    // Device selection
    deviceLabel.setText("Inference Device:", juce::dontSendNotification);
    deviceLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(deviceLabel);
    
    deviceComboBox.addListener(this);
    addAndMakeVisible(deviceComboBox);
    updateDeviceList();
    
    // Thread count
    threadsLabel.setText("Thread Count:", juce::dontSendNotification);
    threadsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(threadsLabel);
    
    threadsSlider.setRange(0, 32, 1);
    threadsSlider.setValue(0);
    threadsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    threadsSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    threadsSlider.onValueChange = [this]()
    {
        numThreads = static_cast<int>(threadsSlider.getValue());
        if (numThreads == 0)
        {
            int autoThreads = std::thread::hardware_concurrency();
            threadsValueLabel.setText("Auto (" + juce::String(autoThreads) + " cores)", 
                                      juce::dontSendNotification);
        }
        else
        {
            threadsValueLabel.setText(juce::String(numThreads), juce::dontSendNotification);
        }
        saveSettings();
        if (onSettingsChanged)
            onSettingsChanged();
    };
    addAndMakeVisible(threadsSlider);
    
    threadsValueLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(threadsValueLabel);
    
    // Info label
    infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    infoLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(infoLabel);
    
    // Load saved settings
    loadSettings();
    
    // Update UI
    threadsSlider.setValue(numThreads, juce::dontSendNotification);
    if (numThreads == 0)
    {
        int autoThreads = std::thread::hardware_concurrency();
        threadsValueLabel.setText("Auto (" + juce::String(autoThreads) + " cores)", 
                                  juce::dontSendNotification);
    }
    else
    {
        threadsValueLabel.setText(juce::String(numThreads), juce::dontSendNotification);
    }
    
    setSize(400, 250);
}

void SettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2d2d2d));
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(20);
    
    // Device selection row
    auto deviceRow = bounds.removeFromTop(30);
    deviceLabel.setBounds(deviceRow.removeFromLeft(120));
    deviceComboBox.setBounds(deviceRow.reduced(0, 2));
    bounds.removeFromTop(10);
    
    // Threads row
    auto threadsRow = bounds.removeFromTop(30);
    threadsLabel.setBounds(threadsRow.removeFromLeft(120));
    threadsValueLabel.setBounds(threadsRow.removeFromRight(100));
    threadsSlider.setBounds(threadsRow.reduced(0, 2));
    bounds.removeFromTop(20);
    
    // Info label
    infoLabel.setBounds(bounds.removeFromTop(60));
}

void SettingsComponent::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &deviceComboBox)
    {
        currentDevice = deviceComboBox.getText();
        saveSettings();
        
        // Update info label
        if (currentDevice == "CPU")
        {
            infoLabel.setText("CPU: Uses your processor for inference.\n"
                              "Most compatible, moderate speed.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "CUDA")
        {
            infoLabel.setText("CUDA: Uses NVIDIA GPU for inference.\n"
                              "Fastest option if you have an NVIDIA GPU.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "DirectML")
        {
            infoLabel.setText("DirectML: Uses GPU via DirectX 12.\n"
                              "Works with most GPUs on Windows.",
                              juce::dontSendNotification);
        }
        else if (currentDevice == "CoreML")
        {
            infoLabel.setText("CoreML: Uses Apple Neural Engine or GPU.\n"
                              "Best option on macOS/iOS devices.",
                              juce::dontSendNotification);
        }
        
        if (onSettingsChanged)
            onSettingsChanged();
    }
}

void SettingsComponent::updateDeviceList()
{
    deviceComboBox.clear();
    
    auto devices = getAvailableDevices();
    int selectedIndex = 0;
    
    for (int i = 0; i < devices.size(); ++i)
    {
        deviceComboBox.addItem(devices[i], i + 1);
        if (devices[i] == currentDevice)
            selectedIndex = i;
    }
    
    deviceComboBox.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
    
    // Update info for initially selected device
    comboBoxChanged(&deviceComboBox);
}

juce::StringArray SettingsComponent::getAvailableDevices()
{
    juce::StringArray devices;
    
    // CPU is always available
    devices.add("CPU");
    
#ifdef HAVE_ONNXRUNTIME
    // Get providers that are compiled into the ONNX Runtime library
    auto availableProviders = Ort::GetAvailableProviders();
    
    // Log available providers for debugging
    DBG("Available ONNX Runtime providers:");
    for (const auto& provider : availableProviders)
    {
        DBG("  - " + juce::String(provider));
    }
    
    // Check which providers are available
    bool hasCuda = false, hasDml = false, hasCoreML = false, hasTensorRT = false;
    
    for (const auto& provider : availableProviders)
    {
        if (provider == "CUDAExecutionProvider")
            hasCuda = true;
        else if (provider == "DmlExecutionProvider")
            hasDml = true;
        else if (provider == "CoreMLExecutionProvider")
            hasCoreML = true;
        else if (provider == "TensorrtExecutionProvider")
            hasTensorRT = true;
    }
    
    // Add available GPU providers
    if (hasCuda)
        devices.add("CUDA");
    if (hasDml)
        devices.add("DirectML");
    if (hasCoreML)
        devices.add("CoreML");
    if (hasTensorRT)
        devices.add("TensorRT");
    
    // If no GPU providers found, show info about how to enable them
    if (!hasCuda && !hasDml && !hasCoreML && !hasTensorRT)
    {
        DBG("No GPU execution providers available in this ONNX Runtime build.");
        DBG("To enable GPU acceleration:");
        DBG("  - Windows DirectML: Download onnxruntime-directml package");
        DBG("  - NVIDIA CUDA: Download onnxruntime-gpu package");
    }
#endif
    
    return devices;
}

void SettingsComponent::loadSettings()
{
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("PitchEditor")
                            .getChildFile("settings.xml");
    
    if (settingsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(settingsFile);
        if (xml != nullptr)
        {
            currentDevice = xml->getStringAttribute("device", "CPU");
            numThreads = xml->getIntAttribute("threads", 0);
            DBG("Loaded settings: device=" + currentDevice + ", threads=" + juce::String(numThreads));
        }
    }
    
    // Update the ComboBox selection to match loaded settings
    for (int i = 0; i < deviceComboBox.getNumItems(); ++i)
    {
        if (deviceComboBox.getItemText(i) == currentDevice)
        {
            deviceComboBox.setSelectedItemIndex(i, juce::dontSendNotification);
            break;
        }
    }
}

void SettingsComponent::saveSettings()
{
    auto settingsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("PitchEditor");
    settingsDir.createDirectory();
    
    auto settingsFile = settingsDir.getChildFile("settings.xml");
    
    juce::XmlElement xml("PitchEditorSettings");
    xml.setAttribute("device", currentDevice);
    xml.setAttribute("threads", numThreads);
    
    xml.writeTo(settingsFile);
}

//==============================================================================
// SettingsDialog
//==============================================================================

SettingsDialog::SettingsDialog()
    : DialogWindow("Settings", juce::Colour(0xff2d2d2d), true)
{
    setContentOwned(&settingsComponent, false);
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    centreWithSize(400, 250);
}

void SettingsDialog::closeButtonPressed()
{
    setVisible(false);
}
