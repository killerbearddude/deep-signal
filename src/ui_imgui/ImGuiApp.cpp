#include "ui_imgui/ImGuiApp.h"
#include "ui_imgui/UiTheme.h"

// Responsibility: process window input and render panels on the UI thread.
// Commands execute synchronously through SimulationService. This loop owns
// frame scheduling, not the simulation clock or gameplay rules.

#include "app/SimulationQueries.h"
#include "ui_imgui/OperationalWindow.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <iostream>
#include <type_traits>
#include <variant>

namespace deep::ui_imgui {

ImGuiApp::ImGuiApp()
    : sdl_{"Deep Signal", 1920, 1080}, service_{}, interactions_{service_, [this] {
        // Invoked only after all members are constructed, on actual New/Load
        // success. Hidden workflows are invalidated now, not when next rendered.
        inspectorPanel_.resetWorldState();
        fleetOrdersPanel_.resetWorldState();
        colonyPanel_.resetWorldState();
        processingEditor_.resetWorldState();
        shipyardPanel_.resetWorldState();
        surveyProgramsPanel_.resetWorldState();
        freightProgramsPanel_.resetWorldState();
        maintenanceProgramsPanel_.resetWorldState();
        siteDevelopmentPanel_.resetWorldState();
        technicalDevelopmentPanel_.resetWorldState();
        sciencePanel_.resetWorldState();
        timeControlPanel_.resetWorldState();
        mainMenuBar_.resetWorldState();
        pendingNavigationFocus_.reset();
        lastTemporaryPreview_.reset();
        actionError_.clear();
        actionErrorUntil_ = 0.0;
    }} {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    applyDeepSignalTheme();

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

        const float menuBottom = renderMainMenu();
        const ShellLayout layout = reserveInformationWorkArea(menuBottom);
        renderDockspace(layout);
        setOperationalWorkArea(layout.dock);
        setOperationalDockId(operationalDockId_);
        // ImGui decorations impose a minimum height. Keep the dock node alive
        // while an unusually small/minimized viewport cannot fit normal windows.
        if (layout.dock.width >= ImGui::GetStyle().WindowMinSize.x &&
            layout.dock.height >= std::max(ImGui::GetStyle().WindowMinSize.y, ImGui::GetFrameHeight())) {
            renderPanels();
            if (initialFocus_ || !mainMenuBar_.focusRequest().empty()) {
                const char* target = mainMenuBar_.focusRequest().empty()
                    ? preferredWorkspaceWindow(workspace_) : mainMenuBar_.focusRequest().c_str();
                ImGui::SetWindowFocus(target);
                initialFocus_ = false;
                mainMenuBar_.clearFocusRequest();
            }
        }
        if (layout.drawable()) {
            // Render after all selection producers and synchronous New/Load
            // paths. The projection and every overview DTO are current now.
            const SimulationQueries queries{service_};
            const InformationPanelFrameResult panelResult = informationPanel_.render(
                queries, interactions_.mainSelection(), interactions_,
                {layout.information.x, layout.information.y},
                {layout.information.width, layout.information.height}, informationDockId_);
            const auto displayedTemporary = interactions_.state().temporaryPreview();
            const InformationPreviewFrameResult previewResult =
                previewLayer_.render(queries, interactions_, layout.information, informationDockId_);
            if (displayedTemporary && displayedTemporary != lastTemporaryPreview_) {
                ImGui::SetWindowFocus(informationPreviewWindowName(displayedTemporary->id).c_str());
            }
            lastTemporaryPreview_ = displayedTemporary;
            if (previewResult.goTo) {
                const NavigationResult result = navigation_.goTo(
                    *previewResult.goTo, queries, interactions_, workspace_, visibility_,
                    strategicMapPanel_, colonyPanel_, fleetPanel_);
                if (result.ok()) {
                    actionError_.clear();
                    pendingNavigationFocus_ = previewResult.goTo->displayedTarget;
                } else {
                    actionError_ = result.message;
                    actionErrorUntil_ = ImGui::GetTime() + 5.0;
                }
            }
            const auto openEditor = [&](const ColonyProcessingOpenIntent& intent) {
                const EditorOpenResult opened = processingEditor_.open(intent, queries, interactions_);
                if (!opened.accepted() && !opened.message.empty()) {
                    actionError_ = opened.message;
                    actionErrorUntil_ = ImGui::GetTime() + 5.0;
                }
            };
            // Navigation is processed first. If it closed or retargeted the
            // displayed source, Configure then fails exact-source validation.
            if (panelResult.configureProcessing) openEditor(*panelResult.configureProcessing);
            if (previewResult.configureProcessing) openEditor(*previewResult.configureProcessing);
            processingEditor_.render(queries, service_, interactions_, layout.dock, operationalDockId_);
            if (focusEditorAfterReset_) {
                if (const auto& editor = processingEditor_.current()) {
                    const std::string name = "Configure processing###ColonyProcessingEditor_W" +
                        std::to_string(editor->id.world.value) + "_E" + std::to_string(editor->id.value);
                    ImGui::SetWindowFocus(name.c_str());
                }
                focusEditorAfterReset_ = false;
            }
            renderActionFeedback(layout.dock);
        }

        ImGui::Render();
        sdl_.beginFrame();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl_.renderer());
        sdl_.endFrame();
    }

    return EXIT_SUCCESS;
}

void ImGuiApp::renderDockspace(const ShellLayout& layout) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Preserve the IDs previously generated by DockSpaceOverViewport, so saved
    // docking relationships survive the change to a smaller explicit host.
    char hostName[48];
    std::snprintf(hostName, sizeof(hostName), "WindowOverViewport_%08X", viewport->ID);
    ImGui::SetNextWindowPos({layout.dock.x, layout.dock.y});
    ImGui::SetNextWindowSize({std::max(1.0F, layout.dock.width), std::max(1.0F, layout.dock.height)});
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0.0F, 0.0F});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin(hostName, nullptr, flags);
    operationalDockId_ = ImGui::GetID("DockSpace");
    // The old operational ID is retained. A separate right-side dockspace
    // gives information tabs their own saved arrangement and usable width.
    if (resetLayout_) {
        focusEditorAfterReset_ = processingEditor_.current().has_value();
        ImGui::DockBuilderRemoveNode(operationalDockId_);
    }
    if (!ImGui::DockBuilderGetNode(operationalDockId_)) {
        ImGui::DockBuilderAddNode(operationalDockId_, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(operationalDockId_, {layout.dock.x, layout.dock.y});
        ImGui::DockBuilderSetNodeSize(operationalDockId_, {layout.dock.width, layout.dock.height});
        for (const char* name : {
                "Strategic Map", "Bodies / System", "Economy Forecast", "Shipyard / Production",
                "Colonies", "Sites / Development", "Technical Development", "Fleets",
                "Fleet Orders", "Freight / Supply Programs", "Maintenance / Support Programs",
                "Evidence / Analysis", "Survey Programs", "Event Log", "Save / Load",
                "Time Control", "Legacy Inspector###Inspector"}) {
            ImGui::DockBuilderDockWindow(name, operationalDockId_);
        }
        if (const auto& editor = processingEditor_.current()) {
            const std::string name = "Configure Processing###ColonyProcessingEditor_W" +
                std::to_string(editor->id.world.value) + "_E" + std::to_string(editor->id.value);
            ImGui::DockBuilderDockWindow(name.c_str(), operationalDockId_);
        }
        ImGui::DockBuilderFinish(operationalDockId_);
        initialFocus_ = true;
    }
    // Even a minimized viewport keeps the docking node alive. Zero window sizes
    // mean auto-fit to ImGui, so the invisible keep-alive host uses a 1 px guard.
    const ImGuiDockNodeFlags dockFlags = layout.drawable() ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
    ImGui::DockSpace(operationalDockId_, {0.0F, 0.0F}, dockFlags);
    ImGui::End();
    ImGui::PopStyleVar(4);

    ImGui::SetNextWindowPos({layout.information.x, layout.information.y});
    ImGui::SetNextWindowSize({std::max(1.0F, layout.information.width),
                              std::max(1.0F, layout.information.height)});
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::Begin("##InformationDockHost", nullptr, flags);
    informationDockId_ = ImGui::GetID("InformationDockSpace");
    if (resetLayout_) ImGui::DockBuilderRemoveNode(informationDockId_);
    if (!ImGui::DockBuilderGetNode(informationDockId_)) {
        ImGui::DockBuilderAddNode(informationDockId_, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(informationDockId_, {layout.information.x, layout.information.y});
        ImGui::DockBuilderSetNodeSize(informationDockId_, {layout.information.width, layout.information.height});
        ImGui::DockBuilderDockWindow("Overview###InformationPanel", informationDockId_);
        if (resetLayout_) {
            for (const auto& preview : interactions_.previewSnapshot()) {
                ImGui::DockBuilderDockWindow(informationPreviewWindowName(preview.id).c_str(), informationDockId_);
            }
        }
        ImGui::DockBuilderFinish(informationDockId_);
    }
    ImGui::DockSpace(informationDockId_, {0.0F, 0.0F}, dockFlags);
    ImGui::End();
    ImGui::PopStyleVar();
    resetLayout_ = false;
}

float ImGuiApp::renderMainMenu() {
    interactions_.reconcile();
    const float bottom = mainMenuBar_.render(service_, saveLoadPanel_, interactions_, workspace_, visibility_);
    resetLayout_ = mainMenuBar_.takeLayoutReset();
    return bottom;
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
    colonyPanel_.render(queries, interactions_, visibility_.colonies);
    fleetPanel_.render(queries, interactions_, visibility_.fleets);
    fleetOrdersPanel_.render(queries, service_, interactions_.mainSelection(), visibility_.fleetOrders);
    surveyProgramsPanel_.render(queries, service_, visibility_.surveyPrograms);
    freightProgramsPanel_.render(queries, service_, visibility_.freightPrograms);
    maintenanceProgramsPanel_.render(queries, service_, visibility_.maintenancePrograms);
    siteDevelopmentPanel_.render(queries, service_, visibility_.siteDevelopment);
    technicalDevelopmentPanel_.render(queries, service_, visibility_.technicalDevelopment);
    sciencePanel_.render(queries,service_,visibility_.science);
    economyForecastPanel_.render(forecasts, visibility_.economyForecast);
    eventLogPanel_.render(queries, visibility_.eventLog);
    inspectorPanel_.render(queries, service_, interactions_.mainSelection(), visibility_.inspector);
    focusNavigationDestination();
}

void ImGuiApp::focusNavigationDestination() {
    if (!pendingNavigationFocus_) return;
    if (pendingNavigationFocus_->world != interactions_.world()) {
        pendingNavigationFocus_.reset();
        return;
    }

    const ObjectTarget target = pendingNavigationFocus_->object;
    const SelectionState selected = interactions_.mainSelection();
    const bool stillSelected = std::visit([&](const auto id) {
        using Id = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<Id, BodyId>) return selected.isBodySelected(id);
        else if constexpr (std::is_same_v<Id, ColonyId>) return selected.isColonySelected(id);
        else return selected.isFleetSelected(id);
    }, target);
    if (!stillSelected) {
        pendingNavigationFocus_.reset();
        return;
    }

    if (std::holds_alternative<BodyId>(target) && visibility_.strategicMap) {
        ImGui::SetWindowFocus("Strategic Map");
    } else if (std::holds_alternative<ColonyId>(target) && visibility_.colonies) {
        ImGui::SetWindowFocus("Colonies");
    } else if (std::holds_alternative<FleetId>(target) && visibility_.fleets) {
        ImGui::SetWindowFocus("Fleets");
    }
    pendingNavigationFocus_.reset();
}

void ImGuiApp::renderActionFeedback(const ShellRegion workArea) {
    if (actionError_.empty()) return;
    if (ImGui::GetTime() >= actionErrorUntil_) {
        actionError_.clear();
        return;
    }
    const float width = std::min(420.0F, workArea.width - 24.0F);
    if (width < 160.0F || workArea.height < 100.0F) return;

    ImGui::SetNextWindowPos({workArea.x + 12.0F, workArea.y + workArea.height - 96.0F});
    ImGui::SetNextWindowSize({width, 84.0F});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("Action feedback###InformationActionFeedback", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.92F, 0.70F, 0.37F, 1.0F});
        ImGui::TextWrapped("%s", actionError_.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Dismiss##action_error")) actionError_.clear();
    }
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
