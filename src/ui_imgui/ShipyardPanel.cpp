#include "ui_imgui/ShipyardPanel.h"

// Implements the first functional shipyard panel for the ImGui shell.
// This file deliberately keeps production interaction narrow: one prototype
// button creates a Survey Cutter order through the existing command boundary.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] double progressPercent(const ShipyardOrderSummary& order) noexcept {
    if (order.requiredBuildPoints <= 0.0) {
        return 0.0;
    }

    return (order.accumulatedBuildPoints / order.requiredBuildPoints) * 100.0;
}

[[nodiscard]] std::optional<ColonySummary> firstProductionColony(const std::vector<ColonySummary>& colonies) {
    const auto it = std::find_if(colonies.begin(), colonies.end(), [](const ColonySummary& colony) {
        return colony.shipyardCapacity > 0.0;
    });
    return it == colonies.end() ? std::optional<ColonySummary>{} : std::optional<ColonySummary>{*it};
}

[[nodiscard]] std::optional<ShipClassSummary> surveyCutterClass(const std::vector<ShipClassSummary>& shipClasses) {
    const auto it = std::find_if(shipClasses.begin(), shipClasses.end(), [](const ShipClassSummary& shipClass) {
        return shipClass.name == "Survey Cutter";
    });
    return it == shipClasses.end() ? std::optional<ShipClassSummary>{} : std::optional<ShipClassSummary>{*it};
}

} // namespace

void ShipyardPanel::render(const SimulationQueries& queries, SimulationService& service, bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Shipyard / Production", &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Prototype production controls");
    if (ImGui::Button("Build Survey Cutter")) {
        buildSurveyCutter(queries, service);
    }

    ImGui::SameLine();
    ImGui::Text("Status: %s", lastCommandSucceeded_ ? "OK" : "Rejected");
    ImGui::TextWrapped("%s", lastCommandMessage_.c_str());
    ImGui::Separator();

    const std::vector<ShipyardOrderSummary> orders = queries.shipyardOrders();
    ImGui::Text("Shipyard orders: %zu", orders.size());

    if (ImGui::BeginTable("ShipyardOrderTable", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Colony");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Requested");
        ImGui::TableSetupColumn("Done");
        ImGui::TableSetupColumn("BP");
        ImGui::TableSetupColumn("Progress");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const ShipyardOrderSummary& order : orders) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(order.id.value));

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(order.colonyName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(order.shipClassName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", order.quantityRequested);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", order.quantityCompleted);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.1f / %.1f", order.accumulatedBuildPoints, order.requiredBuildPoints);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.1f%%", progressPercent(order));
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(order.statusName.c_str());

        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void ShipyardPanel::buildSurveyCutter(const SimulationQueries& queries, SimulationService& service) {
    const std::optional<ColonySummary> colony = firstProductionColony(queries.colonies());
    if (!colony.has_value()) {
        applyResult(CommandResult::failure("No colony with shipyard capacity is available"));
        return;
    }

    const std::optional<ShipClassSummary> shipClass = surveyCutterClass(queries.shipClasses());
    if (!shipClass.has_value()) {
        applyResult(CommandResult::failure("Survey Cutter ship class is not available"));
        return;
    }

    applyResult(service.execute(AssignShipyardBuildCommand{
        .colonyId = colony->id,
        .shipClassId = shipClass->id,
        .quantity = 1
    }));
}

void ShipyardPanel::applyResult(const CommandResult& result) {
    lastCommandSucceeded_ = result.ok;
    lastCommandMessage_ = result.message.empty() ? "Command completed" : result.message;
}

} // namespace deep::ui_imgui
