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
    return "##economy-forecast-row-" + std::to_string(static_cast<std::size_t>(chain.mineral));
}

[[nodiscard]] const char* statusText(const MineralForecastCauseChain& chain) noexcept {
    if (chain.netPerDay < -kMineralComparisonEpsilon) {
        return "Runout Risk";
    }

    if (chain.netPerDay > kMineralComparisonEpsilon) {
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

} // namespace

void EconomyForecastPanel::render(const ForecastService& forecasts, bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Economy Forecast", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<MineralForecastCauseChain> chains = forecasts.mineralForecastCauseChains();
    if (!selectedMineral_.has_value() && !chains.empty()) {
        selectedMineral_ = chains.front().mineral;
    }

    if (ImGui::BeginTable("EconomyForecastTable", 7, kForecastTableFlags)) {
        ImGui::TableSetupColumn("Mineral");
        ImGui::TableSetupColumn("Stockpile");
        ImGui::TableSetupColumn("Income/day");
        ImGui::TableSetupColumn("Demand/day");
        ImGui::TableSetupColumn("Net/day");
        ImGui::TableSetupColumn("Runout");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const MineralForecastCauseChain& chain : chains) {
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
            ImGui::Text("%.1f", chain.activeShipyardDemandPerDay);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f", chain.netPerDay);
            ImGui::TableSetColumnIndex(5);
            if (chain.stockpileRunoutDays.has_value()) {
                ImGui::Text("%d d", *chain.stockpileRunoutDays);
            } else {
                ImGui::TextUnformatted("--");
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(statusText(chain));
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    const MineralForecastCauseChain* selected = selectedChain(chains, selectedMineral_);
    if (selected != nullptr) {
        ImGui::Text("Drivers: %s", selected->mineralName.c_str());
        for (const MineralForecastCauseRow& cause : selected->causes) {
            ImGui::BulletText("%+.1f/day %s", cause.amountPerDay, cause.label.c_str());
            if (!cause.explanation.empty()) {
                ImGui::TextWrapped("  %s", cause.explanation.c_str());
            }
        }
    } else {
        ImGui::TextUnformatted("No mineral forecast rows are available.");
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
