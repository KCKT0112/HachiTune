#include "PitchFilterDebugWindow.h"
#include "../../Utils/UI/Theme.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kOuterMargin = 12;
constexpr int kPanelGap = 10;
constexpr float kCurvePaddingRatio = 0.42f;

float findMaxAbs(const std::vector<float>& a, const std::vector<float>& b) {
  float maxAbs = 0.0f;
  for (const auto value : a) {
    maxAbs = std::max(maxAbs, std::abs(value));
  }
  for (const auto value : b) {
    maxAbs = std::max(maxAbs, std::abs(value));
  }
  return std::max(maxAbs, 0.25f);
}

float findMaxValue(const std::vector<float>& a, const std::vector<float>& b) {
  float maxValue = 0.0f;
  for (const auto value : a) {
    maxValue = std::max(maxValue, value);
  }
  for (const auto value : b) {
    maxValue = std::max(maxValue, value);
  }
  return std::max(maxValue, 1.0e-3f);
}

float computeMean(const std::vector<float>& values) {
  if (values.empty()) {
    return 0.0f;
  }

  float sum = 0.0f;
  for (const auto value : values) {
    sum += value;
  }
  return sum / static_cast<float>(values.size());
}

void drawPanel(juce::Graphics& g,
               const juce::Rectangle<int>& bounds,
               const juce::String& title,
               const juce::String& subtitle) {
  g.setColour(APP_COLOR_SURFACE_RAISED);
  g.fillRoundedRectangle(bounds.toFloat(), 10.0f);

  g.setColour(APP_COLOR_BORDER.withAlpha(0.85f));
  g.drawRoundedRectangle(bounds.toFloat(), 10.0f, 1.0f);

  auto textBounds = bounds.reduced(12, 10);
  g.setColour(APP_COLOR_TEXT_PRIMARY);
  g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
  g.drawText(title, textBounds.removeFromTop(20), juce::Justification::left);

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(12.0f);
  g.drawText(subtitle, textBounds.removeFromTop(18),
             juce::Justification::left);
}

juce::Rectangle<int> getPlotBounds(const juce::Rectangle<int>& bounds) {
  auto plotBounds = bounds.reduced(12, 10);
  plotBounds.removeFromTop(40);
  return plotBounds;
}

void drawCurvePlot(juce::Graphics& g,
                   const juce::Rectangle<int>& bounds,
                   const std::vector<float>& curve,
                   float dcValue,
                   float maxAbsValue,
                   const juce::Colour& colour) {
  if (curve.empty()) {
    return;
  }

  const auto plotBounds = getPlotBounds(bounds).toFloat();
  const float centerY = plotBounds.getCentreY();
  const float xScale = curve.size() > 1
                           ? plotBounds.getWidth() /
                                 static_cast<float>(curve.size() - 1)
                           : 0.0f;

  g.setColour(APP_COLOR_GRID.withAlpha(0.35f));
  g.drawHorizontalLine(static_cast<int>(std::round(centerY)), plotBounds.getX(),
                       plotBounds.getRight());

  const float dcY =
      centerY - (dcValue / maxAbsValue) * plotBounds.getHeight() *
                    kCurvePaddingRatio;
  g.setColour(juce::Colours::orange.withAlpha(0.85f));
  g.drawHorizontalLine(static_cast<int>(std::round(dcY)), plotBounds.getX(),
                       plotBounds.getRight());

  juce::Path path;
  for (size_t i = 0; i < curve.size(); ++i) {
    const float x = plotBounds.getX() + xScale * static_cast<float>(i);
    const float y =
        centerY - (curve[i] / maxAbsValue) * plotBounds.getHeight() *
                      kCurvePaddingRatio;
    if (i == 0) {
      path.startNewSubPath(x, y);
    } else {
      path.lineTo(x, y);
    }
  }

  g.setColour(colour);
  g.strokePath(path, juce::PathStrokeType(2.0f));

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(11.0f);
  g.drawText("+" + juce::String(maxAbsValue, 2) + " st",
             bounds.getX() + 12, bounds.getY() + 44, 96, 14,
             juce::Justification::left);
  g.drawText("0", bounds.getX() + 12,
             static_cast<int>(centerY) - 7, 24, 14,
             juce::Justification::left);
  g.drawText("DC: " + juce::String(dcValue, 3) + " st",
             bounds.getRight() - 128, static_cast<int>(dcY) - 7, 116, 14,
             juce::Justification::right);
  g.drawText("-" + juce::String(maxAbsValue, 2) + " st",
             bounds.getX() + 12, bounds.getBottom() - 24, 96, 14,
             juce::Justification::left);
}

void drawSpectrumPlot(juce::Graphics& g,
                      const juce::Rectangle<int>& bounds,
                      const std::vector<float>& spectrum,
                      const std::vector<float>& frequencyBins,
                      float maxMagnitude,
                      float frameRateHz,
                      float lowpassHz,
                      float highpassHz,
                      const juce::Colour& colour) {
  if (spectrum.empty() || frequencyBins.empty()) {
    return;
  }

  const auto plotBounds = getPlotBounds(bounds).toFloat();
  const float maxFrequency =
      std::max(frameRateHz * 0.5f,
               frequencyBins.back() > 0.0f ? frequencyBins.back() : 1.0f);

  g.setColour(APP_COLOR_GRID.withAlpha(0.35f));
  g.drawHorizontalLine(static_cast<int>(std::round(plotBounds.getBottom())),
                       plotBounds.getX(), plotBounds.getRight());

  juce::Path path;
  for (size_t i = 0; i < spectrum.size() && i < frequencyBins.size(); ++i) {
    const float x =
        plotBounds.getX() +
        (frequencyBins[i] / maxFrequency) * plotBounds.getWidth();
    const float y =
        plotBounds.getBottom() -
        (spectrum[i] / maxMagnitude) * plotBounds.getHeight() * 0.9f;
    if (i == 0) {
      path.startNewSubPath(x, y);
    } else {
      path.lineTo(x, y);
    }
  }

  g.setColour(colour);
  g.strokePath(path, juce::PathStrokeType(2.0f));

  if (highpassHz > 0.0f && maxFrequency > 0.0f) {
    const float highpassX =
        plotBounds.getX() + (highpassHz / maxFrequency) * plotBounds.getWidth();
    g.setColour(juce::Colours::deepskyblue.withAlpha(0.75f));
    g.drawVerticalLine(static_cast<int>(std::round(highpassX)),
                       plotBounds.getY(), plotBounds.getBottom());
  }

  if (lowpassHz > 0.0f && lowpassHz < maxFrequency) {
    const float lowpassX =
        plotBounds.getX() + (lowpassHz / maxFrequency) * plotBounds.getWidth();
    g.setColour(juce::Colours::tomato.withAlpha(0.75f));
    g.drawVerticalLine(static_cast<int>(std::round(lowpassX)),
                       plotBounds.getY(), plotBounds.getBottom());
  }

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(11.0f);
  g.drawText("0 Hz", bounds.getX() + 12, bounds.getBottom() - 24, 48, 14,
             juce::Justification::left);
  g.drawText(juce::String(maxFrequency, 2) + " Hz",
             bounds.getRight() - 88, bounds.getBottom() - 24, 76, 14,
             juce::Justification::right);
}

}  // namespace

void PitchFilterDebugComponent::paint(juce::Graphics& g) {
  g.fillAll(APP_COLOR_BACKGROUND);

  auto bounds = getLocalBounds().reduced(kOuterMargin);
  g.setColour(APP_COLOR_TEXT_PRIMARY);
  g.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
  g.drawText("Pitch Filter Debug", bounds.removeFromTop(24),
             juce::Justification::left);

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(12.0f);
  const juce::String summary =
      hasData
          ? noteDescription + "  |  DC removed: " +
                juce::String(filterResult.dcComponent, 4) + " st  |  HP: " +
                juce::String(filterResult.highpassHz, 3) + " Hz  |  LP: " +
                juce::String(filterResult.lowpassHz, 3) + " Hz  |  Fs: " +
                juce::String(filterResult.frameRateHz, 3) + " Hz"
          : "Select a note to inspect the note-local f0 delta FFT filter.";
  g.drawText(summary, bounds.removeFromTop(20), juce::Justification::left);

  if (!hasData) {
    g.setColour(APP_COLOR_TEXT_MUTED);
    g.drawFittedText("No pitch-filter debug data available.", bounds, 
                     juce::Justification::centred, 2);
    return;
  }

  bounds.removeFromTop(6);
  auto topRow = bounds.removeFromTop((bounds.getHeight() - kPanelGap) / 2);
  bounds.removeFromTop(kPanelGap);
  auto bottomRow = bounds;

  auto originalCurveBounds =
      topRow.removeFromLeft((topRow.getWidth() - kPanelGap) / 2);
  topRow.removeFromLeft(kPanelGap);
  auto reconstructedCurveBounds = topRow;

  auto originalSpectrumBounds =
      bottomRow.removeFromLeft((bottomRow.getWidth() - kPanelGap) / 2);
  bottomRow.removeFromLeft(kPanelGap);
  auto filteredSpectrumBounds = bottomRow;

  const float curveMaxAbs =
      findMaxAbs(originalCurve, filterResult.filteredPitch);
  const float spectrumMax =
      findMaxValue(filterResult.magnitudeSpectrum,
                   filterResult.filteredMagnitudeSpectrum);

  drawPanel(g, originalCurveBounds, "Original f0 delta curve",
            juce::String(originalCurve.size()) + " frames");
  drawCurvePlot(g, originalCurveBounds, originalCurve,
                computeMean(originalCurve), curveMaxAbs, APP_COLOR_PRIMARY);

  drawPanel(g, reconstructedCurveBounds, "Reconstructed f0 delta curve",
            juce::String(filterResult.filteredPitch.size()) + " frames");
  drawCurvePlot(g, reconstructedCurveBounds, filterResult.filteredPitch,
                computeMean(filterResult.filteredPitch), curveMaxAbs,
                APP_COLOR_SECONDARY.brighter(0.15f));

  drawPanel(g, originalSpectrumBounds, "FFT spectrum",
            "Original zero-mean f0 delta");
  drawSpectrumPlot(g, originalSpectrumBounds, filterResult.magnitudeSpectrum,
                   filterResult.frequencyBins, spectrumMax,
                   filterResult.frameRateHz, 0.0f, 0.0f, APP_COLOR_PRIMARY);

  drawPanel(g, filteredSpectrumBounds, "FFT after handle drag",
            "Filtered zero-mean spectrum");
  drawSpectrumPlot(g, filteredSpectrumBounds,
                   filterResult.filteredMagnitudeSpectrum,
                   filterResult.frequencyBins, spectrumMax,
                   filterResult.frameRateHz, filterResult.lowpassHz,
                   filterResult.highpassHz,
                   APP_COLOR_SECONDARY.brighter(0.15f));
}

void PitchFilterDebugComponent::resized() {
}

void PitchFilterDebugComponent::setDebugData(
    const juce::String& newNoteDescription,
    std::vector<float> newOriginalCurve,
    FourierPitchFilter::FilterResult newResult) {
  noteDescription = newNoteDescription;
  originalCurve = std::move(newOriginalCurve);
  filterResult = std::move(newResult);
  hasData = true;
  repaint();
}

void PitchFilterDebugComponent::clearData() {
  noteDescription.clear();
  originalCurve.clear();
  filterResult = {};
  hasData = false;
  repaint();
}

PitchFilterDebugWindow::PitchFilterDebugWindow()
    : juce::DocumentWindow("Pitch Filter Debug", APP_COLOR_BACKGROUND,
                           juce::DocumentWindow::allButtons, false) {
  setOpaque(true);
  setUsingNativeTitleBar(true);
  setResizable(true, true);
  setResizeLimits(720, 480, 1800, 1200);
  addToDesktop();

  auto* content = new PitchFilterDebugComponent();
  debugComponent = content;
  setContentOwned(content, true);
  centreWithSize(960, 680);
}

void PitchFilterDebugWindow::closeButtonPressed() {
  setVisible(false);
}

void PitchFilterDebugWindow::present() {
  setVisible(true);
  toFront(true);
}

void PitchFilterDebugWindow::showDebugData(
    const juce::String& noteDescription,
    std::vector<float> originalCurve,
    FourierPitchFilter::FilterResult result) {
  if (debugComponent != nullptr) {
    debugComponent->setDebugData(noteDescription, std::move(originalCurve),
                                 std::move(result));
  }
}

void PitchFilterDebugWindow::clearData() {
  if (debugComponent != nullptr) {
    debugComponent->clearData();
  }
}
