#include "PitchToolOperations.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace PitchToolOperations {

std::vector<float> tiltDeltaPitch(const std::vector<float>& deltaPitch,
                                  float pivotPosition,
                                  float amount) {
  if (deltaPitch.empty()) {
    return {};
  }

  std::vector<float> result(deltaPitch);
  if (deltaPitch.size() == 1) {
    return result;
  }

  const float clampedPivot = std::clamp(pivotPosition, 0.0f, 1.0f);
  const float maxDistance = std::max(clampedPivot, 1.0f - clampedPivot);
  if (maxDistance <= 0.0f) {
    return result;
  }

  const float invLastIndex = 1.0f / static_cast<float>(deltaPitch.size() - 1);
  for (size_t i = 0; i < deltaPitch.size(); ++i) {
    const float normalizedPosition = static_cast<float>(i) * invLastIndex;
    // Normalize by furthest edge distance so `amount` means a full-end shift.
    const float normalizedDistance =
        (normalizedPosition - clampedPivot) / maxDistance;
    result[i] = deltaPitch[i] + normalizedDistance * amount;
  }

  return result;
}

std::vector<float> reduceVariance(const std::vector<float>& deltaPitch,
                                  float factor) {
  if (deltaPitch.empty()) {
    return {};
  }

  std::vector<float> result(deltaPitch.size(), 0.0f);
  std::transform(deltaPitch.begin(), deltaPitch.end(), result.begin(),
                 [factor](float value) {
                   return value * factor;
                 });

  return result;
}

float computeMean(const std::vector<float>& deltaPitch) {
  if (deltaPitch.empty()) {
    return 0.0f;
  }

  const float sum =
      std::accumulate(deltaPitch.begin(), deltaPitch.end(), 0.0f);
  return sum / static_cast<float>(deltaPitch.size());
}

std::vector<float> applyAllTransformations(
    const std::vector<float>& originalDelta,
    float tiltLeft,
    float tiltRight,
    float varianceScale) {
  if (originalDelta.empty()) {
    return {};
  }

  std::vector<float> result = originalDelta;

  // 1. Apply variance scaling
  // Variance first so that tilt ramp is preserved even at variance=0
  if (std::abs(varianceScale - 1.0f) > 0.001f) {
    result = reduceVariance(result, varianceScale);
  }

  // 2. Apply tilt transformations (combined left + right)
  // TiltLeft: pivot at right (1.0), negative amount
  if (std::abs(tiltLeft) > 0.001f) {
    result = tiltDeltaPitch(result, 1.0f, -tiltLeft);
  }
  
  // TiltRight: pivot at left (0.0), positive amount
  if (std::abs(tiltRight) > 0.001f) {
    result = tiltDeltaPitch(result, 0.0f, tiltRight);
  }

  return result;
}

std::vector<float> applyNoteLocalTransformations(
    const std::vector<float>& originalDelta,
    const Note& note) {
  auto result = applyAllTransformations(originalDelta, note.getTiltLeft(),
                                        note.getTiltRight(),
                                        note.getVarianceScale());

  const float dScale = note.getDeltaScale();
  const float dOffset = note.getDeltaOffset();
  if (std::abs(dScale - 1.0f) > 0.0001f ||
      std::abs(dOffset) > 0.0001f) {
    for (auto& value : result) {
      value = value * dScale + dOffset;
    }
  }

  return result;
}

} // namespace PitchToolOperations
