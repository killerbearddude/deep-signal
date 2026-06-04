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

    if (ImGui::BeginTable("BodySystemTable", 7, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("Owner / Institution");
        ImGui::TableSetupColumn("Colonies");
        ImGui::TableSetupColumn("Mineral Deposits");
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
            ImGui::TextUnformatted(body.ownerInstitutionName.empty() ? "-" : body.ownerInstitutionName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu", body.colonyCount);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%zu", body.mineralDepositCount);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%zu", body.fleetCount);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
