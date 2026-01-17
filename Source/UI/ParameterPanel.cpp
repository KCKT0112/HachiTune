#include "ParameterPanel.h"
#include "StyledComponents.h"
#include "../Utils/Localization.h"

ParameterPanel::ParameterPanel()
{
    // Note info
    addAndMakeVisible(noteInfoLabel);
    noteInfoLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    noteInfoLabel.setText(TR("param.no_selection"), juce::dontSendNotification);
    noteInfoLabel.setJustificationType(juce::Justification::centred);

    // Setup sliders
    setupSlider(pitchOffsetSlider, pitchOffsetLabel, TR("param.pitch_offset"), -24.0, 24.0, 0.0);

    // Volume knob setup
    addAndMakeVisible(volumeKnob);
    addAndMakeVisible(volumeValueLabel);
    volumeKnob.setRange(-12.0, 12.0, 0.1);  // Symmetric dB range, 0 in center
    volumeKnob.setValue(0.0);  // 0 dB = unity gain
    volumeKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volumeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeKnob.setDoubleClickReturnValue(true, 0.0);  // Double-click resets to 0 dB
    volumeKnob.addListener(this);
    volumeKnob.setLookAndFeel(&KnobLookAndFeel::getInstance());
    volumeValueLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    volumeValueLabel.setJustificationType(juce::Justification::centred);
    volumeValueLabel.setText("0.0 dB", juce::dontSendNotification);

    setupSlider(formantShiftSlider, formantShiftLabel, TR("param.formant_shift"), -12.0, 12.0, 0.0);
    setupSlider(globalPitchSlider, globalPitchLabel, TR("param.global_pitch"), -24.0, 24.0, 0.0);

    // Section labels
    pitchSectionLabel.setText(TR("param.pitch"), juce::dontSendNotification);
    volumeSectionLabel.setText(TR("param.volume"), juce::dontSendNotification);
    formantSectionLabel.setText(TR("param.formant"), juce::dontSendNotification);
    globalSectionLabel.setText(TR("param.global"), juce::dontSendNotification);

    for (auto* label : { &pitchSectionLabel, &volumeSectionLabel,
                         &formantSectionLabel, &globalSectionLabel })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, juce::Colour(COLOR_PRIMARY));
        label->setFont(juce::Font(14.0f, juce::Font::bold));
    }

    // Formant slider disabled (not implemented yet)
    formantShiftSlider.setEnabled(false);
    // Global pitch slider is now enabled!
    globalPitchSlider.setEnabled(true);
}

ParameterPanel::~ParameterPanel()
{
    volumeKnob.setLookAndFeel(nullptr);
}

void ParameterPanel::setupSlider(juce::Slider& slider, juce::Label& label,
                                  const juce::String& name, double min, double max, double def)
{
    addAndMakeVisible(slider);
    addAndMakeVisible(label);

    slider.setRange(min, max, 0.01);
    slider.setValue(def);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 22);
    slider.addListener(this);

    // Slider track colors - darker background for better contrast
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF1A1A22));
    slider.setColour(juce::Slider::trackColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.6f));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(COLOR_PRIMARY));

    // Text box colors - match dark theme with subtle border
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF252530));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF3D3D47));
    slider.setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(COLOR_PRIMARY).withAlpha(0.3f));

    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    // Don't fill background - let parent DraggablePanel handle it
    juce::ignoreUnused(g);
}

void ParameterPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Note info
    noteInfoLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    // Pitch section
    pitchSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    pitchOffsetLabel.setBounds(bounds.removeFromTop(20));
    pitchOffsetSlider.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(15);

    // Volume section with knob
    volumeSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    auto volumeArea = bounds.removeFromTop(70);  // Larger area for knob
    const int knobSize = 60;
    volumeKnob.setBounds(volumeArea.getX() + (volumeArea.getWidth() - knobSize) / 2,
                         volumeArea.getY(), knobSize, knobSize);
    volumeValueLabel.setBounds(volumeArea.getX(), volumeArea.getY() + knobSize + 2,
                               volumeArea.getWidth(), 16);
    bounds.removeFromTop(10);

    // Formant section
    formantSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    formantShiftLabel.setBounds(bounds.removeFromTop(20));
    formantShiftSlider.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(30);

    // Global section
    globalSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(5);
    globalPitchLabel.setBounds(bounds.removeFromTop(20));
    globalPitchSlider.setBounds(bounds.removeFromTop(24));
}

void ParameterPanel::sliderValueChanged(juce::Slider* slider)
{
    if (isUpdating) return;

    if (slider == &pitchOffsetSlider && selectedNote)
    {
        selectedNote->setPitchOffset(static_cast<float>(slider->getValue()));
        selectedNote->markDirty();  // Mark as dirty for incremental synthesis

        if (onParameterChanged)
            onParameterChanged();
    }
    else if (slider == &globalPitchSlider && project)
    {
        project->setGlobalPitchOffset(static_cast<float>(slider->getValue()));

        // Mark all notes as dirty for full resynthesis
        for (auto& note : project->getNotes())
            note.markDirty();

        if (onGlobalPitchChanged)
            onGlobalPitchChanged();
    }
    else if (slider == &volumeKnob)
    {
        // Update display
        float dB = static_cast<float>(slider->getValue());
        volumeValueLabel.setText(juce::String(dB, 1) + " dB", juce::dontSendNotification);

        // Notify listener
        if (onVolumeChanged)
            onVolumeChanged(dB);
    }
}

void ParameterPanel::sliderDragEnded(juce::Slider* slider)
{
    if (slider == &pitchOffsetSlider && selectedNote)
    {
        // Trigger incremental synthesis when slider drag ends
        if (onParameterEditFinished)
            onParameterEditFinished();
    }
    else if (slider == &globalPitchSlider && project)
    {
        // Global pitch changed, need full resynthesis
        if (onParameterEditFinished)
            onParameterEditFinished();
    }
}

void ParameterPanel::buttonClicked(juce::Button* button)
{
    juce::ignoreUnused(button);
}

void ParameterPanel::setProject(Project* proj)
{
    project = proj;
    updateGlobalSliders();
}

void ParameterPanel::setSelectedNote(Note* note)
{
    selectedNote = note;
    updateFromNote();
}

void ParameterPanel::updateFromNote()
{
    isUpdating = true;

    if (selectedNote)
    {
        float midi = selectedNote->getAdjustedMidiNote();
        int octave = static_cast<int>(midi / 12) - 1;
        int noteIndex = static_cast<int>(midi) % 12;
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };

        juce::String noteInfo = juce::String(noteNames[noteIndex]) +
                                juce::String(octave) +
                                " (" + juce::String(midi, 1) + ")";
        noteInfoLabel.setText(noteInfo, juce::dontSendNotification);

        pitchOffsetSlider.setValue(selectedNote->getPitchOffset());
        pitchOffsetSlider.setEnabled(true);
    }
    else
    {
        noteInfoLabel.setText("No note selected", juce::dontSendNotification);
        pitchOffsetSlider.setValue(0.0);
        pitchOffsetSlider.setEnabled(false);
    }

    isUpdating = false;
}

void ParameterPanel::updateGlobalSliders()
{
    isUpdating = true;

    if (project)
    {
        globalPitchSlider.setValue(project->getGlobalPitchOffset());
        globalPitchSlider.setEnabled(true);
    }
    else
    {
        globalPitchSlider.setValue(0.0);
        globalPitchSlider.setEnabled(false);
    }

    isUpdating = false;
}
