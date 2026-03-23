#pragma once

#include <vector>

namespace PitchToolOperations {

/**
 * Applies a linear tilt around a pivot position.
 *
 * The value at the pivot stays unchanged, and the contour is shifted
 * linearly across the note. The furthest end from the pivot reaches
 * the full `amount` in semitones.
 */
std::vector<float> tiltDeltaPitch(const std::vector<float>& deltaPitch,
                                  float pivotPosition,
                                  float amount);

/**
 * Scales deviations from the base MIDI note (zero).
 *
 * `factor = 0` flattens to zero (base MIDI note) and `factor = 1` keeps
 * the original contour unchanged.
 */
std::vector<float> reduceVariance(const std::vector<float>& deltaPitch,
                                  float factor);

/**
 * Computes the arithmetic mean of a pitch contour.
 * Returns 0 when the input is empty.
 */
float computeMean(const std::vector<float>& deltaPitch);

/**
 * Applies note-local pitch-shape transformations non-destructively.
 * 
 * This function chains multiple transformations in order:
 * 1. Variance scaling
 * 2. Tilt (left and right combined)
 *
 * Boundary smoothing is applied later on the dense project-wide pitch curve,
 * because the smoothing target spans both notes around a boundary.
 * 
 * @param originalDelta The pristine deltaPitch curve from analysis (never modified)
 * @param tiltLeft Tilt amount at left edge in semitones
 * @param tiltRight Tilt amount at right edge in semitones
 * @param varianceScale Variance scaling factor (1.0=unchanged, 0.0=flat,
 *        >1.0=amplify, <0.0=invert)
 * @return Transformed deltaPitch curve
 */
std::vector<float> applyAllTransformations(const std::vector<float>& originalDelta,
                                           float tiltLeft,
                                           float tiltRight,
                                           float varianceScale);

} // namespace PitchToolOperations
