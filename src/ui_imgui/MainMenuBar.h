#pragma once

// Declares the top-level ImGui menu bar for application-wide commands.
// The menu delegates save/load actions to SaveLoadPanel so the active file path
// and status reporting stay in one UI component, while the View menu owns
// dock-window visibility toggles for reopenable panels.

#include "app/SimulationService.h"
#include "ui_imgui/SaveLoadPanel.h"

namespace deep::ui_imgui {

// Tracks whether each top-level dock window should be submitted this frame.
// The values are intentionally UI-local state; gameplay state remains owned by
// SimulationService and read through query DTOs.
struct PanelVisibility {
    bool saveLoad = true;
    bool timeControl = true;
    bool strategicMap = true;
    bool bodies = true;
    bool inspector = true;
    bool colonies = true;
    bool fleets = true;
    bool fleetOrders = true;
    bool shipyard = true;
    bool economyForecast = true;
    bool eventLog = true;
};

// Renders File and View menu actions for the prototype desktop shell.
// File actions are intentionally limited to New/Save/Load; autosave and native
// file dialogs are deferred until the persistence workflow has settled.
class MainMenuBar {
public:
    // Draws the main menu bar, dispatching persistence actions and panel
    // visibility changes through the supplied UI state.
    void render(SimulationService& service, SaveLoadPanel& saveLoadPanel, PanelVisibility& visibility) const;
};

} // namespace deep::ui_imgui
