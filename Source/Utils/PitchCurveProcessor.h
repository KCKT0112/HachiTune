#pragma once

#include "../Models/Project.h"
#include <vector>

namespace PitchCurveProcessor
{
    struct SmoothingDebugSegment
    {
        std::vector<int> frames;
        std::vector<float> idealMidiValues;
    };

    /**
     * Linearly interpolate pitch through unvoiced regions using the uv mask.
     * Returns a dense pitch (Hz) array with the same length as the input.
     */
    std::vector<float> interpolateWithUvMask(const std::vector<float>& pitchHz,
                                             const std::vector<bool>& uvMask);

    /**
     * Rebuild base pitch (midi) from current notes and keep existing delta.
     * Ensures base/delta are dense and aligned to the project frame count,
     * then composes audioData.f0 (without applying the uv mask).
     */
    void rebuildBaseFromNotes(Project& project);

    /**
     * Rebuild delta pitch from notes after parameter edits.
     *
     * Boundary smoothing now spans both notes around a transition, so this
     * currently falls back to a full note rebuild for correctness.
     */
    void rebuildDeltaForNotes(Project& project, const std::vector<Note*>& affectedNotes);

    /**
     * Expand a note set to include immediate non-rest neighbors whose boundary
     * smoothing depends on the edited notes.
     */
    std::vector<Note*> collectDependentNotes(Project& project,
                                             const std::vector<Note*>& seedNotes);

    /**
     * Collect the current ideal boundary-smoothing curves for debug drawing.
     * The returned MIDI curves are the unblended Bezier targets before they are
     * mixed back into the dense delta pitch. These targets are built on top of
     * the dense base-pitch path, not the current raw F0 contour.
     */
    std::vector<SmoothingDebugSegment> collectIdealSmoothingDebugSegments(
        const Project& project);

    /**
     * Lightweight rebuild for interactive stretch drag.
     * Currently falls back to a full rebuild so the shared boundary smoothing
     * model stays consistent while note timing is changing.
     */
    void rebuildBaseFromNotesForDrag(Project& project, const std::vector<Note*>& affectedNotes);

    /**
     * Rebuild base and delta from a source pitch (Hz). This is used after
     * detection/segmentation or when we need to recompute delta from edited
     * curves. The uv mask is kept as-is; the composed f0 omits uv masking.
     */
    void rebuildCurvesFromSource(Project& project,
                                 const std::vector<float>& sourcePitchHz);

    /**
     * Compose f0 (Hz) from base + delta + optional global offset.
     * When applyUvMask is true, frames marked unvoiced are forced to 0 for
     * synthesis; when false the curve stays dense for UI display.
     */
    std::vector<float> composeF0(const Project& project,
                                 bool applyUvMask,
                                 float globalPitchOffset = 0.0f);

    /**
     * Convenience to update audioData.f0 in-place using composeF0.
     */
    void composeF0InPlace(Project& project,
                          bool applyUvMask,
                          float globalPitchOffset = 0.0f);
} // namespace PitchCurveProcessor


