#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/KiwiLayoutManager.h"
#include "RoundedCard.h"
#include "PanelContainer.h"
#include "DraggablePanel.h"

/**
 * Main workspace component that manages the layout of:
 * - Piano roll (main content area with rounded card)
 * - Panel container (right side panels)
 */
class WorkspaceComponent : public juce::Component
{
public:
    WorkspaceComponent();
    ~WorkspaceComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setMainContent(juce::Component* content);
    void addPanel(const juce::String& id, const juce::String& title,
                  juce::Component* content,
                  bool initiallyVisible = false);

    void showPanel(const juce::String& id, bool show);
    bool isPanelVisible(const juce::String& id) const;

    PanelContainer& getPanelContainer() { return panelContainer; }
    RoundedCard& getMainCard() { return mainCard; }

    std::function<void(const juce::String&, bool)> onPanelVisibilityChanged;

private:
    void updatePanelContainerVisibility();

    // Layout setup
    void setupConstraints();
    void updateLayoutConstraints();

    RoundedCard mainCard;
    PanelContainer panelContainer;

    juce::Component* mainContent = nullptr;
    int panelContainerWidth = 280;

    // Kiwi constraint layout
    KiwiLayoutManager layout;
    KiwiLayoutManager::ComponentVars containerVars;
    KiwiLayoutManager::ComponentVars mainCardVars;
    KiwiLayoutManager::ComponentVars panelContainerVars;

    // Dynamic constraints that change on resize
    kiwi::Constraint containerWidthConstraint;
    kiwi::Constraint containerHeightConstraint;
    kiwi::Constraint panelVisibilityConstraint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceComponent)
};
