#include "HNSepLaneComponent.h"
#include "../Undo/PitchUndoManager.h"
#include "../Utils/HNSepCurveProcessor.h"

#include <cmath>

namespace
{
float computeSegmentRmsDb(const std::vector<float>& samples, int start, int end)
{
  if (samples.empty())
    return -120.0f;

  start = std::clamp(start, 0, static_cast<int>(samples.size()));
  end = std::clamp(end, start, static_cast<int>(samples.size()));
  if (end <= start)
    return -120.0f;

  double sumSquares = 0.0;
  for (int i = start; i < end; ++i)
    sumSquares += static_cast<double>(samples[static_cast<size_t>(i)]) *
                  static_cast<double>(samples[static_cast<size_t>(i)]);

  const float rms = static_cast<float>(
      std::sqrt(sumSquares / static_cast<double>(std::max(1, end - start))));
  return 20.0f * std::log10(std::max(rms, 1.0e-6f));
}
} // namespace

HNSepLaneComponent::HNSepLaneComponent() {
  setWantsKeyboardFocus(false);
  setMouseClickGrabsKeyboardFocus(false);

  // Lane order: Tension on top (2 units of height), Voicing (1 unit), Breath (1 unit).
  // Tension is the primary editing target, so it gets 50% of vertical space.
  lanes[0] = {HNSepParamType::Tension, TR("hnsep.lane.tension"),
              juce::Colour(0xFFFFB74D),
              0.0f, -100.0f, 100.0f, 2.0f / 4.0f};
  lanes[1] = {HNSepParamType::Voicing, TR("hnsep.lane.voicing"),
              juce::Colour(0xFF4FC3F7),
              100.0f, 0.0f, 200.0f, 1.0f / 4.0f};
  lanes[2] = {HNSepParamType::Breath, TR("hnsep.lane.breath"),
              juce::Colour(0xFF81C784),
              100.0f, 0.0f, 200.0f, 1.0f / 4.0f};

  // Helper to set up a range dropdown (shared between voicing and breath).
  auto setupRangeDropdown = [this](juce::ComboBox& dropdown, auto setter) {
    dropdown.addItem("150", 1);
    dropdown.addItem("200", 2);
    dropdown.addItem("300", 3);
    dropdown.addItem("400", 4);
    dropdown.addItem("500", 5);
    dropdown.setSelectedId(2, juce::dontSendNotification); // default 200

    dropdown.setColour(juce::ComboBox::backgroundColourId,
                       APP_COLOR_SURFACE_RAISED.withAlpha(0.9f));
    dropdown.setColour(juce::ComboBox::outlineColourId,
                       APP_COLOR_BORDER_SUBTLE.withAlpha(0.6f));
    dropdown.setColour(juce::ComboBox::textColourId,
                       APP_COLOR_TEXT_PRIMARY);

    dropdown.onChange = [this, &dropdown, setter]() {
      static constexpr float rangeValues[] = {150.0f, 200.0f, 300.0f, 400.0f, 500.0f};
      const int idx = dropdown.getSelectedId() - 1;
      if (idx >= 0 && idx < 5)
        (this->*setter)(rangeValues[idx]);
    };

    addAndMakeVisible(dropdown);
  };

  setupRangeDropdown(voicingRangeDropdown, &HNSepLaneComponent::setMaxVoicing);
  setupRangeDropdown(breathRangeDropdown, &HNSepLaneComponent::setMaxBreath);

  auto setupEnergyDropdown = [this](juce::ComboBox& dropdown, float& targetDb) {
    dropdown.addItem(TR("hnsep.energy.max.-60"), 1);
    dropdown.addItem(TR("hnsep.energy.max.-45"), 2);
    dropdown.addItem(TR("hnsep.energy.max.-30"), 3);
    dropdown.addItem(TR("hnsep.energy.max.-12"), 4);
    dropdown.addItem(TR("hnsep.energy.max.-3"), 5);
    const bool isVoicingDropdown = &dropdown == &voicingEnergyDropdown;
    dropdown.setSelectedId(isVoicingDropdown ? 5 : 4, juce::dontSendNotification);

    dropdown.setColour(juce::ComboBox::backgroundColourId,
                       APP_COLOR_SURFACE_RAISED.withAlpha(0.9f));
    dropdown.setColour(juce::ComboBox::outlineColourId,
                       APP_COLOR_BORDER_SUBTLE.withAlpha(0.6f));
    dropdown.setColour(juce::ComboBox::textColourId,
                       APP_COLOR_TEXT_PRIMARY);

    dropdown.onChange = [&dropdown, &targetDb, this]() {
      static constexpr float dbValues[] = {-60.0f, -45.0f, -30.0f, -12.0f, -3.0f};
      const int idx = dropdown.getSelectedId() - 1;
      if (idx >= 0 && idx < 5) {
        targetDb = dbValues[idx];
        repaint();
      }
    };

    addAndMakeVisible(dropdown);
  };

  setupEnergyDropdown(voicingEnergyDropdown, voicingEnergyMaxDb);
  setupEnergyDropdown(breathEnergyDropdown, breathEnergyMaxDb);

  auto setupEnergyToggle = [this](StyledToggleButton& toggle) {
    toggle.setButtonText(TR("hnsep.energy.show"));
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick = [this]() { repaint(); };
    addAndMakeVisible(toggle);
  };

  setupEnergyToggle(voicingEnergyVisibilityToggle);
  setupEnergyToggle(breathEnergyVisibilityToggle);

  refreshLocalizedText();
}

void HNSepLaneComponent::refreshLocalizedText() {
  lanes[0].label = TR("hnsep.lane.tension");
  lanes[1].label = TR("hnsep.lane.voicing");
  lanes[2].label = TR("hnsep.lane.breath");

  auto refreshEnergyDropdown = [](juce::ComboBox& dropdown, int defaultId) {
    const int selectedId = dropdown.getSelectedId() > 0 ? dropdown.getSelectedId()
                                                        : defaultId;
    dropdown.clear(juce::dontSendNotification);
    dropdown.addItem(TR("hnsep.energy.max.-60"), 1);
    dropdown.addItem(TR("hnsep.energy.max.-45"), 2);
    dropdown.addItem(TR("hnsep.energy.max.-30"), 3);
    dropdown.addItem(TR("hnsep.energy.max.-12"), 4);
    dropdown.addItem(TR("hnsep.energy.max.-3"), 5);
    dropdown.setSelectedId(selectedId, juce::dontSendNotification);
  };

  refreshEnergyDropdown(voicingEnergyDropdown, 5);
  refreshEnergyDropdown(breathEnergyDropdown, 4);
  voicingEnergyVisibilityToggle.setButtonText(TR("hnsep.energy.show"));
  breathEnergyVisibilityToggle.setButtonText(TR("hnsep.energy.show"));

  updateControlBounds();
  repaint();
}

void HNSepLaneComponent::paint(juce::Graphics& g) {
  // Semi-transparent background lets piano roll notes show through clearly
  // when this component is overlaid on the full viewport area.
  g.fillAll(APP_COLOR_SURFACE.withAlpha(0.45f));

  for (int i = 0; i < numLanes; ++i)
    drawLane(g, i);

  for (int i = 0; i < numLanes - 1; ++i)
    drawSeparator(g, i);
}

void HNSepLaneComponent::resized() {
  updateControlBounds();
  repaint();
}

void HNSepLaneComponent::updateControlBounds() {
  // Position range dropdowns at the top-right of their respective lanes.
  // Voicing is lane index 1, Breath is lane index 2 (Tension is index 0, no dropdown).
  const int margin = 6;

  for (int i = 0; i < numLanes; ++i) {
    auto bounds = getLaneBounds(i);
    if (lanes[i].paramType == HNSepParamType::Voicing) {
      const int rightControlOffset = dropdownWidth + 58;
      voicingEnergyDropdown.setBounds(
          bounds.getX() + labelWidth + margin,
          bounds.getY() + margin,
          energyDropdownWidth, dropdownHeight);
      voicingEnergyVisibilityToggle.setBounds(
          voicingEnergyDropdown.getRight() + 4,
          bounds.getY() + margin,
          energyToggleWidth, dropdownHeight);
      voicingRangeDropdown.setBounds(
          bounds.getRight() - rightControlOffset,
          bounds.getY() + margin,
          dropdownWidth, dropdownHeight);
    } else if (lanes[i].paramType == HNSepParamType::Breath) {
      const int rightControlOffset = dropdownWidth + 58;
      breathEnergyDropdown.setBounds(
          bounds.getX() + labelWidth + margin,
          bounds.getY() + margin,
          energyDropdownWidth, dropdownHeight);
      breathEnergyVisibilityToggle.setBounds(
          breathEnergyDropdown.getRight() + 4,
          bounds.getY() + margin,
          energyToggleWidth, dropdownHeight);
      breathRangeDropdown.setBounds(
          bounds.getRight() - rightControlOffset,
          bounds.getY() + margin,
          dropdownWidth, dropdownHeight);
    }
  }
}

void HNSepLaneComponent::mouseDown(const juce::MouseEvent& e) {
  draggingSeparator = -1;
  isGesturePending = false;

  for (int i = 0; i < numLanes - 1; ++i) {
    if (getSeparatorBounds(i).contains(e.getPosition())) {
      draggingSeparator = i;
      dragStartProportion0 = lanes[i].proportion;
      dragStartProportion1 = lanes[i + 1].proportion;
      dragStartY = e.y;
      return;
    }
  }

  const int laneIndex = laneIndexAtY(e.y);
  if (laneIndex < 0)
    return;

  if (e.mods.isLeftButtonDown()) {
    isDrawing = false;
    isResetting = false;
    isGesturePending = true;
    pendingGestureResetting = false;
    activeLane = laneIndex;
    lastDrawFrame = -1;
    lastDrawValue = 0.0f;
    pendingEdits.clear();
    editIndexMap.clear();
    pendingGestureStart = e.position;
    return;
  }

  if (e.mods.isRightButtonDown()) {
    isDrawing = false;
    isResetting = false;
    isGesturePending = true;
    pendingGestureResetting = true;
    activeLane = laneIndex;
    lastDrawFrame = -1;
    lastDrawValue = 0.0f;
    pendingEdits.clear();
    editIndexMap.clear();
    pendingGestureStart = e.position;
  }
}

void HNSepLaneComponent::mouseDrag(const juce::MouseEvent& e) {
  if (draggingSeparator >= 0) {
    const int totalAvailable =
        juce::jmax(1, getHeight() - (numLanes - 1) * separatorHeight);
    const float delta =
        static_cast<float>(e.y - dragStartY) / static_cast<float>(totalAvailable);

    float p0 = dragStartProportion0 + delta;
    const float pairSum = dragStartProportion0 + dragStartProportion1;
    constexpr float minProp = 0.1f;

    const float maxP0 = juce::jmax(minProp, pairSum - minProp);
    p0 = juce::jlimit(minProp, maxP0, p0);
    float p1 = pairSum - p0;
    p1 = juce::jmax(minProp, p1);

    lanes[draggingSeparator].proportion = p0;
    lanes[draggingSeparator + 1].proportion = p1;
    updateControlBounds();
    repaint();
    return;
  }

  if (isGesturePending) {
    isGesturePending = false;
    isDrawing = !pendingGestureResetting;
    isResetting = pendingGestureResetting;
    applyDrawing(pendingGestureStart.x, pendingGestureStart.y);
  }

  if (isDrawing || isResetting) {
    applyDrawing(static_cast<float>(e.x), static_cast<float>(e.y));
    if (onParamEdited)
      onParamEdited();
    repaint();
  }
}

void HNSepLaneComponent::mouseUp(const juce::MouseEvent& e) {
  juce::ignoreUnused(e);

  if (draggingSeparator >= 0) {
    draggingSeparator = -1;
    return;
  }

  if (isGesturePending) {
    isGesturePending = false;
    pendingGestureResetting = false;
    isDrawing = false;
    isResetting = false;
    activeLane = -1;
    lastDrawFrame = -1;
    lastDrawValue = 0.0f;
    pendingEdits.clear();
    editIndexMap.clear();
    return;
  }

  if (isDrawing || isResetting) {
    commitEdits();

    isDrawing = false;
    isResetting = false;
    pendingGestureResetting = false;
    activeLane = -1;
    lastDrawFrame = -1;
    lastDrawValue = 0.0f;
    pendingEdits.clear();
    editIndexMap.clear();

    repaint();
  }
}

void HNSepLaneComponent::mouseMove(const juce::MouseEvent& e) {
  for (int i = 0; i < numLanes - 1; ++i) {
    if (getSeparatorBounds(i).contains(e.getPosition())) {
      setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
      return;
    }
  }

  if (laneIndexAtY(e.y) >= 0) {
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
  } else {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }
}

void HNSepLaneComponent::mouseWheelMove(
    const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
  if (mouseWheelPassthroughTarget != nullptr) {
    mouseWheelPassthroughTarget->mouseWheelMove(
        e.getEventRelativeTo(mouseWheelPassthroughTarget), wheel);
    return;
  }

  juce::Component::mouseWheelMove(e, wheel);
}

juce::Rectangle<int> HNSepLaneComponent::getLaneBounds(int laneIndex) const {
  if (laneIndex < 0 || laneIndex >= numLanes)
    return {};

  const int width = getWidth();
  const int totalAvailable =
      juce::jmax(0, getHeight() - (numLanes - 1) * separatorHeight);

  float proportionSum = 0.0f;
  for (const auto& lane : lanes)
    proportionSum += juce::jmax(0.0f, lane.proportion);

  if (proportionSum <= 0.0f)
    proportionSum = 1.0f;

  int y = 0;
  int consumedHeight = 0;
  for (int i = 0; i < numLanes; ++i) {
    int laneHeight = 0;
    if (i == numLanes - 1) {
      laneHeight = juce::jmax(0, totalAvailable - consumedHeight);
    } else {
      const float normalized = lanes[i].proportion / proportionSum;
      laneHeight = juce::roundToInt(normalized * static_cast<float>(totalAvailable));
      laneHeight = juce::jmax(0, laneHeight);
      consumedHeight += laneHeight;
    }

    if (i == laneIndex)
      return {0, y, width, laneHeight};

    y += laneHeight;
    if (i < numLanes - 1)
      y += separatorHeight;
  }

  return {};
}

juce::Rectangle<int> HNSepLaneComponent::getSeparatorBounds(int sepIndex) const {
  if (sepIndex < 0 || sepIndex >= numLanes - 1)
    return {};

  auto laneBounds = getLaneBounds(sepIndex);
  return {0, laneBounds.getBottom(), getWidth(), separatorHeight};
}

int HNSepLaneComponent::laneIndexAtY(int y) const {
  for (int i = 0; i < numLanes; ++i) {
    if (getLaneBounds(i).contains(juce::Point<int>(juce::jmax(0, getWidth() / 2), y)))
      return i;
  }
  return -1;
}

double HNSepLaneComponent::xToTime(float x) const {
  return (x - static_cast<float>(pianoKeysWidth) + scrollX) /
         static_cast<double>(pixelsPerSecond);
}

float HNSepLaneComponent::timeToX(double time) const {
  return static_cast<float>(time * static_cast<double>(pixelsPerSecond) - scrollX) +
         static_cast<float>(pianoKeysWidth);
}

float HNSepLaneComponent::valueToY(float value, const Lane& lane,
                                   const juce::Rectangle<int>& bounds) const {
  const float minVal = lane.minValue;
  const float maxVal = (lane.paramType == HNSepParamType::Tension)
                           ? lane.maxValue
                           : (lane.paramType == HNSepParamType::Voicing)
                                 ? maxVoicing
                                 : maxBreath;

  if (maxVal <= minVal)
    return static_cast<float>(bounds.getBottom());

  const float v = juce::jlimit(minVal, maxVal, value);
  const float t = (v - minVal) / (maxVal - minVal);
  return static_cast<float>(bounds.getBottom()) -
         t * static_cast<float>(bounds.getHeight());
}

float HNSepLaneComponent::yToValue(float y, const Lane& lane,
                                   const juce::Rectangle<int>& bounds) const {
  const float minVal = lane.minValue;
  const float maxVal = (lane.paramType == HNSepParamType::Tension)
                           ? lane.maxValue
                           : (lane.paramType == HNSepParamType::Voicing)
                                 ? maxVoicing
                                 : maxBreath;

  const float h = static_cast<float>(juce::jmax(1, bounds.getHeight()));
  const float t = (static_cast<float>(bounds.getBottom()) - y) / h;
  return minVal + juce::jlimit(0.0f, 1.0f, t) * (maxVal - minVal);
}

float HNSepLaneComponent::dbToY(float dbValue, float maxDb,
                                const juce::Rectangle<int>& bounds) const {
  const float clamped = juce::jlimit(energyMinDb, maxDb, dbValue);
  const float norm =
      (clamped - energyMinDb) / std::max(0.001f, maxDb - energyMinDb);
  return static_cast<float>(bounds.getBottom()) -
         norm * static_cast<float>(bounds.getHeight());
}

void HNSepLaneComponent::drawLane(juce::Graphics& g, int laneIndex) {
  if (laneIndex < 0 || laneIndex >= numLanes)
    return;

  const auto bounds = getLaneBounds(laneIndex);
  if (bounds.isEmpty())
    return;

  const auto& lane = lanes[laneIndex];

  drawLaneBackground(g, bounds, lane);
  drawLaneEnergyOverlay(g, bounds, lane);
  drawLaneCurves(g, bounds, lane);

  // Label in top-left for quick lane recognition while drawing. This avoids
  // accidental edits caused by similar-looking curve shapes.
  auto labelArea = bounds;
  const auto labelBounds =
      labelArea.removeFromTop(labelHeight).removeFromLeft(labelWidth);
  g.setColour(APP_COLOR_SURFACE_RAISED.withAlpha(0.85f));
  g.fillRoundedRectangle(labelBounds.toFloat().reduced(2.0f, 1.0f), 3.0f);
  g.setColour(APP_COLOR_BORDER_SUBTLE.withAlpha(0.9f));
  g.drawRoundedRectangle(labelBounds.toFloat().reduced(2.0f, 1.0f), 3.0f, 1.0f);
  g.setColour(APP_COLOR_TEXT_PRIMARY.withAlpha(0.95f));
  g.setFont(juce::Font(11.0f));
  g.drawText(lane.label, labelBounds.reduced(4, 0), juce::Justification::centredLeft,
             false);
}

void HNSepLaneComponent::drawLaneBackground(juce::Graphics& g,
                                            const juce::Rectangle<int>& bounds,
                                            const Lane& lane) {
  // Slightly offset lane background so each lane has independent visual depth.
  // Alpha < 1.0 keeps piano roll notes partially visible through the overlay.
  g.setColour(APP_COLOR_SURFACE.darker(0.12f).withAlpha(0.5f));
  g.fillRect(bounds);

  std::vector<float> gridValues;
  if (lane.paramType == HNSepParamType::Tension) {
    gridValues = {-100.0f, -50.0f, 0.0f, 50.0f, 100.0f};
  } else {
    const float laneMax = (lane.paramType == HNSepParamType::Voicing)
                              ? maxVoicing : maxBreath;
    const float step = laneMax <= 200.0f ? 50.0f : 100.0f;
    for (float value = 0.0f; value <= laneMax + 0.001f; value += step)
      gridValues.push_back(value);

    if (std::none_of(gridValues.begin(), gridValues.end(),
                     [](float value) { return std::abs(value - 100.0f) < 0.001f; })) {
      gridValues.push_back(100.0f);
    }

    if (std::none_of(gridValues.begin(), gridValues.end(),
                     [laneMax](float value) {
                       return std::abs(value - laneMax) < 0.001f;
                     })) {
      gridValues.push_back(laneMax);
    }

    std::sort(gridValues.begin(), gridValues.end());
  }

  for (float gridValue : gridValues) {
    const float y = valueToY(gridValue, lane, bounds);
    const bool isDefaultLine =
        (lane.paramType == HNSepParamType::Tension)
            ? std::abs(gridValue - 0.0f) < 0.0001f
            : std::abs(gridValue - 100.0f) < 0.0001f;

    g.setColour(isDefaultLine ? APP_COLOR_BORDER_HIGHLIGHT.withAlpha(0.55f)
                              : APP_COLOR_GRID.withAlpha(0.18f));
    g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));

    // Small value labels on the right help precision when drawing with a pen.
    g.setColour(APP_COLOR_TEXT_MUTED.withAlpha(isDefaultLine ? 0.8f : 0.45f));
    g.setFont(juce::Font(11.5f));
    const int valueLabelWidth = 48;
    const int valueLabelRightInset = 5;
    int labelY = juce::roundToInt(y) - 8;
    const int minLabelY = bounds.getY() + 3;
    const int maxLabelY = bounds.getBottom() - 19;
    labelY = juce::jlimit(minLabelY, maxLabelY, labelY);
    g.drawText(juce::String(juce::roundToInt(gridValue)),
               bounds.getRight() - valueLabelWidth - valueLabelRightInset, labelY,
               valueLabelWidth, 16, juce::Justification::centredRight, false);
  }

  g.setColour(APP_COLOR_BORDER_SUBTLE.withAlpha(0.4f));
  g.drawRect(bounds);
}

void HNSepLaneComponent::drawLaneEnergyOverlay(
    juce::Graphics& g, const juce::Rectangle<int>& bounds, const Lane& lane) {
  if (!project)
    return;

  if (lane.paramType != HNSepParamType::Voicing &&
      lane.paramType != HNSepParamType::Breath)
    return;

  const bool isVisible =
      lane.paramType == HNSepParamType::Voicing
          ? voicingEnergyVisibilityToggle.getToggleState()
          : breathEnergyVisibilityToggle.getToggleState();
  if (!isVisible)
    return;

  const auto& notes = project->getNotes();
  if (notes.empty())
    return;

  const float maxDb = lane.paramType == HNSepParamType::Voicing
                          ? voicingEnergyMaxDb
                          : breathEnergyMaxDb;
  const int viewLeft = 0;
  const int viewRight = getWidth();
  const int viewMinFrame =
      secondsToFrames(static_cast<float>(xToTime(static_cast<float>(viewLeft)))) - 1;
  const int viewMaxFrame =
      secondsToFrames(static_cast<float>(xToTime(static_cast<float>(viewRight)))) + 1;
  const float pixelsPerFrame = framesToSeconds(1) * pixelsPerSecond;
  const int frameStep = juce::jmax(
      1, static_cast<int>(std::ceil(1.0f / juce::jmax(0.001f, pixelsPerFrame))));
  const float xScale = framesToSeconds(1) * pixelsPerSecond;
  const float dashPattern[] = {4.0f, 3.0f};

  for (const auto& note : notes) {
    const int noteStart = note.getStartFrame();
    const int noteEnd = note.getEndFrame();
    const int noteFrames = noteEnd - noteStart;
    if (noteFrames <= 0)
      continue;
    if (noteEnd <= viewMinFrame || noteStart >= viewMaxFrame)
      continue;

    const std::vector<float>* sourceSamples = nullptr;
    const std::vector<float>* curve = nullptr;
    if (lane.paramType == HNSepParamType::Voicing) {
      sourceSamples = &note.getClipHarmonicWaveform();
      curve = &note.getVoicingCurve();
    } else {
      sourceSamples = &note.getClipNoiseWaveform();
      curve = &note.getBreathCurve();
    }

    if (!sourceSamples || sourceSamples->empty())
      continue;

    const int startFrame = juce::jmax(noteStart, viewMinFrame);
    const int endFrame = juce::jmin(noteEnd, viewMaxFrame);
    juce::Path originalPath;
    juce::Path scaledPath;
    bool hasOriginal = false;
    bool hasScaled = false;
    bool anyScaleApplied = false;

    const int noteSamples = static_cast<int>(sourceSamples->size());

    for (int frame = startFrame; frame < endFrame; frame += frameStep) {
      const int localFrame = frame - noteStart;
      const int sampleStart =
          static_cast<int>((static_cast<int64_t>(localFrame) * noteSamples) /
                           std::max(1, noteFrames));
      int sampleEnd =
          static_cast<int>((static_cast<int64_t>(localFrame + frameStep) * noteSamples) /
                           std::max(1, noteFrames));
      sampleEnd = std::max(sampleEnd, sampleStart + 1);

      const float originalDb =
          computeSegmentRmsDb(*sourceSamples, sampleStart, sampleEnd);
      const float x = static_cast<float>((frame * xScale) - scrollX);
      const float originalY = dbToY(originalDb, maxDb, bounds);

      if (!hasOriginal) {
        originalPath.startNewSubPath(x, originalY);
        hasOriginal = true;
      } else {
        originalPath.lineTo(x, originalY);
      }

      const float curveValue =
          (curve && localFrame >= 0 && localFrame < static_cast<int>(curve->size()))
              ? (*curve)[static_cast<size_t>(localFrame)]
              : 100.0f;
      const float scale = curveValue / 100.0f;
      if (std::abs(scale - 1.0f) > 0.001f)
        anyScaleApplied = true;

      const float scaledDb =
          scale > 0.0f ? originalDb + 20.0f * std::log10(scale) : energyMinDb;
      const float scaledY = dbToY(scaledDb, maxDb, bounds);

      if (!hasScaled) {
        scaledPath.startNewSubPath(x, scaledY);
        hasScaled = true;
      } else {
        scaledPath.lineTo(x, scaledY);
      }
    }

    if (hasOriginal && !anyScaleApplied) {
      g.setColour(lane.curveColour.withAlpha(0.18f));
      g.strokePath(originalPath, juce::PathStrokeType(1.0f));
    }

    if (hasOriginal && anyScaleApplied) {
      juce::Path dashedPath;
      juce::PathStrokeType(1.0f).createDashedStroke(
          dashedPath, originalPath, dashPattern,
          static_cast<int>(std::size(dashPattern)));
      g.setColour(lane.curveColour.withAlpha(0.18f));
      g.fillPath(dashedPath);
    }

    if (hasScaled && anyScaleApplied) {
      g.setColour(lane.curveColour.withAlpha(0.18f));
      g.strokePath(scaledPath, juce::PathStrokeType(1.0f));
    }
  }
}

void HNSepLaneComponent::drawLaneCurves(juce::Graphics& g,
                                        const juce::Rectangle<int>& bounds,
                                        const Lane& lane) {
  if (!project)
    return;

  auto& notes = project->getNotes();
  if (notes.empty())
    return;

  const int viewLeft = 0;
  const int viewRight = getWidth();
  const int viewMinFrame = secondsToFrames(static_cast<float>(xToTime(static_cast<float>(viewLeft)))) -
                           1;
  const int viewMaxFrame = secondsToFrames(static_cast<float>(xToTime(static_cast<float>(viewRight)))) +
                           1;
  const float pixelsPerFrame =
      framesToSeconds(1) * pixelsPerSecond;
  const int frameStep = juce::jmax(
      1, static_cast<int>(std::ceil(1.0f / juce::jmax(0.001f, pixelsPerFrame))));
  const float xScale = framesToSeconds(1) * pixelsPerSecond;

  for (const auto& note : notes) {
    const int noteStart = note.getStartFrame();
    const int noteEnd = note.getEndFrame();
    if (noteEnd <= noteStart)
      continue;

    if (noteEnd <= viewMinFrame || noteStart >= viewMaxFrame)
      continue;

    const std::vector<float>* curve = nullptr;
    switch (lane.paramType) {
      case HNSepParamType::Voicing:
        curve = &note.getVoicingCurve();
        break;
      case HNSepParamType::Breath:
        curve = &note.getBreathCurve();
        break;
      case HNSepParamType::Tension:
        curve = &note.getTensionCurve();
        break;
    }

    if (!curve)
      continue;

    const int startFrame = juce::jmax(noteStart, viewMinFrame);
    const int endFrame = juce::jmin(noteEnd, viewMaxFrame);
    if (endFrame <= startFrame)
      continue;

    const float baselineY = valueToY(0.0f, lane, bounds);
    juce::Path fillPath;
    juce::Path linePath;

    bool hasStarted = false;
    float lastX = 0.0f;
    float lastY = baselineY;

    for (int frame = startFrame; frame < endFrame; frame += frameStep) {
      const float x = static_cast<float>((frame * xScale) - scrollX);
      if (x < static_cast<float>(viewLeft) || x > static_cast<float>(viewRight))
        continue;

      const int curveOffset = frame - noteStart;
      float value = lane.defaultValue;

      if (curveOffset >= 0 && curveOffset < static_cast<int>(curve->size())) {
        value = (*curve)[static_cast<size_t>(curveOffset)];
      }

      const float y = valueToY(value, lane, bounds);

      if (!hasStarted) {
        fillPath.startNewSubPath(x, baselineY);
        fillPath.lineTo(x, y);
        linePath.startNewSubPath(x, y);
        hasStarted = true;
      } else {
        fillPath.lineTo(x, y);
        linePath.lineTo(x, y);
      }

      lastX = x;
      lastY = y;
    }

    if (!hasStarted)
      continue;

    // Extend to frame edge to avoid tiny visual gap at note boundaries.
    const float endX = static_cast<float>((endFrame * xScale) - scrollX);
    fillPath.lineTo(endX, lastY);
    fillPath.lineTo(endX, baselineY);
    fillPath.closeSubPath();

    g.setColour(lane.curveColour.withAlpha(0.30f));
    g.fillPath(fillPath);

    g.setColour(lane.curveColour.withAlpha(1.0f));
    g.strokePath(linePath, juce::PathStrokeType(1.5f));
  }
}

void HNSepLaneComponent::drawSeparator(juce::Graphics& g, int sepIndex) {
  const auto bounds = getSeparatorBounds(sepIndex);
  if (bounds.isEmpty())
    return;

  g.setColour(APP_COLOR_SURFACE_RAISED.withAlpha(0.9f));
  g.fillRect(bounds);

  g.setColour(APP_COLOR_BORDER_SUBTLE.withAlpha(0.7f));
  g.drawRect(bounds);

  // Grip marker indicates this strip is draggable and not part of lane content.
  const int centerY = bounds.getCentreY();
  const int centerX = bounds.getCentreX();
  const int gripHalf = 14;
  const int gap = 2;

  g.setColour(APP_COLOR_TEXT_MUTED.withAlpha(0.7f));
  g.drawHorizontalLine(centerY - gap, static_cast<float>(centerX - gripHalf),
                       static_cast<float>(centerX + gripHalf));
  g.drawHorizontalLine(centerY, static_cast<float>(centerX - gripHalf),
                       static_cast<float>(centerX + gripHalf));
  g.drawHorizontalLine(centerY + gap, static_cast<float>(centerX - gripHalf),
                       static_cast<float>(centerX + gripHalf));
}

void HNSepLaneComponent::applyDrawing(float localX, float localY) {
  if (activeLane < 0 || activeLane >= numLanes || !project)
    return;

  auto bounds = getLaneBounds(activeLane);
  auto& lane = lanes[activeLane];

  // Right-drag reset mode writes defaults regardless of y coordinate. This makes
  // "erase" gestures stable even when the pointer leaves lane bounds.
  float value;
  if (isResetting) {
    value = lane.defaultValue;
  } else {
    value = yToValue(localY, lane, bounds);
  }

  const float minVal = lane.minValue;
  const float maxVal =
      (lane.paramType == HNSepParamType::Tension)
          ? lane.maxValue
          : (lane.paramType == HNSepParamType::Voicing) ? maxVoicing : maxBreath;
  value = juce::jlimit(minVal, maxVal, value);

  const double time = xToTime(localX);
  const int frameIndex = secondsToFrames(static_cast<float>(time));

  if (lastDrawFrame < 0) {
    applyValueAtFrame(frameIndex, value);
  } else {
    int start = lastDrawFrame;
    int end = frameIndex;
    int step = (end > start) ? 1 : -1;
    int length = std::abs(end - start);

    for (int i = 0; i <= length; ++i) {
      int idx = start + i * step;
      float t =
          length == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(length);
      float v = juce::jmap(t, 0.0f, 1.0f, lastDrawValue, value);
      applyValueAtFrame(idx, v);
    }
  }

  lastDrawFrame = frameIndex;
  lastDrawValue = value;
}

void HNSepLaneComponent::applyValueAtFrame(int frameIndex, float value) {
  if (!project || activeLane < 0)
    return;

  auto& notes = project->getNotes();
  auto paramType = lanes[activeLane].paramType;
  const float defaultVal = lanes[activeLane].defaultValue;

  for (int ni = 0; ni < static_cast<int>(notes.size()); ++ni) {
    auto& note = notes[ni];
    if (frameIndex >= note.getStartFrame() && frameIndex < note.getEndFrame()) {
      const int frameOffset = frameIndex - note.getStartFrame();
      const int noteLen = note.getDurationFrames();

      auto* curve = getCurveForNote(ni, paramType);
      if (!curve)
        return;

      if (curve->empty()) {
        // Curves are lazily allocated to keep note memory compact until needed.
        curve->resize(static_cast<size_t>(noteLen), defaultVal);
      }

      while (static_cast<int>(curve->size()) < noteLen)
        curve->push_back(defaultVal);

      if (frameOffset < 0 || frameOffset >= static_cast<int>(curve->size()))
        return;

      const float oldValue = (*curve)[static_cast<size_t>(frameOffset)];

      auto key = std::make_pair(ni, frameOffset);
      auto it = editIndexMap.find(key);
      if (it == editIndexMap.end()) {
        editIndexMap.emplace(key, pendingEdits.size());
        pendingEdits.push_back({ni, frameOffset, oldValue, value});
      } else {
        pendingEdits[it->second].newValue = value;
      }

      // Immediate write gives responsive visual feedback while dragging.
      (*curve)[static_cast<size_t>(frameOffset)] = value;
      break;
    }
  }
}

std::vector<float>* HNSepLaneComponent::getCurveForNote(int noteIndex,
                                                        HNSepParamType paramType) {
  if (!project)
    return nullptr;

  auto& notes = project->getNotes();
  if (noteIndex < 0 || noteIndex >= static_cast<int>(notes.size()))
    return nullptr;

  auto& note = notes[noteIndex];

  switch (paramType) {
    case HNSepParamType::Voicing: {
      // Safe: the Note object is mutable (from Project::getNotes()), and the
      // getter returns const-ref for API guardrails only.
      auto& curve = const_cast<std::vector<float>&>(note.getVoicingCurve());
      return &curve;
    }
    case HNSepParamType::Breath: {
      // Safe for same reason as voicing branch.
      auto& curve = const_cast<std::vector<float>&>(note.getBreathCurve());
      return &curve;
    }
    case HNSepParamType::Tension: {
      // Safe for same reason as voicing branch.
      auto& curve = const_cast<std::vector<float>&>(note.getTensionCurve());
      return &curve;
    }
  }

  return nullptr;
}

void HNSepLaneComponent::commitEdits() {
  if (pendingEdits.empty()) {
    editIndexMap.clear();
    return;
  }

  int minNote = std::numeric_limits<int>::max();
  int maxNote = std::numeric_limits<int>::min();
  for (const auto& e : pendingEdits) {
    minNote = std::min(minNote, e.noteIndex);
    maxNote = std::max(maxNote, e.noteIndex);
  }

  if (minNote > maxNote) {
    pendingEdits.clear();
    editIndexMap.clear();
    return;
  }

  auto markDirtyAndNotify = [this](int minN, int maxN) {
    if (project) {
      auto& notes = project->getNotes();
      int minFrame = std::numeric_limits<int>::max();
      int maxFrame = std::numeric_limits<int>::min();

      for (int i = minN; i <= maxN && i < static_cast<int>(notes.size()); ++i) {
        if (i < 0)
          continue;
        minFrame = std::min(minFrame, notes[i].getStartFrame());
        maxFrame = std::max(maxFrame, notes[i].getEndFrame());
      }

      if (minFrame <= maxFrame) {
        HNSepCurveProcessor::rebuildCurvesForRange(*project, minFrame, maxFrame);
        project->setParamDirtyRange(minFrame, maxFrame);
      }
    }

    if (onParamEditFinished)
      onParamEditFinished(minN, maxN);

    repaint();
  };

  if (!undoManager) {
    // If undo manager is unavailable, keep edits applied and still notify so
    // downstream synthesis remains consistent.
    markDirtyAndNotify(minNote, maxNote);
    pendingEdits.clear();
    editIndexMap.clear();
    return;
  }

  const auto paramType = lanes[activeLane].paramType;
  Project* proj = project;

  auto action = std::make_unique<ParameterCurveAction>(
      paramType, std::move(pendingEdits),
      [proj](int noteIndex, HNSepParamType pt) -> std::vector<float>* {
        if (!proj)
          return nullptr;

        auto& notes = proj->getNotes();
        if (noteIndex < 0 || noteIndex >= static_cast<int>(notes.size()))
          return nullptr;

        auto& note = notes[noteIndex];
        switch (pt) {
          case HNSepParamType::Voicing:
            return &const_cast<std::vector<float>&>(note.getVoicingCurve());
          case HNSepParamType::Breath:
            return &const_cast<std::vector<float>&>(note.getBreathCurve());
          case HNSepParamType::Tension:
            return &const_cast<std::vector<float>&>(note.getTensionCurve());
        }
        return nullptr;
      },
      [markDirtyAndNotify](int minN, int maxN) { markDirtyAndNotify(minN, maxN); });

  undoManager->addAction(std::move(action));

  // For the initial commit (not undo/redo), we must notify now because
  // addAction only pushes history; it does not call redo().
  markDirtyAndNotify(minNote, maxNote);

  pendingEdits.clear();
  editIndexMap.clear();
}
