#include "ui_imgui/InspectorPanel.h"
#include "ui_imgui/OperationalWindow.h"

// Implements the shared inspector panel plus the first small command workflow.
// All detail lookups walk copied query DTOs, so the inspector never retains
// references into GameState and cannot mutate simulation state directly.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
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
    const std::optional<StrategicBodySummary> body = queries.strategicBody(bodyId);
    if (!body.has_value()) {
        ImGui::Text("Body #%lld was not found in the current query snapshot.", static_cast<long long>(bodyId.value));
        return;
    }

    ImGui::Text("ID: %lld", static_cast<long long>(body->id.value));
    ImGui::Text("Name: %s", body->name.c_str());
    ImGui::Text("Type: %s", body->typeName.c_str());
    ImGui::Text("Strategic zone: %s", body->strategicZoneName.c_str());
    ImGui::Text("Owner / institution: %s", body->ownerInstitutionName.empty() ? "-" : body->ownerInstitutionName.c_str());
    ImGui::Text("Position: %.2f, %.2f", body->x, body->y);

    const std::vector<BodyDepositSummary> deposits = queries.bodyDeposits(bodyId);
    if (!deposits.empty()) {
        ImGui::Separator();
        ImGui::Text("Deposits");
        if (ImGui::BeginTable("InspectorBodyDeposits", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Mineral");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Confidence");
            ImGui::TableSetupColumn("Confirmed");
            ImGui::TableSetupColumn("Estimated");
            ImGui::TableSetupColumn("Uncertain");
            ImGui::TableSetupColumn("Accessibility");
            ImGui::TableSetupColumn("Strategic relevance");
            ImGui::TableHeadersRow();

            for (const BodyDepositSummary& deposit : deposits) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(deposit.mineralName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(deposit.surveyStateName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.0f%%", deposit.confidence * 100.0);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.0f", deposit.confirmedQuantity);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.0f", deposit.estimatedQuantity);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.0f", deposit.uncertainQuantity);
                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%.2f", deposit.accessibility);
                ImGui::TableSetColumnIndex(7);
                ImGui::TextWrapped("%s", deposit.strategicRelevance.c_str());
            }

            ImGui::EndTable();
        }
    }

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
    const std::optional<FleetSummary> fleet = queries.fleet(fleetId);
    if (!fleet.has_value()) {
        ImGui::Text("Fleet #%lld was not found in the current query snapshot.", static_cast<long long>(fleetId.value));
        return;
    }

    ImGui::Text("ID: %lld", static_cast<long long>(fleet->id.value));
    ImGui::Text("Name: %s", fleet->name.c_str());
    ImGui::Text("Origin/current body: %s", fleet->currentBodyName.c_str());
    ImGui::Text("Ships: %zu", fleet->shipCount);
    ImGui::Text("Fuel: %.1f / %.1f (range %.1f)", fleet->currentFuel, fleet->fuelCapacity, fleet->currentRange);
    ImGui::Text("Order: %s", fleet->activeOrderName.c_str());
    ImGui::Text("Destination: %s", fleet->destinationBodyName.empty() ? "-" : fleet->destinationBodyName.c_str());
    ImGui::Text("Days remaining: %d", fleet->daysRemaining);
}

[[nodiscard]] std::string fleetLabel(const SimulationQueries& queries, const FleetId fleetId) {
    const std::optional<FleetSummary> fleet = queries.fleet(fleetId);
    if (!fleet.has_value()) {
        return "Fleet #" + std::to_string(fleetId.value) + " <missing>";
    }

    return fleet->name + " (#" + std::to_string(fleet->id.value) + ")";
}

void drawCommandStatus(const char* label, const bool succeeded, const std::string& status) {
    ImGui::Text("%s: %s", label, succeeded ? "OK" : "Error");
    ImGui::TextWrapped("%s", status.c_str());
}


} // namespace

void InspectorPanel::render(const SimulationQueries& queries,
                            SimulationService& service,
                            const SelectionState& selection,
                            bool& visible) {
    if (!visible) {
        return;
    }

    if (!beginOperationalWindow("Inspector", &visible)) {
        ImGui::End();
        return;
    }

    if (selection.type() == SelectedObjectType::Fleet) {
        // Selecting a fleet arms it as the source for the next body-click move
        // command. Body selection can then focus the destination without losing
        // the chosen source fleet.
        fleetMoveSource_ = selection.fleetId();
    }

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

    ImGui::Separator();
    ImGui::TextUnformatted("Fleet Movement");

    const std::optional<FleetSummary> sourceFleet = fleetMoveSource_.has_value()
        ? queries.fleet(*fleetMoveSource_)
        : std::optional<FleetSummary>{};
    if (sourceFleet.has_value()) {
        ImGui::Text("Fleet: %s", fleetLabel(queries, sourceFleet->id).c_str());
    } else {
        ImGui::TextUnformatted("Fleet: select a fleet from the map or fleet table.");
    }

    const bool bodySelected = selection.type() == SelectedObjectType::Body;
    const std::optional<StrategicBodySummary> destinationBody = bodySelected
        ? queries.strategicBody(selection.bodyId())
        : std::optional<StrategicBodySummary>{};
    if (destinationBody.has_value()) {
        ImGui::Text("Destination body: %s (#%lld)", destinationBody->name.c_str(),
                    static_cast<long long>(destinationBody->id.value));
    } else {
        ImGui::TextUnformatted("Destination body: select a body marker on the map.");
    }

    const std::optional<FleetMovePreview> movePreview = sourceFleet.has_value() && destinationBody.has_value()
        ? queries.fleetMovePreview(sourceFleet->id, destinationBody->id)
        : std::optional<FleetMovePreview>{};
    const bool destinationIsCurrent = sourceFleet.has_value() && destinationBody.has_value()
        && sourceFleet->currentBodyId == destinationBody->id;
    const bool hasFuelForMove = !movePreview.has_value() || movePreview->canAfford;
    // These UI checks are advisory; MoveFleetCommand still rejects an active
    // fleet. Unlike the Fleet Orders panel, this action requests an immediate
    // move and does not append a leg to the queue.
    const bool canMove = sourceFleet.has_value() && destinationBody.has_value() && !destinationIsCurrent && hasFuelForMove;

    if (movePreview.has_value()) {
        ImGui::Text("Move fuel cost: %.1f; fuel after queued route: %.1f",
                    movePreview->newMoveFuelCost, movePreview->projectedFuelRemaining);
        if (!movePreview->canAfford) {
            ImGui::TextUnformatted("Warning: insufficient fuel for this move.");
        }
    }

    if (!canMove) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Move Fleet Here")) {
        const CommandResult result = service.execute(MoveFleetCommand{
            .fleetId = sourceFleet->id,
            .destinationBodyId = destinationBody->id
        });
        lastMoveSucceeded_ = result.ok;
        lastMoveStatus_ = result.message;
    }

    if (!canMove) {
        ImGui::EndDisabled();
    }

    if (destinationIsCurrent) {
        ImGui::TextUnformatted("Destination is current body.");
    } else if (!hasFuelForMove) {
        ImGui::TextUnformatted("Insufficient fuel.");
    }

    drawCommandStatus("Move command", lastMoveSucceeded_, lastMoveStatus_);

    ImGui::Separator();
    ImGui::TextUnformatted("Fleet Order Cancellation");

    const std::optional<FleetSummary> cancelFleet = selection.type() == SelectedObjectType::Fleet
        ? queries.fleet(selection.fleetId())
        : std::optional<FleetSummary>{};
    const bool canCancel = cancelFleet.has_value() && cancelFleet->hasActiveOrder;
    if (!canCancel) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Cancel Fleet Order")) {
        const CommandResult result = service.execute(CancelFleetOrderCommand{
            .fleetId = selection.fleetId()
        });
        lastCancelSucceeded_ = result.ok;
        lastCancelStatus_ = result.message;
    }

    if (!canCancel) {
        ImGui::EndDisabled();
    }

    drawCommandStatus("Cancel command", lastCancelSucceeded_, lastCancelStatus_);

    ImGui::End();
}

} // namespace deep::ui_imgui
