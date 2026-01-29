#include "WorkspaceComponent.h"

WorkspaceComponent::WorkspaceComponent()
{
    setOpaque(true);

    addAndMakeVisible(mainCard);
    addAndMakeVisible(panelContainer);

    // Initially hide panel container (no panels visible)
    panelContainer.setVisible(false);

    // Setup Kiwi constraint layout
    setupConstraints();
}

void WorkspaceComponent::setupConstraints()
{
    // Create variables for container (this component)
    containerVars = layout.createComponentVars("container");

    // Create variables for main card and panel container
    mainCardVars = layout.createComponentVars("mainCard");
    panelContainerVars = layout.createComponentVars("panelContainer");

    // Container constraints (will be updated in resized())
    containerWidthConstraint = containerVars.width == 800.0;
    containerHeightConstraint = containerVars.height == 600.0;
    layout.addConstraint(containerVars.left == 0.0);
    layout.addConstraint(containerVars.top == 0.0);
    layout.addConstraint(containerWidthConstraint);
    layout.addConstraint(containerHeightConstraint);

    // Main card constraints (fills available space with margins)
    // Left margin: 8px
    layout.addConstraint(mainCardVars.left == containerVars.left + 8.0);
    // Top margin: 2px (smaller to be closer to toolbar)
    layout.addConstraint(mainCardVars.top == containerVars.top + 2.0);
    // Bottom margin: 8px
    layout.addConstraint(mainCardVars.bottom == containerVars.bottom - 8.0);

    // Panel container constraints (280px width on right side when visible)
    // Right margin: 8px
    layout.addConstraint(panelContainerVars.right == containerVars.right - 8.0);
    layout.addConstraint(panelContainerVars.top == containerVars.top + 2.0);
    layout.addConstraint(panelContainerVars.bottom == containerVars.bottom - 8.0);
    layout.addConstraint(panelContainerVars.width == 280.0);

    // Main card right edge depends on panel visibility
    // When panels visible: 8px gap from panel container
    // When panels hidden: 8px from right edge
    // We'll update this dynamically in updateLayoutConstraints()
    panelVisibilityConstraint = mainCardVars.right == containerVars.right - 8.0;
    layout.addConstraint(panelVisibilityConstraint);
}

void WorkspaceComponent::updateLayoutConstraints()
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

    // Update main card right edge based on panel visibility
    layout.removeConstraint(panelVisibilityConstraint);

    bool hasPanels = panelContainer.isVisible();
    if (hasPanels)
    {
        // 8px gap between main card and panel container
        panelVisibilityConstraint = mainCardVars.right == panelContainerVars.left - 8.0;
    }
    else
    {
        // 8px margin from right edge
        panelVisibilityConstraint = mainCardVars.right == containerVars.right - 8.0;
    }
    layout.addConstraint(panelVisibilityConstraint);
}

void WorkspaceComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A24));
}

void WorkspaceComponent::resized()
{
    // Update container size constraints
    updateLayoutConstraints();

    // Apply layout to components
    layout.applyComponentVars(&mainCard, mainCardVars);

    if (panelContainer.isVisible())
    {
        layout.applyComponentVars(&panelContainer, panelContainerVars);
    }
}

void WorkspaceComponent::setMainContent(juce::Component* content)
{
    mainContent = content;
    mainCard.setContentComponent(content);
}

void WorkspaceComponent::addPanel(const juce::String& id, const juce::String& title,
                                   juce::Component* content,
                                   bool initiallyVisible)
{
    // Set content size before adding to panel
    if (content != nullptr)
        content->setSize(panelContainerWidth - 32, 500);

    // Create draggable panel wrapper
    auto panel = std::make_unique<DraggablePanel>(id, title);
    panel->setContentComponent(content);

    // Add to panel container
    panelContainer.addPanel(std::move(panel));

    // Set initial visibility
    if (initiallyVisible)
    {
        panelContainer.showPanel(id, true);
        updatePanelContainerVisibility();

        if (onPanelVisibilityChanged)
            onPanelVisibilityChanged(id, true);
    }
}

void WorkspaceComponent::showPanel(const juce::String& id, bool show)
{
    panelContainer.showPanel(id, show);
    updatePanelContainerVisibility();

    if (onPanelVisibilityChanged)
        onPanelVisibilityChanged(id, show);
}

bool WorkspaceComponent::isPanelVisible(const juce::String& id) const
{
    return panelContainer.isPanelVisible(id);
}

void WorkspaceComponent::updatePanelContainerVisibility()
{
    bool hasPanels = false;
    for (const auto& id : panelContainer.getPanelOrder())
    {
        if (panelContainer.isPanelVisible(id))
        {
            hasPanels = true;
            break;
        }
    }

    panelContainer.setVisible(hasPanels);
    resized();
}
