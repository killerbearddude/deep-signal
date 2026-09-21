#include "ui_imgui/MainMenuBar.h"

// Implements application-level ImGui menu commands.
// Persistence actions still flow through SimulationService via SaveLoadPanel,
// while Workspace presets and manual View toggles compose existing panels
// without coupling them to global state.

#include <imgui.h>

namespace deep::ui_imgui {

void applyWorkspace(Workspace workspace, PanelVisibility& visibility) {
    // Clear every operational panel so manual View changes cannot leak into the
    // next preset. The two global windows are controlled only through View.
    visibility.strategicMap = false;
    visibility.bodies = false;
    visibility.inspector = false;
    visibility.colonies = false;
    visibility.fleets = false;
    visibility.fleetOrders = false;
    visibility.shipyard = false;
    visibility.economyForecast = false;
    visibility.eventLog = false;

    switch (workspace) {
    case Workspace::System:
        visibility.strategicMap = true;
        visibility.bodies = true;
        visibility.inspector = true;
        break;
    case Workspace::Economy:
        visibility.economyForecast = true;
        visibility.inspector = true;
        break;
    case Workspace::Production:
        visibility.shipyard = true;
        visibility.colonies = true;
        visibility.economyForecast = true;
        break;
    case Workspace::Fleets:
        visibility.strategicMap = true;
        visibility.fleets = true;
        visibility.fleetOrders = true;
        visibility.inspector = true;
        break;
    case Workspace::Intelligence:
        // Exploration intelligence and Resource Survey remain in these panels
        // until the later workspace consolidation patches.
        visibility.strategicMap = true;
        visibility.bodies = true;
        visibility.fleetOrders = true;
        visibility.inspector = true;
        break;
    case Workspace::History:
        visibility.eventLog = true;
        visibility.inspector = true;
        break;
    }
}

void MainMenuBar::render(SimulationService& service, SaveLoadPanel& saveLoadPanel,
                         Workspace& workspace, PanelVisibility& visibility) const {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Game")) {
            saveLoadPanel.newGame(service);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save")) {
            saveLoadPanel.save(service);
        }
        if (ImGui::MenuItem("Load")) {
            saveLoadPanel.load(service);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Workspace")) {
        constexpr struct {
            Workspace workspace;
            const char* label;
        } workspaces[] = {
            {Workspace::System, "System"},
            {Workspace::Economy, "Economy"},
            {Workspace::Production, "Production"},
            {Workspace::Fleets, "Fleets"},
            {Workspace::Intelligence, "Intelligence"},
            {Workspace::History, "History"}
        };

        for (const auto& entry : workspaces) {
            // The active item stays clickable to restore its preset after a
            // manual View change or a panel being closed.
            if (ImGui::MenuItem(entry.label, nullptr, workspace == entry.workspace)) {
                workspace = entry.workspace;
                applyWorkspace(workspace, visibility);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        // MenuItem stores directly into the shared visibility flags, making a
        // closed panel immediately reopenable without each panel knowing about
        // the main menu.
        ImGui::MenuItem("Save / Load", nullptr, &visibility.saveLoad);
        ImGui::MenuItem("Time Control", nullptr, &visibility.timeControl);
        ImGui::MenuItem("Strategic Map", nullptr, &visibility.strategicMap);
        ImGui::MenuItem("Bodies / System", nullptr, &visibility.bodies);
        ImGui::MenuItem("Inspector", nullptr, &visibility.inspector);
        ImGui::MenuItem("Colonies", nullptr, &visibility.colonies);
        ImGui::MenuItem("Fleets", nullptr, &visibility.fleets);
        ImGui::MenuItem("Fleet Orders", nullptr, &visibility.fleetOrders);
        ImGui::MenuItem("Shipyard / Production", nullptr, &visibility.shipyard);
        ImGui::MenuItem("Economy Forecast", nullptr, &visibility.economyForecast);
        ImGui::MenuItem("Event Log", nullptr, &visibility.eventLog);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

} // namespace deep::ui_imgui
