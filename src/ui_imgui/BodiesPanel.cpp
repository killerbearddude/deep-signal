#include "ui_imgui/BodiesPanel.h"
#include "ui_imgui/OperationalWindow.h"

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

    if (!beginOperationalWindow("Bodies / System", &visible)) {
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
        ImGui::TableSetupColumn("Observations");
        ImGui::TableSetupColumn("Assessments");
        ImGui::TableSetupColumn("Reserve knowledge");
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
            ImGui::Text("%zu", body.observationBatchCount);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%zu", body.assessmentRevisionCount);
            ImGui::TableSetColumnIndex(10);
            ImGui::TextUnformatted("Unmeasured");
            ImGui::TableSetColumnIndex(11);
            ImGui::Text("%zu", body.fleetCount);
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    const ExplorationIntelligenceSummary intelligence = queries.explorationIntelligence();
    ImGui::TextUnformatted("Exploration Intelligence");
    if (intelligence.warnings.empty()) {
        ImGui::TextUnformatted("Reserve quantities are unmeasured.");
    } else {
        for (const std::string& warning : intelligence.warnings) {
            ImGui::BulletText("%s", warning.c_str());
        }
    }

    const std::size_t depositRows = std::min<std::size_t>(intelligence.declaredChannels.size(), 8U);
    ImGui::Text("Declared resource channels: %zu", intelligence.declaredChannels.size());
    if (depositRows == 0U) {
        ImGui::TextUnformatted("No known astronomical subjects.");
    } else if (ImGui::BeginTable("ExplorationDeclaredChannels", 7, kBodyTableFlags)) {
        ImGui::TableSetupColumn("Body");
        ImGui::TableSetupColumn("Mineral");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Accessibility");
        ImGui::TableSetupColumn("Reserve");
        ImGui::TableSetupColumn("Site suitability");
        ImGui::TableSetupColumn("Relevance");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < depositRows; ++i) {
            const ExplorationDepositIntelligenceRow& row = intelligence.declaredChannels[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.bodyName.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.mineralName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.indication.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(row.accessibility.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted("Unmeasured");
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted("Unassessed");
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
