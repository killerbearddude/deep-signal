#pragma once

// Declares the top-level ImGui menu bar for application-wide commands.
// The menu delegates save/load actions to SaveLoadPanel so the active file path
// and status reporting stay in one UI component. Workspace presets curate panel
// visibility; the View menu retains manual toggles for every dock window.

#include "app/SimulationService.h"
#include "ui_imgui/SaveLoadPanel.h"

namespace deep::ui_imgui {

// UI-only operational contexts; these do not affect simulation or save state.
enum class Workspace {
    System,
    Economy,
    Production,
    Fleets,
    Intelligence,
    History
};

// Tracks whether each top-level dock window should be submitted this frame.
// The values are intentionally UI-local state; gameplay state remains owned by
// SimulationService and read through query DTOs. Defaults match System startup.
struct PanelVisibility {
    bool saveLoad = false;
    bool timeControl = false;
    bool strategicMap = true;
    bool bodies = true;
    bool inspector = true;
    bool colonies = false;
    bool fleets = false;
    bool fleetOrders = false;
    bool shipyard = false;
    bool economyForecast = false;
    bool eventLog = false;
};

// Replaces operational visibility with a curated preset. Global Save/Load and
// Time Control windows retain their manual visibility across workspace changes.
void applyWorkspace(Workspace workspace, PanelVisibility& visibility);

// Renders File, Workspace, and View actions for the desktop shell.
// File actions are intentionally limited to New/Save/Load; autosave and native
// file dialogs are deferred until the persistence workflow has settled.
class MainMenuBar {
public:
    // Draws the main menu bar, dispatching persistence actions and panel
    // visibility changes through the supplied UI state.
    void render(SimulationService& service, SaveLoadPanel& saveLoadPanel,
                Workspace& workspace, PanelVisibility& visibility) const;
};

} // namespace deep::ui_imgui
