#include "FourierPitchFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace {

int nextPowerOfTwoInternal(int n) {
  if (n <= 1) {
    return 1;
  }

  int power = 1;
  while (power < n) {
    power <<= 1;
  }

  return power;
}

std::vector<float> removeDcComponent(const std::vector<float>& input,
                                     float& dcComponent) {
  dcComponent = 0.0f;
  if (input.empty()) {
    return {};
  }

  dcComponent =
      std::accumulate(input.begin(), input.end(), 0.0f) /
      static_cast<float>(input.size());

  std::vector<float> zeroMean(input.size(), 0.0f);
  for (size_t i = 0; i < input.size(); ++i) {
    zeroMean[i] = input[i] - dcComponent;
  }
  return zeroMean;
}

struct SpectrumData {
  std::vector<float> magnitudeSpectrum;
  std::vector<float> frequencyBins;
};

SpectrumData computeMagnitudeSpectrumFromFrequencyDomain(
    const std::vector<juce::dsp::Complex<float>>& frequencyDomain,
    float frameRateHz) {
  SpectrumData spectrum;
  if (frequencyDomain.empty() || frameRateHz <= 0.0f) {
    return spectrum;
  }

  const int fftSize = static_cast<int>(frequencyDomain.size());
  const int nonNegativeBinCount = fftSize / 2 + 1;
  spectrum.magnitudeSpectrum.resize(
      static_cast<size_t>(nonNegativeBinCount), 0.0f);
  spectrum.frequencyBins.resize(static_cast<size_t>(nonNegativeBinCount), 0.0f);

  for (int bin = 0; bin < nonNegativeBinCount; ++bin) {
    spectrum.frequencyBins[static_cast<size_t>(bin)] =
        (static_cast<float>(bin) * frameRateHz) / static_cast<float>(fftSize);
    spectrum.magnitudeSpectrum[static_cast<size_t>(bin)] =
        std::abs(frequencyDomain[static_cast<size_t>(bin)]);
  }

  return spectrum;
}

SpectrumData computeMagnitudeSpectrum(const std::vector<float>& signal,
                                      float frameRateHz) {
  SpectrumData spectrum;
  if (signal.empty() || frameRateHz <= 0.0f) {
    return spectrum;
  }

  const int inputSize = static_cast<int>(signal.size());
  const int fftSize = nextPowerOfTwoInternal(inputSize);

  int fftOrder = 0;
  while ((1 << fftOrder) < fftSize) {
    ++fftOrder;
  }

  juce::dsp::FFT fft(fftOrder);
  std::vector<juce::dsp::Complex<float>> timeDomain(
      static_cast<size_t>(fftSize), juce::dsp::Complex<float>(0.0f, 0.0f));
  std::vector<juce::dsp::Complex<float>> frequencyDomain(
      static_cast<size_t>(fftSize), juce::dsp::Complex<float>(0.0f, 0.0f));

  for (int i = 0; i < inputSize; ++i) {
    timeDomain[static_cast<size_t>(i)] =
        juce::dsp::Complex<float>(signal[static_cast<size_t>(i)], 0.0f);
  }

  fft.perform(timeDomain.data(), frequencyDomain.data(), false);

  const int nonNegativeBinCount = fftSize / 2 + 1;
  spectrum.magnitudeSpectrum.resize(
      static_cast<size_t>(nonNegativeBinCount), 0.0f);
  spectrum.frequencyBins.resize(static_cast<size_t>(nonNegativeBinCount), 0.0f);

  for (int bin = 0; bin < nonNegativeBinCount; ++bin) {
    spectrum.frequencyBins[static_cast<size_t>(bin)] =
        (static_cast<float>(bin) * frameRateHz) /
        static_cast<float>(fftSize);
    spectrum.magnitudeSpectrum[static_cast<size_t>(bin)] =
        std::abs(frequencyDomain[static_cast<size_t>(bin)]);
  }

  return spectrum;
}

std::vector<float> sliceCurve(const std::vector<float>& input,
                              int startFrame,
                              int frameCount) {
  if (input.empty() || frameCount <= 0) {
    return {};
  }

  const int clampedStart =
      juce::jlimit(0, static_cast<int>(input.size()), startFrame);
  const int clampedCount =
      juce::jlimit(0, static_cast<int>(input.size()) - clampedStart,
                   frameCount);
  return std::vector<float>(input.begin() + clampedStart,
                            input.begin() + clampedStart + clampedCount);
}

float smoothHighpassGain(float frequencyHz,
                         float cutoffHz,
                         float transitionHz) {
  if (cutoffHz <= 0.0f) {
    return 1.0f;
  }

  const float startHz = juce::jmax(0.0f, cutoffHz - transitionHz * 0.5f);
  const float endHz = cutoffHz + transitionHz * 0.5f;
  if (frequencyHz <= startHz) {
    return 0.0f;
  }
  if (frequencyHz >= endHz || transitionHz <= 0.0f) {
    return 1.0f;
  }

  const float t = (frequencyHz - startHz) / transitionHz;
  return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
}

float smoothLowpassGain(float frequencyHz,
                        float cutoffHz,
                        float transitionHz,
                        float nyquistHz) {
  if (cutoffHz >= nyquistHz) {
    return 1.0f;
  }

  const float startHz = juce::jmax(0.0f, cutoffHz - transitionHz * 0.5f);
  const float endHz = juce::jmin(nyquistHz, cutoffHz + transitionHz * 0.5f);
  if (frequencyHz <= startHz) {
    return 1.0f;
  }
  if (frequencyHz >= endHz || transitionHz <= 0.0f) {
    return 0.0f;
  }

  const float t = (frequencyHz - startHz) / transitionHz;
  return 0.5f + 0.5f * std::cos(juce::MathConstants<float>::pi * t);
}

}  // namespace

int FourierPitchFilter::nextPowerOfTwo(int n) {
  return nextPowerOfTwoInternal(n);
}

float FourierPitchFilter::highpassStrengthToCutoffHz(float strength,
                                                     float frameRateHz) {
  if (frameRateHz <= 0.0f) {
    return 0.0f;
  }

  const float easedStrength = juce::jlimit(0.0f, 1.0f, strength);
  const float nyquistHz = frameRateHz * 0.5f;
  return nyquistHz * easedStrength * easedStrength;
}

float FourierPitchFilter::lowpassStrengthToCutoffHz(float strength,
                                                    float frameRateHz) {
  if (frameRateHz <= 0.0f) {
    return 0.0f;
  }

  const float nyquistHz = frameRateHz * 0.5f;
  const float easedStrength = juce::jlimit(0.0f, 1.0f, strength);
  const float remaining = 1.0f - easedStrength;
  return nyquistHz * remaining * remaining;
}

FourierPitchFilter::FilterResult
FourierPitchFilter::filterPitchCurve(const std::vector<float>& deltaPitch,
                                     float lowpassHz,
                                     float highpassHz,
                                     float frameRateHz,
                                     int cropStartFrame,
                                     int cropFrameCount) {
  FilterResult result;
  result.contextInputPitch = deltaPitch;
  result.frameRateHz = frameRateHz;

  if (deltaPitch.empty()) {
    return result;
  }

  const int inputSize = static_cast<int>(deltaPitch.size());
  result.cropStartFrame = juce::jlimit(0, inputSize, cropStartFrame);
  result.cropFrameCount =
      cropFrameCount < 0
          ? inputSize - result.cropStartFrame
          : juce::jlimit(0, inputSize - result.cropStartFrame, cropFrameCount);
  result.filteredPitch =
      sliceCurve(deltaPitch, result.cropStartFrame, result.cropFrameCount);

  result.zeroMeanInputPitch = removeDcComponent(deltaPitch, result.dcComponent);
  if (frameRateHz <= 0.0f) {
    result.zeroMeanFilteredPitch = result.zeroMeanInputPitch;
    result.contextFilteredPitch = deltaPitch;
    result.filteredPitch = sliceCurve(result.contextFilteredPitch,
                                      result.cropStartFrame,
                                      result.cropFrameCount);
    const auto spectrum =
        computeMagnitudeSpectrum(result.zeroMeanInputPitch, frameRateHz);
    result.magnitudeSpectrum = spectrum.magnitudeSpectrum;
    result.filteredMagnitudeSpectrum = spectrum.magnitudeSpectrum;
    result.frequencyBins = spectrum.frequencyBins;
    return result;
  }

  const int fftSize = nextPowerOfTwo(inputSize);

  int fftOrder = 0;
  while ((1 << fftOrder) < fftSize) {
    ++fftOrder;
  }

  juce::dsp::FFT fft(fftOrder);
  std::vector<juce::dsp::Complex<float>> timeDomain(
      static_cast<size_t>(fftSize), juce::dsp::Complex<float>(0.0f, 0.0f));
  std::vector<juce::dsp::Complex<float>> frequencyDomain(
      static_cast<size_t>(fftSize), juce::dsp::Complex<float>(0.0f, 0.0f));
  std::vector<juce::dsp::Complex<float>> filteredDomain(
      static_cast<size_t>(fftSize), juce::dsp::Complex<float>(0.0f, 0.0f));

  for (int i = 0; i < inputSize; ++i) {
    timeDomain[static_cast<size_t>(i)] =
        juce::dsp::Complex<float>(result.zeroMeanInputPitch[static_cast<size_t>(i)],
                                  0.0f);
  }

  fft.perform(timeDomain.data(), frequencyDomain.data(), false);

  const auto originalSpectrum =
      computeMagnitudeSpectrumFromFrequencyDomain(frequencyDomain, frameRateHz);
  result.magnitudeSpectrum = originalSpectrum.magnitudeSpectrum;
  result.frequencyBins = originalSpectrum.frequencyBins;

  const float nyquistHz = frameRateHz * 0.5f;
  float clampedLowpassHz = lowpassHz < 0.0f
                               ? nyquistHz
                               : juce::jlimit(0.0f, nyquistHz, lowpassHz);
  float clampedHighpassHz = juce::jlimit(0.0f, nyquistHz, highpassHz);
  result.lowpassHz = clampedLowpassHz;
  result.highpassHz = clampedHighpassHz;

  const float binResolutionHz =
      frameRateHz / static_cast<float>(fftSize);
  const float highpassTransitionHz =
      clampedHighpassHz > 0.0f
          ? std::max(binResolutionHz * 2.0f, clampedHighpassHz * 0.12f)
          : 0.0f;
  const float lowpassTransitionHz =
      clampedLowpassHz < nyquistHz
          ? std::max(binResolutionHz * 2.0f, clampedLowpassHz * 0.12f)
          : 0.0f;

  const int nonNegativeBinCount = fftSize / 2 + 1;
  for (int bin = 0; bin < nonNegativeBinCount; ++bin) {
    const float frequencyHz = (static_cast<float>(bin) * frameRateHz) /
                              static_cast<float>(fftSize);

    float gain = 1.0f;
    gain *= smoothHighpassGain(frequencyHz, clampedHighpassHz,
                               highpassTransitionHz);
    gain *= smoothLowpassGain(frequencyHz, clampedLowpassHz,
                              lowpassTransitionHz, nyquistHz);

    filteredDomain[static_cast<size_t>(bin)] =
        frequencyDomain[static_cast<size_t>(bin)] * gain;

    if (bin > 0 && bin < fftSize / 2) {
      filteredDomain[static_cast<size_t>(fftSize - bin)] =
          frequencyDomain[static_cast<size_t>(fftSize - bin)] * gain;
    }
  }

  if ((fftSize % 2) == 0) {
    const int nyquistBin = fftSize / 2;
    const float frequencyHz = (static_cast<float>(nyquistBin) * frameRateHz) /
                              static_cast<float>(fftSize);
    float gain = smoothHighpassGain(frequencyHz, clampedHighpassHz,
                                    highpassTransitionHz);
    gain *= smoothLowpassGain(frequencyHz, clampedLowpassHz,
                              lowpassTransitionHz, nyquistHz);
    filteredDomain[static_cast<size_t>(nyquistBin)] =
        frequencyDomain[static_cast<size_t>(nyquistBin)] * gain;
  }

  fft.perform(filteredDomain.data(), timeDomain.data(), true);

  result.zeroMeanFilteredPitch.assign(static_cast<size_t>(inputSize), 0.0f);
  for (int i = 0; i < inputSize; ++i) {
    result.zeroMeanFilteredPitch[static_cast<size_t>(i)] =
        timeDomain[static_cast<size_t>(i)].real();
  }
  result.contextFilteredPitch.resize(static_cast<size_t>(inputSize), 0.0f);
  for (int i = 0; i < inputSize; ++i) {
    result.contextFilteredPitch[static_cast<size_t>(i)] =
        result.zeroMeanFilteredPitch[static_cast<size_t>(i)] +
        result.dcComponent;
  }
  result.filteredPitch = sliceCurve(result.contextFilteredPitch,
                                    result.cropStartFrame,
                                    result.cropFrameCount);

  const auto filteredSpectrum =
      computeMagnitudeSpectrumFromFrequencyDomain(filteredDomain, frameRateHz);
  result.filteredMagnitudeSpectrum = filteredSpectrum.magnitudeSpectrum;
  if (result.frequencyBins.empty()) {
    result.frequencyBins = filteredSpectrum.frequencyBins;
  }

  return result;
}
