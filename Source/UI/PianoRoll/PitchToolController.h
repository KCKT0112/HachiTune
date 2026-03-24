#pragma once

#include "../../JuceHeader.h"
#include "../../Models/Note.h"
#include "../../Models/Project.h"
#include "../../Undo/UndoActions.h"
#include "../../Utils/FourierPitchFilter.h"
#include "../../Utils/PitchToolOperations.h"
#include "../../Utils/TransformParams.h"
#include "CoordinateMapper.h"
#include "PitchToolHandles.h"
#include <functional>
#include <memory>
#include <vector>

/**
 * Handles mouse interaction with pitch tool handles.
 */
class PitchToolController {
public:
  PitchToolController();

  /**
   * Handle mouse down on pitch tool handle.
   * @return true if event was handled, false otherwise
   */
  bool mouseDown(const juce::MouseEvent& e,
                 const PitchToolHandles& handles,
                 const std::vector<Note*>& selectedNotes,
                 const CoordinateMapper& mapper);

  /**
   * Handle mouse drag on active handle.
   * @return true if event was handled, false otherwise
   */
  bool mouseDrag(const juce::MouseEvent& e,
                 std::vector<Note*>& selectedNotes,
                 const CoordinateMapper& mapper);

  /**
   * Handle mouse up (commit operation to undo stack).
   * @return true if event was handled, false otherwise
   */
  bool mouseUp(const juce::MouseEvent& e,
               PitchUndoManager* undoManager,
               std::function<void(int, int)> onRangeChanged);

  /**
   * Check if currently dragging a handle.
   */
  bool isDragging() const { return dragging; }

  /**
   * Cancel current drag operation.
   */
  void cancel();

  /**
   * Set project reference (required for accessing audioData.deltaPitch).
   */
  void setProject(Project* proj) { project = proj; }

  /**
   * Callback fired when pitch is edited (for triggering repaint).
   */
  std::function<void()> onPitchEdited;

  /**
   * Callback fired while previewing FFT filter edits for debug visualization.
   */
  std::function<void(Note*,
                     const std::vector<float>&,
                     const FourierPitchFilter::FilterResult&)>
      onFilterPreviewChanged;

private:
  Project* project = nullptr;
  bool dragging = false;
  PitchToolHandles::HandleType activeHandleType = PitchToolHandles::HandleType::None;
  Note* activeHandleNote = nullptr;
  Note* activeBoundaryPartner = nullptr;
  std::vector<Note*> affectedNotes;
  std::vector<TransformParams> originalParams;
  std::vector<std::vector<float>> originalDeltaCurves;
  juce::Point<float> dragStartPos;

  void applyOperation(std::vector<Note*>& notes,
                      PitchToolHandles::HandleType type,
                      float dragDeltaX,
                      float dragDeltaY,
                      const CoordinateMapper& mapper);
};
