#include "ui_imgui/MainMenuBar.h"

// Implements the first application-level ImGui menu commands.
// Persistence actions still flow through SimulationService via SaveLoadPanel,
// preventing the UI from depending on save repository implementation details.

#include <imgui.h>

namespace deep::ui_imgui {

void MainMenuBar::render(SimulationService& service, SaveLoadPanel& saveLoadPanel) const {
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

    ImGui::EndMainMenuBar();
}

} // namespace deep::ui_imgui
