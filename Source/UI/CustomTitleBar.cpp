#include "CustomTitleBar.h"

// Window button colors
namespace TitleBarColors {
#if JUCE_MAC
    constexpr juce::uint32 closeNormal = 0xFFFF5F57;
    constexpr juce::uint32 closeHover = 0xFFFF5F57;
    constexpr juce::uint32 minimizeNormal = 0xFFFEBC2E;
    constexpr juce::uint32 minimizeHover = 0xFFFEBC2E;
    constexpr juce::uint32 maximizeNormal = 0xFF28C840;
    constexpr juce::uint32 maximizeHover = 0xFF28C840;
    constexpr int buttonSize = 12;
    constexpr int buttonSpacing = 8;
    constexpr int buttonMargin = 12;
#else
    constexpr juce::uint32 closeNormal = 0xFF3A3A45;
    constexpr juce::uint32 closeHover = 0xFFE81123;
    constexpr juce::uint32 minimizeNormal = 0xFF3A3A45;
    constexpr juce::uint32 minimizeHover = 0xFF4A4A55;
    constexpr juce::uint32 maximizeNormal = 0xFF3A3A45;
    constexpr juce::uint32 maximizeHover = 0xFF4A4A55;
    constexpr int buttonWidth = 46;
    constexpr int buttonHeight = 32;
#endif
    constexpr juce::uint32 background = 0xFF1E1E28;
    constexpr juce::uint32 titleText = 0xFFCCCCCC;
}

// WindowButton implementation
CustomTitleBar::WindowButton::WindowButton(Type type)
    : juce::Button(""), buttonType(type)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void CustomTitleBar::WindowButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat();

#if JUCE_MAC
    // macOS traffic light style
    juce::Colour baseColor;
    switch (buttonType) {
        case Close: baseColor = juce::Colour(TitleBarColors::closeNormal); break;
        case Minimize: baseColor = juce::Colour(TitleBarColors::minimizeNormal); break;
        case Maximize: baseColor = juce::Colour(TitleBarColors::maximizeNormal); break;
    }

    if (isButtonDown)
        baseColor = baseColor.darker(0.2f);

    g.setColour(baseColor);
    g.fillEllipse(bounds.reduced(1));

    // Draw icon on hover
    if (isMouseOver) {
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        auto center = bounds.getCentre();
        float iconSize = bounds.getWidth() * 0.35f;

        if (buttonType == Close) {
            g.drawLine(center.x - iconSize, center.y - iconSize,
                      center.x + iconSize, center.y + iconSize, 1.5f);
            g.drawLine(center.x + iconSize, center.y - iconSize,
                      center.x - iconSize, center.y + iconSize, 1.5f);
        } else if (buttonType == Minimize) {
            g.drawLine(center.x - iconSize, center.y,
                      center.x + iconSize, center.y, 1.5f);
        } else {
            // Maximize - diagonal arrows
            g.drawLine(center.x - iconSize, center.y + iconSize,
                      center.x + iconSize, center.y - iconSize, 1.5f);
        }
    }
#else
    // Windows/Linux style
    juce::Colour bgColor;
    switch (buttonType) {
        case Close:
            bgColor = isMouseOver ? juce::Colour(TitleBarColors::closeHover)
                                  : juce::Colour(TitleBarColors::closeNormal);
            break;
        default:
            bgColor = isMouseOver ? juce::Colour(TitleBarColors::minimizeHover)
                                  : juce::Colour(TitleBarColors::minimizeNormal);
            break;
    }

    if (isButtonDown)
        bgColor = bgColor.darker(0.1f);

    g.setColour(bgColor);
    g.fillRect(bounds);

    // Draw icon
    g.setColour(juce::Colour(0xFFCCCCCC));
    auto center = bounds.getCentre();
    float iconSize = 5.0f;

    if (buttonType == Close) {
        g.drawLine(center.x - iconSize, center.y - iconSize,
                  center.x + iconSize, center.y + iconSize, 1.0f);
        g.drawLine(center.x + iconSize, center.y - iconSize,
                  center.x - iconSize, center.y + iconSize, 1.0f);
    } else if (buttonType == Minimize) {
        g.drawLine(center.x - iconSize, center.y,
                  center.x + iconSize, center.y, 1.0f);
    } else {
        // Maximize - rectangle
        g.drawRect(center.x - iconSize, center.y - iconSize,
                  iconSize * 2, iconSize * 2, 1.0f);
    }
#endif
}

// CustomTitleBar implementation
CustomTitleBar::CustomTitleBar()
{
    title = "Pitch Editor";

#if !JUCE_MAC
    // Only create custom buttons on non-macOS (macOS uses native traffic lights)
    minimizeButton = std::make_unique<WindowButton>(WindowButton::Minimize);
    maximizeButton = std::make_unique<WindowButton>(WindowButton::Maximize);
    closeButton = std::make_unique<WindowButton>(WindowButton::Close);

    closeButton->onClick = [this]() { closeWindow(); };
    minimizeButton->onClick = [this]() { minimizeWindow(); };
    maximizeButton->onClick = [this]() { toggleMaximize(); };

    addAndMakeVisible(*closeButton);
    addAndMakeVisible(*minimizeButton);
    addAndMakeVisible(*maximizeButton);
#endif

    // Setup Kiwi constraint layout
    setupConstraints();
}

CustomTitleBar::~CustomTitleBar() = default;

void CustomTitleBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(TitleBarColors::background));

    g.setColour(juce::Colour(TitleBarColors::titleText));
    g.setFont(13.0f);

#if JUCE_MAC
    // Title after traffic lights area on macOS (traffic lights take ~70px)
    g.drawText(title, 75, 0, getWidth() - 85, getHeight(), juce::Justification::centredLeft);
#else
    // Left-aligned title on Windows/Linux
    g.drawText(title, 12, 0, getWidth() - 150, getHeight(), juce::Justification::centredLeft);
#endif

    // Bottom border
    g.setColour(juce::Colour(0xFF3A3A45));
    g.drawHorizontalLine(getHeight() - 1, 0, static_cast<float>(getWidth()));
}

void CustomTitleBar::setupConstraints()
{
    // Create variables for container (this component)
    containerVars = layout.createComponentVars("container");

    // Container constraints (will be updated in resized())
    containerWidthConstraint = containerVars.width == 800.0;
    containerHeightConstraint = containerVars.height == static_cast<double>(titleBarHeight);
    layout.addConstraint(containerVars.left == 0.0);
    layout.addConstraint(containerVars.top == 0.0);
    layout.addConstraint(containerWidthConstraint);
    layout.addConstraint(containerHeightConstraint);

#if !JUCE_MAC
    // Create variables for window buttons (Windows/Linux only)
    closeButtonVars = layout.createComponentVars("closeButton");
    maximizeButtonVars = layout.createComponentVars("maximizeButton");
    minimizeButtonVars = layout.createComponentVars("minimizeButton");

    const double buttonWidth = static_cast<double>(TitleBarColors::buttonWidth);
    const double buttonHeight = static_cast<double>(TitleBarColors::buttonHeight);

    // Close button (rightmost)
    layout.addConstraint(closeButtonVars.right == containerVars.right);
    layout.addConstraint(closeButtonVars.top == containerVars.top);
    layout.addConstraint(closeButtonVars.width == buttonWidth);
    layout.addConstraint(closeButtonVars.height == buttonHeight);

    // Maximize button (middle)
    layout.addConstraint(maximizeButtonVars.right == closeButtonVars.left);
    layout.addConstraint(maximizeButtonVars.top == containerVars.top);
    layout.addConstraint(maximizeButtonVars.width == buttonWidth);
    layout.addConstraint(maximizeButtonVars.height == buttonHeight);

    // Minimize button (leftmost of the three)
    layout.addConstraint(minimizeButtonVars.right == maximizeButtonVars.left);
    layout.addConstraint(minimizeButtonVars.top == containerVars.top);
    layout.addConstraint(minimizeButtonVars.width == buttonWidth);
    layout.addConstraint(minimizeButtonVars.height == buttonHeight);
#endif
}

void CustomTitleBar::updateLayoutConstraints()
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

void CustomTitleBar::resized()
{
    // Update container size constraints
    updateLayoutConstraints();

#if !JUCE_MAC
    // Apply layout to window buttons
    layout.applyComponentVars(closeButton.get(), closeButtonVars);
    layout.applyComponentVars(maximizeButton.get(), maximizeButtonVars);
    layout.applyComponentVars(minimizeButton.get(), minimizeButtonVars);
#endif
}

void CustomTitleBar::mouseDown(const juce::MouseEvent& e)
{
    if (auto* window = getTopLevelComponent())
        dragger.startDraggingComponent(window, e.getEventRelativeTo(window));
}

void CustomTitleBar::mouseDrag(const juce::MouseEvent& e)
{
    if (auto* window = getTopLevelComponent())
        dragger.dragComponent(window, e.getEventRelativeTo(window), nullptr);
}

void CustomTitleBar::mouseDoubleClick(const juce::MouseEvent&)
{
    toggleMaximize();
}

void CustomTitleBar::setTitle(const juce::String& newTitle)
{
    title = newTitle;
    repaint();
}

void CustomTitleBar::closeWindow()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void CustomTitleBar::minimizeWindow()
{
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setMinimised(true);
}

void CustomTitleBar::toggleMaximize()
{
    if (auto* window = getTopLevelComponent())
    {
        if (isMaximized)
        {
            window->setBounds(normalBounds);
            isMaximized = false;
        }
        else
        {
            normalBounds = window->getBounds();
            auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display)
                window->setBounds(display->userArea);
            isMaximized = true;
        }
    }
}
