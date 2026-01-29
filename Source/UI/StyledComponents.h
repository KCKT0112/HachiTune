#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"
#include "../Utils/DPIScaleManager.h"
#include <cmath>

/**
 * Global font manager - loads custom font from Resources/fonts/
 * Falls back to system font if not found.
 * Uses reference counting to support multiple plugin instances.
 */
class AppFont
{
public:
    static void initialize()
    {
        auto& instance = getInstance();
        ++instance.refCount;

        if (instance.initialized)
            return;

        instance.initialized = true;

        // Try to load custom font from Resources/fonts/
        juce::File appDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

        // Check multiple possible locations
        juce::StringArray fontPaths = {
            appDir.getChildFile("Resources/fonts/NotoSansCJKjp-Regular.otf").getFullPathName(),
            appDir.getChildFile("../Resources/fonts/NotoSansCJKjp-Regular.otf").getFullPathName(),
            appDir.getChildFile("fonts/NotoSansCJKjp-Regular.otf").getFullPathName(),
#if JUCE_MAC
            appDir.getChildFile("../Resources/fonts/NotoSansCJKjp-Regular.otf").getFullPathName(),
#endif
        };

        for (const auto& path : fontPaths)
        {
            juce::File fontFile(path);
            if (fontFile.existsAsFile())
            {
                juce::MemoryBlock fontData;
                if (fontFile.loadFileAsData(fontData))
                {
                    instance.customTypeface = juce::Typeface::createSystemTypefaceFor(
                        fontData.getData(), fontData.getSize());
                    if (instance.customTypeface != nullptr)
                    {
                        instance.fontLoaded = true;
                        DBG("Loaded custom font: " + path);
                        break;
                    }
                }
            }
        }

        if (!instance.fontLoaded)
        {
            DBG("Custom font not found, using system font");
        }
    }

    /**
     * Release font resources. Call this before application/plugin shutdown
     * to avoid JUCE leak detector warnings.
     * Uses reference counting - only releases when last user calls shutdown.
     */
    static void shutdown()
    {
        auto& instance = getInstance();
        if (instance.refCount > 0)
            --instance.refCount;

        if (instance.refCount == 0 && instance.initialized)
        {
            instance.customTypeface = nullptr;
            instance.fontLoaded = false;
            instance.initialized = false;
        }
    }

    static juce::Font getFont(float height = 14.0f)
    {
        auto& instance = getInstance();
        if (instance.fontLoaded && instance.customTypeface != nullptr)
            return juce::Font(instance.customTypeface).withHeight(height);

        // Fallback to system font
#if JUCE_MAC
        return juce::Font("Hiragino Sans", height, juce::Font::plain);
#elif JUCE_WINDOWS
        return juce::Font("Yu Gothic UI", height, juce::Font::plain);
#else
        return juce::Font(height);
#endif
    }

    static juce::Font getBoldFont(float height = 14.0f)
    {
        auto& instance = getInstance();
        if (instance.fontLoaded && instance.customTypeface != nullptr)
            return juce::Font(instance.customTypeface).withHeight(height).boldened();

        // Fallback to system font
#if JUCE_MAC
        return juce::Font("Hiragino Sans", height, juce::Font::bold);
#elif JUCE_WINDOWS
        return juce::Font("Yu Gothic UI", height, juce::Font::bold);
#else
        return juce::Font(height).boldened();
#endif
    }

    static bool isCustomFontLoaded()
    {
        return getInstance().fontLoaded;
    }

    /**
     * Get a DPI-scaled font.
     * This automatically applies the current DPI scale factor.
     */
    static juce::Font getScaledFont(float baseHeight = 14.0f)
    {
        float scaleFactor = juce::Desktop::getInstance().getGlobalScaleFactor();
        return getFont(baseHeight * scaleFactor);
    }

    /**
     * Get a DPI-scaled bold font.
     * This automatically applies the current DPI scale factor.
     */
    static juce::Font getScaledBoldFont(float baseHeight = 14.0f)
    {
        float scaleFactor = juce::Desktop::getInstance().getGlobalScaleFactor();
        return getBoldFont(baseHeight * scaleFactor);
    }

private:
    AppFont() = default;

    static AppFont& getInstance()
    {
        static AppFont instance;
        return instance;
    }

    juce::Typeface::Ptr customTypeface;
    bool fontLoaded = false;
    bool initialized = false;
    int refCount = 0;
};

/**
 * Shared dark theme LookAndFeel for the application.
 */
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return AppFont::getFont(14.0f);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return AppFont::getFont(14.0f);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return AppFont::getFont(14.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return AppFont::getFont(14.0f);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                          bool isSeparator, bool isActive, bool isHighlighted,
                          bool isTicked, bool hasSubMenu,
                          const juce::String& text, const juce::String& shortcutKeyText,
                          const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawTickBox(juce::Graphics& g, juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar,
                         int width, int height, double progress,
                         const juce::String& textToShow) override;

    static DarkLookAndFeel& getInstance()
    {
        static DarkLookAndFeel instance;
        return instance;
    }
};

/**
 * Pre-styled slider with dark theme colors.
 */
class StyledSlider : public juce::Slider
{
public:
    StyledSlider()
    {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2D2D37));
        setColour(juce::Slider::trackColourId, juce::Colour(APP_COLOR_PRIMARY).withAlpha(0.6f));
        setColour(juce::Slider::thumbColourId, juce::Colour(APP_COLOR_PRIMARY));
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2D2D37));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
};

/**
 * Pre-styled combo box with dark theme colors.
 */
class StyledComboBox : public juce::ComboBox
{
public:
    StyledComboBox()
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF3D3D47));
        setColour(juce::ComboBox::textColourId, juce::Colours::white);
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF4A4A55));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(APP_COLOR_PRIMARY));
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledComboBox() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled toggle button with custom checkbox.
 */
class StyledToggleButton : public juce::ToggleButton
{
public:
    StyledToggleButton(const juce::String& buttonText = {}) : juce::ToggleButton(buttonText)
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledToggleButton() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled label with light grey text.
 */
class StyledLabel : public juce::Label
{
public:
    StyledLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }
};

/**
 * Section header label with primary color.
 */
class SectionLabel : public juce::Label
{
public:
    SectionLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, juce::Colour(APP_COLOR_PRIMARY));
        setFont(juce::Font(14.0f, juce::Font::bold));
    }
};

/**
 * VST-style 3D knob LookAndFeel.
 * Draws a realistic rotary knob with metallic appearance and pointer indicator.
 */
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel() = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        const float diameter = static_cast<float>(juce::jmin(width, height));
        const float radius = (diameter / 2.0f) - 4.0f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        const bool isEnabled = slider.isEnabled();
        const float alpha = isEnabled ? 1.0f : 0.4f;

        // === Outer track ring ===
        const float trackRadius = radius + 2.0f;
        g.setColour(juce::Colour(0xFF1E1E26).withAlpha(alpha));
        g.drawEllipse(centreX - trackRadius, centreY - trackRadius,
                      trackRadius * 2.0f, trackRadius * 2.0f, 3.0f);

        // === Knob body ===
        const float knobRadius = radius * 0.85f;

        // Outer shadow
        g.setColour(juce::Colour(0xFF0A0A0E).withAlpha(alpha * 0.5f));
        g.fillEllipse(centreX - knobRadius - 1.0f, centreY - knobRadius + 2.0f,
                      knobRadius * 2.0f + 2.0f, knobRadius * 2.0f + 2.0f);

        // Main knob body - gradient from top-left to bottom-right
        juce::ColourGradient bodyGradient(
            juce::Colour(0xFF5A5A65).withAlpha(alpha), centreX - knobRadius * 0.7f, centreY - knobRadius * 0.7f,
            juce::Colour(0xFF28282F).withAlpha(alpha), centreX + knobRadius * 0.7f, centreY + knobRadius * 0.7f, false);
        g.setGradientFill(bodyGradient);
        g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

        // Inner bevel / rim
        g.setColour(juce::Colour(0xFF3A3A44).withAlpha(alpha));
        g.drawEllipse(centreX - knobRadius + 1.5f, centreY - knobRadius + 1.5f,
                      (knobRadius - 1.5f) * 2.0f, (knobRadius - 1.5f) * 2.0f, 1.0f);

        // === Pointer line ===
        const float pointerLength = knobRadius * 0.6f;
        const float pointerStartRadius = knobRadius * 0.2f;

        juce::Path pointer;
        pointer.startNewSubPath(0.0f, -pointerStartRadius);
        pointer.lineTo(0.0f, -pointerLength);

        g.setColour(juce::Colour(APP_COLOR_PRIMARY).withAlpha(alpha));
        g.strokePath(pointer, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        // Small dot at pointer tip
        float tipX = centreX + std::sin(angle) * (pointerLength - 2.0f);
        float tipY = centreY - std::cos(angle) * (pointerLength - 2.0f);
        g.fillEllipse(tipX - 2.5f, tipY - 2.5f, 5.0f, 5.0f);
    }

    static KnobLookAndFeel& getInstance()
    {
        static KnobLookAndFeel instance;
        return instance;
    }
};

/**
 * Custom styled message box component matching the app's dark theme.
 */
class StyledMessageBox : public juce::Component
{
public:
    enum IconType
    {
        NoIcon,
        InfoIcon,
        WarningIcon,
        ErrorIcon
    };

    StyledMessageBox(const juce::String& title, const juce::String& message, IconType iconType = NoIcon)
        : titleText(title), messageText(message), iconType(iconType)
    {
        setOpaque(true);
        
        // Add OK button
        okButton = std::make_unique<juce::TextButton>("OK");
        okButton->setSize(80, 32);
        okButton->onClick = [this] { 
            if (onClose != nullptr)
                onClose();
        };
        addAndMakeVisible(okButton.get());
        
        // Style the button
        okButton->setColour(juce::TextButton::buttonColourId, juce::Colour(APP_COLOR_PRIMARY));
        okButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        okButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(APP_COLOR_PRIMARY).brighter(0.2f));
        
        setSize(400, 200);
    }
    
    void resized() override
    {
        if (okButton != nullptr)
        {
            okButton->setCentrePosition(getWidth() / 2, getHeight() - 30);
        }
    }
    
    std::function<void()> onClose;

    void paint(juce::Graphics& g) override
    {
        // Background
        g.fillAll(juce::Colour(APP_COLOR_BACKGROUND));

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(titleText, 20, 20, getWidth() - 40, 30, juce::Justification::left);

        // Icon (if any)
        int iconX = 20;
        int iconY = 60;
        int iconSize = 32;
        
        if (iconType != NoIcon)
        {
            juce::Colour iconColour;
            if (iconType == InfoIcon)
                iconColour = juce::Colour(APP_COLOR_PRIMARY);
            else if (iconType == WarningIcon)
                iconColour = juce::Colour(0xFFFFAA00);
            else if (iconType == ErrorIcon)
                iconColour = juce::Colour(0xFFFF4444);

            g.setColour(iconColour);
            g.fillEllipse(iconX, iconY, iconSize, iconSize);
            g.setColour(juce::Colour(APP_COLOR_BACKGROUND));
            g.setFont(juce::Font(iconSize * 0.6f, juce::Font::bold));
            
            juce::String iconChar;
            if (iconType == InfoIcon)
                iconChar = "i";
            else if (iconType == WarningIcon)
                iconChar = "!";
            else if (iconType == ErrorIcon)
                iconChar = "X";

            g.drawText(iconChar, iconX, iconY, iconSize, iconSize, juce::Justification::centred);
            iconX += iconSize + 15;
        }

        // Message text
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(14.0f));
        g.drawMultiLineText(messageText, iconX, iconY + 5, getWidth() - iconX - 20, juce::Justification::topLeft);
    }

    static void show(juce::Component* parent, const juce::String& title, const juce::String& message, IconType iconType = NoIcon)
    {
        auto* dialog = new StyledMessageDialog(parent, title, message, iconType);
        dialog->enterModalState(true, nullptr, true);
    }

private:
    juce::String titleText;
    juce::String messageText;
    IconType iconType;
    std::unique_ptr<juce::TextButton> okButton;

    class StyledMessageDialog : public juce::DialogWindow
    {
    public:
        StyledMessageDialog(juce::Component* parent, const juce::String& title, const juce::String& message, StyledMessageBox::IconType iconType)
            : juce::DialogWindow(title, juce::Colour(APP_COLOR_BACKGROUND), true)
        {
            setOpaque(true);
            setUsingNativeTitleBar(false);
            setResizable(false, false);
            
            // Remove close button from title bar
            setTitleBarButtonsRequired(0, false);
            
            messageBox = std::make_unique<StyledMessageBox>(title, message, iconType);
            messageBox->onClose = [this] { closeDialog(); };
            setContentOwned(messageBox.get(), false);
            
            int dialogWidth = 420;
            int dialogHeight = 220;
            setSize(dialogWidth, dialogHeight);
            
            if (parent != nullptr)
                centreAroundComponent(parent, dialogWidth, dialogHeight);
            else
                centreWithSize(dialogWidth, dialogHeight);
        }

        void closeButtonPressed() override
        {
            // This should not be called since we removed the close button
            closeDialog();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(APP_COLOR_BACKGROUND));
        }

    private:
        void closeDialog()
        {
            exitModalState(0);
        }

        std::unique_ptr<StyledMessageBox> messageBox;
    };
};
