#include "ui_imgui/ColonyPanel.h"

// Implements the first read-only colony table for the ImGui shell.
// It displays copied query DTO fields only, preserving the app/sim boundary.

#include <imgui.h>

#include <vector>

namespace deep::ui_imgui {

void ColonyPanel::render(const SimulationQueries& queries) const {
    ImGui::Begin("Colonies");

    const std::vector<ColonySummary> colonies = queries.colonies();
    ImGui::Text("Colonies: %zu", colonies.size());

    if (ImGui::BeginTable("ColonySummaryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Mines");
        ImGui::TableSetupColumn("Shipyard BP/day");
        ImGui::TableHeadersRow();

        for (const ColonySummary& colony : colonies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(colony.id.value));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(colony.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(colony.bodyName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", colony.mines);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f", colony.shipyardCapacity);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
