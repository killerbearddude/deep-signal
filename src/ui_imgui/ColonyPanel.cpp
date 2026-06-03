#include "ui_imgui/ColonyPanel.h"

// Implements the colony table plus first-pass processing allocation controls.
// Processing edits are intentionally small command submissions: policy selection
// changes simulation behavior, but all actual resource conversion still occurs
// inside Simulation::advanceDays.

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
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

[[nodiscard]] const char* policyName(const ProcessingPolicy policy) noexcept {
    switch (policy) {
    case ProcessingPolicy::Balanced:
        return "Balanced";
    case ProcessingPolicy::ShipbuildingFocus:
        return "Shipbuilding Focus";
    case ProcessingPolicy::FuelFocus:
        return "Fuel Focus";
    case ProcessingPolicy::ElectronicsFocus:
        return "Electronics Focus";
    case ProcessingPolicy::StockpileRecovery:
        return "Stockpile Recovery";
    case ProcessingPolicy::Manual:
        return "Manual";
    }

    return "Unknown";
}

constexpr std::array<ProcessingPolicy, 6> kPolicies{
    ProcessingPolicy::Balanced,
    ProcessingPolicy::ShipbuildingFocus,
    ProcessingPolicy::FuelFocus,
    ProcessingPolicy::ElectronicsFocus,
    ProcessingPolicy::StockpileRecovery,
    ProcessingPolicy::Manual
};

[[nodiscard]] const ColonySummary* selectedColony(const std::vector<ColonySummary>& colonies,
                                                  const SelectionState& selection) noexcept {
    if (selection.type() != SelectedObjectType::Colony) {
        return colonies.empty() ? nullptr : &colonies.front();
    }

    const ColonyId selectedId = selection.colonyId();
    const auto it = std::find_if(colonies.begin(), colonies.end(), [selectedId](const ColonySummary& colony) {
        return colony.id == selectedId;
    });
    return it == colonies.end() ? nullptr : &(*it);
}

[[nodiscard]] std::vector<ProcessingAllocation> allocationsFromWeights(const std::array<double, processedMaterialCount()>& weights) {
    std::vector<ProcessingAllocation> allocations;
    allocations.reserve(weights.size());

    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= kProcessedMaterialComparisonEpsilon) {
            continue;
        }

        allocations.push_back(ProcessingAllocation{
            .material = static_cast<ProcessedMaterial>(i),
            .weight = weights[i]
        });
    }

    return allocations;
}

} // namespace

void ColonyPanel::render(const SimulationQueries& queries,
                         SimulationService& service,
                         SelectionState& selection,
                         bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Colonies", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<ColonySummary> colonies = queries.colonies();
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

            // A hidden-label selectable spans the full row while keeping the ID
            // text visible and stable for table sorting/inspection later.
            if (ImGui::Selectable(rowId(colony).c_str(), selection.isColonySelected(colony.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selection.selectColony(colony.id);
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

    const ColonySummary* colony = selectedColony(colonies, selection);
    if (colony == nullptr) {
        ImGui::TextUnformatted("Select a colony to edit processing allocation.");
        ImGui::End();
        return;
    }

    if (!editingColony_.has_value() || *editingColony_ != colony->id) {
        loadEditorFromColony(*colony);
    }

    ImGui::Separator();
    ImGui::Text("Processing Allocation: %s", colony->name.c_str());

    if (ImGui::BeginCombo("Processing Policy", policyName(selectedPolicy_))) {
        for (const ProcessingPolicy policy : kPolicies) {
            const bool selected = selectedPolicy_ == policy;
            if (ImGui::Selectable(policyName(policy), selected)) {
                selectedPolicy_ = policy;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextUnformatted("Manual Allocation");
    constexpr double kMinManualPercent = 0.0;
    constexpr double kMaxManualPercent = 100.0;
    for (std::size_t i = 0; i < manualWeights_.size(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        ImGui::SliderScalar(std::string{toString(material)}.c_str(),
                            ImGuiDataType_Double,
                            &manualWeights_[i],
                            &kMinManualPercent,
                            &kMaxManualPercent,
                            "%.1f%%");
    }

    if (ImGui::Button("Normalize")) {
        normalizeManualWeights();
        statusMessage_ = "Manual allocation normalized.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        const CommandResult result = service.execute(SetColonyProcessingPolicyCommand{
            .colonyId = colony->id,
            .policy = selectedPolicy_,
            .manualAllocations = allocationsFromWeights(manualWeights_)
        });
        statusMessage_ = result.message;
        if (result.ok) {
            editingColony_.reset();
        }
    }

    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }

    ImGui::End();
}

void ColonyPanel::loadEditorFromColony(const ColonySummary& colony) {
    editingColony_ = colony.id;
    selectedPolicy_ = colony.processingPolicy;
    manualWeights_.fill(0.0);

    for (const ProcessingAllocationSummary& allocation : colony.manualProcessingAllocations) {
        const std::size_t index = processedMaterialIndex(allocation.material);
        if (index < manualWeights_.size()) {
            manualWeights_[index] = allocation.weight;
        }
    }

    const bool hasManualWeights = std::any_of(manualWeights_.begin(), manualWeights_.end(), [](const double weight) {
        return weight > kProcessedMaterialComparisonEpsilon;
    });
    if (!hasManualWeights) {
        manualWeights_.fill(100.0 / static_cast<double>(manualWeights_.size()));
    }

    statusMessage_.clear();
}

void ColonyPanel::normalizeManualWeights() noexcept {
    double total = 0.0;
    for (double& weight : manualWeights_) {
        weight = std::max(0.0, weight);
        total += weight;
    }

    if (total <= kProcessedMaterialComparisonEpsilon) {
        manualWeights_.fill(100.0 / static_cast<double>(manualWeights_.size()));
        return;
    }

    for (double& weight : manualWeights_) {
        weight = weight * 100.0 / total;
    }
}

} // namespace deep::ui_imgui
