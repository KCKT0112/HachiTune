#include "ToolbarComponent.h"
#include "PianoRollComponent.h"  // For EditMode enum
#include "StyledComponents.h"
#include "../Utils/Localization.h"
#include "../Utils/SvgUtils.h"
#include "BinaryData.h"

ToolbarComponent::ToolbarComponent()
{
    // Load SVG icons with white tint
    auto playIcon = SvgUtils::loadSvg(BinaryData::playline_svg, BinaryData::playline_svgSize, juce::Colours::white);
    auto pauseIcon = SvgUtils::loadSvg(BinaryData::pauseline_svg, BinaryData::pauseline_svgSize, juce::Colours::white);
    auto stopIcon = SvgUtils::loadSvg(BinaryData::stopline_svg, BinaryData::stopline_svgSize, juce::Colours::white);
    auto startIcon = SvgUtils::loadSvg(BinaryData::movestartline_svg, BinaryData::movestartline_svgSize, juce::Colours::white);
    auto endIcon = SvgUtils::loadSvg(BinaryData::moveendline_svg, BinaryData::moveendline_svgSize, juce::Colours::white);
    auto cursorIcon = SvgUtils::loadSvg(BinaryData::cursor_24_filled_svg, BinaryData::cursor_24_filled_svgSize, juce::Colours::white);
    auto stretchIcon = SvgUtils::loadSvg(BinaryData::stretch_24_filled_svg, BinaryData::stretch_24_filled_svgSize, juce::Colours::white);
    auto pitchEditIcon = SvgUtils::loadSvg(BinaryData::pitch_edit_24_filled_svg, BinaryData::pitch_edit_24_filled_svgSize, juce::Colours::white);
    auto scissorsIcon = SvgUtils::loadSvg(BinaryData::scissors_24_filled_svg, BinaryData::scissors_24_filled_svgSize, juce::Colours::white);
    auto followIcon = SvgUtils::loadSvg(BinaryData::follow24filled_svg, BinaryData::follow24filled_svgSize, juce::Colours::white);
    auto loopIcon = SvgUtils::loadSvg(BinaryData::loop24filled_svg, BinaryData::loop24filled_svgSize, juce::Colours::white);
    const juce::String parametersIconSvg =
        R"(<svg viewBox="0 0 24 24" fill="currentColor" xmlns="http://www.w3.org/2000/svg"><rect x="3" y="2" width="2" height="20" rx="1"/><circle cx="4" cy="9" r="3"/><rect x="11" y="2" width="2" height="20" rx="1"/><circle cx="12" cy="15" r="3"/><rect x="19" y="2" width="2" height="20" rx="1"/><circle cx="20" cy="6" r="3"/></svg>)";
    auto parametersIcon = SvgUtils::createDrawableFromSvg(parametersIconSvg, juce::Colours::white);

    playButton.setImages(playIcon.get());
    stopButton.setImages(stopIcon.get());
    goToStartButton.setImages(startIcon.get());
    goToEndButton.setImages(endIcon.get());
    selectModeButton.setImages(cursorIcon.get());
    stretchModeButton.setImages(stretchIcon.get());
    drawModeButton.setImages(pitchEditIcon.get());
    splitModeButton.setImages(scissorsIcon.get());
    followButton.setImages(followIcon.get());
    loopButton.setImages(loopIcon.get());
    parametersButton.setImages(parametersIcon.get());

    // Set edge indent for icon padding (makes icons smaller within button bounds)
    goToStartButton.setEdgeIndent(4);
    playButton.setEdgeIndent(6);
    stopButton.setEdgeIndent(6);
    goToEndButton.setEdgeIndent(4);
    selectModeButton.setEdgeIndent(6);
    stretchModeButton.setEdgeIndent(6);
    drawModeButton.setEdgeIndent(6);
    splitModeButton.setEdgeIndent(6);
    followButton.setEdgeIndent(6);
    loopButton.setEdgeIndent(6);
    parametersButton.setEdgeIndent(6);

    // Store pause icon for later use
    pauseDrawable = std::move(pauseIcon);
    playDrawable = SvgUtils::loadSvg(BinaryData::playline_svg, BinaryData::playline_svgSize, juce::Colours::white);

    // Configure buttons
    addAndMakeVisible(goToStartButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(goToEndButton);
    addAndMakeVisible(selectModeButton);
    addAndMakeVisible(stretchModeButton);
    addAndMakeVisible(drawModeButton);
    addAndMakeVisible(splitModeButton);
    addAndMakeVisible(followButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(parametersButton);

    // Plugin mode buttons (hidden by default)
    addChildComponent(reanalyzeButton);
    addChildComponent(araModeLabel);

    // ARA mode label style (background drawn in paint() for rounded corners)
    araModeLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    araModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    araModeLabel.setJustificationType(juce::Justification::centred);
    araModeLabel.setFont(juce::Font(11.0f, juce::Font::bold));

    goToStartButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    goToEndButton.addListener(this);
    selectModeButton.addListener(this);
    stretchModeButton.addListener(this);
    drawModeButton.addListener(this);
    splitModeButton.addListener(this);
    followButton.addListener(this);
    loopButton.addListener(this);
    parametersButton.addListener(this);
    reanalyzeButton.addListener(this);

    // Set localized text (tooltips for icon buttons)
    selectModeButton.setTooltip(TR("toolbar.select"));
    stretchModeButton.setTooltip(TR("toolbar.stretch"));
    drawModeButton.setTooltip(TR("toolbar.draw"));
    splitModeButton.setTooltip(TR("toolbar.split"));
    followButton.setTooltip(TR("toolbar.follow"));
    loopButton.setTooltip(TR("toolbar.loop"));
    parametersButton.setTooltip(TR("panel.parameters"));
    reanalyzeButton.setButtonText(TR("toolbar.reanalyze"));
    zoomLabel.setText(TR("toolbar.zoom"), juce::dontSendNotification);

    // Style reanalyze button
    reanalyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3D3D47));
    reanalyzeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    // Set default active states
    selectModeButton.setActive(true);
    followButton.setActive(true);  // Follow is on by default
    loopButton.setActive(false);
    parametersButton.setActive(false);

    // Time label with app font (larger and bold for readability)
    addAndMakeVisible(timeLabel);
    timeLabel.setText("00:00.000 / 00:00.000", juce::dontSendNotification);
    timeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFCCCCCC));
    timeLabel.setJustificationType(juce::Justification::centred);
    timeLabel.setFont(AppFont::getBoldFont(20.0f));

    // Zoom slider
    addAndMakeVisible(zoomLabel);
    addAndMakeVisible(zoomSlider);

    zoomLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    zoomSlider.setRange(MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND, 1.0);
    zoomSlider.setValue(100.0);
    zoomSlider.setSkewFactorFromMidPoint(200.0);
    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.addListener(this);

    zoomSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2D2D37));
    zoomSlider.setColour(juce::Slider::trackColourId, juce::Colour(APP_COLOR_PRIMARY).withAlpha(0.6f));
    zoomSlider.setColour(juce::Slider::thumbColourId, juce::Colour(APP_COLOR_PRIMARY));

    // Progress bar (hidden by default)
    addChildComponent(progressBar);
    addChildComponent(progressLabel);

    progressLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    progressLabel.setJustificationType(juce::Justification::centredLeft);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(APP_COLOR_PRIMARY));
    progressBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xFF2D2D37));
    progressBar.setLookAndFeel(&DarkLookAndFeel::getInstance());
    
    // Status label (hidden by default)
    addChildComponent(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(11.0f));

    // Setup Kiwi constraint layout
    setupConstraints();
}

void ToolbarComponent::setupConstraints()
{
    // Create variables for container (this component)
    containerVars = layout.createComponentVars("container");

    // Create variables for main groups
    playbackGroupVars = layout.createComponentVars("playbackGroup");
    toolContainerGroupVars = layout.createComponentVars("toolContainer");
    timeLabelVars = layout.createComponentVars("timeLabel");
    rightAreaVars = layout.createComponentVars("rightArea");
    parametersButtonVars = layout.createComponentVars("parametersButton");

    // Container constraints (will be updated in resized())
    containerWidthConstraint = containerVars.width == 800.0;
    containerHeightConstraint = containerVars.height == 52.0;
    layout.addConstraint(containerVars.left == 0.0);
    layout.addConstraint(containerVars.top == 0.0);
    layout.addConstraint(containerWidthConstraint);
    layout.addConstraint(containerHeightConstraint);

    // Playback group (left side of center section)
    // Width: 120px for standalone (4 buttons * 28px + gaps), 200px for plugin mode
    layout.addConstraint(playbackGroupVars.width == 120.0);  // Default to standalone
    layout.addConstraint(playbackGroupVars.height == containerVars.height - 8.0);
    layout.addConstraint(playbackGroupVars.top == containerVars.top + 4.0);

    // Tool container (center of center section)
    // Width: 32px * 6 buttons + 8px padding = 200px for standalone, 136px for plugin (4 buttons)
    layout.addConstraint(toolContainerGroupVars.width == 200.0);  // Default to standalone (6 buttons)
    layout.addConstraint(toolContainerGroupVars.height == containerVars.height - 8.0);
    layout.addConstraint(toolContainerGroupVars.top == containerVars.top + 4.0);
    layout.addConstraint(toolContainerGroupVars.left == playbackGroupVars.right + 16.0);

    // Time label (right side of center section)
    layout.addConstraint(timeLabelVars.width == 160.0);
    layout.addConstraint(timeLabelVars.height == containerVars.height - 8.0);
    layout.addConstraint(timeLabelVars.top == containerVars.top + 4.0);
    layout.addConstraint(timeLabelVars.left == toolContainerGroupVars.right + 16.0);

    // Center the entire center section (playback + tools + time)
    // The center point of the time label should be at the center of the container
    layout.addConstraint((playbackGroupVars.left + timeLabelVars.right) / 2.0 == containerVars.centerX);

    // Right area (status/progress)
    layout.addConstraint(rightAreaVars.width == 200.0);
    layout.addConstraint(rightAreaVars.height == containerVars.height - 8.0);
    layout.addConstraint(rightAreaVars.top == containerVars.top + 4.0);
    layout.addConstraint(rightAreaVars.right == parametersButtonVars.left - 10.0);

    // Parameters button (far right)
    layout.addConstraint(parametersButtonVars.width == 28.0);
    layout.addConstraint(parametersButtonVars.height == 28.0);
    layout.addConstraint(parametersButtonVars.right == containerVars.right - 18.0);
    layout.addConstraint(parametersButtonVars.centerY == containerVars.centerY);
}

void ToolbarComponent::updateLayoutConstraints()
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

    // Update playback group width based on mode
    // Note: We need to remove and re-add width constraints when mode changes
    // For now, we'll handle this in setPluginMode()
}

ToolbarComponent::~ToolbarComponent()
{
    progressBar.setLookAndFeel(nullptr);
}

void ToolbarComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A24));

    // Draw rounded background for tool buttons container
    if (!toolContainerBounds.isEmpty())
    {
        g.setColour(juce::Colour(0xFF2D2D37));
        g.fillRoundedRectangle(toolContainerBounds.toFloat(), 6.0f);
    }

    // Draw rounded background for time label
    if (timeLabel.isVisible())
    {
        g.setColour(juce::Colour(0xFF2D2D37));
        g.fillRoundedRectangle(timeLabel.getBounds().toFloat(), 6.0f);
    }

    // Draw rounded background for ARA mode label
    if (pluginMode && araModeLabel.isVisible())
    {
        g.setColour(juce::Colour(APP_COLOR_PRIMARY));
        g.fillRoundedRectangle(araModeLabel.getBounds().toFloat(), 8.0f);
    }
}

void ToolbarComponent::resized()
{
    // Update container size constraints
    updateLayoutConstraints();

    // Apply layout to get group bounds
    layout.applyComponentVars(&timeLabel, timeLabelVars);
    layout.applyComponentVars(&parametersButton, parametersButtonVars);

    // Get bounds from solved variables for manual button positioning
    auto playbackBounds = juce::Rectangle<int>(
        static_cast<int>(playbackGroupVars.left.value()),
        static_cast<int>(playbackGroupVars.top.value()),
        static_cast<int>(playbackGroupVars.width.value()),
        static_cast<int>(playbackGroupVars.height.value())
    );

    auto toolContainerBounds = juce::Rectangle<int>(
        static_cast<int>(toolContainerGroupVars.left.value()),
        static_cast<int>(toolContainerGroupVars.top.value()),
        static_cast<int>(toolContainerGroupVars.width.value()),
        static_cast<int>(toolContainerGroupVars.height.value())
    );

    auto rightBounds = juce::Rectangle<int>(
        static_cast<int>(rightAreaVars.left.value()),
        static_cast<int>(rightAreaVars.top.value()),
        static_cast<int>(rightAreaVars.width.value()),
        static_cast<int>(rightAreaVars.height.value())
    );

    // Position playback controls or plugin mode buttons
    int currentX = playbackBounds.getX();
    if (pluginMode)
    {
        araModeLabel.setBounds(currentX, playbackBounds.getY(), 90, playbackBounds.getHeight());
        currentX += 98;
        reanalyzeButton.setBounds(currentX, playbackBounds.getY(), 100, playbackBounds.getHeight());
    }
    else
    {
        goToStartButton.setBounds(currentX, playbackBounds.getY() + 2, 28, playbackBounds.getHeight() - 4);
        currentX += 32;
        playButton.setBounds(currentX, playbackBounds.getY() + 2, 28, playbackBounds.getHeight() - 4);
        currentX += 32;
        stopButton.setBounds(currentX, playbackBounds.getY() + 2, 28, playbackBounds.getHeight() - 4);
        currentX += 32;
        goToEndButton.setBounds(currentX, playbackBounds.getY() + 2, 28, playbackBounds.getHeight() - 4);
    }

    // Position tool buttons within container
    this->toolContainerBounds = toolContainerBounds;  // Store for painting
    const int toolButtonSize = 32;
    const int toolContainerPadding = 4;
    auto toolArea = toolContainerBounds.reduced(toolContainerPadding, toolContainerPadding);
    int toolX = toolArea.getX();

    selectModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    stretchModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    drawModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;
    splitModeButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    toolX += toolButtonSize;

    if (!pluginMode)
    {
        followButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
        toolX += toolButtonSize;
        loopButton.setBounds(toolX, toolArea.getY(), toolButtonSize, toolArea.getHeight());
    }

    // Position status/progress in right area
    if (showingStatus && !showingProgress)
    {
        statusLabel.setBounds(rightBounds.removeFromLeft(120));
    }
    if (showingProgress)
    {
        auto progressArea = rightBounds.withWidth(std::min(180, rightBounds.getWidth()));
        const int progressBarHeight = progressArea.getHeight() / 2;
        progressLabel.setBounds(progressArea.removeFromTop(progressArea.getHeight() - progressBarHeight));
        progressBar.setBounds(progressArea.withHeight(progressBarHeight));
    }

    // Hide zoom controls (not used in current layout)
    zoomLabel.setVisible(false);
    zoomSlider.setVisible(false);
}

void ToolbarComponent::buttonClicked(juce::Button* button)
{
    if (button == &goToStartButton && onGoToStart)
        onGoToStart();
    else if (button == &goToEndButton && onGoToEnd)
        onGoToEnd();
    else if (button == &playButton)
    {
        if (isPlaying)
        {
            if (onPause)
                onPause();
        }
        else
        {
            if (onPlay)
                onPlay();
        }
    }
    else if (button == &stopButton && onStop)
        onStop();
    else if (button == &reanalyzeButton && onReanalyze)
        onReanalyze();
    else if (button == &selectModeButton)
    {
        setEditMode(EditMode::Select);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Select);
    }
    else if (button == &stretchModeButton)
    {
        setEditMode(EditMode::Stretch);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Stretch);
    }
    else if (button == &drawModeButton)
    {
        setEditMode(EditMode::Draw);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Draw);
    }
    else if (button == &splitModeButton)
    {
        setEditMode(EditMode::Split);
        if (onEditModeChanged)
            onEditModeChanged(EditMode::Split);
    }
    else if (button == &followButton)
    {
        followPlayback = !followPlayback;
        followButton.setActive(followPlayback);
    }
    else if (button == &loopButton)
    {
        loopEnabled = !loopEnabled;
        loopButton.setActive(loopEnabled);
        if (onLoopToggled)
            onLoopToggled(loopEnabled);
    }
    else if (button == &parametersButton)
    {
        parametersVisible = !parametersVisible;
        parametersButton.setActive(parametersVisible);
        if (onToggleParameters)
            onToggleParameters(parametersVisible);
    }
}

void ToolbarComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &zoomSlider && onZoomChanged)
        onZoomChanged(static_cast<float>(slider->getValue()));
}

void ToolbarComponent::setPlaying(bool playing)
{
    isPlaying = playing;
    playButton.setImages(playing ? pauseDrawable.get() : playDrawable.get());
}

void ToolbarComponent::setCurrentTime(double time)
{
    currentTime = time;
    updateTimeDisplay();
}

void ToolbarComponent::setTotalTime(double time)
{
    totalTime = time;
    updateTimeDisplay();
}

void ToolbarComponent::setEditMode(EditMode mode)
{
    currentEditModeInt = static_cast<int>(mode);
    selectModeButton.setActive(mode == EditMode::Select);
    stretchModeButton.setActive(mode == EditMode::Stretch);
    drawModeButton.setActive(mode == EditMode::Draw);
    splitModeButton.setActive(mode == EditMode::Split);
}

void ToolbarComponent::setZoom(float pixelsPerSecond)
{
    // Update slider without triggering callback
    zoomSlider.setValue(pixelsPerSecond, juce::dontSendNotification);
}

void ToolbarComponent::setLoopEnabled(bool enabled)
{
    loopEnabled = enabled;
    loopButton.setActive(loopEnabled);
}

void ToolbarComponent::setParametersVisible(bool visible)
{
    parametersVisible = visible;
    parametersButton.setActive(parametersVisible);
}

void ToolbarComponent::showProgress(const juce::String& message)
{
    showingProgress = true;
    progressLabel.setText(message, juce::dontSendNotification);
    progressLabel.setVisible(true);
    progressBar.setVisible(true);
    progressValue = -1.0;  // Indeterminate
    resized();
    repaint();
}

void ToolbarComponent::hideProgress()
{
    showingProgress = false;
    progressLabel.setVisible(false);
    progressBar.setVisible(false);
    resized();
    repaint();
}

void ToolbarComponent::setProgress(float progress)
{
    if (progress < 0)
        progressValue = -1.0;  // Indeterminate
    else
        progressValue = static_cast<double>(juce::jlimit(0.0f, 1.0f, progress));
}

void ToolbarComponent::setStatusMessage(const juce::String& message)
{
    if (message.isEmpty())
    {
        showingStatus = false;
        statusLabel.setVisible(false);
    }
    else
    {
        showingStatus = true;
        statusLabel.setText(message, juce::dontSendNotification);
        statusLabel.setVisible(true);
    }
    resized();
    repaint();
}

void ToolbarComponent::updateTimeDisplay()
{
    timeLabel.setText(formatTime(currentTime) + " / " + formatTime(totalTime),
                      juce::dontSendNotification);
}

juce::String ToolbarComponent::formatTime(double seconds)
{
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);

    return juce::String::formatted("%02d:%02d.%03d", mins, secs, ms);
}

void ToolbarComponent::mouseDown(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (auto* window = getTopLevelComponent())
        dragger.startDraggingComponent(window, e.getEventRelativeTo(window));
#else
    juce::ignoreUnused(e);
#endif
}

void ToolbarComponent::mouseDrag(const juce::MouseEvent& e)
{
#if JUCE_MAC
    if (auto* window = getTopLevelComponent())
        dragger.dragComponent(window, e.getEventRelativeTo(window), nullptr);
#else
    juce::ignoreUnused(e);
#endif
}

void ToolbarComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
}

void ToolbarComponent::setPluginMode(bool isPlugin)
{
    pluginMode = isPlugin;

    goToStartButton.setVisible(!isPlugin);
    playButton.setVisible(!isPlugin);
    stopButton.setVisible(!isPlugin);
    goToEndButton.setVisible(!isPlugin);
    reanalyzeButton.setVisible(isPlugin);
    araModeLabel.setVisible(isPlugin);

    // In plugin mode, hide follow button (host controls playback)
    followButton.setVisible(!isPlugin);
    loopButton.setVisible(!isPlugin);

    resized();
}

void ToolbarComponent::setARAMode(bool isARA)
{
    araMode = isARA;
    araModeLabel.setText(isARA ? TR("toolbar.ara_mode") : TR("toolbar.non_ara"), juce::dontSendNotification);
}
