#include "ui_imgui/ImGuiApp.h"

// Implements the first SDL3/Dear ImGui shell for Deep Signal.
// The shell now hosts small functional panels while keeping all reads behind
// SimulationQueries and all mutations behind SimulationService commands.

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
    mainMenuBar_.render(service_, saveLoadPanel_);
}

void ImGuiApp::renderPanels() {
    // Recreate the query facade each frame so panels read a fresh snapshot after
    // time-control commands mutate SimulationService. The facade is lightweight
    // and does not expose mutable GameState access to panel code.
    const SimulationQueries queries{service_};

    saveLoadPanel_.render(service_);
    timeControlPanel_.render(service_);
    strategicMapPanel_.render(queries, selection_);
    colonyPanel_.render(queries, selection_);
    fleetPanel_.render(queries, selection_);
    eventLogPanel_.render(queries);
    inspectorPanel_.render(queries, selection_);
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
