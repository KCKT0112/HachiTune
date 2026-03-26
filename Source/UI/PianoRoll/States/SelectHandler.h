#pragma once

#include "InteractionHandler.h"

#include <vector>

class Note;

/**
 * Handles selection, note dragging (single + multi), box selection,
 * pitch tool handle interactions, and double-click snap.
 */
class SelectHandler : public InteractionHandler {
public:
  explicit SelectHandler(PianoRollComponent &owner);

  bool mouseDown(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseDrag(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseUp(const juce::MouseEvent &e, float worldX,
               float worldY) override;
  void mouseMove(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  void mouseDoubleClick(const juce::MouseEvent &e, float worldX,
                        float worldY) override;
  bool isActive() const override;
  void cancel() override;

  bool isSingleNoteDragging() const { return isDragging; }
  Note *getDraggedNote() const { return draggedNote; }

private:
  /** Rebuild base pitch, fire edited/finished callbacks, and repaint. */
  void rebuildAndNotify();

  void prepareDragBasePreview();
  void applyDragBasePreview(float pitchOffsetSemitones);
  void restoreDragBasePreview();

  // Single note drag state
  bool isDragging = false;
  Note *draggedNote = nullptr;
  float dragStartY = 0.0f;
  float originalPitchOffset = 0.0f;
  float originalMidiNote = 60.0f;
  float boundaryF0Start = 0.0f;
  float boundaryF0End = 0.0f;
  std::vector<float> originalF0Values;
  float lastDragPitchOffset = 0.0f;
  int dragPreviewStartFrame = -1;
  int dragPreviewEndFrame = -1;
  std::vector<float> dragPreviewWeights;
  std::vector<float> dragBasePitchSnapshot;
  std::vector<float> dragF0Snapshot;
};
