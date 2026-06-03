#include "ui_imgui/FleetPanel.h"

// Implements a read-only fleet table for the ImGui shell.
// Row clicks update shared SelectionState; the inspector remembers the selected
// fleet as the source for the first move-order workflow.

#include <imgui.h>

#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string rowId(const FleetSummary& fleet) {
    return "##fleet_row_" + std::to_string(fleet.id.value);
}

} // namespace

void FleetPanel::render(const SimulationQueries& queries, SelectionState& selection, bool& visible) const {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Fleets", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<FleetSummary> fleets = queries.fleets();
    ImGui::Text("Fleets: %zu", fleets.size());

    if (ImGui::BeginTable("FleetSummaryTable", 11, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Origin");
        ImGui::TableSetupColumn("Destination");
        ImGui::TableSetupColumn("Ships");
        ImGui::TableSetupColumn("Fuel");
        ImGui::TableSetupColumn("Range");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Active ETA");
        ImGui::TableSetupColumn("Queued");
        ImGui::TableSetupColumn("Route Days");
        ImGui::TableHeadersRow();

        for (const FleetSummary& fleet : fleets) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(fleet).c_str(), selection.isFleetSelected(fleet.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
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
            ImGui::Text("%.1f / %.1f", fleet.currentFuel, fleet.fuelCapacity);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.1f", fleet.currentRange);
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(fleet.activeOrderName.c_str());
            ImGui::TableSetColumnIndex(8);
            if (fleet.activeOrderEtaDays.has_value()) {
                ImGui::Text("%d", *fleet.activeOrderEtaDays);
            } else {
                ImGui::TextUnformatted("-");
            }
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%zu", fleet.queuedOrders.size());
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("%d", fleet.totalRouteDurationDays);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
