#pragma once

// Responsibility: compose the desktop event loop, panels, and shared selection.
// Owns ImGui lifetime and UI state; SimulationService owns gameplay state and
// SdlApp owns native resources. Panels read DTOs and submit commands, not records.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationService.h"
#include "app/ForecastService.h"
#include "platform/SdlApp.h"
#include "ui_imgui/BodiesPanel.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/EconomyForecastPanel.h"
#include "ui_imgui/EventLogPanel.h"
#include "ui_imgui/FleetPanel.h"
#include "ui_imgui/FleetOrdersPanel.h"
#include "ui_imgui/InspectorPanel.h"
#include "ui_imgui/MainMenuBar.h"
#include "ui_imgui/SaveLoadPanel.h"
#include "ui_imgui/ShipyardPanel.h"
#include "ui_imgui/StrategicMapPanel.h"
#include "ui_imgui/TimeControlPanel.h"

namespace deep::ui_imgui {

// Owns the ImGui context for the desktop prototype shell.
// SDL resources are delegated to platform::SdlApp and application state is kept
// behind SimulationService so UI code does not own raw simulation internals.
// Threading: construction, rendering, commands, and teardown run on the same
// thread. There is no background simulation or synchronization in this shell.
class ImGuiApp {
public:
    // Creates the SDL window and initializes the Dear ImGui SDL3 renderer backend.
    // Backend initialization failures release initialized backends/context and
    // throw; member unwinding then releases the SDL resources.
    ImGuiApp();

    ImGuiApp(const ImGuiApp&) = delete;
    ImGuiApp& operator=(const ImGuiApp&) = delete;
    ImGuiApp(ImGuiApp&&) = delete;
    ImGuiApp& operator=(ImGuiApp&&) = delete;

    // Shuts down ImGui backends before SDL resources are destroyed.
    ~ImGuiApp();

    // Runs the UI event loop until the user closes the window. Rendering alone
    // never advances days; time controls advance them and New/Load can replace
    // the current date.
    int run();

private:
    // Draws a full-window dockspace that future panels can dock into.
    void renderDockspace();

    // Draws application commands, workspace presets, and manual panel toggles.
    void renderMainMenu();

    // Draws the first functional simulation panels using app-layer query DTOs.
    void renderPanels();

    // SDL must outlive ImGui backend shutdown in the destructor body.
    platform::SdlApp sdl_;
    SimulationService service_;
    InformationInteractionAdapter interactions_;
    MainMenuBar mainMenuBar_;
    SaveLoadPanel saveLoadPanel_;
    TimeControlPanel timeControlPanel_;
    BodiesPanel bodiesPanel_;
    ShipyardPanel shipyardPanel_;
    EconomyForecastPanel economyForecastPanel_;
    ColonyPanel colonyPanel_;
    FleetPanel fleetPanel_;
    FleetOrdersPanel fleetOrdersPanel_;
    EventLogPanel eventLogPanel_;
    InspectorPanel inspectorPanel_;
    StrategicMapPanel strategicMapPanel_;
    Workspace workspace_ = Workspace::System;
    PanelVisibility visibility_;
};

} // namespace deep::ui_imgui
