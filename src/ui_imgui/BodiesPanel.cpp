#include "ui_imgui/BodiesPanel.h"

// Implements the read-only Bodies/System overview panel.
// The table remains a query DTO consumer only; no simulation mutation is allowed
// from this view.

#include <imgui.h>

#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string rowId(const BodySystemSummary& body) {
    return "##body-system-row-" + std::to_string(body.id.value);
}

constexpr ImGuiTableFlags kBodyTableFlags = ImGuiTableFlags_Borders |
                                            ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_Resizable |
                                            ImGuiTableFlags_Reorderable |
                                            ImGuiTableFlags_Hideable |
                                            ImGuiTableFlags_SizingStretchProp;

} // namespace

void BodiesPanel::render(const SimulationQueries& queries, SelectionState& selection, bool& visible) const {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Bodies / System", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<BodySystemSummary> bodies = queries.bodySystemOverview();
    ImGui::Text("Bodies: %zu", bodies.size());

    if (ImGui::BeginTable("BodySystemTable", 12, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("Parent");
        ImGui::TableSetupColumn("Orbit Radius");
        ImGui::TableSetupColumn("Period");
        ImGui::TableSetupColumn("Owner / Institution");
        ImGui::TableSetupColumn("Colonies");
        ImGui::TableSetupColumn("Deposits");
        ImGui::TableSetupColumn("Known / Est / Unknown");
        ImGui::TableSetupColumn("Confirmed / Est / Uncertain");
        ImGui::TableSetupColumn("Fleets");
        ImGui::TableHeadersRow();

        for (const BodySystemSummary& body : bodies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(body).c_str(), selection.isBodySelected(body.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selection.selectBody(body.id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(body.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(body.typeName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(body.strategicZoneName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(body.parentBodyName.empty() ? "-" : body.parentBodyName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1fM km", body.orbitalRadiusKm / 1'000'000.0);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.1f d", body.orbitalPeriodDays);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(body.ownerInstitutionName.empty() ? "-" : body.ownerInstitutionName.c_str());
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%zu", body.colonyCount);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%zu", body.mineralDepositCount);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%zu / %zu / %zu", body.knownDepositCount, body.estimatedDepositCount, body.unknownDepositCount);
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("%.0f / %.0f / %.0f", body.confirmedDepositQuantity, body.estimatedDepositQuantity, body.uncertainDepositQuantity);
            ImGui::TableSetColumnIndex(11);
            ImGui::Text("%zu", body.fleetCount);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
