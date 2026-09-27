#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/OperationalWindow.h"

// The table reads owned ColonySummary rows each frame. It has no processing
// draft, command authority, or fallback editor target when selection is empty.

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

constexpr ImGuiTableFlags kColonyTableFlags = ImGuiTableFlags_Borders |
                                             ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_Resizable |
                                             ImGuiTableFlags_Reorderable |
                                             ImGuiTableFlags_Hideable |
                                             ImGuiTableFlags_SizingStretchProp;

[[nodiscard]] std::string rowId(const ColonySummary& colony) {
    return "##colony_row_" + std::to_string(colony.id.value);
}

} // namespace

void ColonyPanel::requestReveal(const ObjectReference target) {
    pendingReveal_ = target;
}

void ColonyPanel::render(const SimulationQueries& queries,
                         InformationInteractionAdapter& interactions,
                         bool& visible) {
    const auto displayedWorld = interactions.world();
    if (pendingReveal_ && pendingReveal_->world != displayedWorld) pendingReveal_.reset();
    if (!visible) return;

    const std::vector<ColonySummary> colonies = queries.colonies();
    std::optional<ColonyId> revealRow;
    if (pendingReveal_) {
        if (const auto* colonyId = std::get_if<ColonyId>(&pendingReveal_->object)) {
            const auto it = std::find_if(colonies.begin(), colonies.end(), [colonyId](const ColonySummary& colony) {
                return colony.id == *colonyId;
            });
            if (it != colonies.end()) revealRow = *colonyId;
        }
        if (!revealRow) pendingReveal_.reset();
        else {
            ImGui::SetNextWindowCollapsed(false);
            ImGui::SetNextWindowFocus();
        }
    }

    if (!beginOperationalWindow("Colonies", &visible)) {
        ImGui::End();
        return;
    }

    auto selection = interactions.mainSelection();
    ImGui::Text("Colonies: %zu", colonies.size());
    if (ImGui::BeginTable("ColonySummaryTable", 9, kColonyTableFlags)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Mines");
        ImGui::TableSetupColumn("Processors/day");
        ImGui::TableSetupColumn("Policy");
        ImGui::TableSetupColumn("Shipyard BP/day");
        ImGui::TableSetupColumn("Raw Stockpile");
        ImGui::TableSetupColumn("Processed Stockpile");
        ImGui::TableHeadersRow();

        for (const ColonySummary& colony : colonies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(rowId(colony).c_str(), selection.isColonySelected(colony.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                (void)interactions.select({displayedWorld, ObjectTarget{colony.id}});
                selection = interactions.mainSelection();
            }
            if (revealRow == colony.id) {
                ImGui::SetScrollHereY(0.5F);
                pendingReveal_.reset();
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
            ImGui::Text("%.2f", colony.processorCapacity);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(colony.processingPolicyName.c_str());
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.2f", colony.shipyardCapacity);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.1f", colony.totalRawStockpile);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%.1f", colony.totalProcessedStockpile);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace deep::ui_imgui
