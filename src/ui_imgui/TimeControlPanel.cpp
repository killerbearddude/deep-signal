#include "ui_imgui/TimeControlPanel.h"

// Implements time controls for the first functional ImGui shell.
// Mutations are deliberately routed through SimulationService::execute so the UI
// cannot bypass command validation or future audit behavior.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

namespace deep::ui_imgui {

void TimeControlPanel::render(SimulationService& service, bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Time Control", &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Advance simulation time");
    ImGui::Separator();

    if (ImGui::Button("Advance 1 day")) {
        advance(service, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Advance 5 days")) {
        advance(service, 5);
    }
    ImGui::SameLine();
    if (ImGui::Button("Advance 30 days")) {
        advance(service, 30);
    }

    ImGui::Separator();
    ImGui::Text("Last command: %s", lastCommandMessage_.c_str());
    ImGui::Text("Status: %s", lastCommandSucceeded_ ? "OK" : "Rejected");

    ImGui::End();
}

void TimeControlPanel::advance(SimulationService& service, const int days) {
    const CommandResult result = service.execute(AdvanceDaysCommand{.days = days});
    lastCommandSucceeded_ = result.ok;
    lastCommandMessage_ = result.message.empty() ? "Simulation advanced" : result.message;
}

} // namespace deep::ui_imgui
