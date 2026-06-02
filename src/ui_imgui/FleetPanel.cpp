#include "ui_imgui/FleetPanel.h"

// Implements a read-only fleet table for the ImGui shell.
// Row clicks update shared SelectionState; order edits remain out of scope for
// this patch and will continue to go through commands when added later.

#include <imgui.h>

#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string rowId(const FleetSummary& fleet) {
    return "##fleet_row_" + std::to_string(fleet.id.value);
}

} // namespace

void FleetPanel::render(const SimulationQueries& queries, SelectionState& selection) const {
    ImGui::Begin("Fleets");

    const std::vector<FleetSummary> fleets = queries.fleets();
    ImGui::Text("Fleets: %zu", fleets.size());

    if (ImGui::BeginTable("FleetSummaryTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Location");
        ImGui::TableSetupColumn("Destination");
        ImGui::TableSetupColumn("Ships");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Days Left");
        ImGui::TableHeadersRow();

        for (const FleetSummary& fleet : fleets) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(fleet).c_str(), selection.isFleetSelected(fleet.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                selection.selectFleet(fleet.id);
            }
            ImGui::SameLine();
            ImGui::Text("%lld", static_cast<long long>(fleet.id.value));

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(fleet.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fleet.currentBodyName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(fleet.destinationBodyName.empty() ? "-" : fleet.destinationBodyName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu", fleet.shipCount);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(fleet.activeOrderName.c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d", fleet.daysRemaining);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
