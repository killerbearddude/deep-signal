#include "ui_imgui/ShipyardPanel.h"
#include "ui_imgui/OperationalWindow.h"

// A compact component editor and shipyard list share this operational window.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::optional<ColonySummary> firstProductionColony(const std::vector<ColonySummary>& colonies) {
    return colonies.empty() ? std::optional<ColonySummary>{} : std::optional<ColonySummary>{colonies.front()};
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

    if (!beginOperationalWindow("Shipyard / Production", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<ShipClassSummary> classes = queries.shipClasses();
    if (!classes.empty()) {
        const auto selected = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
            return selectedBuildClassId_ == row.id;
        });
        if (selected == classes.end()) selectedBuildClassId_ = classes.front().id;
        const auto current = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
            return selectedBuildClassId_ == row.id;
        });
        const std::string label = current->name + " r" + std::to_string(current->revision) +
            " (#" + std::to_string(current->id.value) + ")";
        if (ImGui::BeginCombo("Build class", label.c_str())) {
            for (const ShipClassSummary& row : classes) {
                const std::string option = row.name + " r" + std::to_string(row.revision) +
                    " (#" + std::to_string(row.id.value) + ")";
                if (ImGui::Selectable(option.c_str(), selectedBuildClassId_ == row.id)) {
                    selectedBuildClassId_ = row.id;
                }
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::Button("Build Selected Class")) {
        buildSelectedClass(queries, service);
    }

    ImGui::SameLine();
    ImGui::Text("Status: %s", lastCommandSucceeded_ ? "OK" : "Rejected");
    ImGui::TextWrapped("%s", lastCommandMessage_.c_str());
    ImGui::Separator();

    renderDesignEditor(queries, service, classes);
    ImGui::Separator();

    const std::vector<ProductionBacklogSummary> backlog = queries.productionBacklog();
    ImGui::Text("Shipyard orders: %zu", backlog.size());

    if (ImGui::BeginTable("ShipyardOrderTable", 13, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
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
        ImGui::TableSetupColumn("Explanation");
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
            ImGui::Text("%s (#%lld)", order.shipClassName.c_str(),
                        static_cast<long long>(order.shipClassId.value));
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
            ImGui::TableSetColumnIndex(12);
            ImGui::TextWrapped("%s", order.explanation.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void ShipyardPanel::buildSelectedClass(const SimulationQueries& queries, SimulationService& service) {
    const std::optional<ColonySummary> colony = firstProductionColony(queries.colonies());
    if (!colony.has_value()) {
        applyResult(CommandResult::failure("No colony is available"));
        return;
    }

    if (!selectedBuildClassId_.has_value()) {
        applyResult(CommandResult::failure("No ship class is selected"));
        return;
    }

    applyResult(service.execute(AssignShipyardBuildCommand{
        .colonyId = colony->id,
        .shipClassId = *selectedBuildClassId_,
        .quantity = 1
    }));
}

void ShipyardPanel::resetWorldState() {
    selectedBuildClassId_.reset();
    draftSourceId_.reset();
    draftQuantities_.clear();
    draftName_.fill('\0');
}

void ShipyardPanel::selectDraftSource(const ShipClassSummary& source,
                                     const std::vector<ShipComponentSummary>& catalog) {
    draftSourceId_ = source.id;
    draftQuantities_.assign(catalog.size(), 0);
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        for (const ShipComponentInstall& install : source.components) {
            if (install.componentId == catalog[i].id) draftQuantities_[i] = install.quantity;
        }
    }
    draftName_.fill('\0');
    const std::string name = source.name + " revision";
    std::memcpy(draftName_.data(), name.data(), std::min(name.size(), draftName_.size() - 1));
}

void ShipyardPanel::renderDesignEditor(const SimulationQueries& queries, SimulationService& service,
                                      const std::vector<ShipClassSummary>& classes) {
    if (classes.empty()) return;
    const auto catalog = queries.shipComponents();
    const auto source = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
        return draftSourceId_ == row.id;
    });
    if (source == classes.end() || draftQuantities_.size() != catalog.size()) {
        selectDraftSource(classes.front(), catalog);
    }
    const auto selected = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
        return draftSourceId_ == row.id;
    });
    ImGui::TextUnformatted("New immutable design revision");
    const std::string currentLabel = selected->name + " r" + std::to_string(selected->revision);
    if (ImGui::BeginCombo("Start from", currentLabel.c_str())) {
        for (const ShipClassSummary& row : classes) {
            const std::string label = row.name + " r" + std::to_string(row.revision) +
                " (#" + std::to_string(row.id.value) + ")";
            if (ImGui::Selectable(label.c_str(), draftSourceId_ == row.id)) selectDraftSource(row, catalog);
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Revision name", draftName_.data(), draftName_.size());
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(catalog[i].name.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0F);
        ImGui::InputInt("Quantity", &draftQuantities_[i]);
        draftQuantities_[i] = std::max(0, draftQuantities_[i]);
        ImGui::TextDisabled("Mass %.0f  Vol %.0f  Hull space %.0f  Power +%.0f / -%.0f  Tank %.0f  Survey %.0f  BP %.0f",
            catalog[i].mass, catalog[i].volume, catalog[i].internalVolumeCapacity,
            catalog[i].powerGeneration, catalog[i].powerDemand,
            catalog[i].propellantCapacity, catalog[i].surveyCapability, catalog[i].buildPoints);
        std::ostringstream componentCost;
        for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
            if (catalog[i].buildCost.amount[material] > 0.0) {
                if (componentCost.tellp() > 0) componentCost << ", ";
                componentCost << toString(static_cast<ProcessedMaterial>(material)) << ' '
                              << catalog[i].buildCost.amount[material];
            }
        }
        const std::string componentCostText = componentCost.str();
        ImGui::TextWrapped("Materials: %s", componentCostText.empty() ? "--" : componentCostText.c_str());
        ImGui::PopID();
    }
    std::vector<ShipComponentInstall> draft;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (draftQuantities_[i] > 0) draft.push_back({catalog[i].id, draftQuantities_[i]});
    }
    const ShipDesignDraftPreview preview = queries.previewShipDesign(draft);
    const auto& design = preview.design;
    ImGui::Text("Dry mass %.1f | Volume %.1f / %.1f (available %.1f)",
                design.dryMass, design.usedVolume, design.volumeCapacity, design.availableVolume);
    ImGui::Text("Power %.1f generated / %.1f demanded (margin %.1f)",
                design.powerGeneration, design.powerDemand, design.powerMargin);
    ImGui::Text("Tankage %.1f | Survey %.1f | Build points %.1f",
                design.propellantCapacity, design.surveyCapability, design.buildPoints);
    ImGui::Text("Constructible: %s", design.constructible ? "Yes" : "No");
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        if (design.buildCost.amount[i] > 0.0) {
            ImGui::Text("%s: %.1f", toString(static_cast<ProcessedMaterial>(i)).data(), design.buildCost.amount[i]);
        }
    }
    for (const std::string& warning : preview.warnings) ImGui::TextWrapped("Warning: %s", warning.c_str());
    if (ImGui::Button("Save New Revision")) {
        applyResult(service.execute(CreateShipClassRevisionCommand{
            .name = draftName_.data(), .role = selected->role,
            .basedOnClassId = draftSourceId_, .components = draft
        }));
        if (lastCommandSucceeded_) {
            const auto updated = queries.shipClasses();
            selectedBuildClassId_ = updated.back().id;
            selectDraftSource(updated.back(), catalog);
        }
    }
}

void ShipyardPanel::applyResult(const CommandResult& result) {
    lastCommandSucceeded_ = result.ok;
    lastCommandMessage_ = result.message.empty() ? "Command completed" : result.message;
}

} // namespace deep::ui_imgui
