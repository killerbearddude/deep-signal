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

// The returned pointer borrows this render's DTO vector. Non-colony selection
// shows the first colony; a missing explicitly selected colony has no fallback.
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

void addProcessingWeight(std::array<double, processedMaterialCount()>& weights,
                         const ProcessedMaterial material,
                         const double weight) noexcept {
    if (weight <= 0.0 || processedMaterialIndex(material) >= weights.size()) {
        return;
    }

    weights[processedMaterialIndex(material)] += weight;
}

[[nodiscard]] double processedStockpileAmount(const ColonySummary& colony, const ProcessedMaterial material) noexcept {
    const auto it = std::find_if(colony.processedStockpiles.begin(), colony.processedStockpiles.end(), [material](const ProcessedMaterialStockpileSummary& row) {
        return row.material == material;
    });
    return it == colony.processedStockpiles.end() ? 0.0 : it->amount;
}

// Preview the unapplied policy without running production. These preset ratios
// mirror simulation rules; raw-input shortages can reduce actual daily output.
// TODO: share the policy calculation when it changes so this editor, forecasts,
// and the authoritative processing pass cannot drift independently.
[[nodiscard]] std::array<double, processedMaterialCount()> policyWeightsForDisplay(
    const ColonySummary& colony,
    const ProcessingPolicy policy,
    const std::array<double, processedMaterialCount()>& manualWeights) noexcept {
    std::array<double, processedMaterialCount()> weights{};

    switch (policy) {
    case ProcessingPolicy::Balanced:
        weights.fill(1.0);
        break;
    case ProcessingPolicy::ShipbuildingFocus:
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 4.0);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::OrdnanceMaterials, 0.5);
        break;
    case ProcessingPolicy::FuelFocus:
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 0.5);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 0.5);
        break;
    case ProcessingPolicy::ElectronicsFocus:
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 1.0);
        break;
    case ProcessingPolicy::StockpileRecovery:
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
            weights[i] = 1.0 / (1.0 + std::max(0.0, processedStockpileAmount(colony, material)));
        }
        break;
    case ProcessingPolicy::Manual:
        weights = manualWeights;
        break;
    }

    return weights;
}

[[nodiscard]] double totalPositiveWeight(const std::array<double, processedMaterialCount()>& weights) noexcept {
    double total = 0.0;
    for (const double weight : weights) {
        total += std::max(0.0, weight);
    }
    return total;
}

[[nodiscard]] double normalizedPercent(const std::array<double, processedMaterialCount()>& weights, const std::size_t index) noexcept {
    const double total = totalPositiveWeight(weights);
    if (total <= kProcessedMaterialComparisonEpsilon || index >= weights.size()) {
        return 0.0;
    }

    return std::max(0.0, weights[index]) * 100.0 / total;
}

} // namespace

void ColonyPanel::render(const SimulationQueries& queries,
                         SimulationService& service,
                         InformationInteractionAdapter& interactions,
                         bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Colonies", &visible)) {
        ImGui::End();
        return;
    }

    const auto displayedWorld = interactions.world();
    auto selection = interactions.mainSelection();
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
                (void)interactions.select({displayedWorld, ObjectTarget{colony.id}});
                // Refresh from the authority before this panel's editor consumes
                // selection. This is a projection, never a second selection owner.
                selection = interactions.mainSelection();
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

    // Keep an in-progress draft across frames instead of overwriting every edit.
    // The shell's success-only replacement callback invalidates old-world drafts,
    // including hidden panels, before this code can resolve a reused colony ID.
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

    const bool manualMode = selectedPolicy_ == ProcessingPolicy::Manual;
    std::array<double, processedMaterialCount()> displayedWeights =
        policyWeightsForDisplay(*colony, selectedPolicy_, manualWeights_);

    ImGui::TextUnformatted(manualMode ? "Manual Allocation Weights" : "Policy-Derived Allocation");
    if (!manualMode) {
        ImGui::TextWrapped("Preset policies derive allocation weights automatically. Switch to Manual to edit weights.");
    }

    constexpr double kMinManualWeight = 0.0;
    constexpr double kMaxManualWeight = 10.0;
    if (!manualMode) {
        ImGui::BeginDisabled();
    }
    for (std::size_t i = 0; i < manualWeights_.size(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        const std::string materialName{toString(material)};
        double& editableWeight = manualMode ? manualWeights_[i] : displayedWeights[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SliderScalar(std::string{"Weight##" + materialName}.c_str(),
                            ImGuiDataType_Double,
                            &editableWeight,
                            &kMinManualWeight,
                            &kMaxManualWeight,
                            "%.2f");
        if (manualMode) {
            displayedWeights[i] = manualWeights_[i];
        }
        ImGui::SameLine();
        ImGui::Text("%s  %.1f%%", materialName.c_str(), normalizedPercent(displayedWeights, i));
        ImGui::PopID();
    }
    if (!manualMode) {
        ImGui::EndDisabled();
    }

    if (manualMode && ImGui::Button("Normalize")) {
        normalizeManualWeights();
        statusMessage_ = "Manual weights normalized; effective allocation remains 100%.";
    }
    if (manualMode) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Apply")) {
        const CommandResult result = service.execute(SetColonyProcessingPolicyCommand{
            .colonyId = colony->id,
            .policy = selectedPolicy_,
            .manualAllocations = manualMode ? allocationsFromWeights(manualWeights_) : std::vector<ProcessingAllocation>{}
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
        manualWeights_.fill(1.0);
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
        manualWeights_.fill(1.0);
        return;
    }

    for (double& weight : manualWeights_) {
        weight /= total;
    }
}

} // namespace deep::ui_imgui
