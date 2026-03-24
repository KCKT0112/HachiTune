#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/FourierPitchFilter.h"

class PitchFilterDebugComponent : public juce::Component {
public:
  PitchFilterDebugComponent() = default;
  ~PitchFilterDebugComponent() override = default;

  void paint(juce::Graphics& g) override;
  void resized() override;

  void setDebugData(const juce::String& noteDescription,
                    std::vector<float> originalCurve,
                    FourierPitchFilter::FilterResult result);
  void clearData();

private:
  juce::String noteDescription;
  std::vector<float> originalCurve;
  FourierPitchFilter::FilterResult filterResult;
  bool hasData = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchFilterDebugComponent)
};

class PitchFilterDebugWindow : public juce::DocumentWindow {
public:
  PitchFilterDebugWindow();
  ~PitchFilterDebugWindow() override = default;

  void closeButtonPressed() override;
  void present();
  void showDebugData(const juce::String& noteDescription,
                     std::vector<float> originalCurve,
                     FourierPitchFilter::FilterResult result);
  void clearData();

private:
  PitchFilterDebugComponent* debugComponent = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchFilterDebugWindow)
};
