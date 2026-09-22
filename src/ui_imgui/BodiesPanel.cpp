#include "ui_imgui/BodiesPanel.h"

// Implements the read-only Bodies/System overview panel.
// The table remains a query DTO consumer only; no simulation mutation is allowed
// from this view.

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::string rowId(const BodySystemSummary& body) {
    return "##body-system-row-" + std::to_string(body.id.value);
}

constexpr ImGuiTableFlags kBodyTableFlags = ImGuiTableFlags_Borders |
                                            ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_Resizable |
                                            ImGuiTableFlags_Reorderable |
                                            ImGuiTableFlags_Hideable |
                                            ImGuiTableFlags_SizingStretchProp;

} // namespace

void BodiesPanel::render(const SimulationQueries& queries, InformationInteractionAdapter& interactions, bool& visible) const {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Bodies / System", &visible)) {
        ImGui::End();
        return;
    }

    const auto displayedWorld = interactions.world();
    auto selection = interactions.mainSelection();
    const std::vector<BodySystemSummary> bodies = queries.bodySystemOverview();
    ImGui::Text("Bodies: %zu", bodies.size());

    if (ImGui::BeginTable("BodySystemTable", 12, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("Parent");
        ImGui::TableSetupColumn("Orbit Radius");
        ImGui::TableSetupColumn("Period");
        ImGui::TableSetupColumn("Owner / Institution");
        ImGui::TableSetupColumn("Colonies");
        ImGui::TableSetupColumn("Deposits");
        ImGui::TableSetupColumn("Known / Est / Unknown");
        ImGui::TableSetupColumn("Confirmed / Est / Uncertain");
        ImGui::TableSetupColumn("Fleets");
        ImGui::TableHeadersRow();

        for (const BodySystemSummary& body : bodies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::Selectable(rowId(body).c_str(), selection.isBodySelected(body.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                (void)interactions.select({displayedWorld, ObjectTarget{body.id}});
                selection = interactions.mainSelection();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(body.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(body.typeName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(body.strategicZoneName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(body.parentBodyName.empty() ? "-" : body.parentBodyName.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1fM km", body.orbitalRadiusKm / 1'000'000.0);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.1f d", body.orbitalPeriodDays);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(body.ownerInstitutionName.empty() ? "-" : body.ownerInstitutionName.c_str());
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%zu", body.colonyCount);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%zu", body.mineralDepositCount);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%zu / %zu / %zu", body.knownDepositCount, body.estimatedDepositCount, body.unknownDepositCount);
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("%.0f / %.0f / %.0f", body.confirmedDepositQuantity, body.estimatedDepositQuantity, body.uncertainDepositQuantity);
            ImGui::TableSetColumnIndex(11);
            ImGui::Text("%zu", body.fleetCount);
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    const ExplorationIntelligenceSummary intelligence = queries.explorationIntelligence();
    ImGui::TextUnformatted("Exploration Intelligence");
    if (intelligence.warnings.empty()) {
        ImGui::TextUnformatted("No mineral forecast currently depends mostly on estimated reserves.");
    } else {
        for (const std::string& warning : intelligence.warnings) {
            ImGui::BulletText("%s", warning.c_str());
        }
    }

    const std::size_t depositRows = std::min<std::size_t>(intelligence.lowConfidenceDeposits.size(), 8U);
    ImGui::Text("Low-confidence survey targets: %zu", intelligence.lowConfidenceDeposits.size());
    if (depositRows == 0U) {
        ImGui::TextUnformatted("All known deposits are fully confirmed.");
    } else if (ImGui::BeginTable("ExplorationLowConfidenceDeposits", 7, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Mineral");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Confidence");
        ImGui::TableSetupColumn("Confirmed");
        ImGui::TableSetupColumn("Est / Unknown");
        ImGui::TableSetupColumn("Relevance");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < depositRows; ++i) {
            const ExplorationDepositIntelligenceRow& row = intelligence.lowConfidenceDeposits[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.bodyName.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.mineralName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.surveyStateName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.0f%%", row.confidence * 100.0);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.0f", row.confirmedQuantity);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.0f / %.0f", row.estimatedQuantity, row.unknownPotentialQuantity);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextWrapped("%s", row.strategicRelevance.c_str());
        }

        ImGui::EndTable();
    }

    const std::size_t surveyRows = std::min<std::size_t>(intelligence.recentSurveyResults.size(), 5U);
    ImGui::Text("Recent survey results: %zu", intelligence.recentSurveyResults.size());
    if (surveyRows == 0U) {
        ImGui::TextUnformatted("No resource survey results have been recorded yet.");
    } else if (ImGui::BeginTable("ExplorationRecentSurveyResults", 4, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Day");
        ImGui::TableSetupColumn("Fleet");
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Result");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < surveyRows; ++i) {
            const RecentSurveyResultSummary& row = intelligence.recentSurveyResults[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(row.day));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.fleetName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.bodyName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", row.summary.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
