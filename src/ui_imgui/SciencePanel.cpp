// Compact evidence/analysis workflow in the existing ImGui shell. Scientific
// limitations and readiness come from records/queries, not UI arithmetic.
#include "ui_imgui/SciencePanel.h"
#include "ui_imgui/OperationalWindow.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
namespace deep::ui_imgui {
namespace {
template <class Id, class Rows> void choose(const char* label, std::optional<Id>& value, const Rows& rows) {
    std::string current = "Unassigned";
    for (const auto& r : rows)
        if (value == r.id)
            current = r.name;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("Unassigned", !value))
            value.reset();
        for (const auto& r : rows) {
            const auto text = r.name + " (#" + std::to_string(r.id.value) + ")";
            if (ImGui::Selectable(text.c_str(), value == r.id))
                value = r.id;
        }
        ImGui::EndCombo();
    }
}
const char* reading(ResourceIndication value) {
    switch (value) {
    case ResourceIndication::Detected:
        return "Detected indication";
    case ResourceIndication::NotDetectedWithinLimit:
        return "Not detected within this method's limit; absence not established";
    case ResourceIndication::InsufficientExposure:
        return "Insufficient exposure; not a negative measurement";
    }
    return "Invalid";
}
const char* access(AccessibilityReading value) {
    switch (value) {
    case AccessibilityReading::NotMeasured:
        return "Unmeasured";
    case AccessibilityReading::Low:
        return "Low [0, 0.25)";
    case AccessibilityReading::Moderate:
        return "Moderate [0.25, 0.75)";
    case AccessibilityReading::High:
        return "High [0.75, unbounded)";
    }
    return "Invalid";
}
} // namespace
void SciencePanel::resetWorldState() {
    draft_ = {};
    name_.fill('\0');
    editing_.reset();
    body_.reset();
    notice_.clear();
}
void SciencePanel::render(const SimulationQueries& queries, SimulationService& service, bool& visible) {
    if (!visible)
        return;
    if (!beginOperationalWindow("Evidence / Analysis", &visible)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText(editing_ ? "Amend analysis authority" : "Authorize analysis intent");
    ImGui::InputText("Name", name_.data(), name_.size());
    draft_.name = name_.data();
    ImGui::BeginDisabled(editing_.has_value());
    std::optional<ColonyId> colony = draft_.colonyId ? std::optional{draft_.colonyId} : std::nullopt;
    choose("Laboratory colony", colony, queries.colonies());
    draft_.colonyId = colony.value_or(ColonyId{});
    bool follow = std::holds_alternative<FollowSurveyInput>(draft_.source);
    if (ImGui::RadioButton("Follow one survey", follow))
        draft_.source = FollowSurveyInput{};
    ImGui::SameLine();
    if (ImGui::RadioButton("Fixed acquired batches", !follow))
        draft_.source = FixedBatchInput{};
    if (auto* source = std::get_if<FollowSurveyInput>(&draft_.source)) {
        struct Choice {
            SurveyProgramId id;
            std::string name;
        };
        std::vector<Choice> choices;
        for (const auto& p : queries.surveyPrograms())
            choices.push_back({p.id, p.charter.name});
        std::optional<SurveyProgramId> id =
            source->programId ? std::optional{source->programId} : std::nullopt;
        choose("Source survey", id, choices);
        source->programId = id.value_or(SurveyProgramId{});
    } else {
        auto& fixed = std::get<FixedBatchInput>(draft_.source).batches;
        for (const auto& batch : queries.acquiredObservations()) {
            bool selected = std::find(fixed.begin(), fixed.end(), batch.id) != fixed.end();
            const auto label = "Batch #" + std::to_string(batch.id.value) + " acquired day " +
                               std::to_string(batch.acquiredDay);
            if (ImGui::Checkbox(label.c_str(), &selected)) {
                if (selected)
                    fixed.push_back(batch.id);
                else
                    std::erase(fixed, batch.id);
            }
        }
        ImGui::TextUnformatted("Fixed inputs execute in selection order.");
    }
    ImGui::EndDisabled();
    choose("Scientific team", draft_.requestedTeamId, queries.surveyTeams());
    choose("Responsible leader", draft_.requestedLeaderId, queries.personnel());
    if (ImGui::TreeNode("Advanced work authority")) {
        bool limited = draft_.workAllowance.has_value();
        if (ImGui::Checkbox("Limit lifetime analyst team-workdays", &limited))
            draft_.workAllowance = limited ? std::optional{0.0} : std::nullopt;
        if (draft_.workAllowance)
            ImGui::InputDouble("Team-workdays", &*draft_.workAllowance);
        ImGui::TreePop();
    }
    const auto preview = queries.analysisDraftPreview(draft_);
    ImGui::TextWrapped("%s", (preview.valid ? preview.condition : preview.error).c_str());
    ImGui::BeginDisabled(!preview.valid);
    if (ImGui::Button(editing_ ? "Commit amendment" : "Authorize analysis")) {
        const auto result =
            editing_
                ? service.execute(AmendAnalysisProgramCommand{
                      *editing_,
                      {draft_.name, draft_.requestedTeamId, draft_.requestedLeaderId, draft_.workAllowance}})
                : service.execute(CreateAnalysisProgramCommand{draft_});
        notice_ = result.message;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("New draft")) {
        draft_ = {};
        name_.fill('\0');
        editing_.reset();
    }
    ImGui::TextWrapped("%s", notice_.c_str());
    ImGui::SeparatorText("Analysis commitments");
    for (const auto& row : queries.analysisPrograms()) {
        const auto& p = row.program;
        ImGui::PushID(static_cast<int>(p.id.value));
        if (ImGui::TreeNodeEx(p.charter.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("%s | %s", row.condition.c_str(), row.sourceStatus.c_str());
            ImGui::Text("Lab: %s; %.3f team-workdays/day", row.laboratory.c_str(), row.laboratoryCapacity);
            ImGui::Text("Requested scientist: %s | actual: %s", row.requestedTeam.c_str(),
                        row.actualTeam.c_str());
            ImGui::TextWrapped("Location: %s; owner: %s", row.teamLocation.c_str(), row.teamOwner.c_str());
            ImGui::Text("Actual work %.3f; current job remaining %.3f", row.workPerformed,
                        row.activeWorkRemaining);
            if (row.remainingAllowance)
                ImGui::Text("Remaining authorized work: %.3f", *row.remainingAllowance);
            if (row.currentJobEta)
                ImGui::Text("Current job ETA: %d d at current uncontested team/lab capacity",
                            *row.currentJobEta);
            else
                ImGui::TextUnformatted("Current job ETA unavailable; no promise for future follower inputs");
            if (p.lifecycle != AnalysisLifecycle::Closed) {
                if (ImGui::Button("Amend")) {
                    draft_ = p.charter;
                    editing_ = p.id;
                    std::snprintf(name_.data(), name_.size(), "%s", draft_.name.c_str());
                }
                ImGui::SameLine();
                if (p.lifecycle == AnalysisLifecycle::Suspended) {
                    if (ImGui::Button("Resume"))
                        notice_ = service.execute(ResumeAnalysisProgramCommand{p.id}).message;
                } else if (ImGui::Button("Suspend"))
                    notice_ = service.execute(SuspendAnalysisProgramCommand{p.id}).message;
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    notice_ = service.execute(CancelAnalysisProgramCommand{p.id}).message;
            }
            if (!p.issue.signature.empty()) {
                ImGui::TextWrapped("%s", p.issue.message.c_str());
                if (!p.issue.acknowledged && ImGui::Button("Acknowledge issue"))
                    notice_ =
                        service.execute(AcknowledgeAnalysisIssueCommand{p.id, p.issue.signature}).message;
            }
            for (const auto& job : p.jobs)
                ImGui::Text("Job #%lld: batch #%lld, start day %lld, outcome %s",
                            static_cast<long long>(job.id.value), static_cast<long long>(job.batchId.value),
                            static_cast<long long>(job.startedDay),
                            job.outcome == AnalysisJobOutcome::Active      ? "Active"
                            : job.outcome == AnalysisJobOutcome::Completed ? "Completed"
                                                                           : "Cancelled");
            if (ImGui::TreeNode("Actual dated labor")) {
                for (const auto& r : p.receipts)
                    ImGui::Text("Day %lld: job #%lld, scientist #%lld, leader #%lld, charter %d: %.3f",
                                static_cast<long long>(r.day), static_cast<long long>(r.jobId.value),
                                static_cast<long long>(r.teamId.value),
                                static_cast<long long>(r.leaderId.value), r.charterRevision, r.work);
                ImGui::TreePop();
            }
            for (const auto& r : p.reports)
                ImGui::TextWrapped("Report day %lld: %d completed; %.3f actual work; %d backlog. %s %s",
                                   static_cast<long long>(r.endDay), r.jobsCompleted, r.workPerformed,
                                   r.inputBacklog, r.sourceStatus.c_str(), r.limitations.c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Body evidence dossier");
    const auto acquired = queries.acquiredObservations();
    if (!body_ && !acquired.empty())
        body_ = acquired.back().bodyId;
    choose("Known body", body_, queries.bodySystemOverview());
    if (body_) {
        const auto dossier = queries.evidenceDossier(*body_);
        ImGui::TextUnformatted("Reserve quantity: Unmeasured. Construction/site suitability: Unassessed.");
        ImGui::TextWrapped(
            "Sampling uses completion-day physical state, not a five-day average. Information is available "
            "at D+1 regardless of distance; people remain at their real location.");
        if (dossier.observations.empty())
            ImGui::TextUnformatted("Unknown: no observations. This does not mean no resources.");
        for (const auto& batch : dossier.observations) {
            const auto label = "Raw batch #" + std::to_string(batch.id.value) + " acquired day " +
                               std::to_string(batch.acquiredDay);
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text(
                    "Field window %lld to %lld; available day %lld; fleet #%lld",
                    static_cast<long long>(batch.firstWorkDay), static_cast<long long>(batch.acquiredDay),
                    static_cast<long long>(batch.availableDay), static_cast<long long>(batch.fleetId.value));
                ImGui::Text("Observation age: %lld day(s)",
                    static_cast<long long>(queries.surveyTime().day-batch.acquiredDay));
                if (batch.surveyProgramId)
                    ImGui::Text("Survey #%lld pass %d; scientist #%lld",
                                static_cast<long long>(batch.surveyProgramId->value), batch.passNumber,
                                static_cast<long long>(batch.teamId->value));
                else
                    ImGui::TextUnformatted(
                        "Immediate manual acquisition; no invented team or elapsed field days");
                for (const auto& instrument : batch.instruments) {
                    ImGui::Text("Ship #%lld class #%lld component #%lld: %s v%d, threshold %.3f normalized "
                                "signal; exposure %d/5",
                                static_cast<long long>(instrument.exposure.shipId.value),
                                static_cast<long long>(instrument.exposure.classId.value),
                                static_cast<long long>(instrument.exposure.componentId.value),
                                instrument.profile.name.c_str(), instrument.profile.methodVersion,
                                instrument.profile.detectionThreshold, instrument.exposure.workdays);
                    for (const auto& c : instrument.channels)
                        ImGui::TextWrapped("%s: %s; accessibility %s",
                                           std::string(toString(c.mineral)).c_str(), reading(c.indication),
                                           access(c.accessibility));
                }
                ImGui::TreePop();
            }
        }
        for (const auto& a : dossier.assessments) {
            const auto label =
                "Assessment #" + std::to_string(a.id.value) + " revision " + std::to_string(a.revision);
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Published day %lld by job #%lld; %s; %s", static_cast<long long>(a.publishedDay),
                            static_cast<long long>(a.triggeringJobId.value),
                            a.contentChanged ? "Claim content changed" : "Claim content unchanged",
                            a.repeatedEvidence ? "Repeated evidence; no certainty gain"
                                               : "Additional recorded evidence");
                for (auto job : a.findingIds)
                    for (const auto& finding : dossier.findings)
                        if (finding.jobId == job)
                            ImGui::Text("Completed job #%lld -> batch #%lld, acquired day %lld",
                                        static_cast<long long>(job.value),
                                        static_cast<long long>(finding.batchId.value),
                                        static_cast<long long>(finding.acquiredDay));
                for (const auto& claim : a.claims) {
                    const char* indication =
                        claim.indication == IndicationAssessment::Indicated ? "Indicated"
                        : claim.indication == IndicationAssessment::NotDetectedWithinReportedLimits
                            ? "Not detected within reported limits"
                            : "Unknown";
                    const char* accessibility =
                        claim.accessibility == AccessibilityAssessment::Mixed
                            ? "Mixed recorded classes"
                            : access(static_cast<AccessibilityReading>(claim.accessibility));
                    ImGui::Text("%s: %s; accessibility %s", std::string(toString(claim.mineral)).c_str(),
                                indication, accessibility);
                    if (claim.indicationDay)
                        ImGui::Text("Scientific as-of day %lld",
                                    static_cast<long long>(*claim.indicationDay));
                    if (claim.accessibilityDay)
                        ImGui::Text("Accessibility as-of day %lld",
                                    static_cast<long long>(*claim.accessibilityDay));
                    for (auto job : claim.indicationInputs)
                        ImGui::Text("Indication provenance: job #%lld", static_cast<long long>(job.value));
                    for (auto job : claim.accessibilityInputs)
                        ImGui::Text("Accessibility provenance: job #%lld", static_cast<long long>(job.value));
                    if (claim.earlierIndication)
                        ImGui::TextUnformatted(
                            "Earlier indication; latest pass did not detect within its limits.");
                    for (const auto& alternative : claim.alternatives)
                        ImGui::TextWrapped("Method #%lld v%d at threshold %.3f: %s; %s",
                                           static_cast<long long>(alternative.profileId.value),
                                           alternative.methodVersion, alternative.threshold,
                                           reading(alternative.indication),
                                           access(alternative.accessibility));
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::End();
}
} // namespace deep::ui_imgui
