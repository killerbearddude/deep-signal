#include "ui_imgui/SurveyProgramsPanel.h"
#include "ui_imgui/OperationalWindow.h"

#include "sim/Commands.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

template <typename Id>
[[nodiscard]] std::string namedOption(const std::string& name, const Id id) {
    return name + " (#" + std::to_string(id.value) + ")";
}

[[nodiscard]] const char* optionalLabel(const std::string& name, const bool selected) {
    return selected ? name.c_str() : "Unassigned";
}

} // namespace

void SurveyProgramsPanel::resetWorldState() {
    draftName_.fill('\0');
    draft_ = SurveyProgramCharter{};
    selectedProgramId_.reset();
    editingProgramId_.reset();
    notice_ = "Ready";
    lastActionSucceeded_ = true;
}

SurveyProgramCharter SurveyProgramsPanel::currentCharter() const {
    SurveyProgramCharter charter = draft_;
    charter.name = draftName_.data();
    return charter;
}

void SurveyProgramsPanel::applyResult(const CommandResult& result) {
    lastActionSucceeded_ = result.ok;
    notice_ = result.message.empty() ? (result.ok ? "Action accepted" : "Action rejected") : result.message;
}

void SurveyProgramsPanel::editProgram(const SurveyProgramSummary& program) {
    draft_ = program.charter;
    if (program.pendingHomeColonyId) draft_.homeColonyId = *program.pendingHomeColonyId;
    draftName_.fill('\0');
    std::memcpy(draftName_.data(), draft_.name.data(), std::min(draft_.name.size(), draftName_.size() - 1));
    editingProgramId_ = program.id;
}

void SurveyProgramsPanel::render(const SimulationQueries& queries, SimulationService& service, bool& visible) {
    if (!visible) return;
    if (!beginOperationalWindow("Survey Programs", &visible)) {
        ImGui::End();
        return;
    }

    // Query at the point of use. Commands may change the world during this frame,
    // so detail is refreshed after the editor and never borrows GameState records.
    renderEditor(queries, service);
    ImGui::SeparatorText("Programs");
    const std::vector<SurveyProgramSummary> programs = queries.surveyPrograms();
    if (programs.empty()) {
        selectedProgramId_.reset();
        ImGui::TextUnformatted("No programs authorized yet.");
    } else {
        const auto selected = std::find_if(programs.begin(), programs.end(), [this](const auto& row) {
            return selectedProgramId_ == row.id;
        });
        if (selected == programs.end()) selectedProgramId_ = programs.front().id;
        if (ImGui::BeginTable("SurveyProgramsOverview", 8,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("Program");
            ImGui::TableSetupColumn("Lifecycle");
            ImGui::TableSetupColumn("Condition");
            ImGui::TableSetupColumn("Current task");
            ImGui::TableSetupColumn("Visits");
            ImGui::TableSetupColumn("Leader");
            ImGui::TableSetupColumn("Known issue");
            ImGui::TableSetupColumn("Next report");
            ImGui::TableHeadersRow();
            for (const SurveyProgramSummary& row : programs) {
                ImGui::PushID(static_cast<int>(row.id.value));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(row.charter.name.c_str(), selectedProgramId_ == row.id,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    selectedProgramId_ = row.id;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.lifecycleName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", row.condition.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.taskName.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d/%d", row.completedVisits, row.requestedVisits);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(row.leaderName.c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextWrapped("%s", row.issueMessage.empty() ? "-" : row.issueMessage.c_str());
                ImGui::TableSetColumnIndex(7);
                ImGui::Text("Day %lld", static_cast<long long>(row.nextReportDay));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        const auto current = std::find_if(programs.begin(), programs.end(), [this](const auto& row) {
            return selectedProgramId_ == row.id;
        });
        if (current != programs.end()) renderProgramDetail(*current, queries, service);
    }

    ImGui::Separator();
    ImGui::Text("Last action: %s", lastActionSucceeded_ ? "Accepted" : "Rejected");
    ImGui::TextWrapped("%s", notice_.c_str());
    ImGui::End();
}

void SurveyProgramsPanel::renderEditor(const SimulationQueries& queries, SimulationService& service) {
    if (!ImGui::CollapsingHeader("Authorize or amend charter", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const std::vector<ColonySummary> colonies = queries.colonies();
    const std::vector<FleetSummary> fleets = queries.fleets();
    const std::vector<SurveyTeamSummary> teams = queries.surveyTeams();
    const std::vector<PersonSummary> leaders = queries.personnel();
    const std::vector<StrategicBodySummary> bodies = queries.strategicBodies();

    if (!draft_.homeColonyId && !colonies.empty()) draft_.homeColonyId = colonies.front().id;
    if (editingProgramId_) {
        ImGui::Text("Amending program #%lld", static_cast<long long>(editingProgramId_->value));
        ImGui::SameLine();
        if (ImGui::SmallButton("New charter")) {
            draftName_.fill('\0');
            draft_ = SurveyProgramCharter{};
            editingProgramId_.reset();
            if (!colonies.empty()) draft_.homeColonyId = colonies.front().id;
        }
    }
    ImGui::InputText("Name", draftName_.data(), draftName_.size());

    std::string homeLabel = "Select home colony";
    for (const ColonySummary& row : colonies) {
        if (row.id == draft_.homeColonyId) homeLabel = namedOption(row.name, row.id);
    }
    if (ImGui::BeginCombo("Home colony", homeLabel.c_str())) {
        for (const ColonySummary& row : colonies) {
            const std::string option = namedOption(row.name, row.id);
            if (ImGui::Selectable(option.c_str(), draft_.homeColonyId == row.id)) draft_.homeColonyId = row.id;
        }
        ImGui::EndCombo();
    }

    std::string fleetLabel;
    for (const FleetSummary& row : fleets) {
        if (draft_.requestedFleetId == row.id) fleetLabel = namedOption(row.name, row.id);
    }
    if (ImGui::BeginCombo("Requested fleet", optionalLabel(fleetLabel, !fleetLabel.empty()))) {
        if (ImGui::Selectable("Unassigned", !draft_.requestedFleetId)) draft_.requestedFleetId.reset();
        for (const FleetSummary& row : fleets) {
            const std::string option = namedOption(row.name, row.id) + " (fuel "
                + std::to_string(static_cast<int>(row.currentFuel)) + "/"
                + std::to_string(static_cast<int>(row.fuelCapacity)) + ")";
            if (ImGui::Selectable(option.c_str(), draft_.requestedFleetId == row.id)) draft_.requestedFleetId = row.id;
            if (row.controllingProgramId) {
                ImGui::SetItemTooltip("Currently controlled by program #%lld: %s",
                    static_cast<long long>(row.controllingProgramId->value), row.controllingProgramName.c_str());
            }
        }
        ImGui::EndCombo();
    }

    std::string teamLabel;
    for (const SurveyTeamSummary& row : teams) {
        if (draft_.requestedTeamId == row.id) teamLabel = namedOption(row.name, row.id);
    }
    if (ImGui::BeginCombo("Requested team", optionalLabel(teamLabel, !teamLabel.empty()))) {
        if (ImGui::Selectable("Unassigned", !draft_.requestedTeamId)) draft_.requestedTeamId.reset();
        for (const SurveyTeamSummary& row : teams) {
            const std::string option = namedOption(row.name, row.id) + " (" + row.locationName + ")";
            if (ImGui::Selectable(option.c_str(), draft_.requestedTeamId == row.id)) draft_.requestedTeamId = row.id;
        }
        ImGui::EndCombo();
    }

    std::string leaderLabel;
    for (const PersonSummary& row : leaders) {
        if (draft_.requestedLeaderId == row.id) leaderLabel = namedOption(row.name, row.id);
    }
    if (ImGui::BeginCombo("Program leader", optionalLabel(leaderLabel, !leaderLabel.empty()))) {
        if (ImGui::Selectable("Unassigned", !draft_.requestedLeaderId)) draft_.requestedLeaderId.reset();
        for (const PersonSummary& row : leaders) {
            const std::string option = namedOption(row.name, row.id) + " (" + row.surveyPlanningApproachName + ")";
            if (ImGui::Selectable(option.c_str(), draft_.requestedLeaderId == row.id)) draft_.requestedLeaderId = row.id;
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Add target", "Select a body")) {
        for (const StrategicBodySummary& body : bodies) {
            const bool alreadyAdded = std::any_of(draft_.targets.begin(), draft_.targets.end(),
                [&body](const SurveyProgramTarget& row) { return row.bodyId == body.id; });
            if (alreadyAdded) continue;
            const std::string option = namedOption(body.name, body.id);
            if (ImGui::Selectable(option.c_str())) {
                draft_.targets.push_back(SurveyProgramTarget{.bodyId = body.id, .priority = 0, .requestedPasses = 1});
            }
        }
        ImGui::EndCombo();
    }
    if (draft_.targets.empty()) ImGui::TextUnformatted("Add at least one target body.");
    for (std::size_t i = 0; i < draft_.targets.size(); ++i) {
        SurveyProgramTarget& target = draft_.targets[i];
        const auto body = std::find_if(bodies.begin(), bodies.end(), [&target](const auto& row) {
            return row.id == target.bodyId;
        });
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%zu. %s", i + 1, body == bodies.end() ? "<missing body>" : body->name.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(78.0F);
        ImGui::InputInt("Priority", &target.priority);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(78.0F);
        ImGui::InputInt("Passes", &target.requestedPasses);
        ImGui::SameLine();
        const bool moveUp = i > 0 && ImGui::SmallButton("Up");
        if (i > 0) ImGui::SameLine();
        const bool moveDown = i + 1 < draft_.targets.size() && ImGui::SmallButton("Down");
        if (i + 1 < draft_.targets.size()) ImGui::SameLine();
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::PopID();
        if (moveUp) {
            std::iter_swap(draft_.targets.begin() + static_cast<std::ptrdiff_t>(i),
                draft_.targets.begin() + static_cast<std::ptrdiff_t>(i - 1));
            break;
        }
        if (moveDown) {
            std::iter_swap(draft_.targets.begin() + static_cast<std::ptrdiff_t>(i),
                draft_.targets.begin() + static_cast<std::ptrdiff_t>(i + 1));
            break;
        }
        if (remove) {
            draft_.targets.erase(draft_.targets.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }

    bool capped = draft_.policy.maxAdditionalPropellant.has_value();
    if (ImGui::Checkbox("Limit additional propellant", &capped)) {
        if (capped) draft_.policy.maxAdditionalPropellant = 0.0;
        else draft_.policy.maxAdditionalPropellant.reset();
    }
    if (draft_.policy.maxAdditionalPropellant) {
        ImGui::InputDouble("Additional propellant limit", &*draft_.policy.maxAdditionalPropellant, 1.0, 10.0, "%.2f");
    }
    ImGui::InputDouble("Home stock floor", &draft_.policy.homeStockFloor, 1.0, 10.0, "%.2f");
    ImGui::InputDouble("Return contingency fraction", &draft_.policy.returnContingencyFraction, 0.05, 0.2, "%.2f");

    const SurveyProgramCharter charter = currentCharter();
    const SurveyProgramCharterPreview preview = queries.previewSurveyProgramCharter(charter, editingProgramId_);
    ImGui::TextWrapped("%s", preview.validationMessage.c_str());
    if (!preview.executionCondition.empty()) {
        ImGui::TextWrapped("Known execution outlook: %s", preview.executionCondition.c_str());
    }
    for (const std::string& reason : preview.waitingReasons) {
        ImGui::BulletText("May wait: %s", reason.c_str());
    }
    if (!preview.firstTargetChoiceReason.empty()) ImGui::TextWrapped("%s", preview.firstTargetChoiceReason.c_str());

    if (ImGui::Button(editingProgramId_ ? "Amend charter" : "Authorize program")) {
        if (editingProgramId_) {
            applyResult(service.execute(AmendSurveyProgramCommand{.programId = *editingProgramId_, .charter = charter}));
            if (lastActionSucceeded_) selectedProgramId_ = editingProgramId_;
        } else {
            applyResult(service.execute(CreateSurveyProgramCommand{.charter = charter}));
            if (lastActionSucceeded_) {
                const auto created = queries.surveyPrograms();
                if (!created.empty()) selectedProgramId_ = created.back().id;
                draftName_.fill('\0');
                draft_ = SurveyProgramCharter{};
                if (!colonies.empty()) draft_.homeColonyId = colonies.front().id;
            }
        }
    }
}

void SurveyProgramsPanel::renderProgramDetail(const SurveyProgramSummary& program,
                                              const SimulationQueries& queries,
                                              SimulationService& service) {
    ImGui::SeparatorText("Selected program");
    ImGui::Text("%s (#%lld, charter revision %d)", program.charter.name.c_str(),
        static_cast<long long>(program.id.value), program.charterRevision);
    ImGui::Text("Lifecycle: %s", program.lifecycleName.c_str());
    ImGui::TextWrapped("Condition: %s", program.condition.c_str());
    if (program.lifecycle == SurveyProgramLifecycle::Authorized && !program.leasedFleetId) {
        const SurveyProgramCharterPreview preview = queries.previewSurveyProgramCharter(program.charter, program.id);
        for (const std::string& reason : preview.waitingReasons) {
            ImGui::BulletText("Current known limit: %s", reason.c_str());
        }
    }
    ImGui::Text("Current task: %s; pass %d; work %d day(s)", program.taskName.c_str(),
        program.taskPassNumber, program.workDaysCompleted);
    ImGui::Text("Home: %s | Fleet: %s | Team: %s", program.homeName.c_str(),
        program.requestedFleetName.c_str(), program.requestedTeamName.c_str());
    if (!program.pendingHomeName.empty()) {
        ImGui::TextWrapped("Pending home change to %s after committed work reaches a safe boundary.",
            program.pendingHomeName.c_str());
    }
    ImGui::Text("Leader: %s (%s)", program.leaderName.c_str(),
        program.leaderApproachName.empty() ? "no approach" : program.leaderApproachName.c_str());
    ImGui::Text("Actual lease: %s / %s", program.leasedFleetName.c_str(), program.leasedTeamName.c_str());
    if (program.taskFleetId || program.taskTeamId) {
        ImGui::Text("Partial task assets: %s / %s; leader %s", program.taskFleetName.c_str(),
            program.taskTeamName.c_str(), program.taskLeaderName.c_str());
        if (program.taskFleetId != program.charter.requestedFleetId ||
            program.taskTeamId != program.charter.requestedTeamId) {
            ImGui::TextWrapped("The amended request differs from assets tied to partial work. Continuation requires the recorded task assets at its actual location.");
        }
    }
    ImGui::TextWrapped("Physical location: %s", program.currentLocationName.c_str());
    if (!program.teamLocationName.empty() && program.leasedFleetId) {
        ImGui::TextWrapped("Team location: %s", program.teamLocationName.c_str());
    }
    ImGui::Text("Visits: %d/%d | Fuel loaded: %.2f | Fuel burned: %.2f", program.completedVisits,
        program.requestedVisits, program.fuelLoaded, program.fuelBurned);
    if (program.historicalCompletedVisits != program.completedVisits) {
        ImGui::Text("Lifetime visit receipts: %d (including earlier charter work)",
            program.historicalCompletedVisits);
    }
    ImGui::Text("Next report: day %lld", static_cast<long long>(program.nextReportDay));
    if (!program.targetChoiceReason.empty()) ImGui::TextWrapped("Target choice: %s", program.targetChoiceReason.c_str());
    for (const SurveyProgramTargetSummary& target : program.targets) {
        ImGui::BulletText("%s: %d/%d pass(es), priority %d", target.bodyName.c_str(),
            target.completedPasses, target.requestedPasses, target.priority);
    }
    if (!program.issueSignature.empty()) {
        ImGui::TextWrapped("Issue: %s (%s)", program.issueMessage.c_str(),
            program.issueAcknowledged ? "acknowledged" : "decision needed");
        if (!program.issueAcknowledged && ImGui::Button("Acknowledge; keep waiting")) {
            applyResult(service.execute(AcknowledgeSurveyProgramIssueCommand{
                .programId = program.id, .signature = program.issueSignature
            }));
        }
    }

    if (ImGui::Button("Edit charter")) editProgram(program);
    if (program.lifecycle == SurveyProgramLifecycle::Authorized) {
        ImGui::SameLine();
        if (ImGui::Button("Suspend")) applyResult(service.execute(SuspendSurveyProgramCommand{program.id}));
    } else if (program.lifecycle == SurveyProgramLifecycle::Suspended) {
        ImGui::SameLine();
        if (ImGui::Button("Resume")) applyResult(service.execute(ResumeSurveyProgramCommand{program.id}));
    }
    if (program.lifecycle != SurveyProgramLifecycle::Closed && program.lifecycle != SurveyProgramLifecycle::Closing) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) applyResult(service.execute(CancelSurveyProgramCommand{program.id}));
    }

    if (ImGui::CollapsingHeader("Reports")) {
        if (program.reports.empty()) ImGui::TextUnformatted("No report boundary reached yet.");
        for (auto it = program.reports.rbegin(); it != program.reports.rend(); ++it) {
            const SurveyProgramReportSummary& report = *it;
            ImGui::PushID(static_cast<int>(report.endDay));
            const std::string label = std::string{report.isNinetyDayReview ? "90-day review" : "30-day report"}
                + ": days " + std::to_string(report.startDay) + "-" + std::to_string(report.endDay);
            if (ImGui::TreeNode(label.c_str())) {
                ImGui::Text("Charter revision %d | Leader %s (%s)", report.charterRevision,
                    report.leaderName.c_str(), report.approachName.c_str());
                ImGui::Text("Visits %d | Work %lld day(s)", report.visitsCompleted,
                    static_cast<long long>(report.workDays));
                ImGui::Text("Fuel loaded %.2f | burned %.2f", report.fuelLoaded, report.fuelBurned);
                ImGui::Text("Assignment: %s / %s at %s", report.fleetName.c_str(),
                    report.teamName.c_str(), report.fleetBodyName.c_str());
                if (!report.waitingReason.empty()) ImGui::TextWrapped("Known limitation: %s", report.waitingReason.c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

} // namespace deep::ui_imgui
