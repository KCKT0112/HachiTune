#pragma once

#include "../JuceHeader.h"
#include "KiwiLayoutManager.h"

/**
 * Example component demonstrating Kiwi constraint layout usage.
 *
 * This component shows how to create a responsive layout using constraints:
 * - Header bar at the top (fixed height)
 * - Footer bar at the bottom (fixed height)
 * - Content area in the middle (fills remaining space)
 * - Sidebar on the left (fixed width)
 * - Main content on the right (fills remaining space)
 */
class KiwiLayoutExample : public juce::Component
{
public:
    KiwiLayoutExample()
    {
        // Create child components
        addAndMakeVisible(header);
        addAndMakeVisible(footer);
        addAndMakeVisible(sidebar);
        addAndMakeVisible(content);

        header.setColour(juce::TextButton::buttonColourId, juce::Colours::darkblue);
        footer.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
        sidebar.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        content.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);

        header.setButtonText("Header");
        footer.setButtonText("Footer");
        sidebar.setButtonText("Sidebar");
        content.setButtonText("Content");

        setupConstraints();
    }

    void resized() override
    {
        // Update container size constraints
        layout.removeConstraint(containerWidthConstraint);
        layout.removeConstraint(containerHeightConstraint);

        containerWidthConstraint = containerVars.width == static_cast<double>(getWidth());
        containerHeightConstraint = containerVars.height == static_cast<double>(getHeight());

        layout.addConstraint(containerWidthConstraint);
        layout.addConstraint(containerHeightConstraint);

        // Apply layout to all components
        layout.applyComponentVars(&header, headerVars);
        layout.applyComponentVars(&footer, footerVars);
        layout.applyComponentVars(&sidebar, sidebarVars);
        layout.applyComponentVars(&content, contentVars);
    }

private:
    void setupConstraints()
    {
        // Create variables for container (this component)
        containerVars = layout.createComponentVars("container");

        // Create variables for child components
        headerVars = layout.createComponentVars("header");
        footerVars = layout.createComponentVars("footer");
        sidebarVars = layout.createComponentVars("sidebar");
        contentVars = layout.createComponentVars("content");

        // Container constraints (will be updated in resized())
        containerWidthConstraint = containerVars.width == 800.0;
        containerHeightConstraint = containerVars.height == 600.0;
        layout.addConstraint(containerVars.left == 0.0);
        layout.addConstraint(containerVars.top == 0.0);
        layout.addConstraint(containerWidthConstraint);
        layout.addConstraint(containerHeightConstraint);

        // Header constraints
        layout.addConstraint(headerVars.left == containerVars.left);
        layout.addConstraint(headerVars.top == containerVars.top);
        layout.addConstraint(headerVars.width == containerVars.width);
        layout.addConstraint(headerVars.height == 50.0);  // Fixed height

        // Footer constraints
        layout.addConstraint(footerVars.left == containerVars.left);
        layout.addConstraint(footerVars.bottom == containerVars.bottom);
        layout.addConstraint(footerVars.width == containerVars.width);
        layout.addConstraint(footerVars.height == 40.0);  // Fixed height

        // Sidebar constraints
        layout.addConstraint(sidebarVars.left == containerVars.left);
        layout.addConstraint(sidebarVars.top == headerVars.bottom);
        layout.addConstraint(sidebarVars.bottom == footerVars.top);
        layout.addConstraint(sidebarVars.width == 200.0);  // Fixed width

        // Content constraints
        layout.addConstraint(contentVars.left == sidebarVars.right);
        layout.addConstraint(contentVars.top == headerVars.bottom);
        layout.addConstraint(contentVars.right == containerVars.right);
        layout.addConstraint(contentVars.bottom == footerVars.top);
    }

    KiwiLayoutManager layout;

    // Component variables
    KiwiLayoutManager::ComponentVars containerVars;
    KiwiLayoutManager::ComponentVars headerVars;
    KiwiLayoutManager::ComponentVars footerVars;
    KiwiLayoutManager::ComponentVars sidebarVars;
    KiwiLayoutManager::ComponentVars contentVars;

    // Dynamic constraints that change on resize
    kiwi::Constraint containerWidthConstraint;
    kiwi::Constraint containerHeightConstraint;

    // Child components
    juce::TextButton header;
    juce::TextButton footer;
    juce::TextButton sidebar;
    juce::TextButton content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KiwiLayoutExample)
};
