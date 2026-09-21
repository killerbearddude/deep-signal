#include "ui_imgui/ShipyardPanel.h"

// Implements the first functional shipyard panel for the ImGui shell.
// This file deliberately keeps production interaction narrow: one prototype
// button creates a Survey Cutter order through the existing command boundary.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <sstream>
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

// Flattens per-material requirements into one compact table cell. The DTO still
// carries typed rows so future UI can replace this with expandable details.
[[nodiscard]] std::string materialRequirementsText(const std::vector<ProcessedMaterialStockpileSummary>& requirements) {
    if (requirements.empty()) {
        return "--";
    }

    std::ostringstream out;
    bool first = true;
    for (const ProcessedMaterialStockpileSummary& requirement : requirements) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << requirement.materialName << ' ' << requirement.amount;
    }

    return out.str();
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

    if (ImGui::BeginTable("ShipyardOrderTable", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colony");
        ImGui::TableSetupColumn("Queue");
        ImGui::TableSetupColumn("Order");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Done");
        ImGui::TableSetupColumn("Ships Left");
        ImGui::TableSetupColumn("BP Remaining");
        ImGui::TableSetupColumn("ETA");
        ImGui::TableSetupColumn("Materials Remaining");
        ImGui::TableSetupColumn("Blocking Material");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const ProductionBacklogSummary& order : backlog) {
            const std::string requirements = materialRequirementsText(order.requiredMaterialsRemaining);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(order.colonyName.c_str());

            ImGui::TableSetColumnIndex(1);
            if (order.queuePosition > 0) {
                ImGui::Text("#%d", order.queuePosition);
            } else {
                ImGui::TextUnformatted("--");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%lld", static_cast<long long>(order.orderId.value));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(order.shipClassName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", order.quantityRequested);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", order.quantityCompleted);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d", order.shipsRemaining);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.1f", order.buildPointsRemaining);
            ImGui::TableSetColumnIndex(8);
            if (order.etaDays.has_value()) {
                ImGui::Text("%d d", *order.etaDays);
            } else {
                ImGui::TextUnformatted("--");
            }
            ImGui::TableSetColumnIndex(9);
            ImGui::TextWrapped("%s", requirements.c_str());
            ImGui::TableSetColumnIndex(10);
            ImGui::TextUnformatted(order.blockingMaterialName.empty() ? "--" : order.blockingMaterialName.c_str());
            ImGui::TableSetColumnIndex(11);
            ImGui::TextUnformatted(order.statusName.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void ShipyardPanel::buildSurveyCutter(const SimulationQueries& queries, SimulationService& service) {
    // Prototype assumption: scenario order selects the default shipyard and the
    // class is identified by display name. Replace both with explicit typed-ID
    // selectors when exposing multiple production locations or editable classes.
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
