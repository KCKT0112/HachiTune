#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Undo/ParameterActions.h"
#include "../UI/Components/StyledWidgets.h"
#include "../Utils/Constants.h"
#include "../Utils/Localization.h"
#include "../Utils/UI/Theme.h"

class PitchUndoManager;

/**
 * Three-lane parameter curve editor for hnsep (voicing, breath, tension).
 *
 * Layout:
 *   [Voicing lane]  separator  [Breath lane]  separator  [Tension lane]
 *
 * Each lane renders per-note parameter curves as filled/line graphs.
 * - Left-click-drag draws values with linear interpolation between skipped frames.
 * - Right-click-drag resets the curve to default values.
 * - Edits are committed as ParameterCurveAction on mouse up for undo support.
 * - Separators between lanes are draggable to resize proportions.
 *
 * X-axis is synchronized with PianoRollComponent via pixelsPerSecond / scrollX.
 *
 * @see ParameterActions.h for undo action types.
 * @see DrawHandler.cpp for the linear interpolation pattern this replicates.
 */
class HNSepLaneComponent : public juce::Component
{
public:
  HNSepLaneComponent();
  ~HNSepLaneComponent() override = default;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;
  void mouseMove(const juce::MouseEvent &e) override;
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override;
  void refreshLocalizedText();

  // Data binding
  void setProject(Project *proj) { project = proj; repaint(); }
  Project *getProject() const { return project; }
  void setUndoManager(PitchUndoManager *mgr) { undoManager = mgr; }
  void setMouseWheelPassthroughTarget(juce::Component *target)
  {
    mouseWheelPassthroughTarget = target;
  }

  // Scroll/zoom synchronization with piano roll
  void setPixelsPerSecond(float pps)
  {
    if (std::abs(pixelsPerSecond - pps) < 0.01f)
      return;
    pixelsPerSecond = pps;
    repaint();
  }
  float getPixelsPerSecond() const { return pixelsPerSecond; }
  void setScrollX(double x)
  {
    if (std::abs(scrollX - x) < 0.5)
      return;
    scrollX = x;
    repaint();
  }
  double getScrollX() const { return scrollX; }

  // Range dropdowns for voicing and breath max values
  void setMaxVoicing(float maxVal) { maxVoicing = maxVal; repaint(); }
  float getMaxVoicing() const { return maxVoicing; }
  void setMaxBreath(float maxVal) { maxBreath = maxVal; repaint(); }
  float getMaxBreath() const { return maxBreath; }

  // Left-side piano keys width to align with piano roll
  void setPianoKeysWidth(int width) { pianoKeysWidth = width; repaint(); }

  /**
   * Callback fired on mouse up after drawing, for the parent to trigger
   * resynthesis. Arguments: (minNoteIndex, maxNoteIndex) of affected notes.
   */
  std::function<void(int, int)> onParamEditFinished;

  /**
   * Callback fired during drawing (mouse drag) for live preview repaint.
   */
  std::function<void()> onParamEdited;

private:
  // -----------------------------------------------------------------------
  // Lane geometry
  // -----------------------------------------------------------------------

  /** Information about a single parameter lane. */
  struct Lane
  {
    HNSepParamType paramType;
    juce::String label;
    juce::Colour curveColour;
    float defaultValue;    // Default value when resetting
    float minValue;        // Min of parameter range
    float maxValue;        // Max of parameter range (voicing: maxVoicing, breath: maxBreath)
    float proportion;      // Relative height fraction (sums to 1.0)
  };

  static constexpr int numLanes = 3;
  Lane lanes[numLanes];

  /** Returns the pixel bounds of lane i within this component. */
  juce::Rectangle<int> getLaneBounds(int laneIndex) const;

  /** Returns the separator hit rect between lane i and lane i+1. */
  juce::Rectangle<int> getSeparatorBounds(int sepIndex) const;

  /** Which lane is under the given local y coordinate? -1 if none. */
  int laneIndexAtY(int y) const;

  // -----------------------------------------------------------------------
  // Coordinate mapping
  // -----------------------------------------------------------------------

  /** Convert local x to time in seconds (accounts for pianoKeysWidth + scrollX). */
  double xToTime(float x) const;

  /** Convert time in seconds to local x. */
  float timeToX(double time) const;

  /** Convert parameter value to local y within a lane bounds. */
  float valueToY(float value, const Lane &lane, const juce::Rectangle<int> &bounds) const;

  /** Convert local y to parameter value within a lane bounds. */
  float yToValue(float y, const Lane &lane, const juce::Rectangle<int> &bounds) const;

  /** Convert RMS energy in dB to local y within a lane bounds. */
  float dbToY(float dbValue, float maxDb,
              const juce::Rectangle<int> &bounds) const;

  // -----------------------------------------------------------------------
  // Drawing
  // -----------------------------------------------------------------------

  void drawLane(juce::Graphics &g, int laneIndex);
  void drawLaneBackground(juce::Graphics &g, const juce::Rectangle<int> &bounds,
                          const Lane &lane);
  void drawLaneEnergyOverlay(juce::Graphics &g, const juce::Rectangle<int> &bounds,
                             const Lane &lane);
  void drawLaneCurves(juce::Graphics &g, const juce::Rectangle<int> &bounds,
                      const Lane &lane);
  void drawSeparator(juce::Graphics &g, int sepIndex);
  void updateControlBounds();

  // -----------------------------------------------------------------------
  // Mouse interaction state
  // -----------------------------------------------------------------------

  bool isDrawing = false;
  bool isResetting = false; // Right-click-drag resets to default
  bool isGesturePending = false;
  bool pendingGestureResetting = false;
  int activeLane = -1;      // Which lane is being drawn in
  int lastDrawFrame = -1;
  float lastDrawValue = 0.0f;
  juce::Point<float> pendingGestureStart;

  // Accumulated edits for current drag gesture
  std::vector<ParameterFrameEdit> pendingEdits;
  // Map: (noteIndex, frameOffset) -> index into pendingEdits for dedup
  std::map<std::pair<int, int>, size_t> editIndexMap;

  // Separator dragging
  int draggingSeparator = -1; // -1 = not dragging
  float dragStartProportion0 = 0.0f;
  float dragStartProportion1 = 0.0f;
  int dragStartY = 0;

  // -----------------------------------------------------------------------
  // Edit helpers
  // -----------------------------------------------------------------------

  /** Apply a single parameter value at the given frame index. */
  void applyValueAtFrame(int frameIndex, float value);

  /** Apply drawing at a position, with linear interpolation from last point. */
  void applyDrawing(float localX, float localY);

  /** Commit pending edits as an undoable action. */
  void commitEdits();

  /** Get the curve vector pointer for a note + param type (for undo lambda). */
  std::vector<float> *getCurveForNote(int noteIndex, HNSepParamType paramType);

  // -----------------------------------------------------------------------
  // State
  // -----------------------------------------------------------------------

  Project *project = nullptr;
  PitchUndoManager *undoManager = nullptr;
  juce::Component *mouseWheelPassthroughTarget = nullptr;

  static constexpr float energyMinDb = -90.0f;
  float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
  double scrollX = 0.0;
  float maxVoicing = 200.0f;  // Dropdown max for voicing lane
  float maxBreath = 200.0f;   // Dropdown max for breath lane
  float voicingEnergyMaxDb = -3.0f;
  float breathEnergyMaxDb = -12.0f;
  int pianoKeysWidth = 60;

  // Separate range dropdowns for voicing and breath lanes
  juce::ComboBox voicingRangeDropdown;
  juce::ComboBox breathRangeDropdown;
  juce::ComboBox voicingEnergyDropdown;
  juce::ComboBox breathEnergyDropdown;
  StyledToggleButton voicingEnergyVisibilityToggle;
  StyledToggleButton breathEnergyVisibilityToggle;

  static constexpr int separatorHeight = 6;
  static constexpr int labelWidth = 50;
  static constexpr int labelHeight = 16;
  static constexpr int dropdownWidth = 70;
  static constexpr int energyDropdownWidth = 76;
  static constexpr int energyToggleWidth = 72;
  static constexpr int dropdownHeight = 20;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HNSepLaneComponent)
};
