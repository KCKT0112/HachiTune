#include "PitchFilterDebugWindow.h"
#include "../../Utils/UI/Theme.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {

constexpr int kOuterMargin = 12;
constexpr int kPanelGap = 10;
constexpr float kCurvePaddingRatio = 0.42f;
constexpr int kYAxisWidth = 52;
constexpr int kXAxisHeight = 20;

float findMaxAbs(std::initializer_list<const std::vector<float>*> curves) {
  float maxAbs = 0.0f;
  for (const auto* curve : curves) {
    if (curve == nullptr) {
      continue;
    }
    for (const auto value : *curve) {
      maxAbs = std::max(maxAbs, std::abs(value));
    }
  }
  return std::max(maxAbs, 0.25f);
}

float findMaxValue(const std::vector<float>& values) {
  float maxValue = 0.0f;
  for (const auto value : values) {
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
                   const juce::Colour& colour,
                   int cropStartFrame = -1,
                   int cropFrameCount = 0) {
  if (curve.empty()) {
    return;
  }

  auto plotBounds = getPlotBounds(bounds);
  auto yAxisBounds = plotBounds.removeFromLeft(kYAxisWidth);
  auto xAxisBounds = plotBounds.removeFromBottom(kXAxisHeight);
  const auto plotArea = plotBounds.toFloat();
  const float centerY = plotArea.getCentreY();
  const float xScale = curve.size() > 1
                           ? plotArea.getWidth() /
                                 static_cast<float>(curve.size() - 1)
                           : 0.0f;

  if (cropStartFrame >= 0 && cropFrameCount > 0 && curve.size() > 1) {
    const int cropEndFrame = std::min(static_cast<int>(curve.size()),
                                      cropStartFrame + cropFrameCount);
    const float cropStartX =
        plotArea.getX() + xScale * static_cast<float>(cropStartFrame);
    const float cropEndX =
        plotArea.getX() + xScale * static_cast<float>(cropEndFrame - 1);
    g.setColour(APP_COLOR_PRIMARY.withAlpha(0.10f));
    g.fillRect(juce::Rectangle<float>(cropStartX, plotArea.getY(),
                                      std::max(1.0f, cropEndX - cropStartX),
                                      plotArea.getHeight()));
    g.setColour(APP_COLOR_PRIMARY.withAlpha(0.65f));
    g.drawVerticalLine(static_cast<int>(std::round(cropStartX)), plotArea.getY(),
                       plotArea.getBottom());
    g.drawVerticalLine(static_cast<int>(std::round(cropEndX)), plotArea.getY(),
                       plotArea.getBottom());
  }

  g.setColour(APP_COLOR_GRID.withAlpha(0.35f));
  g.drawHorizontalLine(static_cast<int>(std::round(centerY)), plotArea.getX(),
                       plotArea.getRight());

  const float dcY =
      centerY - (dcValue / maxAbsValue) * plotArea.getHeight() *
                    kCurvePaddingRatio;
  g.setColour(juce::Colours::orange.withAlpha(0.85f));
  g.drawHorizontalLine(static_cast<int>(std::round(dcY)), plotArea.getX(),
                       plotArea.getRight());

  juce::Path path;
  for (size_t i = 0; i < curve.size(); ++i) {
    const float x = plotArea.getX() + xScale * static_cast<float>(i);
    const float y =
        centerY - (curve[i] / maxAbsValue) * plotArea.getHeight() *
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
  g.drawFittedText("Delta (st)", yAxisBounds.reduced(4, 0),
                   juce::Justification::centredTop, 1);
  g.drawText("+" + juce::String(maxAbsValue, 2) + " st",
             yAxisBounds.getRight(), bounds.getY() + 44, 96, 14,
             juce::Justification::left);
  g.drawText("0", yAxisBounds.getRight(),
             static_cast<int>(centerY) - 7, 24, 14,
             juce::Justification::left);
  g.drawText("DC: " + juce::String(dcValue, 3) + " st",
             bounds.getRight() - 128, static_cast<int>(dcY) - 7, 116, 14,
             juce::Justification::right);
  g.drawText("-" + juce::String(maxAbsValue, 2) + " st",
             yAxisBounds.getRight(), bounds.getBottom() - 24, 96, 14,
             juce::Justification::left);
  g.drawFittedText("Frame", xAxisBounds.removeFromRight(80),
                   juce::Justification::centredRight, 1);
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

  auto plotBounds = getPlotBounds(bounds);
  auto yAxisBounds = plotBounds.removeFromLeft(kYAxisWidth);
  auto xAxisBounds = plotBounds.removeFromBottom(kXAxisHeight);
  const auto plotArea = plotBounds.toFloat();
  const float maxFrequency =
      std::max(frameRateHz * 0.5f,
               frequencyBins.back() > 0.0f ? frequencyBins.back() : 1.0f);

  g.setColour(APP_COLOR_GRID.withAlpha(0.35f));
  g.drawHorizontalLine(static_cast<int>(std::round(plotArea.getBottom())),
                       plotArea.getX(), plotArea.getRight());

  juce::Path path;
  for (size_t i = 0; i < spectrum.size() && i < frequencyBins.size(); ++i) {
    const float x =
        plotArea.getX() +
        (frequencyBins[i] / maxFrequency) * plotArea.getWidth();
    const float y =
        plotArea.getBottom() -
        (spectrum[i] / maxMagnitude) * plotArea.getHeight() * 0.9f;
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
        plotArea.getX() + (highpassHz / maxFrequency) * plotArea.getWidth();
    g.setColour(juce::Colours::deepskyblue.withAlpha(0.75f));
    g.drawVerticalLine(static_cast<int>(std::round(highpassX)),
                       plotArea.getY(), plotArea.getBottom());
  }

  if (lowpassHz > 0.0f && lowpassHz < maxFrequency) {
    const float lowpassX =
        plotArea.getX() + (lowpassHz / maxFrequency) * plotArea.getWidth();
    g.setColour(juce::Colours::tomato.withAlpha(0.75f));
    g.drawVerticalLine(static_cast<int>(std::round(lowpassX)),
                       plotArea.getY(), plotArea.getBottom());
  }

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(11.0f);
  g.drawFittedText("Magnitude", yAxisBounds.reduced(4, 0),
                   juce::Justification::centredTop, 1);
  g.drawText(juce::String(maxMagnitude, 2), yAxisBounds.getRight(),
             bounds.getY() + 44, 64, 14, juce::Justification::left);
  g.drawText("0", yAxisBounds.getX() + 4, bounds.getBottom() - 24, 20, 14,
             juce::Justification::left);
  g.drawText("0 Hz", yAxisBounds.getRight(), bounds.getBottom() - 24, 48, 14,
             juce::Justification::left);
  g.drawText(juce::String(maxFrequency, 2) + " Hz",
             bounds.getRight() - 88, bounds.getBottom() - 24, 76, 14,
             juce::Justification::right);
  g.drawFittedText("Frequency (Hz)", xAxisBounds.removeFromRight(110),
                   juce::Justification::centredRight, 1);
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
  const juce::String contextSummary =
      hasData && !filterResult.contextInputPitch.empty()
          ? "  |  Context: " +
                juce::String(filterResult.contextStartFrame) + "-" +
                juce::String(filterResult.contextStartFrame +
                             static_cast<int>(filterResult.contextInputPitch.size()) - 1)
          : "";
  const juce::String summary =
      hasData
          ? noteDescription + contextSummary + "  |  DC removed: " +
                juce::String(filterResult.dcComponent, 4) +
                " st  |  DC restored: " +
                juce::String(filterResult.restoredDcComponent, 4) +
                " st (" + juce::String(filterResult.dcRestoreRatio * 100.0f, 1) +
                "%)  |  HP: " +
                juce::String(filterResult.highpassHz, 3) + " Hz  |  LP: " +
                juce::String(filterResult.lowpassHz, 3) + " Hz  |  Fs: " +
                juce::String(filterResult.frameRateHz, 3) + " Hz"
          : "Select a note to inspect the context-based f0 delta FFT filter.";
  g.drawText(summary, bounds.removeFromTop(20), juce::Justification::left);

  if (!hasData) {
    g.setColour(APP_COLOR_TEXT_MUTED);
    g.drawFittedText("No pitch-filter debug data available.", bounds, 
                     juce::Justification::centred, 2);
    return;
  }

  bounds.removeFromTop(6);
  const int rowHeight = (bounds.getHeight() - kPanelGap * 2) / 3;
  auto firstRow = bounds.removeFromTop(rowHeight);
  bounds.removeFromTop(kPanelGap);
  auto secondRow = bounds.removeFromTop(rowHeight);
  bounds.removeFromTop(kPanelGap);
  auto thirdRow = bounds;

  auto splitRow = [](juce::Rectangle<int> row) {
    auto left = row.removeFromLeft((row.getWidth() - kPanelGap) / 2);
    row.removeFromLeft(kPanelGap);
    return std::pair<juce::Rectangle<int>, juce::Rectangle<int>>(left, row);
  };

  const auto firstPanels = splitRow(firstRow);
  const auto secondPanels = splitRow(secondRow);
  const auto thirdPanels = splitRow(thirdRow);

  const float curveMaxAbs = findMaxAbs({
      &filterResult.contextInputPitch,
      &originalCurve,
      &filterResult.contextFilteredPitch,
      &filterResult.filteredPitch,
  });
  const float spectrumMax = findMaxValue(filterResult.magnitudeSpectrum);

  drawPanel(g, firstPanels.first, "Context f0 delta curve",
            "Extended source delta with cropped note highlighted");
  drawCurvePlot(g, firstPanels.first, filterResult.contextInputPitch,
                computeMean(filterResult.contextInputPitch), curveMaxAbs,
                APP_COLOR_PRIMARY, filterResult.cropStartFrame,
                filterResult.cropFrameCount);

  drawPanel(g, firstPanels.second, "Current note region",
            juce::String(originalCurve.size()) + " cropped frames");
  drawCurvePlot(g, firstPanels.second, originalCurve,
                computeMean(originalCurve), curveMaxAbs,
                juce::Colours::mediumseagreen);

  drawPanel(g, secondPanels.first, "Filtered context curve",
            "FFT filtered context before crop");
  drawCurvePlot(g, secondPanels.first, filterResult.contextFilteredPitch,
                computeMean(filterResult.contextFilteredPitch), curveMaxAbs,
                juce::Colours::goldenrod, filterResult.cropStartFrame,
                filterResult.cropFrameCount);

  drawPanel(g, secondPanels.second, "Filtered note region",
            juce::String(filterResult.filteredPitch.size()) + " frames");
  drawCurvePlot(g, secondPanels.second, filterResult.filteredPitch,
                computeMean(filterResult.filteredPitch), curveMaxAbs,
                APP_COLOR_SECONDARY.brighter(0.15f));

  drawPanel(g, thirdPanels.first, "FFT spectrum",
            "Context zero-mean input spectrum");
  drawSpectrumPlot(g, thirdPanels.first, filterResult.magnitudeSpectrum,
                   filterResult.frequencyBins, spectrumMax,
                   filterResult.frameRateHz, 0.0f, 0.0f, APP_COLOR_PRIMARY);

  drawPanel(g, thirdPanels.second, "FFT after handle drag",
            "Filtered context spectrum before crop");
  drawSpectrumPlot(g, thirdPanels.second,
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
