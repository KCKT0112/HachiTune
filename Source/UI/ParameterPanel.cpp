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
        label->setColour(juce::Label::textColourId, juce::Colour(APP_COLOR_PRIMARY));
        label->setFont(juce::Font(14.0f, juce::Font::bold));
    }

    // Formant slider disabled (not implemented yet)
    formantShiftSlider.setEnabled(false);
    // Global pitch slider is now enabled!
    globalPitchSlider.setEnabled(true);

    // Setup Kiwi constraint layout
    setupConstraints();
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
    slider.setColour(juce::Slider::trackColourId, juce::Colour(APP_COLOR_PRIMARY).withAlpha(0.6f));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(APP_COLOR_PRIMARY));

    // Text box colors - match dark theme with subtle border
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF252530));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF3D3D47));
    slider.setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(APP_COLOR_PRIMARY).withAlpha(0.3f));

    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

void ParameterPanel::setupConstraints()
{
    // Create variables for container (this component)
    containerVars = layout.createComponentVars("container");

    // Create variables for all sections
    noteInfoVars = layout.createComponentVars("noteInfo");
    pitchSectionVars = layout.createComponentVars("pitchSection");
    pitchLabelVars = layout.createComponentVars("pitchLabel");
    pitchSliderVars = layout.createComponentVars("pitchSlider");
    volumeSectionVars = layout.createComponentVars("volumeSection");
    volumeKnobVars = layout.createComponentVars("volumeKnob");
    volumeValueVars = layout.createComponentVars("volumeValue");
    formantSectionVars = layout.createComponentVars("formantSection");
    formantLabelVars = layout.createComponentVars("formantLabel");
    formantSliderVars = layout.createComponentVars("formantSlider");
    globalSectionVars = layout.createComponentVars("globalSection");
    globalLabelVars = layout.createComponentVars("globalLabel");
    globalSliderVars = layout.createComponentVars("globalSlider");

    // Container constraints (will be updated in resized())
    containerWidthConstraint = containerVars.width == 280.0;
    containerHeightConstraint = containerVars.height == 500.0;
    layout.addConstraint(containerVars.left == 0.0);
    layout.addConstraint(containerVars.top == 0.0);
    layout.addConstraint(containerWidthConstraint);
    layout.addConstraint(containerHeightConstraint);

    const double margin = 10.0;
    const double sectionHeight = 20.0;
    const double labelHeight = 20.0;
    const double sliderHeight = 24.0;
    const double knobSize = 60.0;
    const double valueLabelHeight = 16.0;

    // Note info (30px height, 10px gap below)
    layout.addConstraint(noteInfoVars.left == containerVars.left + margin);
    layout.addConstraint(noteInfoVars.right == containerVars.right - margin);
    layout.addConstraint(noteInfoVars.top == containerVars.top + margin);
    layout.addConstraint(noteInfoVars.height == 30.0);

    // Pitch section (20px height, 5px gap below)
    layout.addConstraint(pitchSectionVars.left == containerVars.left + margin);
    layout.addConstraint(pitchSectionVars.right == containerVars.right - margin);
    layout.addConstraint(pitchSectionVars.top == noteInfoVars.bottom + 10.0);
    layout.addConstraint(pitchSectionVars.height == sectionHeight);

    // Pitch label
    layout.addConstraint(pitchLabelVars.left == containerVars.left + margin);
    layout.addConstraint(pitchLabelVars.right == containerVars.right - margin);
    layout.addConstraint(pitchLabelVars.top == pitchSectionVars.bottom + 5.0);
    layout.addConstraint(pitchLabelVars.height == labelHeight);

    // Pitch slider
    layout.addConstraint(pitchSliderVars.left == containerVars.left + margin);
    layout.addConstraint(pitchSliderVars.right == containerVars.right - margin);
    layout.addConstraint(pitchSliderVars.top == pitchLabelVars.bottom);
    layout.addConstraint(pitchSliderVars.height == sliderHeight);

    // Volume section
    layout.addConstraint(volumeSectionVars.left == containerVars.left + margin);
    layout.addConstraint(volumeSectionVars.right == containerVars.right - margin);
    layout.addConstraint(volumeSectionVars.top == pitchSliderVars.bottom + 15.0);
    layout.addConstraint(volumeSectionVars.height == sectionHeight);

    // Volume knob (centered horizontally)
    layout.addConstraint(volumeKnobVars.width == knobSize);
    layout.addConstraint(volumeKnobVars.height == knobSize);
    layout.addConstraint(volumeKnobVars.centerX == containerVars.centerX);
    layout.addConstraint(volumeKnobVars.top == volumeSectionVars.bottom + 5.0);

    // Volume value label (below knob)
    layout.addConstraint(volumeValueVars.left == containerVars.left + margin);
    layout.addConstraint(volumeValueVars.right == containerVars.right - margin);
    layout.addConstraint(volumeValueVars.top == volumeKnobVars.bottom + 2.0);
    layout.addConstraint(volumeValueVars.height == valueLabelHeight);

    // Formant section
    layout.addConstraint(formantSectionVars.left == containerVars.left + margin);
    layout.addConstraint(formantSectionVars.right == containerVars.right - margin);
    layout.addConstraint(formantSectionVars.top == volumeValueVars.bottom + 10.0);
    layout.addConstraint(formantSectionVars.height == sectionHeight);

    // Formant label
    layout.addConstraint(formantLabelVars.left == containerVars.left + margin);
    layout.addConstraint(formantLabelVars.right == containerVars.right - margin);
    layout.addConstraint(formantLabelVars.top == formantSectionVars.bottom + 5.0);
    layout.addConstraint(formantLabelVars.height == labelHeight);

    // Formant slider
    layout.addConstraint(formantSliderVars.left == containerVars.left + margin);
    layout.addConstraint(formantSliderVars.right == containerVars.right - margin);
    layout.addConstraint(formantSliderVars.top == formantLabelVars.bottom);
    layout.addConstraint(formantSliderVars.height == sliderHeight);

    // Global section
    layout.addConstraint(globalSectionVars.left == containerVars.left + margin);
    layout.addConstraint(globalSectionVars.right == containerVars.right - margin);
    layout.addConstraint(globalSectionVars.top == formantSliderVars.bottom + 30.0);
    layout.addConstraint(globalSectionVars.height == sectionHeight);

    // Global label
    layout.addConstraint(globalLabelVars.left == containerVars.left + margin);
    layout.addConstraint(globalLabelVars.right == containerVars.right - margin);
    layout.addConstraint(globalLabelVars.top == globalSectionVars.bottom + 5.0);
    layout.addConstraint(globalLabelVars.height == labelHeight);

    // Global slider
    layout.addConstraint(globalSliderVars.left == containerVars.left + margin);
    layout.addConstraint(globalSliderVars.right == containerVars.right - margin);
    layout.addConstraint(globalSliderVars.top == globalLabelVars.bottom);
    layout.addConstraint(globalSliderVars.height == sliderHeight);
}

void ParameterPanel::updateLayoutConstraints()
{
    // Remove old size constraints
    layout.removeConstraint(containerWidthConstraint);
    layout.removeConstraint(containerHeightConstraint);

    // Create new size constraints with current dimensions
    containerWidthConstraint = containerVars.width == static_cast<double>(getWidth());
    containerHeightConstraint = containerVars.height == static_cast<double>(getHeight());

    // Add new constraints
    layout.addConstraint(containerWidthConstraint);
    layout.addConstraint(containerHeightConstraint);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    // Don't fill background - let parent DraggablePanel handle it
    juce::ignoreUnused(g);
}

void ParameterPanel::resized()
{
    // Update container size constraints
    updateLayoutConstraints();

    // Apply layout to all components
    layout.applyComponentVars(&noteInfoLabel, noteInfoVars);
    layout.applyComponentVars(&pitchSectionLabel, pitchSectionVars);
    layout.applyComponentVars(&pitchOffsetLabel, pitchLabelVars);
    layout.applyComponentVars(&pitchOffsetSlider, pitchSliderVars);
    layout.applyComponentVars(&volumeSectionLabel, volumeSectionVars);
    layout.applyComponentVars(&volumeKnob, volumeKnobVars);
    layout.applyComponentVars(&volumeValueLabel, volumeValueVars);
    layout.applyComponentVars(&formantSectionLabel, formantSectionVars);
    layout.applyComponentVars(&formantShiftLabel, formantLabelVars);
    layout.applyComponentVars(&formantShiftSlider, formantSliderVars);
    layout.applyComponentVars(&globalSectionLabel, globalSectionVars);
    layout.applyComponentVars(&globalPitchLabel, globalLabelVars);
    layout.applyComponentVars(&globalPitchSlider, globalSliderVars);
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
        noteInfoLabel.setText(TR("param.no_selection"), juce::dontSendNotification);
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
