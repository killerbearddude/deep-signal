// Colony-supported workshop authoring, physical equipment and accountable work
// history. Readiness comes from pure simulation rules through query DTOs.
#include "ui_imgui/MaintenanceProgramsPanel.h"
#include "ui_imgui/OperationalWindow.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace deep::ui_imgui {
namespace {
template <class Id>
void selectOptional(const char* label, std::optional<Id>& value,
                    const std::vector<std::pair<Id, std::string>>& choices) {
    std::string current = "Unassigned";
    for (const auto& [id, name] : choices)
        if (value == id)
            current = name + " (#" + std::to_string(id.value) + ")";
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("Unassigned", !value))
            value.reset();
        for (const auto& [id, name] : choices) {
            const auto title = name + " (#" + std::to_string(id.value) + ")";
            if (ImGui::Selectable(title.c_str(), value == id))
                value = id;
        }
        ImGui::EndCombo();
    }
}
void materialSet(const char* label, const ProcessedMaterialSet& set) {
    ImGui::TextUnformatted(label);
    bool any = false;
    for (std::size_t j = 0; j < processedMaterialCount(); ++j)
        if (set.amount[j] > 0.0) {
            any = true;
            ImGui::Text("%s: %.3f", std::string(toString(static_cast<ProcessedMaterial>(j))).c_str(),
                        set.amount[j]);
        }
    if (!any)
        ImGui::TextUnformatted("All quantities are zero.");
}
} // namespace
void MaintenanceProgramsPanel::resetWorldState() {
    name_.fill('\0');
    draft_ = {};
    editing_.reset();
    selected_.reset();
    notice_ = "Ready";
}
void MaintenanceProgramsPanel::render(const SimulationQueries& queries, SimulationService& service,
                                      bool& visible) {
    if (!visible)
        return;
    if (!beginOperationalWindow("Maintenance / Support Programs", &visible)) {
        ImGui::End();
        return;
    }
    renderEditor(queries, service);
    ImGui::SeparatorText("Standing providers");
    const auto programs = queries.maintenancePrograms();
    if (programs.empty()) {
        selected_.reset();
        ImGui::TextUnformatted("No maintenance providers authorized yet.");
    } else {
        if (std::none_of(programs.begin(), programs.end(),
                         [&](const auto& p) { return p.program.id == selected_; }))
            selected_ = programs.front().program.id;
        for (const auto& p : programs) {
            const auto label = p.program.charter.name + " (#" + std::to_string(p.program.id.value) + ")";
            if (ImGui::Selectable(label.c_str(), selected_ == p.program.id))
                selected_ = p.program.id;
        }
        const auto selected = std::find_if(programs.begin(), programs.end(),
                                           [&](const auto& p) { return p.program.id == selected_; });
        if (selected != programs.end())
            renderDetail(*selected, queries, service);
    }
    ImGui::SeparatorText("Finite engineering teams");
    for (const auto& row : queries.maintenanceTeams()) {
        ImGui::Text("%s at %s | %.3f team-workdays/day", row.team.name.c_str(), row.locationName.c_str(),
                    row.team.workdaysPerDay);
        for (const auto& family : row.qualifiedFamilyNames)
            ImGui::BulletText("Qualified: %s", family.c_str());
        if (row.controllingProgramId)
            ImGui::Text("Leased to maintenance #%lld",
                        static_cast<long long>(row.controllingProgramId->value));
    }
    ImGui::TextWrapped("Last action: %s", notice_.c_str());
    ImGui::End();
}
void MaintenanceProgramsPanel::renderEditor(const SimulationQueries& queries, SimulationService& service) {
    ImGui::SeparatorText(editing_ ? "Amend support commitment" : "Authorize standing support");
    ImGui::InputText("Provider name", name_.data(), name_.size());
    draft_.name = name_.data();
    const auto colonies = queries.colonies();
    if (!draft_.serviceColonyId && !colonies.empty())
        draft_.serviceColonyId = colonies.front().id;
    std::string colonyName = "Select service colony";
    for (const auto& row : colonies)
        if (row.id == draft_.serviceColonyId)
            colonyName = row.name;
    ImGui::BeginDisabled(editing_.has_value());
    if (ImGui::BeginCombo("Fixed service colony", colonyName.c_str())) {
        for (const auto& row : colonies)
            if (ImGui::Selectable(row.name.c_str(), row.id == draft_.serviceColonyId))
                draft_.serviceColonyId = row.id;
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    std::vector<std::pair<FleetId, std::string>> fleets;
    for (const auto& row : queries.fleets())
        fleets.push_back({row.id, row.name});
    selectOptional("Tender fleet", draft_.requestedTenderId, fleets);
    std::vector<std::pair<MaintenanceTeamId, std::string>> teams;
    for (const auto& row : queries.maintenanceTeams())
        teams.push_back({row.team.id, row.team.name});
    selectOptional("Engineering team", draft_.requestedTeamId, teams);
    std::vector<std::pair<PersonId, std::string>> leaders;
    for (const auto& row : queries.personnel())
        leaders.push_back({row.id, row.name});
    selectOptional("Responsible leader", draft_.requestedLeaderId, leaders);
    ImGui::TextUnformatted("Authorized clients (selection order is service priority)");
    for (const auto& [id, name] : fleets) {
        bool checked = std::find(draft_.clients.begin(), draft_.clients.end(), id) != draft_.clients.end();
        const auto title = name + "##client" + std::to_string(id.value);
        if (ImGui::Checkbox(title.c_str(), &checked)) {
            if (checked)
                draft_.clients.push_back(id);
            else
                std::erase(draft_.clients, id);
        }
    }
    if (ImGui::TreeNode("Advanced supply floors and lifetime consumption allowances")) {
        bool limited = draft_.policy.lifetimeAllowances.has_value();
        if (ImGui::Checkbox("Limit material consumption", &limited))
            draft_.policy.lifetimeAllowances = limited ? std::optional{ProcessedMaterialSet{}} : std::nullopt;
        ImGui::TextWrapped("A zero allowance is zero permission. Supplies must be real colony stock; freight "
                           "cargo is unavailable until unloaded.");
        for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
            ImGui::PushID(static_cast<int>(j));
            const auto name = std::string(toString(static_cast<ProcessedMaterial>(j)));
            ImGui::TextUnformatted(name.c_str());
            ImGui::InputDouble("Stock floor", &draft_.policy.floors.amount[j], 1.0, 10.0, "%.3f");
            if (limited)
                ImGui::InputDouble("Lifetime allowance", &draft_.policy.lifetimeAllowances->amount[j], 1.0,
                                   10.0, "%.3f");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    const auto preview = queries.previewMaintenanceCharter(draft_, editing_);
    ImGui::TextWrapped("%s", preview.structurallyValid ? preview.condition.c_str()
                                                       : preview.validationMessage.c_str());
    ImGui::BeginDisabled(!preview.structurallyValid);
    if (ImGui::Button(editing_ ? "Save support amendment" : "Authorize support")) {
        const auto result = editing_ ? service.execute(AmendMaintenanceProgramCommand{
                                           *editing_, maintenanceAmendmentFromCharter(draft_)})
                                     : service.execute(CreateMaintenanceProgramCommand{draft_});
        notice_ = result.message;
        if (result.ok)
            editing_.reset();
    }
    ImGui::EndDisabled();
    if (editing_) {
        ImGui::SameLine();
        if (ImGui::Button("New provider draft")) {
            editing_.reset();
            draft_ = {};
            name_.fill('\0');
        }
    }
}
void MaintenanceProgramsPanel::renderDetail(const MaintenanceProgramSummary& row,
                                            const SimulationQueries& queries, SimulationService& service) {
    const auto& p = row.program;
    ImGui::SeparatorText("Actual service status");
    ImGui::TextWrapped("%s", row.condition.c_str());
    ImGui::Text("Service colony: %s | Tender: %s | Team: %s | Leader: %s", row.colonyName.c_str(),
                row.tenderName.c_str(), row.teamName.c_str(), row.leaderName.c_str());
    ImGui::TextUnformatted(
        "The client retains survey movement control. Service is a hold, not a second lease.");
    if (p.lifecycle != MaintenanceProgramLifecycle::Closed) {
        if (ImGui::Button("Amend support")) {
            editing_ = p.id;
            draft_ = p.charter;
            name_.fill('\0');
            std::memcpy(name_.data(), draft_.name.data(), std::min(draft_.name.size(), name_.size() - 1));
        }
        ImGui::SameLine();
        if (p.lifecycle == MaintenanceProgramLifecycle::Authorized) {
            if (ImGui::Button("Suspend support"))
                notice_ = service.execute(SuspendMaintenanceProgramCommand{p.id}).message;
        } else if (ImGui::Button("Resume support"))
            notice_ = service.execute(ResumeMaintenanceProgramCommand{p.id}).message;
        ImGui::SameLine();
        if (ImGui::Button("Cancel support"))
            notice_ = service.execute(CancelMaintenanceProgramCommand{p.id}).message;
    }
    if (!p.issue.signature.empty() && !p.issue.acknowledged &&
        ImGui::Button("Acknowledge maintenance issue")) {
        notice_ = service.execute(AcknowledgeMaintenanceIssueCommand{p.id, p.issue.signature}).message;
    }
    const auto families = queries.equipmentFamilies();
    for (const auto& rate : row.operationalWorkshops) {
        auto family = std::find_if(families.begin(), families.end(),
                                   [&](const auto& f) { return f.id == rate.familyId; });
        ImGui::Text("Workshop %s: %.3f compatible team-workdays/day on first powered hull",
                    family == families.end() ? "unknown" : family->name.c_str(), rate.teamWorkdaysPerDay);
    }
    ImGui::Text("Restored instrument duty: %.3f | Engineering work: %.3f", p.restoredDuty, p.teamWorkdays);
    materialSet("Materials actually consumed", p.consumed);
    materialSet("Current protected stock floors", p.charter.policy.floors);
    if (p.charter.policy.lifetimeAllowances)
        materialSet("Current lifetime consumption allowances (zero means no permission)",
                    *p.charter.policy.lifetimeAllowances);
    else
        ImGui::TextUnformatted("Current lifetime allowances: no program cap");
    ImGui::SeparatorText("Pending requests in client priority order");
    for (const auto& request : row.pendingClients)
        ImGui::BulletText("%s: %s", request.name.c_str(), request.condition.c_str());
    if (const auto* job = activeServiceJob(p)) {
        ImGui::Text(
            "Active full-service job %d | client fleet #%lld | original provider/client revisions %d/%d",
            job->number, static_cast<long long>(job->clientFleetId.value), job->providerRevision,
            job->clientRevision);
        if (job->workshopShipId)
            ImGui::Text("Pinned workshop hull #%lld | target group %d",
                        static_cast<long long>(job->workshopShipId->value),
                        job->activeTarget.value_or(0) + 1);
        ImGui::TextWrapped("Next work: %s", row.nextWork.condition.c_str());
    }
    for (const auto& equipment : row.clientEquipment) {
        ImGui::Text("%s / %s: %.3f of %.3f usable survey duty remaining per unit (quantity %d)",
                    equipment.shipName.c_str(), equipment.componentName.c_str(), equipment.remainingDuty,
                    equipment.dutyCapacity, equipment.quantity);
        ImGui::Text("Remaining full-service work: %.3f team-workdays",
                    equipment.fullServiceNeed.teamWorkdays);
        materialSet("Remaining full-service materials", equipment.fullServiceNeed.consumed);
    }
    if (ImGui::TreeNode("Actual service work history")) {
        for (const auto& receipt : p.receipts)
            ImGui::Text("Day %lld job %d: ship #%lld component #%lld workshop #%lld restored %.3f duty, %.3f "
                        "team-workdays",
                        static_cast<long long>(receipt.day), receipt.jobNumber,
                        static_cast<long long>(receipt.clientShipId.value),
                        static_cast<long long>(receipt.componentId.value),
                        static_cast<long long>(receipt.workshopShipId.value), receipt.restoredDuty,
                        receipt.teamWorkdays);
        for (const auto& job : p.jobs)
            if (job.endedDay)
                ImGui::Text("Job %d ended day %lld: %s", job.number, static_cast<long long>(*job.endedDay),
                            job.endReason.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Maintenance reports")) {
        for (const auto& report : p.reports) {
            ImGui::Text("Days %lld-%lld%s: completed %d, withdrawn %d, restored %.3f, work %.3f",
                        static_cast<long long>(report.startDay), static_cast<long long>(report.endDay),
                        report.isNinetyDayReview ? " (90-day review)" : "", report.jobsCompleted,
                        report.jobsWithdrawn, report.restoredDuty, report.teamWorkdays);
            ImGui::TextWrapped("%s", report.waitingReason.c_str());
            materialSet("Period consumption", report.consumed);
            ImGui::Text("Reported engineering capacity %.3f/day | next-group compatible workshop %.3f/day",
                        report.availableTeamRate, report.compatibleWorkshopRate);
            materialSet("Historical stock floors", report.policy.floors);
            if (report.policy.lifetimeAllowances)
                materialSet("Historical lifetime allowances", *report.policy.lifetimeAllowances);
            else
                ImGui::TextUnformatted("Historical lifetime allowances: no program cap");
        }
        ImGui::TreePop();
    }
}
} // namespace deep::ui_imgui
