#include "ui_imgui/MainMenuBar.h"

// Implements application-level ImGui menu commands.
// Persistence actions still flow through SimulationService via SaveLoadPanel,
// and the View menu toggles dock-window visibility without coupling panels to
// global state.

#include <imgui.h>

namespace deep::ui_imgui {

void MainMenuBar::render(SimulationService& service, SaveLoadPanel& saveLoadPanel, PanelVisibility& visibility) const {
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
