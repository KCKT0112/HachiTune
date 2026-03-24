#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * FFT-based pitch curve filtering.
 *
 * Converts pitch curves to frequency domain, applies low-pass/high-pass
 * filtering, and returns the filtered curve and spectrum data for
 * visualization.
 */
class FourierPitchFilter {
public:
  struct FilterResult {
    std::vector<float> filteredPitch;
    std::vector<float> contextInputPitch;
    std::vector<float> contextFilteredPitch;
    std::vector<float> zeroMeanInputPitch;
    std::vector<float> zeroMeanFilteredPitch;
    std::vector<float> magnitudeSpectrum;
    std::vector<float> filteredMagnitudeSpectrum;
    std::vector<float> frequencyBins;
    int cropStartFrame = 0;
    int cropFrameCount = 0;
    int contextStartFrame = 0;
    float dcComponent = 0.0f;
    float lowpassHz = 0.0f;
    float highpassHz = 0.0f;
    float frameRateHz = 0.0f;
  };

  /**
   * Apply bandpass filtering to a pitch curve.
   *
   * @param deltaPitch Input pitch deviations in semitones.
   * @param lowpassHz Lowpass cutoff in Hz (remove above this frequency).
   * @param highpassHz Highpass cutoff in Hz (remove below this frequency).
   * @param frameRateHz Sample rate of the pitch curve frames in Hz.
   * @return FilterResult containing filtered pitch plus debug spectra/curves.
   */
  static FilterResult filterPitchCurve(const std::vector<float>& deltaPitch,
                                       float lowpassHz,
                                       float highpassHz,
                                       float frameRateHz,
                                       int cropStartFrame = 0,
                                       int cropFrameCount = -1);

  static float highpassStrengthToCutoffHz(float strength, float frameRateHz);
  static float lowpassStrengthToCutoffHz(float strength, float frameRateHz);

private:
  static int nextPowerOfTwo(int n);
};
