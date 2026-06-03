#include "ui_imgui/FleetOrdersPanel.h"

// Implements a dedicated fleet command surface for the ImGui workstation.
// The panel deliberately reuses the existing single-step fleet commands and does
// not introduce route planning, range validation, fuel rules, or order queues.

#include "sim/Commands.h"

#include <imgui.h>

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
    ImGui::Text("Order: %s", fleet.activeOrderName.c_str());
    ImGui::Text("Destination: %s", fleet.destinationBodyName.empty() ? "-" : fleet.destinationBodyName.c_str());
    ImGui::Text("Days remaining: %d", fleet.daysRemaining);
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

    if (!ImGui::Begin("Fleet Orders", &visible)) {
        ImGui::End();
        return;
    }

    const std::optional<FleetSummary> fleet = selectedFleetId_.has_value()
        ? queries.fleet(*selectedFleetId_)
        : std::optional<FleetSummary>{};

    if (fleet.has_value()) {
        drawFleetSummary(*fleet);
    } else {
        ImGui::TextUnformatted("Select a fleet from the map or Fleets table.");
    }

    ImGui::Separator();
    drawDestinationSelector(queries);

    // Keep button enablement focused on UI-obvious prerequisites. The simulation
    // remains the authority for final command validation and error messages.
    const bool destinationSelected = destinationBodyId_.has_value();
    const bool destinationIsCurrent = fleet.has_value() && destinationSelected && *destinationBodyId_ == fleet->currentBodyId;
    const bool canMove = fleet.has_value() && destinationSelected && !fleet->hasActiveOrder && !destinationIsCurrent;

    if (!canMove) {
        ImGui::BeginDisabled();
    }
    const bool moveClicked = ImGui::Button("Move Fleet");
    if (!canMove) {
        ImGui::EndDisabled();
    }

    if (moveClicked && fleet.has_value() && destinationBodyId_.has_value()) {
        const CommandResult result = service.execute(MoveFleetCommand{
            .fleetId = fleet->id,
            .destinationBodyId = *destinationBodyId_
        });
        commandSucceeded_ = result.ok;
        commandStatus_ = result.message;
    }

    if (fleet.has_value() && fleet->hasActiveOrder) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Fleet already has an active order.");
    } else if (destinationIsCurrent) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Destination is current body.");
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

    drawCommandStatus(commandSucceeded_, commandStatus_);

    ImGui::End();
}

void FleetOrdersPanel::syncSelection(const SimulationQueries& queries, const SelectionState& selection) {
    if (selection.type() == SelectedObjectType::Fleet && queries.fleet(selection.fleetId()).has_value()) {
        selectedFleetId_ = selection.fleetId();
    }

    if (selection.type() == SelectedObjectType::Body && queries.strategicBody(selection.bodyId()).has_value()) {
        destinationBodyId_ = selection.bodyId();
    }

    // Drop stale UI-held IDs after save/load/new-game changes. This prevents a
    // command button from targeting an object that no longer exists in the
    // current SimulationService snapshot.
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
