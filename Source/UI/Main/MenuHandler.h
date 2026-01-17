#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UndoManager.h"
#include "../../Utils/Localization.h"
#include <functional>

/**
 * Handles menu bar creation and menu item selection.
 */
class MenuHandler : public juce::MenuBarModel {
public:
    MenuHandler();
    ~MenuHandler() override = default;

    void setPluginMode(bool isPlugin) { pluginMode = isPlugin; }
    void setUndoManager(PitchUndoManager* mgr) { undoManager = mgr; }

    // MenuBarModel interface
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Callbacks
    std::function<void()> onOpenFile;
    std::function<void()> onSaveProject;
    std::function<void()> onExportFile;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onShowSettings;
    std::function<void()> onQuit;
    std::function<void()> onExportSOMEDebug;
    std::function<void(bool)> onShowDeltaPitchChanged;
    std::function<void(bool)> onShowBasePitchChanged;

    // View settings
    void setShowDeltaPitch(bool show) { showDeltaPitch = show; }
    void setShowBasePitch(bool show) { showBasePitch = show; }
    bool getShowDeltaPitch() const { return showDeltaPitch; }
    bool getShowBasePitch() const { return showBasePitch; }

private:
    enum MenuIDs {
        MenuOpen = 1,
        MenuSave,
        MenuExport,
        MenuQuit,
        MenuUndo,
        MenuRedo,
        MenuSettings,
        MenuExportSOMEDebug,
        MenuShowDeltaPitch,
        MenuShowBasePitch
    };

    bool pluginMode = false;
    bool showDeltaPitch = true;
    bool showBasePitch = false;
    PitchUndoManager* undoManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MenuHandler)
};
