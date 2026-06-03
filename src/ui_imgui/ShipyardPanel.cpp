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

    const std::vector<ProductionBacklogSummary> backlog = queries.productionBacklog();
    ImGui::Text("Shipyard orders: %zu", backlog.size());

    if (ImGui::BeginTable("ShipyardOrderTable", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colony");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Done");
        ImGui::TableSetupColumn("BP Remaining");
        ImGui::TableSetupColumn("ETA");
        ImGui::TableSetupColumn("Blocking Mineral");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const ProductionBacklogSummary& order : backlog) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(order.colonyName.c_str());

            ImGui::TableSetColumnIndex(1);
            if (order.queuePosition > 0) {
                ImGui::Text("%lld (#%d)", static_cast<long long>(order.orderId.value), order.queuePosition);
            } else {
                ImGui::Text("%lld", static_cast<long long>(order.orderId.value));
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(order.shipClassName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", order.quantityRequested);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", order.quantityCompleted);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.1f", order.buildPointsRemaining);
            ImGui::TableSetColumnIndex(6);
            if (order.etaDays.has_value()) {
                ImGui::Text("%d d", *order.etaDays);
            } else {
                ImGui::TextUnformatted("--");
            }
            ImGui::TableSetColumnIndex(7);
            ImGui::TextUnformatted(order.blockingMineralName.empty() ? "--" : order.blockingMineralName.c_str());
            ImGui::TableSetColumnIndex(8);
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
