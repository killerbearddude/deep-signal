#include "ui_imgui/EconomyForecastPanel.h"

// Implements the read-only economy forecast/cause-chain panel.
// The panel intentionally adapts existing ForecastService DTOs only; forecast
// math stays in the app layer and simulation state remains untouched.

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

constexpr ImGuiTableFlags kForecastTableFlags = ImGuiTableFlags_Borders |
                                                ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_Resizable |
                                                ImGuiTableFlags_Reorderable |
                                                ImGuiTableFlags_Hideable |
                                                ImGuiTableFlags_SizingStretchProp;

[[nodiscard]] std::string rowId(const MineralForecastCauseChain& chain) {
    return "##economy-forecast-mineral-row-" + std::to_string(static_cast<std::size_t>(chain.mineral));
}

[[nodiscard]] std::string rowId(const ProcessedMaterialForecastCauseChain& chain) {
    return "##economy-forecast-material-row-" + std::to_string(static_cast<std::size_t>(chain.material));
}

[[nodiscard]] const char* statusText(const double netPerDay) noexcept {
    if (netPerDay < -kMineralComparisonEpsilon) {
        return "Runout Risk";
    }

    if (netPerDay > kMineralComparisonEpsilon) {
        return "Surplus";
    }

    return "Stable";
}

[[nodiscard]] const MineralForecastCauseChain* selectedChain(const std::vector<MineralForecastCauseChain>& chains,
                                                             const std::optional<Mineral> selectedMineral) noexcept {
    if (chains.empty()) {
        return nullptr;
    }

    if (!selectedMineral.has_value()) {
        return &chains.front();
    }

    const auto it = std::find_if(chains.begin(), chains.end(), [selectedMineral](const MineralForecastCauseChain& chain) {
        return chain.mineral == *selectedMineral;
    });
    return it == chains.end() ? &chains.front() : &(*it);
}

[[nodiscard]] const ProcessedMaterialForecastCauseChain* selectedChain(
    const std::vector<ProcessedMaterialForecastCauseChain>& chains,
    const std::optional<ProcessedMaterial> selectedMaterial) noexcept {
    if (chains.empty()) {
        return nullptr;
    }

    if (!selectedMaterial.has_value()) {
        return &chains.front();
    }

    const auto it = std::find_if(chains.begin(), chains.end(), [selectedMaterial](const ProcessedMaterialForecastCauseChain& chain) {
        return chain.material == *selectedMaterial;
    });
    return it == chains.end() ? &chains.front() : &(*it);
}

void renderCauseRows(const std::string& title, const std::vector<MineralForecastCauseRow>& causes) {
    ImGui::Text("Drivers: %s", title.c_str());
    for (const MineralForecastCauseRow& cause : causes) {
        ImGui::BulletText("%+.1f/day %s", cause.amountPerDay, cause.label.c_str());
        if (!cause.explanation.empty()) {
            ImGui::TextWrapped("  %s", cause.explanation.c_str());
        }
    }
}

} // namespace

void EconomyForecastPanel::render(const ForecastService& forecasts, bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Economy Forecast", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<MineralForecastCauseChain> mineralChains = forecasts.mineralForecastCauseChains();
    if (!selectedMineral_.has_value() && !mineralChains.empty()) {
        selectedMineral_ = mineralChains.front().mineral;
    }

    ImGui::TextUnformatted("Raw resources");
    if (ImGui::BeginTable("EconomyForecastRawMineralTable", 7, kForecastTableFlags)) {
        ImGui::TableSetupColumn("Mineral");
        ImGui::TableSetupColumn("Stockpile");
        ImGui::TableSetupColumn("Income/day");
        ImGui::TableSetupColumn("Demand/day");
        ImGui::TableSetupColumn("Net/day");
        ImGui::TableSetupColumn("Runout");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const MineralForecastCauseChain& chain : mineralChains) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(chain).c_str(), selectedMineral_ == chain.mineral,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selectedMineral_ = chain.mineral;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(chain.mineralName.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", chain.stockpile);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", chain.miningIncomePerDay);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", chain.committedDemandPerDay);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f", chain.netPerDay);
            ImGui::TableSetColumnIndex(5);
            if (chain.stockpileRunoutDays.has_value()) {
                ImGui::Text("%d d", *chain.stockpileRunoutDays);
            } else {
                ImGui::TextUnformatted("--");
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(statusText(chain.netPerDay));
        }

        ImGui::EndTable();
    }

    const MineralForecastCauseChain* selectedMineral = selectedChain(mineralChains, selectedMineral_);
    if (selectedMineral != nullptr) {
        renderCauseRows(selectedMineral->mineralName, selectedMineral->causes);
    } else {
        ImGui::TextUnformatted("No raw-resource forecast rows are available.");
    }

    ImGui::Separator();
    const std::vector<ProcessedMaterialForecastCauseChain> materialChains = forecasts.processedMaterialForecastCauseChains();
    if (!selectedMaterial_.has_value() && !materialChains.empty()) {
        selectedMaterial_ = materialChains.front().material;
    }

    ImGui::TextUnformatted("Processed industrial materials");
    if (ImGui::BeginTable("EconomyForecastProcessedMaterialTable", 7, kForecastTableFlags)) {
        ImGui::TableSetupColumn("Material");
        ImGui::TableSetupColumn("Stockpile");
        ImGui::TableSetupColumn("Income/day");
        ImGui::TableSetupColumn("Demand/day");
        ImGui::TableSetupColumn("Net/day");
        ImGui::TableSetupColumn("Runout");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const ProcessedMaterialForecastCauseChain& chain : materialChains) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(chain).c_str(), selectedMaterial_ == chain.material,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selectedMaterial_ = chain.material;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(chain.materialName.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", chain.stockpile);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", chain.processingIncomePerDay);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", chain.committedDemandPerDay);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f", chain.netPerDay);
            ImGui::TableSetColumnIndex(5);
            if (chain.stockpileRunoutDays.has_value()) {
                ImGui::Text("%d d", *chain.stockpileRunoutDays);
            } else {
                ImGui::TextUnformatted("--");
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(statusText(chain.netPerDay));
        }

        ImGui::EndTable();
    }

    const ProcessedMaterialForecastCauseChain* selectedMaterial = selectedChain(materialChains, selectedMaterial_);
    if (selectedMaterial != nullptr) {
        renderCauseRows(selectedMaterial->materialName, selectedMaterial->causes);
    } else {
        ImGui::TextUnformatted("No processed-material forecast rows are available.");
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
