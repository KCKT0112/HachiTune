#include "ExportHelper.h"
#include <climits>
#include <cmath>

namespace ExportHelper {

namespace
{
juce::String normalizeExtension(juce::String extension)
{
  extension = extension.trim().toLowerCase();

  while (extension.startsWithChar('.'))
    extension = extension.substring(1);

  return extension;
}
}

juce::String getFormatDisplayName(ExportFormat format) {
  switch (format) {
  case ExportFormat::wav:
    return "WAV";
  case ExportFormat::flac:
    return "FLAC";
  case ExportFormat::aiff:
    return "AIFF";
  case ExportFormat::ogg:
    return "OGG";
  }
  return "WAV";
}

juce::String getFormatExtension(ExportFormat format) {
  switch (format) {
  case ExportFormat::wav:
    return "wav";
  case ExportFormat::flac:
    return "flac";
  case ExportFormat::aiff:
    return "aiff";
  case ExportFormat::ogg:
    return "ogg";
  }
  return "wav";
}

juce::String getFormatWildcard(ExportFormat format) {
  return "*." + getFormatExtension(format);
}

juce::AudioBuffer<float> convertChannels(const juce::AudioBuffer<float> &input,
                                          int outChannels) {
  outChannels = juce::jlimit(1, 2, outChannels);
  const int inChannels = juce::jmax(1, input.getNumChannels());
  const int numSamples = input.getNumSamples();

  juce::AudioBuffer<float> output(outChannels, numSamples);
  output.clear();

  if (outChannels == 1) {
    float *dst = output.getWritePointer(0);
    for (int i = 0; i < numSamples; ++i) {
      float sum = 0.0f;
      for (int ch = 0; ch < inChannels; ++ch)
        sum += input.getSample(ch, i);
      dst[i] = sum / static_cast<float>(inChannels);
    }
    return output;
  }

  // Stereo output
  if (inChannels == 1) {
    output.copyFrom(0, 0, input, 0, 0, numSamples);
    output.copyFrom(1, 0, input, 0, 0, numSamples);
  } else {
    output.copyFrom(0, 0, input, 0, 0, numSamples);
    output.copyFrom(1, 0, input, 1, 0, numSamples);
  }
  return output;
}

juce::AudioBuffer<float> resampleAudio(const juce::AudioBuffer<float> &input,
                                        int sourceRate, int targetRate) {
  if (sourceRate <= 0 || targetRate <= 0 || sourceRate == targetRate)
    return input;

  const int channels = juce::jmax(1, input.getNumChannels());
  const int inSamples = input.getNumSamples();
  const double ratio = static_cast<double>(sourceRate) / targetRate;
  const int outSamples = juce::jmax(1, static_cast<int>(std::llround(inSamples / ratio)));

  juce::AudioBuffer<float> output(channels, outSamples);
  output.clear();

  for (int ch = 0; ch < channels; ++ch) {
    juce::LagrangeInterpolator interpolator;
    interpolator.reset();
    interpolator.process(ratio, input.getReadPointer(ch), output.getWritePointer(ch),
                         outSamples);
  }
  return output;
}

static std::optional<int> parseFirstInt(const juce::String &s) {
  juce::String digits;
  bool started = false;
  for (auto c : s) {
    if (juce::CharacterFunctions::isDigit(c)) {
      digits += juce::String::charToString(c);
      started = true;
    } else if (started) {
      break;
    }
  }
  if (digits.isEmpty())
    return std::nullopt;
  return digits.getIntValue();
}

int chooseQualityIndex(const juce::StringArray &options, int targetKbps) {
  if (options.isEmpty())
    return 0;

  int bestIdx = 0;
  int bestDist = INT_MAX;
  for (int i = 0; i < options.size(); ++i) {
    auto parsed = parseFirstInt(options[i]);
    if (!parsed.has_value())
      continue;
    const int dist = std::abs(parsed.value() - targetKbps);
    if (dist < bestDist) {
      bestDist = dist;
      bestIdx = i;
    }
  }
  return bestIdx;
}

juce::AudioFormat *findFormatForExtension(juce::AudioFormatManager &manager,
                                          const juce::String &extension) {
  const auto normalizedTarget = normalizeExtension(extension);
  for (int i = 0; i < manager.getNumKnownFormats(); ++i) {
    auto *fmt = manager.getKnownFormat(i);
    if (!fmt)
      continue;
    auto exts = fmt->getFileExtensions();
    for (const auto &ext : exts) {
      if (normalizeExtension(ext) == normalizedTarget)
        return fmt;
    }
  }
  return nullptr;
}

// =============================================================================
// ExportSettingsContent — dialog component for choosing export parameters
// =============================================================================

class ExportSettingsContent final : public juce::Component, private juce::Button::Listener {
public:
  ExportSettingsContent(int inputSampleRate,
                        std::function<void(std::optional<ExportSettings>)> done)
      : onDone(std::move(done)) {
    addAndMakeVisible(title);
    title.setText("Export Settings", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, APP_COLOR_TEXT_PRIMARY);
    title.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    setupCombo(formatBox, formatLabel, "Format", {"WAV", "FLAC", "AIFF", "OGG"}, 1);

    int srId = 3;
    if (inputSampleRate >= 47000)
      srId = 4;
    else if (inputSampleRate >= 43000)
      srId = 3;
    else if (inputSampleRate >= 30000)
      srId = 2;
    else
      srId = 1;
    setupCombo(sampleRateBox, sampleRateLabel, "Sample Rate",
               {"22050", "32000", "44100", "48000"}, srId);
    setupCombo(bitDepthBox, bitDepthLabel, "Bit Depth", {"16", "24", "32"}, 1);
    setupCombo(bitrateBox, bitrateLabel, "Bitrate (kbps)",
               {"64", "96", "128", "160", "192", "256", "320"}, 5);
    setupCombo(channelsBox, channelsLabel, "Channels", {"Mono", "Stereo"}, 1);

    addAndMakeVisible(cancelButton);
    addAndMakeVisible(exportButton);
    cancelButton.setButtonText("Cancel");
    exportButton.setButtonText("Export");
    cancelButton.addListener(this);
    exportButton.addListener(this);
  }

  ~ExportSettingsContent() override {
    cancelButton.removeListener(this);
    exportButton.removeListener(this);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(APP_COLOR_SURFACE);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(12);
    title.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);

    const int rowH = 28;
    layoutRow(area.removeFromTop(rowH), formatLabel, formatBox);
    area.removeFromTop(6);
    layoutRow(area.removeFromTop(rowH), sampleRateLabel, sampleRateBox);
    area.removeFromTop(6);
    layoutRow(area.removeFromTop(rowH), bitDepthLabel, bitDepthBox);
    area.removeFromTop(6);
    layoutRow(area.removeFromTop(rowH), bitrateLabel, bitrateBox);
    area.removeFromTop(6);
    layoutRow(area.removeFromTop(rowH), channelsLabel, channelsBox);

    area.removeFromTop(12);
    auto btnRow = area.removeFromTop(30);
    auto right = btnRow.removeFromRight(190);
    cancelButton.setBounds(right.removeFromLeft(90));
    right.removeFromLeft(10);
    exportButton.setBounds(right.removeFromLeft(90));
  }

private:
  void setupCombo(juce::ComboBox &box, juce::Label &label, const juce::String &labelText,
                  const juce::StringArray &items, int selectedId) {
    addAndMakeVisible(label);
    addAndMakeVisible(box);
    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    box.addItemList(items, 1);
    box.setSelectedId(selectedId, juce::dontSendNotification);
  }

  static void layoutRow(juce::Rectangle<int> row, juce::Label &label, juce::ComboBox &box) {
    label.setBounds(row.removeFromLeft(120));
    box.setBounds(row);
  }

  ExportSettings getSettings() const {
    ExportSettings settings;
    switch (formatBox.getSelectedId()) {
    case 2:
      settings.format = ExportFormat::flac;
      break;
    case 3:
      settings.format = ExportFormat::aiff;
      break;
    case 4:
      settings.format = ExportFormat::ogg;
      break;
    default:
      settings.format = ExportFormat::wav;
      break;
    }
    settings.sampleRate = sampleRateBox.getText().getIntValue();
    settings.bitsPerSample = bitDepthBox.getText().getIntValue();
    settings.bitrateKbps = bitrateBox.getText().getIntValue();
    settings.channels = channelsBox.getSelectedId() == 2 ? 2 : 1;
    return settings;
  }

  void buttonClicked(juce::Button *button) override {
    auto closeWith = [this](std::optional<ExportSettings> result) {
      if (onDone)
        onDone(std::move(result));
      if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
    };

    if (button == &exportButton)
      closeWith(getSettings());
    else if (button == &cancelButton)
      closeWith(std::nullopt);
  }

  std::function<void(std::optional<ExportSettings>)> onDone;
  juce::Label title;
  juce::Label formatLabel, sampleRateLabel, bitDepthLabel, bitrateLabel, channelsLabel;
  juce::ComboBox formatBox, sampleRateBox, bitDepthBox, bitrateBox, channelsBox;
  juce::TextButton cancelButton, exportButton;
};

void showExportSettingsDialogAsync(
    juce::Component *parent, int inputSampleRate,
    std::function<void(std::optional<ExportSettings>)> onDone) {
  auto *content = new ExportSettingsContent(inputSampleRate, std::move(onDone));
  content->setSize(420, 270);

  juce::DialogWindow::LaunchOptions opts;
  opts.content.setOwned(content);
  opts.dialogTitle = "Export Settings";
  opts.componentToCentreAround = parent;
  opts.dialogBackgroundColour = APP_COLOR_SURFACE;
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = false;
  opts.resizable = false;
  opts.useBottomRightCornerResizer = false;
  opts.launchAsync();
}

} // namespace ExportHelper
