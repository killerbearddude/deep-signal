#include "ui_imgui/InformationRelationshipRows.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace deep::ui_imgui {
namespace {

constexpr ImVec4 kAccent{0.49F, 0.82F, 0.79F, 1.0F};
constexpr ImVec4 kMuted{0.60F, 0.71F, 0.76F, 1.0F};
constexpr ImVec4 kRowDivider{0.20F, 0.34F, 0.38F, 0.65F};
constexpr ImVec4 kInspect{0.11F, 0.27F, 0.30F, 1.0F};
constexpr ImVec4 kInspectHovered{0.16F, 0.36F, 0.38F, 1.0F};
constexpr ImVec4 kInspectActive{0.21F, 0.44F, 0.44F, 1.0F};

void wrappedText(const std::string_view value) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopTextWrapPos();
}

[[nodiscard]] const char* roleCaption(const RelationshipRole role) {
    switch (role) {
    case RelationshipRole::ParentBody: return "Parent body";
    case RelationshipRole::Body: return "Body";
    case RelationshipRole::Colony: return "Colony";
    case RelationshipRole::StationedFleet: return "Stationed fleet";
    case RelationshipRole::CurrentBody: return "Current body";
    case RelationshipRole::DepartureBody: return "Departure body";
    case RelationshipRole::DestinationBody: return "Destination";
    }
    return "Related object";
}

// Names are never used for ImGui identity. Typed ID, relationship role, and
// originating world keep a button stable through renames and distinguish ID
// reuse across a successful New/Load. The adapter rejects stale clicks; each
// window supplies its own ImGui ID scope.
[[nodiscard]] std::string rowIdentity(const InformationRelationship& row) {
    const std::int64_t objectId = std::visit([](const auto id) { return id.value; },
                                             row.target.object);
    return "relationship_" + std::to_string(row.target.world.value) + '_' +
        std::to_string(static_cast<int>(row.role)) + '_' +
        std::to_string(row.target.object.index()) + '_' + std::to_string(objectId);
}

void sectionHeading(const std::string_view section) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(section.data(), section.data() + section.size());
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void renderGroup(const std::vector<InformationRelationship>& relationships,
                 const std::size_t begin, const std::size_t end,
                 std::vector<ObjectReference>& inspectionRequests) {
    // Group boundaries and row identities are independent of presentation
    // names. A fixed-width action column works in the compact preview window.
    // Section identity stays the same when a preceding relationship appears
    // or disappears; row controls retain their own typed, world-scoped IDs.
    const std::string tableId = "##relationship_rows_" + relationships[begin].section;
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, kRowDivider);
    if (!ImGui::BeginTable(tableId.c_str(), 2,
                           ImGuiTableFlags_SizingStretchProp |
                           ImGuiTableFlags_BordersInnerH |
                           ImGuiTableFlags_NoSavedSettings |
                           ImGuiTableFlags_NoPadOuterX)) {
        ImGui::PopStyleColor();
        return;
    }
    ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Inspect", ImGuiTableColumnFlags_WidthFixed, 82.0F);
    for (std::size_t i = begin; i < end; ++i) {
        const auto& row = relationships[i];
        const std::string identity = rowIdentity(row);
        ImGui::PushID(identity.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (row.section == "RELATIONSHIPS") {
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextUnformatted(roleCaption(row.role));
            ImGui::PopStyleColor();
        }
        wrappedText(row.name.empty() ? std::string_view{"Unknown"} : std::string_view{row.name});
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        wrappedText(row.typeLabel);
        ImGui::PopStyleColor();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Button, kInspect);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kInspectHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kInspectActive);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        if (ImGui::SmallButton("Inspect >")) {
            inspectionRequests.push_back(row.target);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopStyleColor();
}

} // namespace

void renderInformationRelationshipRows(
    const std::vector<InformationRelationship>& relationships,
    std::vector<ObjectReference>& inspectionRequests) {
    for (std::size_t begin = 0; begin < relationships.size();) {
        std::size_t end = begin + 1;
        while (end < relationships.size() && relationships[end].section == relationships[begin].section) {
            ++end;
        }
        sectionHeading(relationships[begin].section);
        renderGroup(relationships, begin, end, inspectionRequests);
        begin = end;
    }
}

} // namespace deep::ui_imgui
