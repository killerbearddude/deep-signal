// Compact technical-development workflow: one visible opportunity, finite staff
// and facility authoring, acquired test evidence, local production, and support.
#include "ui_imgui/TechnicalDevelopmentPanel.h"
#include "ui_imgui/OperationalWindow.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace deep::ui_imgui {
namespace {
template <class Id, class Rows, class IdFn, class NameFn>
void optionalChoice(const char* label, std::optional<Id>& selected, const Rows& rows, IdFn id, NameFn name) {
    std::string current = "Unassigned";
    for (const auto& row : rows)
        if (selected == id(row))
            current = name(row);
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("Unassigned", !selected))
            selected.reset();
        for (const auto& row : rows) {
            const auto rowId = id(row);
            const auto text = name(row) + " (#" + std::to_string(rowId.value) + ")";
            if (ImGui::Selectable(text.c_str(), selected == rowId))
                selected = rowId;
        }
        ImGui::EndCombo();
    }
}
const char* stageName(TechnicalDevelopmentStage stage) {
    switch (stage) {
    case TechnicalDevelopmentStage::ConceptEngineering:
        return "Concept engineering";
    case TechnicalDevelopmentStage::PrototypeFabrication:
        return "Prototype fabrication";
    case TechnicalDevelopmentStage::PrototypeTesting:
        return "Prototype testing";
    case TechnicalDevelopmentStage::ProductionQualification:
        return "Production-process qualification";
    case TechnicalDevelopmentStage::SupportQualification:
        return "Support qualification";
    case TechnicalDevelopmentStage::Complete:
        return "Complete";
    }
    return "Invalid";
}
const char* scopeName(TechnicalDevelopmentScope scope) {
    switch (scope) {
    case TechnicalDevelopmentScope::DemonstratePrototype:
        return "Demonstrate prototype";
    case TechnicalDevelopmentScope::ProductionReady:
        return "Production ready";
    case TechnicalDevelopmentScope::ProductionAndSupportReady:
        return "Production and support ready";
    }
    return "Invalid";
}
void materials(const ProcessedMaterialSet& set) {
    for (std::size_t index = 0; index < processedMaterialCount(); ++index)
        if (set.amount[index] > 0.0)
            ImGui::BulletText("%s: %.3f",
                              std::string(toString(static_cast<ProcessedMaterial>(index))).c_str(),
                              set.amount[index]);
}
} // namespace

void TechnicalDevelopmentPanel::resetWorldState() {
    draft_ = {};
    name_.fill('\0');
    selected_.reset();
    editing_.reset();
    notice_ = "Ready";
}

void TechnicalDevelopmentPanel::render(const SimulationQueries& queries, SimulationService& service,
                                       bool& visible) {
    if (!visible)
        return;
    if (!beginOperationalWindow("Technical Development", &visible)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Optional engineering development creates evidence and physical capability. "
                       "The established Specialist Survey Array remains valid without this program.");
    ImGui::SeparatorText("Recognized opportunity");
    for (const auto& row : queries.technologyOpportunities()) {
        ImGui::Text("%s — %s", row.opportunity.name.c_str(), row.statusName.c_str());
        ImGui::TextWrapped("Objective: %s", row.opportunity.objective.c_str());
        ImGui::TextWrapped("Known tradeoff: %s", row.opportunity.knownTradeoff.c_str());
        ImGui::Text("Established comparator: %s | Public target <= %.3f normalized signal",
                    row.baselineComponentName.c_str(), row.opportunity.targetDetectionThreshold);
        if (row.demonstratedThreshold)
            ImGui::Text("Demonstrated by tests: %.3f", *row.demonstratedThreshold);
        else
            ImGui::TextUnformatted("Achieved sensitivity: not established before prototype tests");
    }
    if (ImGui::CollapsingHeader("Authorize or amend program", ImGuiTreeNodeFlags_DefaultOpen))
        renderEditor(queries, service);
    ImGui::SeparatorText("Technical-development commitments");
    const auto programs = queries.technicalDevelopments();
    if (!programs.empty() && std::none_of(programs.begin(), programs.end(),
                                          [&](const auto& row) { return selected_ == row.program.id; }))
        selected_ = programs.front().program.id;
    for (const auto& row : programs) {
        const auto label = row.program.charter.name + " (#" + std::to_string(row.program.id.value) + ") — " +
                           stageName(row.program.stage);
        if (ImGui::Selectable(label.c_str(), selected_ == row.program.id))
            selected_ = row.program.id;
    }
    for (const auto& row : programs)
        if (selected_ == row.program.id)
            renderProgram(row, service);
    ImGui::TextWrapped("Last action: %s", notice_.c_str());
    ImGui::End();
}

void TechnicalDevelopmentPanel::renderEditor(const SimulationQueries& queries, SimulationService& service) {
    const auto opportunities = queries.technologyOpportunities();
    const auto colonies = queries.colonies();
    const auto facilities = queries.technicalFacilities();
    const auto teams = queries.maintenanceTeams();
    const auto people = queries.personnel();
    if (!draft_.opportunityId && !opportunities.empty())
        draft_.opportunityId = opportunities.front().opportunity.id;
    if (!draft_.developmentColonyId && !colonies.empty())
        draft_.developmentColonyId = colonies.front().id;
    ImGui::InputText("Program name", name_.data(), name_.size());
    draft_.name = name_.data();
    ImGui::BeginDisabled(editing_.has_value());
    if (!opportunities.empty())
        ImGui::Text("Opportunity: %s", opportunities.front().opportunity.name.c_str());
    std::optional<ColonyId> colony =
        draft_.developmentColonyId ? std::optional{draft_.developmentColonyId} : std::nullopt;
    optionalChoice(
        "Fixed development colony", colony, colonies, [](const auto& row) { return row.id; },
        [](const auto& row) { return row.name; });
    draft_.developmentColonyId = colony.value_or(ColonyId{});
    ImGui::EndDisabled();
    optionalChoice(
        "Technical facility", draft_.requestedFacilityId, facilities, [](const auto& row) { return row.id; },
        [](const auto& row) { return row.name; });
    optionalChoice(
        "Engineering team", draft_.requestedTeamId, teams, [](const auto& row) { return row.team.id; },
        [](const auto& row) { return row.team.name + " at " + row.locationName; });
    optionalChoice(
        "Responsible leader", draft_.requestedLeaderId, people, [](const auto& row) { return row.id; },
        [](const auto& row) { return row.name; });
    if (ImGui::BeginCombo("Requested scope", scopeName(draft_.scope))) {
        for (int value = 0; value <= 2; ++value) {
            const auto scope = static_cast<TechnicalDevelopmentScope>(value);
            if (ImGui::Selectable(scopeName(scope), draft_.scope == scope))
                draft_.scope = scope;
        }
        ImGui::EndCombo();
    }
    if (ImGui::TreeNode("Material floors and lifetime authority")) {
        bool capped = draft_.policy.lifetimeAllowances.has_value();
        if (ImGui::Checkbox("Limit lifetime development materials", &capped))
            draft_.policy.lifetimeAllowances = capped ? std::optional{ProcessedMaterialSet{}} : std::nullopt;
        for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TextUnformatted(std::string(toString(static_cast<ProcessedMaterial>(index))).c_str());
            ImGui::InputDouble("Colony stock floor", &draft_.policy.floors.amount[index], 1, 10, "%.3f");
            if (draft_.policy.lifetimeAllowances)
                ImGui::InputDouble("Lifetime amount", &draft_.policy.lifetimeAllowances->amount[index], 1, 10,
                                   "%.3f");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    const auto preview = queries.previewTechnicalDevelopment(draft_, editing_);
    ImGui::TextWrapped("%s", preview.structurallyValid ? preview.condition.c_str()
                                                       : preview.validationMessage.c_str());
    if (preview.structurallyValid) {
        ImGui::Text("Starting stage: %s | Work: %.3f team/facility-days", stageName(preview.startingStage),
                    preview.requiredWork);
        materials(preview.requiredMaterials);
    }
    ImGui::TextWrapped(
        "Authorization records intent. Missing staff, facility, or material is an honest wait; "
        "it creates no prototype, component, process, or support qualification.");
    ImGui::BeginDisabled(!preview.structurallyValid);
    if (ImGui::Button(editing_ ? "Save technical amendment" : "Authorize technical development")) {
        const auto result = editing_ ? service.execute(AmendTechnicalDevelopmentCommand{*editing_, draft_})
                                     : service.execute(CreateTechnicalDevelopmentCommand{draft_});
        notice_ = result.message;
        if (result.ok)
            editing_.reset();
    }
    ImGui::EndDisabled();
}

void TechnicalDevelopmentPanel::renderProgram(const TechnicalDevelopmentSummary& row,
                                              SimulationService& service) {
    const auto& program = row.program;
    ImGui::SeparatorText("Selected program detail");
    ImGui::Text("%s at %s | Scope: %s", row.opportunityName.c_str(), row.colonyName.c_str(),
                scopeName(program.charter.scope));
    ImGui::TextWrapped("%s", row.condition.c_str());
    ImGui::Text("Facility: %s (%.3f workdays/day) | Team: %s at %s",
                row.facilityName.empty() ? "Unassigned" : row.facilityName.c_str(), row.facilityRate,
                row.teamName.empty() ? "Unassigned" : row.teamName.c_str(), row.teamLocation.c_str());
    ImGui::Text("Actual engineering lease: %s",
                program.leasedTeamId ? ("team #" + std::to_string(program.leasedTeamId->value)).c_str()
                                     : "none");
    if (!row.teamOwner.empty())
        ImGui::TextWrapped("Actual team owner: %s", row.teamOwner.c_str());
    ImGui::Text("Stage: %s | Work %.3f / %.3f", stageName(program.stage), program.stageWork,
                row.requiredStageWork);
    ImGui::TextUnformatted("Current-stage consumed materials:");
    materials(program.stageConsumed);
    double lifetimeWork = 0.0;
    ProcessedMaterialSet lifetimeMaterials;
    for (const auto& receipt : program.receipts) {
        lifetimeWork += receipt.work;
        lifetimeMaterials.addSet(receipt.consumed);
    }
    ImGui::Text("Lifetime engineering work: %.3f", lifetimeWork);
    materials(lifetimeMaterials);
    ImGui::TextUnformatted("Full current-stage requirement:");
    materials(row.requiredStageMaterials);
    if (row.prototype)
        ImGui::Text(
            "Prototype #%lld at colony #%lld — state %d", static_cast<long long>(row.prototype->id.value),
            static_cast<long long>(row.prototype->colonyId.value), static_cast<int>(row.prototype->state));
    for (const auto& test : row.tests)
        ImGui::BulletText("Test %d day %lld: measured %.3f vs target %.3f — %s", test.sequence,
                          static_cast<long long>(test.day), test.measuredDetectionThreshold,
                          test.targetDetectionThreshold, test.meetsTarget ? "met" : "missed");
    if (!row.tests.empty())
        ImGui::TextWrapped("These are engineering test records. They are separate from geological "
                           "observations, analysis findings, assessments, and site operating records.");
    if (row.developed)
        ImGui::Text("Demonstrated component #%lld / profile #%lld",
                    static_cast<long long>(row.developed->componentId.value),
                    static_cast<long long>(row.developed->measurementProfileId.value));
    ImGui::Text("Local serial process: %s | Selected-team support: %s",
                row.localProductionReady ? "effective" : "not effective",
                row.supportQualified ? "qualified" : "not qualified");
    if (!program.issue.signature.empty())
        ImGui::TextWrapped("Technical issue: %s%s", program.issue.message.c_str(),
                           program.issue.acknowledged ? " (acknowledged)" : "");
    if (ImGui::Button("Edit authority")) {
        editing_ = program.id;
        draft_ = program.charter;
        name_.fill('\0');
        std::memcpy(name_.data(), draft_.name.data(), std::min(draft_.name.size(), name_.size() - 1));
    }
    ImGui::SameLine();
    if (program.lifecycle == TechnicalDevelopmentLifecycle::Authorized) {
        if (ImGui::Button("Suspend"))
            notice_ = service.execute(SuspendTechnicalDevelopmentCommand{program.id}).message;
    } else if (program.lifecycle == TechnicalDevelopmentLifecycle::Suspended) {
        if (ImGui::Button("Resume"))
            notice_ = service.execute(ResumeTechnicalDevelopmentCommand{program.id}).message;
    }
    if (program.lifecycle != TechnicalDevelopmentLifecycle::Closed) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel future work"))
            notice_ = service.execute(CancelTechnicalDevelopmentCommand{program.id}).message;
    }
    if (!program.issue.acknowledged) {
        ImGui::SameLine();
        if (ImGui::Button("Acknowledge issue"))
            notice_ =
                service
                    .execute(AcknowledgeTechnicalDevelopmentIssueCommand{program.id, program.issue.signature})
                    .message;
    }
    for (const auto& report : program.reports)
        ImGui::BulletText("Report day %lld: stage %s, period work %.3f, tests %d, wait: %s",
                          static_cast<long long>(report.endDay), stageName(report.stage), report.periodWork,
                          report.testCount, report.waitingReason.c_str());
}

} // namespace deep::ui_imgui
