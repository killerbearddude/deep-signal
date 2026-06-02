#pragma once

// Declares the minimal Dear ImGui application shell.
// The shell renders placeholder docked panels against SimulationService without
// introducing gameplay controls, map rendering, or new simulation behavior.

#include "app/SimulationService.h"
#include "platform/SdlApp.h"

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

    // Draws non-interactive placeholder panels only; gameplay UI comes later.
    void renderPlaceholderPanels();

    platform::SdlApp sdl_;
    SimulationService service_;
};

} // namespace deep::ui_imgui
