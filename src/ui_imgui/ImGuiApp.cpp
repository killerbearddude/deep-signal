#include "ui_imgui/ImGuiApp.h"

// Responsibility: process window input and render panels on the UI thread.
// Commands execute synchronously through SimulationService. This loop owns
// frame scheduling, not the simulation clock or gameplay rules.

#include "app/SimulationQueries.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdlib>
#include <exception>
#include <iostream>

namespace deep::ui_imgui {

ImGuiApp::ImGuiApp()
    : sdl_{"Deep Signal", 1280, 720}, service_{}, interactions_{service_, [this] {
        // Invoked only after all members are constructed, on actual New/Load
        // success. Hidden workflows are invalidated now, not when next rendered.
        inspectorPanel_.resetWorldState();
        fleetOrdersPanel_.resetWorldState();
        colonyPanel_.resetWorldState();
    }} {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(sdl_.window(), sdl_.renderer())) {
        ImGui::DestroyContext();
        throw std::runtime_error{"ImGui_ImplSDL3_InitForSDLRenderer failed"};
    }

    if (!ImGui_ImplSDLRenderer3_Init(sdl_.renderer())) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error{"ImGui_ImplSDLRenderer3_Init failed"};
    }
}

ImGuiApp::~ImGuiApp() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

int ImGuiApp::run() {
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (sdl_.isQuitEvent(event)) {
                running = false;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        renderDockspace();
        renderMainMenu();
        renderPanels();

        ImGui::Render();
        sdl_.beginFrame();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl_.renderer());
        sdl_.endFrame();
    }

    return EXIT_SUCCESS;
}

void ImGuiApp::renderDockspace() {
    // DockSpaceOverViewport gives the shell a single central docking target
    // without committing to any final game-window layout yet.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
}

void ImGuiApp::renderMainMenu() {
    interactions_.reconcile();
    mainMenuBar_.render(service_, saveLoadPanel_, interactions_, workspace_, visibility_);
}

void ImGuiApp::renderPanels() {
    // These facades borrow the service; they do not capture a frozen snapshot.
    // Each query observes state at call time, so later panels see commands from
    // earlier panels this frame. Already-copied DTOs update on their next query.
    // Keep reads and commands serialized if panel scheduling changes.
    const SimulationQueries queries{service_};
    const ForecastService forecasts{service_};

    // File-menu replacement already completed its lifecycle synchronously. The
    // legacy panel uses that same path and may replace the world in this frame.
    interactions_.reconcile();
    saveLoadPanel_.render(service_, interactions_, visibility_.saveLoad);
    interactions_.reconcile();
    timeControlPanel_.render(service_, visibility_.timeControl);
    shipyardPanel_.render(queries, service_, visibility_.shipyard);
    // Producers stamp displayed data and dispatch only actual widget activations.
    // Each later reader gets a newly reconciled projection, not a frame-start copy.
    strategicMapPanel_.render(queries, interactions_, visibility_.strategicMap);
    bodiesPanel_.render(queries, interactions_, visibility_.bodies);
    colonyPanel_.render(queries, service_, interactions_, visibility_.colonies);
    fleetPanel_.render(queries, interactions_, visibility_.fleets);
    fleetOrdersPanel_.render(queries, service_, interactions_.mainSelection(), visibility_.fleetOrders);
    economyForecastPanel_.render(forecasts, visibility_.economyForecast);
    eventLogPanel_.render(queries, visibility_.eventLog);
    inspectorPanel_.render(queries, service_, interactions_.mainSelection(), visibility_.inspector);
}

} // namespace deep::ui_imgui

int main() {
    try {
        deep::ui_imgui::ImGuiApp app;
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "Deep Signal UI failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
