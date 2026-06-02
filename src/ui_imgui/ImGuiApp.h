#pragma once

// Declares the minimal Dear ImGui application shell.
// The shell renders functional prototype panels against SimulationService without
// exposing raw GameState vectors.

#include "app/SimulationService.h"
#include "platform/SdlApp.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/EventLogPanel.h"
#include "ui_imgui/FleetPanel.h"
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

    // Draws the first functional simulation panels using app-layer query DTOs.
    void renderPanels();

    platform::SdlApp sdl_;
    SimulationService service_;
    TimeControlPanel timeControlPanel_;
    ColonyPanel colonyPanel_;
    FleetPanel fleetPanel_;
    EventLogPanel eventLogPanel_;
    StrategicMapPanel strategicMapPanel_;
};

} // namespace deep::ui_imgui
