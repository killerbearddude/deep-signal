#include "ui_imgui/InspectorPanel.h"

// Implements the shared read-only inspector panel.
// All lookups walk copied query DTOs, so the inspector never retains references
// into GameState and cannot mutate simulation state directly.

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace deep::ui_imgui {
namespace {

template <typename Summary, typename IdT>
[[nodiscard]] std::optional<Summary> findById(const std::vector<Summary>& summaries, const IdT id) {
    const auto it = std::find_if(summaries.begin(), summaries.end(), [id](const Summary& summary) {
        return summary.id == id;
    });
    return it == summaries.end() ? std::optional<Summary>{} : std::optional<Summary>{*it};
}

[[nodiscard]] const char* selectedTypeName(const SelectedObjectType type) noexcept {
    switch (type) {
    case SelectedObjectType::None:
        return "None";
    case SelectedObjectType::Body:
        return "Body";
    case SelectedObjectType::Colony:
        return "Colony";
    case SelectedObjectType::Fleet:
        return "Fleet";
    }

    return "Unknown";
}

void drawBodyDetails(const SimulationQueries& queries, const BodyId bodyId) {
    const std::optional<StrategicBodySummary> body = findById(queries.strategicBodies(), bodyId);
    if (!body.has_value()) {
        ImGui::Text("Body #%lld was not found in the current query snapshot.", static_cast<long long>(bodyId.value));
        return;
    }

    ImGui::Text("ID: %lld", static_cast<long long>(body->id.value));
    ImGui::Text("Name: %s", body->name.c_str());
    ImGui::Text("Type: %s", body->typeName.c_str());
    ImGui::Text("Position: %.2f, %.2f", body->x, body->y);

    // Surface the first colony tied to the selected body as navigation context;
    // this is read-only and does not imply body-owned colony lifetime.
    const std::vector<ColonySummary> colonies = queries.colonies();
    const auto colonyIt = std::find_if(colonies.begin(), colonies.end(), [bodyId](const ColonySummary& colony) {
        return colony.bodyId == bodyId;
    });
    if (colonyIt != colonies.end()) {
        ImGui::Separator();
        ImGui::Text("Colony: %s", colonyIt->name.c_str());
        ImGui::Text("Mines: %.2f", colonyIt->mines);
        ImGui::Text("Shipyard BP/day: %.2f", colonyIt->shipyardCapacity);
    }
}

void drawColonyDetails(const SimulationQueries& queries, const ColonyId colonyId) {
    const std::optional<ColonySummary> colony = findById(queries.colonies(), colonyId);
    if (!colony.has_value()) {
        ImGui::Text("Colony #%lld was not found in the current query snapshot.", static_cast<long long>(colonyId.value));
        return;
    }

    ImGui::Text("ID: %lld", static_cast<long long>(colony->id.value));
    ImGui::Text("Name: %s", colony->name.c_str());
    ImGui::Text("Body: %s", colony->bodyName.c_str());
    ImGui::Text("Body ID: %lld", static_cast<long long>(colony->bodyId.value));
    ImGui::Text("Mines: %.2f", colony->mines);
    ImGui::Text("Shipyard BP/day: %.2f", colony->shipyardCapacity);
}

void drawFleetDetails(const SimulationQueries& queries, const FleetId fleetId) {
    const std::optional<FleetSummary> fleet = findById(queries.fleets(), fleetId);
    if (!fleet.has_value()) {
        ImGui::Text("Fleet #%lld was not found in the current query snapshot.", static_cast<long long>(fleetId.value));
        return;
    }

    ImGui::Text("ID: %lld", static_cast<long long>(fleet->id.value));
    ImGui::Text("Name: %s", fleet->name.c_str());
    ImGui::Text("Location: %s", fleet->currentBodyName.c_str());
    ImGui::Text("Ships: %zu", fleet->shipCount);
    ImGui::Text("Order: %s", fleet->activeOrderName.c_str());
    ImGui::Text("Destination: %s", fleet->destinationBodyName.empty() ? "-" : fleet->destinationBodyName.c_str());
    ImGui::Text("Days remaining: %d", fleet->daysRemaining);
}

} // namespace

void InspectorPanel::render(const SimulationQueries& queries, const SelectionState& selection) const {
    ImGui::Begin("Inspector");

    ImGui::Text("Selected type: %s", selectedTypeName(selection.type()));
    if (selection.type() != SelectedObjectType::None) {
        ImGui::Text("Selected ID: %lld", static_cast<long long>(selection.selectedId()));
    }
    ImGui::Separator();

    switch (selection.type()) {
    case SelectedObjectType::None:
        ImGui::TextUnformatted("Select a body, colony, or fleet from the map or tables.");
        break;
    case SelectedObjectType::Body:
        drawBodyDetails(queries, selection.bodyId());
        break;
    case SelectedObjectType::Colony:
        drawColonyDetails(queries, selection.colonyId());
        break;
    case SelectedObjectType::Fleet:
        drawFleetDetails(queries, selection.fleetId());
        break;
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
