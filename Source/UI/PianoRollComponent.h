#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"
#include "../Utils/DrawCurve.h"
#include "../Utils/BasePitchPreview.h"
#include "../Utils/UndoManager.h"
#include "PianoRoll/BoxSelector.h"
#include "PianoRoll/CoordinateMapper.h"
#include "PianoRoll/NoteSplitter.h"
#include "PianoRoll/PianoRollRenderer.h"
#include "PianoRoll/PitchEditor.h"
#include "PianoRoll/ScrollZoomController.h"

#include <deque>
#include <memory>
#include <unordered_map>

class PitchUndoManager;

/**
 * Edit mode for the piano roll.
 */
enum class EditMode {
  Select, // Normal selection and dragging
  Modulation, // Note Edit: Modulate
  Drift, // Note Edit: Drift
  Slope, // Note Edit: Slope
  Stretch, // Stretch note timing
  Draw,   // Pitch drawing mode
  Split   // Note splitting mode
};

/**
 * Piano roll component for displaying and editing notes.
 * Supports DPI-aware scaling for multi-monitor setups.
 */
class PianoRollComponent : public juce::Component,
                           public juce::ScrollBar::Listener,
                           public juce::KeyListener {
public:
  PianoRollComponent();
  ~PianoRollComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;
  void mouseMove(const juce::MouseEvent &e) override;
  void mouseDoubleClick(const juce::MouseEvent &e) override;
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseMagnify(const juce::MouseEvent &e, float scaleFactor) override;

  // Focus handling - re-grab focus when lost (important for plugin mode)
  void focusLost(FocusChangeType cause) override;
  void focusGained(FocusChangeType cause) override;

  // KeyListener
  bool keyPressed(const juce::KeyPress &key,
                  juce::Component *originatingComponent) override;

  // ScrollBar::Listener
  void scrollBarMoved(juce::ScrollBar *scrollBar,
                      double newRangeStart) override;

  // Project
  void setProject(Project *proj);
  Project *getProject() const { return project; }

  // Undo Manager
  void setUndoManager(PitchUndoManager *manager);
  PitchUndoManager *getUndoManager() const { return undoManager; }

  // Cursor
  void setCursorTime(double time);
  double getCursorTime() const { return cursorTime; }

  // Zoom with optional center point
  void setPixelsPerSecond(float pps, bool centerOnCursor = false);
  void setPixelsPerSemitone(float pps);
  float getPixelsPerSecond() const { return pixelsPerSecond; }
  float getPixelsPerSemitone() const { return pixelsPerSemitone; }

  // Scroll
  void setScrollX(double x);
  double getScrollX() const { return scrollX; }
  void centerOnPitchRange(float minMidi, float maxMidi);

  // Edit mode
  void setEditMode(EditMode mode);
  EditMode getEditMode() const { return editMode; }

  // Cancel current drawing operation (used when undo is triggered during
  // drawing)
  void cancelDrawing();

  // View settings
  void setShowDeltaPitch(bool show) {
    showDeltaPitch = show;
    repaint();
  }
  void setShowBasePitch(bool show) {
    showBasePitch = show;
    repaint();
  }
  bool getShowDeltaPitch() const { return showDeltaPitch; }
  bool getShowBasePitch() const { return showBasePitch; }

  // Callbacks
  std::function<void(Note *)> onNoteSelected;
  std::function<void()> onPitchEdited;
  std::function<void()> onPitchEditFinished; // Called when dragging ends
  std::function<void(double)> onSeek;
  std::function<void(float)> onZoomChanged;
  std::function<void(double)> onScrollChanged;
  std::function<void(const LoopRange &)> onLoopRangeChanged;
  std::function<void(int, int)>
      onReinterpolateUV; // Called to re-infer UV regions (startFrame, endFrame)
  std::function<void()> onUndo; // Called when Ctrl+Z is pressed
  std::function<void()> onRedo; // Called when Ctrl+Y/Ctrl+Shift+Z is pressed
  std::function<void()> onPlayPause; // Called when Space is pressed

private:
  void drawBackgroundWaveform(juce::Graphics &g,
                              const juce::Rectangle<int> &visibleArea);
  void drawGrid(juce::Graphics &g);
  void drawTimeline(juce::Graphics &g);
  void drawLoopTimeline(juce::Graphics &g);
  void drawNotes(juce::Graphics &g);
  void drawPitchCurves(juce::Graphics &g);
  void drawCursor(juce::Graphics &g);
  void drawPianoKeys(juce::Graphics &g);
  void drawDrawingCursor(juce::Graphics &g); // Draw mode indicator
  void drawSelectionRect(juce::Graphics &g); // Box selection rectangle
  void drawLoopOverlay(juce::Graphics &g);
  void drawStretchGuides(juce::Graphics &g);

  float midiToY(float midiNote) const;
  float yToMidi(float y) const;
  float timeToX(double time) const;
  double xToTime(float x) const;

  Note *findNoteAt(float x, float y);
  void updateScrollBars();
  void updateBasePitchCacheIfNeeded();
  void reapplyBasePitchForNote(
      Note *note); // Recalculate F0 from base pitch + delta after undo/redo
  void prepareDragBasePreview();
  void prepareNoteEditPreview();
  void applyDragBasePreview(float pitchOffsetSemitones);
  void applyNoteEditPreview(const std::vector<float> &deltaPitch);
  void restoreDragBasePreview();
  bool isNoteEditMode() const {
    return editMode == EditMode::Modulation || editMode == EditMode::Drift ||
           editMode == EditMode::Slope;
  }
  struct StretchBoundary {
    Note *left = nullptr;
    Note *right = nullptr;
    int frame = 0;
  };

  struct StretchDragState {
    bool active = false;
    bool changed = false;
    StretchBoundary boundary;
    int originalBoundary = 0;
    int originalLeftStart = 0;
    int originalLeftEnd = 0;
    int originalRightStart = 0;
    int originalRightEnd = 0;
    int rangeStartFull = 0;
    int rangeEndFull = 0;
    int rangeStart = 0;
    int rangeEnd = 0;
    int minFrame = 0;
    int maxFrame = 0;
    int currentBoundary = 0;
    std::vector<float> leftDelta;
    std::vector<float> rightDelta;
    std::vector<bool> leftVoiced;
    std::vector<bool> rightVoiced;
    std::vector<float> originalLeftClip;
    std::vector<float> originalRightClip;
    std::vector<std::vector<float>> originalMelRangeFull;
    std::vector<float> originalDeltaRangeFull;
    std::vector<bool> originalVoicedRangeFull;
  };

  std::vector<StretchBoundary> collectStretchBoundaries() const;
  int findStretchBoundaryIndex(float worldX, float tolerancePx) const;
  void startStretchDrag(const StretchBoundary &boundary);
  void updateStretchDrag(int targetFrame);
  void finishStretchDrag();
  void cancelStretchDrag();

  // Pitch drawing helpers
  void applyPitchDrawing(float x, float y);
  void commitPitchDrawing();
  void commitNoteEditing();
  void applyPitchPoint(int frameIndex, int midiCents);
  void startNewPitchCurve(int frameIndex, int midiCents);

  Project *project = nullptr;
  PitchUndoManager *undoManager = nullptr;

  // New modular components
  std::unique_ptr<CoordinateMapper> coordMapper;
  std::unique_ptr<PianoRollRenderer> renderer;
  std::unique_ptr<ScrollZoomController> scrollZoomController;
  std::unique_ptr<PitchEditor> pitchEditor;
  std::unique_ptr<BoxSelector> boxSelector;
  std::unique_ptr<NoteSplitter> noteSplitter;

  float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
  float pixelsPerSemitone = DEFAULT_PIXELS_PER_SEMITONE;

  double cursorTime = 0.0;
  double scrollX = 0.0;
  double scrollY = 0.0;

  // Layout constants
  static constexpr int pianoKeysWidth = 60;
  static constexpr int timelineHeight = 24;
  static constexpr int loopTimelineHeight = 16;
  static constexpr int headerHeight = timelineHeight + loopTimelineHeight;

  // Edit mode
  EditMode editMode = EditMode::Select;

  // View settings
  bool showDeltaPitch = true;
  bool showBasePitch = false;

  // Dragging state
  bool isDragging = false;
  Note *draggedNote = nullptr;
  float dragStartY = 0.0f;
  float originalPitchOffset = 0.0f;
  float originalMidiNote = 60.0f; // Original MIDI note before drag
  float boundaryF0Start =
      0.0f; // F0 value before note start (for smooth transition)
  float boundaryF0End = 0.0f; // F0 value after note end (for smooth transition)
  std::vector<float> originalF0Values; // F0 values before drag for undo
  float lastDragPitchOffset = 0.0f;
  int dragPreviewStartFrame = -1;
  int dragPreviewEndFrame = -1;
  std::vector<float> dragPreviewWeights;
  std::vector<float> dragBasePitchSnapshot;
  std::vector<float> dragF0Snapshot;

  // Pitch drawing state
  bool isDrawing = false;
  std::vector<F0FrameEdit> drawingEdits; // unique edits per frame
  std::unordered_map<int, size_t> drawingEditIndexByFrame;
  int lastDrawFrame = -1;
  int lastDrawValueCents = 0;
  DrawCurve *activeDrawCurve = nullptr;
  std::deque<std::unique_ptr<DrawCurve>> drawCurves;

  // Note edit (Modulation, Drift, Slope) state
  bool isNoteEditing = false;
  Note *neNote = nullptr;
  float neStartX = 0.0f;
  float neStartY = 0.0f;
  float neBoundaryF0Start = 0.0f; // F0 value before note start (for smooth transition)
  float neBoundaryF0End = 0.0f; // F0 value after note end (for smooth transition)
  std::vector<float> neOriginalF0Values; // F0 values before drag for undo
  std::vector<float> neOriginalDeltaPitch;
  int nePreviewStartFrame = -1;
  int nePreviewEndFrame = -1;
  std::vector<float> nePreviewWeights;
  std::vector<float> neDeltaPitchSnapshot;
  std::vector<float> neF0Snapshot;

  // Split mode guide line
  float splitGuideX =
      -1.0f; // World X coordinate for split guide line (-1 = hidden)
  Note *splitGuideNote = nullptr; // Note being hovered for split

  // Stretch mode state
  StretchDragState stretchDrag;
  int hoveredStretchBoundaryIndex = -1;
  static constexpr float stretchHandleHitPadding = 6.0f;
  static constexpr int minStretchNoteFrames = 3;

  // Loop range drag state
  enum class LoopDragMode {
    None,
    Create,
    ResizeStart,
    ResizeEnd,
    Move
  };
  LoopDragMode loopDragMode = LoopDragMode::None;
  float loopDragStartX = 0.0f;
  double loopDragStartSeconds = 0.0;
  double loopDragEndSeconds = 0.0;
  double loopDragAnchorSeconds = 0.0;
  double loopDragOriginalStart = 0.0;
  double loopDragOriginalEnd = 0.0;
  static constexpr float loopHandleHitPadding = 6.0f;

  // Scrollbars
  juce::ScrollBar horizontalScrollBar{false};
  juce::ScrollBar verticalScrollBar{true};

  // Waveform cache for performance
  juce::Image waveformCache;
  double cachedScrollX = -1.0;
  float cachedPixelsPerSecond = -1.0f;
  int cachedWidth = 0;
  int cachedHeight = 0;

  // Base pitch curve cache for performance
  // Only recalculates when notes change, not on every repaint
  std::vector<float> cachedBasePitch;
  size_t cachedNoteCount = 0;
  int cachedTotalFrames = 0;
  bool cacheInvalidated = true; // Start invalidated, force first calculation

public:
  void invalidateBasePitchCache() {
    cacheInvalidated = true;
    cachedNoteCount = 0;
    cachedBasePitch.clear();
    cachedBasePitch.shrink_to_fit(); // Release memory
  }

private:
  // Optional: disable base pitch rendering for performance testing
  static constexpr bool ENABLE_BASE_PITCH_DEBUG =
      true; // Set to false to disable

  // Mouse drag throttling
  juce::int64 lastDragRepaintTime = 0;
  static constexpr juce::int64 minDragRepaintInterval = 16; // ~60fps max
  juce::int64 lastStretchPreviewTime = 0;
  static constexpr juce::int64 minStretchPreviewInterval = 120;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};
