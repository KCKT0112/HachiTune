#pragma once

#include "UndoableAction.h"
#include "../Models/Note.h"
#include "../Models/Project.h"
#include "../Utils/PitchCurveProcessor.h"
#include "../Utils/TransformParams.h"
#include <vector>
#include <functional>
#include <limits>

/**
 * Undo action for pitch tool operations (tilt, variance, smooth).
 * Stores transformation parameters, not full pitch curves.
 */
class PitchToolAction : public UndoableAction
{
public:
    PitchToolAction(
        Project *project,
        std::vector<Note *> affectedNotes,
        const std::vector<TransformParams> &oldParams,
        const std::vector<TransformParams> &newParams,
        std::function<void(int, int)> onRangeChanged = nullptr)
        : project(project),
          notes(std::move(affectedNotes)),
          oldParams(oldParams),
          newParams(newParams),
          onRangeChanged(std::move(onRangeChanged)) {}

    void undo() override
    {
        applyParams(oldParams);
    }

    void redo() override
    {
        applyParams(newParams);
    }

    juce::String getName() const override { return "Apply Pitch Tool"; }

private:
    void applyParams(const std::vector<TransformParams> &params)
    {
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (notes[i] && i < params.size())
            {
                params[i].applyToNote(*notes[i]);
                notes[i]->markDirty();
                notes[i]->markSynthDirty();
            }
        }

        if (project)
        {
            PitchCurveProcessor::rebuildBaseFromNotes(*project);

            const auto dependentNotes =
                PitchCurveProcessor::collectDependentNotes(*project, notes);

            if (!dependentNotes.empty())
            {
                int minFrame = std::numeric_limits<int>::max();
                int maxFrame = std::numeric_limits<int>::min();
                for (const auto *note : dependentNotes)
                {
                    minFrame = std::min(minFrame, note->getStartFrame());
                    maxFrame = std::max(maxFrame, note->getEndFrame());
                }
                project->setF0DirtyRange(minFrame, maxFrame);
            }

            if (onRangeChanged && !dependentNotes.empty())
            {
                int minFrame = dependentNotes[0]->getStartFrame();
                int maxFrame = dependentNotes[0]->getEndFrame();
                for (const auto *note : dependentNotes)
                {
                    minFrame = std::min(minFrame, note->getStartFrame());
                    maxFrame = std::max(maxFrame, note->getEndFrame());
                }
                onRangeChanged(minFrame, maxFrame);
            }
        }
    }

    Project *project;
    std::vector<Note *> notes;
    std::vector<TransformParams> oldParams;
    std::vector<TransformParams> newParams;
    std::function<void(int, int)> onRangeChanged;
};

/**
 * Undo action for destructive note-local pitch filtering.
 * Stores the note-local source curves that rebuild the dense deltaPitch.
 */
class PitchToolCurveAction : public UndoableAction
{
public:
    PitchToolCurveAction(
        Project* project,
        std::vector<Note*> affectedNotes,
        std::vector<std::vector<float>> oldCurves,
        std::vector<std::vector<float>> newCurves,
        std::function<void(int, int)> onRangeChanged = nullptr)
        : project(project),
          notes(std::move(affectedNotes)),
          oldCurves(std::move(oldCurves)),
          newCurves(std::move(newCurves)),
          onRangeChanged(std::move(onRangeChanged)) {}

    void undo() override
    {
        applyCurves(oldCurves);
    }

    void redo() override
    {
        applyCurves(newCurves);
    }

    juce::String getName() const override { return "Apply Pitch Filter"; }

private:
    void applyCurves(const std::vector<std::vector<float>>& curves)
    {
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (notes[i] && i < curves.size())
            {
                notes[i]->setOriginalDeltaPitch(curves[i]);
                notes[i]->markDirty();
                notes[i]->markSynthDirty();
            }
        }

        if (project)
        {
            const auto dependentNotes =
                PitchCurveProcessor::collectDependentNotes(*project, notes);
            PitchCurveProcessor::rebuildDeltaForNotes(*project, dependentNotes);

            if (!dependentNotes.empty())
            {
                int minFrame = std::numeric_limits<int>::max();
                int maxFrame = std::numeric_limits<int>::min();
                for (const auto* note : dependentNotes)
                {
                    minFrame = std::min(minFrame, note->getStartFrame());
                    maxFrame = std::max(maxFrame, note->getEndFrame());
                }
                project->setF0DirtyRange(minFrame, maxFrame);

                if (onRangeChanged)
                    onRangeChanged(minFrame, maxFrame);
            }
        }
    }

    Project* project;
    std::vector<Note*> notes;
    std::vector<std::vector<float>> oldCurves;
    std::vector<std::vector<float>> newCurves;
    std::function<void(int, int)> onRangeChanged;
};
