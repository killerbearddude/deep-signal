#pragma once

// Declares the minimal Dear ImGui application shell.
// The shell renders functional prototype panels against SimulationService without
// exposing raw GameState vectors.

#include "app/SelectionState.h"
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
class ImGuiApp {
public:
    // Creates the SDL window and initializes the Dear ImGui SDL3 renderer backend.
    ImGuiApp();

    ImGuiApp(const ImGuiApp&) = delete;
    ImGuiApp& operator=(const ImGuiApp&) = delete;
    ImGuiApp(ImGuiApp&&) = delete;
    ImGuiApp& operator=(ImGuiApp&&) = delete;

    // Shuts down ImGui backends before SDL resources are destroyed.
    ~ImGuiApp();

    // Runs the UI event loop until the user closes the window.
    int run();

private:
    // Draws a full-window dockspace that future panels can dock into.
    void renderDockspace();

    // Draws application-level menu commands and panel visibility toggles.
    void renderMainMenu();

    // Draws the first functional simulation panels using app-layer query DTOs.
    void renderPanels();

    platform::SdlApp sdl_;
    SimulationService service_;
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
    SelectionState selection_;
    PanelVisibility visibility_;
};

} // namespace deep::ui_imgui
