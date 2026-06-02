#include "ui_imgui/ImGuiApp.h"

// Implements the first SDL3/Dear ImGui shell for Deep Signal.
// The goal is only to prove that a dockable desktop window can be launched on
// top of the existing app layer; real game panels and controls are deferred.

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdlib>
#include <exception>
#include <iostream>

namespace deep::ui_imgui {

ImGuiApp::ImGuiApp()
    : sdl_{"Deep Signal", 1280, 720}, service_{} {
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
        renderPlaceholderPanels();

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

void ImGuiApp::renderPlaceholderPanels() {
    const GameState& state = service_.state();

    ImGui::Begin("Empire Overview");
    ImGui::TextUnformatted("Placeholder panel");
    ImGui::Separator();
    ImGui::Text("Day: %lld", static_cast<long long>(state.currentDate.day));
    ImGui::Text("Colonies: %zu", state.colonies.size());
    ImGui::Text("Fleets: %zu", state.fleets.size());
    ImGui::End();

    ImGui::Begin("Event Log");
    ImGui::TextUnformatted("Placeholder panel");
    ImGui::Separator();
    ImGui::Text("Audit events: %zu", state.eventLog.size());
    ImGui::Text("Economy telemetry rows: %zu", state.dailyEconomySnapshots.size());
    ImGui::End();

    ImGui::Begin("Inspector");
    ImGui::TextUnformatted("Placeholder panel");
    ImGui::TextUnformatted("Selection and gameplay controls will be added in later patches.");
    ImGui::End();
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
