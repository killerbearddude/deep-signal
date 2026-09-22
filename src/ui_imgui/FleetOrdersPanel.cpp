#include "ui_imgui/FleetOrdersPanel.h"
#include "ui_imgui/OperationalWindow.h"

// Implements a dedicated fleet command surface for the ImGui workstation.
// The panel exposes the current order and a small queued-order list while
// keeping all mutation behind SimulationService commands.

#include "sim/Commands.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string bodyComboLabel(const StrategicBodySummary& body) {
    return body.name + " (#" + std::to_string(body.id.value) + ")";
}

[[nodiscard]] std::string selectedDestinationLabel(const SimulationQueries& queries,
                                                   const std::optional<BodyId> destinationBodyId) {
    if (!destinationBodyId.has_value()) {
        return "Select destination body";
    }

    const std::optional<StrategicBodySummary> body = queries.strategicBody(*destinationBodyId);
    return body.has_value() ? bodyComboLabel(*body) : std::string{"<missing destination>"};
}

void drawFleetSummary(const FleetSummary& fleet) {
    ImGui::Text("Fleet: %s (#%lld)", fleet.name.c_str(), static_cast<long long>(fleet.id.value));
    ImGui::Text("Current body: %s", fleet.currentBodyName.c_str());
    ImGui::Text("Ships: %zu", fleet.shipCount);
    ImGui::Text("Fuel: %.1f / %.1f (%.1f%%)", fleet.currentFuel, fleet.fuelCapacity, fleet.fuelPercent);
    ImGui::Text("Current range: %.1f map unit(s)", fleet.currentRange);
}

void drawCurrentOrder(const FleetSummary& fleet) {
    ImGui::SeparatorText("Current Order");
    if (!fleet.hasActiveOrder) {
        ImGui::TextUnformatted("None");
        return;
    }

    ImGui::Text("Order: %s", fleet.activeOrderName.c_str());
    ImGui::Text("Destination: %s", fleet.destinationBodyName.empty() ? "-" : fleet.destinationBodyName.c_str());
    ImGui::Text("ETA: %d day(s)", fleet.activeOrderEtaDays.value_or(fleet.daysRemaining));
    ImGui::Text("Route type: %s", fleet.activeOrderRouteVisualStyleName.empty()
        ? "-"
        : fleet.activeOrderRouteVisualStyleName.c_str());
    ImGui::Text("Projected arrival day: %lld", static_cast<long long>(fleet.activeOrderProjectedArrivalDay));
    ImGui::Text("Transit distance: %.1f million km", fleet.activeOrderTransitDistanceKm / 1'000'000.0);
    ImGui::Text("Burn acceleration: %.3f g", fleet.activeOrderBurnAccelerationG);
    ImGui::Text("Burn phase: %s", fleet.activeOrderBurnPhase.empty() ? "-" : fleet.activeOrderBurnPhase.c_str());
}

void drawTimelinePreview(const FleetSummary& fleet) {
    ImGui::SeparatorText("Timeline Preview");

    if (!fleet.hasActiveOrder && fleet.queuedOrders.empty()) {
        ImGui::TextUnformatted("No active or queued orders for this fleet.");
        return;
    }

    if (fleet.queuedOrders.empty()) {
        ImGui::TextUnformatted("Queue is empty; only the current order will execute.");
    }

    ImGui::Text("Total route duration: %d day(s)", fleet.totalRouteDurationDays);

    if (ImGui::BeginTable("fleet_order_timeline", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Step");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Route Type");
        ImGui::TableSetupColumn("Destination");
        ImGui::TableSetupColumn("Start Day");
        ImGui::TableSetupColumn("Arrival Day");
        ImGui::TableSetupColumn("ETA");
        ImGui::TableSetupColumn("Distance");
        ImGui::TableSetupColumn("Fuel Cost");
        ImGui::TableSetupColumn("Fuel After");
        ImGui::TableHeadersRow();

        if (fleet.hasActiveOrder) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Current");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(fleet.activeOrderName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fleet.activeOrderRouteVisualStyleName.empty()
                ? "-"
                : fleet.activeOrderRouteVisualStyleName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(fleet.destinationBodyName.empty() ? "-" : fleet.destinationBodyName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted("Now");
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%lld", static_cast<long long>(fleet.activeOrderProjectedArrivalDay));
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d day(s)", fleet.activeOrderEtaDays.value_or(fleet.daysRemaining));
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.1fM km", fleet.activeOrderTransitDistanceKm / 1'000'000.0);
            ImGui::TableSetColumnIndex(8);
            ImGui::TextUnformatted("spent");
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%.1f", fleet.currentFuel);
        }

        for (const FleetQueuedOrderSummary& queuedOrder : fleet.queuedOrders) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", queuedOrder.queuePosition);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(queuedOrder.orderName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(queuedOrder.routeVisualStyleName.empty() ? "-" : queuedOrder.routeVisualStyleName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(queuedOrder.destinationBodyName.empty() ? "-" : queuedOrder.destinationBodyName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%lld", static_cast<long long>(queuedOrder.projectedStartDay));
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%lld", static_cast<long long>(queuedOrder.projectedArrivalDay));
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d day(s)", queuedOrder.etaDays);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.1fM km", queuedOrder.transitDistanceKm / 1'000'000.0);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%.1f", queuedOrder.fuelCost);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%.1f%s", queuedOrder.projectedFuelRemaining, queuedOrder.fuelAffordable ? "" : " !");
        }

        ImGui::EndTable();
    }
}

void drawCommandStatus(const bool succeeded, const std::string& status) {
    ImGui::Separator();
    ImGui::Text("Command status: %s", succeeded ? "OK" : "Error");
    ImGui::TextWrapped("%s", status.c_str());
}

} // namespace

void FleetOrdersPanel::render(const SimulationQueries& queries,
                              SimulationService& service,
                              const SelectionState& selection,
                              bool& visible) {
    if (!visible) {
        return;
    }

    syncSelection(queries, selection);

    if (!beginOperationalWindow("Fleet Orders", &visible)) {
        ImGui::End();
        return;
    }

    const std::optional<FleetSummary> fleet = selectedFleetId_.has_value()
        ? queries.fleet(*selectedFleetId_)
        : std::optional<FleetSummary>{};

    if (fleet.has_value()) {
        drawFleetSummary(*fleet);
        drawCurrentOrder(*fleet);
        drawTimelinePreview(*fleet);
    } else {
        ImGui::TextUnformatted("Select a fleet from the map or Fleets table.");
    }

    ImGui::SeparatorText("Add Order");
    drawDestinationSelector(queries);

    // A queued return-to-origin order can be valid while the fleet is already
    // moving away from that origin. Only reject same-body moves for idle fleets
    // where the queued order would start immediately as a no-op.
    const bool destinationSelected = destinationBodyId_.has_value();
    const std::optional<FleetMovePreview> movePreview = fleet.has_value() && destinationSelected
        ? queries.fleetMovePreview(fleet->id, *destinationBodyId_)
        : std::optional<FleetMovePreview>{};
    const bool idleDestinationIsCurrent = fleet.has_value() &&
                                          !fleet->hasActiveOrder &&
                                          destinationSelected &&
                                          *destinationBodyId_ == fleet->currentBodyId;
    // Preview gating is advisory. The service revalidates the complete route
    // against current state when the player actually submits the command.
    const bool hasFuelForMove = !movePreview.has_value() || movePreview->canAfford;
    const bool canQueueMove = fleet.has_value() && destinationSelected && !idleDestinationIsCurrent && hasFuelForMove;

    if (movePreview.has_value()) {
        ImGui::Text("Move fuel cost: %.1f", movePreview->newMoveFuelCost);
        ImGui::Text("Route type: %s", movePreview->routeVisualStyleName.c_str());
        ImGui::Text("Transit distance: %.1f million km | ETA: %d day(s) | Accel: %.3f g",
                    movePreview->transitDistanceKm / 1'000'000.0, movePreview->etaDays, movePreview->burnAccelerationG);
        ImGui::Text("Queued route fuel required: %.1f / available %.1f",
                    movePreview->queuedFuelRequired, movePreview->fuelAvailable);
        if (!movePreview->canAfford) {
            ImGui::TextUnformatted("Warning: insufficient fuel for this queued route.");
        }
    }

    if (!canQueueMove) {
        ImGui::BeginDisabled();
    }
    const bool addMoveClicked = ImGui::Button("Add Move Order to Queue");
    if (!canQueueMove) {
        ImGui::EndDisabled();
    }

    if (addMoveClicked && fleet.has_value() && destinationBodyId_.has_value()) {
        const CommandResult result = service.execute(QueueFleetMoveOrderCommand{
            .fleetId = fleet->id,
            .destinationBodyId = *destinationBodyId_
        });
        commandSucceeded_ = result.ok;
        commandStatus_ = result.message;
    }

    if (idleDestinationIsCurrent) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Destination is current body.");
    } else if (!hasFuelForMove) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Insufficient fuel.");
    }

    ImGui::SeparatorText("Survey");
    const std::optional<ResourceSurveyPreview> surveyPreview = fleet.has_value() && destinationSelected
        ? queries.resourceSurveyPreview(fleet->id, *destinationBodyId_)
        : std::optional<ResourceSurveyPreview>{};

    if (surveyPreview.has_value()) {
        ImGui::Text("Survey target: %s", surveyPreview->bodyName.c_str());
        ImGui::Text("Surveyable deposits: %zu", surveyPreview->surveyableDepositCount);
        if (surveyPreview->surveyableDepositCount > 0U) {
            ImGui::Text("Avg confidence: %.0f%% -> %.0f%%",
                        surveyPreview->averageConfidenceBefore * 100.0,
                        surveyPreview->projectedAverageConfidenceAfter * 100.0);
        }
        if (!surveyPreview->warningText.empty()) {
            ImGui::TextWrapped("%s", surveyPreview->warningText.c_str());
        }
    } else {
        ImGui::TextUnformatted("Select a fleet and body to preview resource survey eligibility.");
    }

    const bool canSurvey = surveyPreview.has_value() && surveyPreview->canSurvey;
    if (!canSurvey) {
        ImGui::BeginDisabled();
    }
    const bool surveyClicked = ImGui::Button("Survey Body");
    if (!canSurvey) {
        ImGui::EndDisabled();
    }

    if (surveyClicked && fleet.has_value() && destinationBodyId_.has_value()) {
        const CommandResult result = service.execute(ResourceSurveyCommand{
            .fleetId = fleet->id,
            .bodyId = *destinationBodyId_
        });
        commandSucceeded_ = result.ok;
        commandStatus_ = result.message;
    }

    const bool canCancel = fleet.has_value() && fleet->hasActiveOrder;
    if (!canCancel) {
        ImGui::BeginDisabled();
    }
    const bool cancelClicked = ImGui::Button("Cancel Order");
    if (!canCancel) {
        ImGui::EndDisabled();
    }

    if (cancelClicked && fleet.has_value()) {
        const CommandResult result = service.execute(CancelFleetOrderCommand{.fleetId = fleet->id});
        commandSucceeded_ = result.ok;
        commandStatus_ = result.message;
    }

    ImGui::SameLine();
    const bool canClearQueue = fleet.has_value() && !fleet->queuedOrders.empty();
    if (!canClearQueue) {
        ImGui::BeginDisabled();
    }
    const bool clearQueueClicked = ImGui::Button("Clear Queue");
    if (!canClearQueue) {
        ImGui::EndDisabled();
    }

    if (clearQueueClicked && fleet.has_value()) {
        const CommandResult result = service.execute(ClearFleetOrderQueueCommand{.fleetId = fleet->id});
        commandSucceeded_ = result.ok;
        commandStatus_ = result.message;
    }

    drawCommandStatus(commandSucceeded_, commandStatus_);

    ImGui::End();
}

void FleetOrdersPanel::syncSelection(const SimulationQueries& queries, const SelectionState& selection) {
    if (selection.type() == SelectedObjectType::Fleet && queries.fleet(selection.fleetId()).has_value()) {
        selectedFleetId_ = selection.fleetId();
    }

    // FIXME: Applying the same body selection every frame overwrites a later
    // destination-combo edit. Synchronize on selection changes when this workflow
    // is revised, while retaining the independently remembered source fleet.
    if (selection.type() == SelectedObjectType::Body && queries.strategicBody(selection.bodyId()).has_value()) {
        destinationBodyId_ = selection.bodyId();
    }

    // Drop IDs absent from the current world. Existence checks do not detect IDs
    // reused by New/Load; these IDs carry no session identity.
    if (selectedFleetId_.has_value() && !queries.fleet(*selectedFleetId_).has_value()) {
        selectedFleetId_.reset();
    }
    if (destinationBodyId_.has_value() && !queries.strategicBody(*destinationBodyId_).has_value()) {
        destinationBodyId_.reset();
    }
}

void FleetOrdersPanel::drawDestinationSelector(const SimulationQueries& queries) {
    const std::vector<StrategicBodySummary> bodies = queries.strategicBodies();
    const std::string preview = selectedDestinationLabel(queries, destinationBodyId_);

    if (ImGui::BeginCombo("Destination Body", preview.c_str())) {
        for (const StrategicBodySummary& body : bodies) {
            const bool selected = destinationBodyId_.has_value() && *destinationBodyId_ == body.id;
            const std::string label = bodyComboLabel(body);
            if (ImGui::Selectable(label.c_str(), selected)) {
                destinationBodyId_ = body.id;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace deep::ui_imgui
