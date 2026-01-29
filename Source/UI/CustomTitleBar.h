#pragma once

#include "../JuceHeader.h"
#include "../Utils/KiwiLayoutManager.h"

/**
 * Custom title bar component for frameless window.
 * Platform-specific styling: macOS uses traffic lights on left, Windows/Linux uses buttons on right.
 */
class CustomTitleBar : public juce::Component
{
public:
    CustomTitleBar();
    ~CustomTitleBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    void setTitle(const juce::String& title);

    static constexpr int titleBarHeight = 32;

private:
    class WindowButton : public juce::Button
    {
    public:
        enum Type { Close, Minimize, Maximize };
        WindowButton(Type type);
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    private:
        Type buttonType;
    };

    void closeWindow();
    void minimizeWindow();
    void toggleMaximize();

    // Layout setup
    void setupConstraints();
    void updateLayoutConstraints();

    juce::String title;
    juce::ComponentDragger dragger;

#if !JUCE_MAC
    std::unique_ptr<WindowButton> minimizeButton;
    std::unique_ptr<WindowButton> maximizeButton;
    std::unique_ptr<WindowButton> closeButton;
#endif

    bool isMaximized = false;
    juce::Rectangle<int> normalBounds;

    // Kiwi constraint layout
    KiwiLayoutManager layout;
    KiwiLayoutManager::ComponentVars containerVars;

#if !JUCE_MAC
    KiwiLayoutManager::ComponentVars closeButtonVars;
    KiwiLayoutManager::ComponentVars maximizeButtonVars;
    KiwiLayoutManager::ComponentVars minimizeButtonVars;
#endif

    // Dynamic constraints
    kiwi::Constraint containerWidthConstraint;
    kiwi::Constraint containerHeightConstraint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomTitleBar)
};
