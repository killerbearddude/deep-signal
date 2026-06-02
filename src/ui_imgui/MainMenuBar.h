#pragma once

// Declares the top-level ImGui menu bar for application-wide commands.
// The menu delegates save/load actions to SaveLoadPanel so the active file path
// and status reporting stay in one UI component.

#include "app/SimulationService.h"
#include "ui_imgui/SaveLoadPanel.h"

namespace deep::ui_imgui {

// Renders File-menu actions for the prototype desktop shell.
// Actions are intentionally limited to New/Save/Load; autosave and native file
// dialogs are deferred until the persistence workflow has settled.
class MainMenuBar {
public:
    // Draws the main menu bar and dispatches selected actions through the panel.
    void render(SimulationService& service, SaveLoadPanel& saveLoadPanel) const;
};

} // namespace deep::ui_imgui
