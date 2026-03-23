#include "PitchToolController.h"
#include "../../Utils/PitchCurveProcessor.h"

#include <algorithm>
#include <limits>

namespace {

Note* findBoundaryPartner(Project* project,
                          Note* note,
                          PitchToolHandles::HandleType handleType) {
  if (!project || !note) {
    return nullptr;
  }

  auto& allNotes = project->getNotes();
  auto it = std::find_if(allNotes.begin(), allNotes.end(),
                         [note](const Note& candidate) {
                           return &candidate == note;
                         });
  if (it == allNotes.end()) {
    return nullptr;
  }

  if (handleType == PitchToolHandles::HandleType::SmoothLeft) {
    auto prevIt = it;
    while (prevIt != allNotes.begin()) {
      --prevIt;
      if (!prevIt->isRest()) {
        return &*prevIt;
      }
    }
    return nullptr;
  }

  if (handleType == PitchToolHandles::HandleType::SmoothRight) {
    auto nextIt = it;
    ++nextIt;
    while (nextIt != allNotes.end()) {
      if (!nextIt->isRest()) {
        return &*nextIt;
      }
      ++nextIt;
    }
  }

  return nullptr;
}

}  // namespace

PitchToolController::PitchToolController() {
}

bool PitchToolController::mouseDown(const juce::MouseEvent& e,
                                    const PitchToolHandles& handles,
                                    const std::vector<Note*>& selectedNotes,
                                    const CoordinateMapper& mapper) {
  juce::ignoreUnused(mapper);

  const int hitIndex = handles.hitTest(e.position.x, e.position.y);
  if (hitIndex < 0) {
    return false;
  }

  const auto& handle = handles.getHandle(hitIndex);
  activeHandleType = handle.type;
  activeHandleNote = handle.note;
  activeBoundaryPartner = nullptr;

  if (handle.note != nullptr &&
      (handle.type == PitchToolHandles::HandleType::SmoothLeft ||
       handle.type == PitchToolHandles::HandleType::SmoothRight)) {
    affectedNotes = {handle.note};
    activeBoundaryPartner =
        findBoundaryPartner(project, handle.note, handle.type);
    if (activeBoundaryPartner != nullptr) {
      affectedNotes.push_back(activeBoundaryPartner);
    }
  } else if (handle.note != nullptr) {
    affectedNotes = {handle.note};
  } else if (!selectedNotes.empty()) {
    affectedNotes = selectedNotes;
  } else {
    return false;
  }

  originalParams.clear();
  originalParams.reserve(affectedNotes.size());
  for (auto* note : affectedNotes) {
    if (note) {
      auto params = TransformParams::fromNote(*note);

      // Store a baseline MIDI note with the tilt mean removed so dragging the
      // tilt handles stays absolute instead of accumulating repeatedly.
      const float currentTiltMean =
          (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
      params.midiNote = note->getMidiNote() - currentTiltMean;

      originalParams.push_back(params);
    } else {
      originalParams.emplace_back();
    }
  }

  dragStartPos = e.position;
  dragging = true;
  return true;
}

bool PitchToolController::mouseDrag(const juce::MouseEvent& e,
                                    std::vector<Note*>& selectedNotes,
                                    const CoordinateMapper& mapper) {
  juce::ignoreUnused(selectedNotes);

  if (!dragging) {
    return false;
  }

  const float deltaX = e.position.x - dragStartPos.x;
  const float deltaY = e.position.y - dragStartPos.y;

  applyOperation(affectedNotes, activeHandleType, deltaX, deltaY, mapper);
  return true;
}

bool PitchToolController::mouseUp(
    const juce::MouseEvent& e,
    PitchUndoManager* undoManager,
    std::function<void(int, int)> onRangeChanged) {
  juce::ignoreUnused(e);

  if (!dragging) {
    return false;
  }

  std::vector<TransformParams> newParams;
  newParams.reserve(affectedNotes.size());
  for (auto* note : affectedNotes) {
    if (note) {
      newParams.push_back(TransformParams::fromNote(*note));
    } else {
      newParams.emplace_back();
    }
  }

  std::vector<TransformParams> undoOldParams = originalParams;
  for (size_t i = 0; i < undoOldParams.size(); ++i) {
    const float tiltMean =
        (undoOldParams[i].tiltLeft + undoOldParams[i].tiltRight) / 2.0f;
    undoOldParams[i].midiNote += tiltMean;
  }

  auto action = std::make_unique<PitchToolAction>(
      project, affectedNotes, undoOldParams, newParams, onRangeChanged);

  if (undoManager) {
    undoManager->addAction(std::move(action));
  }

  if (onRangeChanged) {
    for (auto* note : affectedNotes) {
      if (note) {
        onRangeChanged(note->getStartFrame(), note->getEndFrame());
      }
    }
  }

  dragging = false;
  activeHandleType = PitchToolHandles::HandleType::None;
  activeHandleNote = nullptr;
  activeBoundaryPartner = nullptr;
  affectedNotes.clear();
  originalParams.clear();
  return true;
}

void PitchToolController::applyOperation(std::vector<Note*>& notes,
                                         PitchToolHandles::HandleType type,
                                         float dragDeltaX,
                                         float dragDeltaY,
                                         const CoordinateMapper& mapper) {
  if (!project) {
    return;
  }

  juce::ignoreUnused(dragDeltaX);

  const float pixelsPerSemitone =
      juce::jmax(1.0f, mapper.getPixelsPerSemitone());
  const float semitoneDelta = -dragDeltaY / pixelsPerSemitone;

  auto restoreOriginalState = [this]() {
    for (size_t i = 0; i < affectedNotes.size(); ++i) {
      if (i >= originalParams.size() || affectedNotes[i] == nullptr) {
        continue;
      }

      const auto& origParams = originalParams[i];
      origParams.applyToNote(*affectedNotes[i]);
      const float tiltMean =
          (origParams.tiltLeft + origParams.tiltRight) / 2.0f;
      affectedNotes[i]->setMidiNote(origParams.midiNote + tiltMean);
    }
  };

  auto findOriginalParams = [this](Note* target) -> const TransformParams* {
    for (size_t i = 0; i < affectedNotes.size(); ++i) {
      if (affectedNotes[i] == target && i < originalParams.size()) {
        return &originalParams[i];
      }
    }
    return nullptr;
  };

  if ((type == PitchToolHandles::HandleType::SmoothLeft ||
       type == PitchToolHandles::HandleType::SmoothRight) &&
      activeHandleNote != nullptr) {
    restoreOriginalState();

    Note* editedNote = activeHandleNote;
    Note* partnerNote = activeBoundaryPartner;
    const auto* editedOrig = findOriginalParams(editedNote);
    const auto* partnerOrig =
        partnerNote != nullptr ? findOriginalParams(partnerNote) : nullptr;

    const int originalSharedFrames =
        type == PitchToolHandles::HandleType::SmoothLeft
            ? std::max(editedOrig != nullptr ? editedOrig->smoothLeftFrames
                                            : editedNote->getSmoothLeftFrames(),
                       partnerOrig != nullptr ? partnerOrig->smoothRightFrames
                                              : (partnerNote != nullptr
                                                     ? partnerNote->getSmoothRightFrames()
                                                     : 0))
            : std::max(editedOrig != nullptr ? editedOrig->smoothRightFrames
                                            : editedNote->getSmoothRightFrames(),
                       partnerOrig != nullptr ? partnerOrig->smoothLeftFrames
                                              : (partnerNote != nullptr
                                                     ? partnerNote->getSmoothLeftFrames()
                                                     : 0));

    const int maxFrames =
        std::max(editedNote->getDurationFrames(),
                 partnerNote != nullptr ? partnerNote->getDurationFrames() : 0);
    const int frameDelta = static_cast<int>(std::round(
        (-dragDeltaY / 120.0f) * static_cast<float>(maxFrames)));
    const int newSharedFrames = juce::jlimit(
        0, maxFrames, originalSharedFrames + frameDelta);

    if (type == PitchToolHandles::HandleType::SmoothLeft) {
      editedNote->setSmoothLeftFrames(newSharedFrames);
      if (partnerNote != nullptr) {
        partnerNote->setSmoothRightFrames(newSharedFrames);
      }
    } else {
      editedNote->setSmoothRightFrames(newSharedFrames);
      if (partnerNote != nullptr) {
        partnerNote->setSmoothLeftFrames(newSharedFrames);
      }
    }

    editedNote->markDirty();
    if (partnerNote != nullptr) {
      partnerNote->markDirty();
    }
  } else {
    for (size_t i = 0; i < notes.size(); ++i) {
      auto* note = notes[i];
      if (!note || i >= originalParams.size()) {
        continue;
      }

      const auto& origParams = originalParams[i];
      origParams.applyToNote(*note);

      switch (type) {
        case PitchToolHandles::HandleType::TiltLeft:
        {
          note->setTiltLeft(origParams.tiltLeft + semitoneDelta);
          const float newTiltMean =
              (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
          note->setMidiNote(origParams.midiNote + newTiltMean);
          break;
        }
        case PitchToolHandles::HandleType::TiltRight:
        {
          note->setTiltRight(origParams.tiltRight + semitoneDelta);
          const float newTiltMean =
              (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
          note->setMidiNote(origParams.midiNote + newTiltMean);
          break;
        }
        case PitchToolHandles::HandleType::ReduceVariance:
        {
          const float dragDelta = -dragDeltaY / 100.0f;
          note->setVarianceScale(origParams.varianceScale + dragDelta);
          const float currentTiltMean =
              (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
          note->setMidiNote(origParams.midiNote + currentTiltMean);
          break;
        }
        case PitchToolHandles::HandleType::SmoothLeft:
        case PitchToolHandles::HandleType::SmoothRight:
        case PitchToolHandles::HandleType::None:
        default:
          continue;
      }

      note->markDirty();
    }
  }

  const auto dependentNotes =
      PitchCurveProcessor::collectDependentNotes(*project, notes);

  // Boundary smoothing now spans both notes around a transition, so the dense
  // curve is rebuilt from note parameters for correctness during drag.
  PitchCurveProcessor::rebuildDeltaForNotes(*project, dependentNotes);

  if (!dependentNotes.empty()) {
    int minFrame = std::numeric_limits<int>::max();
    int maxFrame = std::numeric_limits<int>::min();
    for (const auto* note : dependentNotes) {
      minFrame = std::min(minFrame, note->getStartFrame());
      maxFrame = std::max(maxFrame, note->getEndFrame());
    }
    project->setF0DirtyRange(minFrame, maxFrame);
  }

  if (onPitchEdited) {
    onPitchEdited();
  }
}

void PitchToolController::cancel() {
  if (!dragging) {
    return;
  }

  for (size_t i = 0; i < affectedNotes.size(); ++i) {
    if (i < originalParams.size() && affectedNotes[i]) {
      const auto& params = originalParams[i];
      params.applyToNote(*affectedNotes[i]);

      const float tiltMean = (params.tiltLeft + params.tiltRight) / 2.0f;
      affectedNotes[i]->setMidiNote(params.midiNote + tiltMean);
    }
  }

  dragging = false;
  activeHandleType = PitchToolHandles::HandleType::None;
  activeHandleNote = nullptr;
  activeBoundaryPartner = nullptr;
  affectedNotes.clear();
  originalParams.clear();
}
