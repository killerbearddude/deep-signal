#include "ui_imgui/ColonyPanel.h"

// Implements the read-only colony table for the ImGui shell.
// Row clicks update shared SelectionState, but displayed data still comes only
// from copied app-layer query DTOs.

#include <imgui.h>

#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string rowId(const ColonySummary& colony) {
    return "##colony_row_" + std::to_string(colony.id.value);
}

} // namespace

void ColonyPanel::render(const SimulationQueries& queries, SelectionState& selection) const {
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

            // A hidden-label selectable spans the full row while keeping the ID
            // text visible and stable for table sorting/inspection later.
            if (ImGui::Selectable(rowId(colony).c_str(), selection.isColonySelected(colony.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selection.selectColony(colony.id);
            }
            ImGui::SameLine();
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
