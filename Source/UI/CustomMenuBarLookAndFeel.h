#pragma once

#include "../JuceHeader.h"
#include "../Utils/Constants.h"

class CustomMenuBarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomMenuBarLookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(COLOR_BACKGROUND));
        setColour(juce::PopupMenu::textColourId, juce::Colours::white);
        setColour(juce::PopupMenu::headerTextColourId, juce::Colours::white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(COLOR_PRIMARY));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(juce::Colour(COLOR_BACKGROUND));
        g.setColour(juce::Colour(COLOR_GRID_BAR));
        g.drawRect(0, 0, width, height, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                          bool isSeparator, bool isActive, bool isHighlighted,
                          bool isTicked, bool hasSubMenu,
                          const juce::String& text,
                          const juce::String& shortcutKeyText,
                          const juce::Drawable* icon,
                          const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            auto r = area.reduced(5, 0);
            r.removeFromTop(r.getHeight() / 2 - 1);
            g.setColour(juce::Colour(COLOR_GRID_BAR));
            g.fillRect(r.removeFromTop(1));
        }
        else
        {
            auto textColourToUse = textColour != nullptr ? *textColour
                                 : findColour(juce::PopupMenu::textColourId);

            if (isHighlighted && isActive)
            {
                g.setColour(juce::Colour(COLOR_PRIMARY));
                g.fillRect(area);
                textColourToUse = juce::Colours::white;
            }

            if (!isActive)
                textColourToUse = textColourToUse.withAlpha(0.5f);

            auto r = area.reduced(1);
            g.setColour(textColourToUse);
            g.setFont(getPopupMenuFont());

            auto iconArea = r.removeFromLeft(r.getHeight()).toFloat().reduced(2);

            if (icon != nullptr)
                icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
            else if (isTicked)
            {
                auto tick = getTickShape(1.0f);
                g.fillPath(tick, tick.getTransformToScaleToFit(iconArea, true));
            }

            if (hasSubMenu)
            {
                auto arrowH = 0.6f * getPopupMenuFont().getAscent();
                auto x = static_cast<float>(r.removeFromRight((int)arrowH).getX());
                auto halfH = static_cast<float>(r.getCentreY());

                juce::Path path;
                path.addTriangle(x, halfH - arrowH * 0.5f,
                               x, halfH + arrowH * 0.5f,
                               x + arrowH * 0.6f, halfH);

                g.fillPath(path);
            }

            r.removeFromRight(3);
            g.drawFittedText(text, r, juce::Justification::centredLeft, 1);

            if (shortcutKeyText.isNotEmpty())
            {
                auto f2 = getPopupMenuFont();
                f2.setHeight(f2.getHeight() * 0.75f);
                g.setFont(f2);
                g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
            }
        }
    }

    void drawMenuBarBackground(juce::Graphics& g, int width, int height,
                              bool, juce::MenuBarComponent&) override
    {
        g.fillAll(juce::Colour(COLOR_BACKGROUND));
        g.setColour(juce::Colour(COLOR_GRID_BAR));
        g.drawLine(0, height - 1, width, height - 1);
    }

    void drawMenuBarItem(juce::Graphics& g, int width, int height,
                        int itemIndex, const juce::String& itemText,
                        bool isMouseOverItem, bool isMenuOpen,
                        bool, juce::MenuBarComponent&) override
    {
        if (isMenuOpen || isMouseOverItem)
        {
            g.setColour(juce::Colour(COLOR_PRIMARY));
            g.fillRect(0, 0, width, height);
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(height * 0.6f));
        g.drawFittedText(itemText, 0, 0, width, height, juce::Justification::centred, 1);
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font(14.0f);
    }
};
