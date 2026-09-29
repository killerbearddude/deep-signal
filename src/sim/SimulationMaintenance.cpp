// Simulation command boundary for standing service authority. Only the daily
// executor consumes materials/restores duty; commands preserve physical progress.
#include "sim/MaintenanceProgramExecution.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/Simulation.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace deep {
namespace {
MaintenanceProgram* find(std::vector<MaintenanceProgram>& rows, MaintenanceProgramId id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
} // namespace
CommandResult Simulation::createMaintenanceProgram(const CreateMaintenanceProgramCommand& command) {
    const auto reject = [this](const std::string& why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    };
    if (const auto error = validateMaintenanceCharter(state_, command.charter))
        return reject(*error);
    if (state_.ids.nextMaintenanceProgramId == std::numeric_limits<std::int64_t>::max())
        return reject("Maintenance ID limit reached");
    MaintenanceProgram prepared;
    prepared.id = MaintenanceProgramId{state_.ids.nextMaintenanceProgramId};
    prepared.charter = command.charter;
    prepared.createdDay = state_.date.day;
    prepared.reportStartDay = state_.date.day;
    try {
        prepared.nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30);
    } catch (const std::exception&) {
        return reject("Maintenance reporting date limit reached");
    }
    state_.maintenancePrograms.push_back(std::move(prepared));
    ++state_.ids.nextMaintenanceProgramId;
    const auto& p = state_.maintenancePrograms.back();
    appendEvent(EventSeverity::Info, MaintenanceProgramAuditEvent{
                                         p.id, MaintenanceAuditKind::Authorized, 0,
                                         "Standing maintenance authorized; no assets or supplies reserved"});
    return CommandResult::success("Maintenance authorized; " + maintenanceExecutionCondition(state_, p));
}
CommandResult Simulation::amendMaintenanceProgram(const AmendMaintenanceProgramCommand& command) {
    const auto reject = [this](const std::string& why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    };
    auto* p = find(state_.maintenancePrograms, command.programId);
    if (!p || p->lifecycle == MaintenanceProgramLifecycle::Closed)
        return reject("Maintenance provider is missing or closed");
    if (p->charterRevision == std::numeric_limits<int>::max())
        return reject("Maintenance charter revision limit reached");
    auto prepared = p->charter;
    applyMaintenanceAmendment(prepared, command.amendment);
    if (const auto error = validateMaintenanceCharter(state_, prepared))
        return reject(*error);
    const auto* job = activeServiceJob(*p);
    const bool resourcesChanged = prepared.requestedTenderId != p->charter.requestedTenderId ||
                                  prepared.requestedTeamId != p->charter.requestedTeamId ||
                                  prepared.requestedLeaderId != p->charter.requestedLeaderId;
    const bool removesClient = job && std::find(prepared.clients.begin(), prepared.clients.end(),
                                                job->clientFleetId) == prepared.clients.end();
    const MaintenanceExecutionHooks hooks{
        [this](EventSeverity severity, SimEventPayload event) { appendEvent(severity, std::move(event)); }};
    if (resourcesChanged || removesClient) {
        withdrawMaintenanceJob(state_, *p, "Provider assignment or active client scope amended", hooks);
        releaseMaintenanceAssets(state_, *p);
    }
    std::swap(p->charter, prepared);
    ++p->charterRevision;
    acknowledgeKnownMaintenanceLimit(state_, *p);
    appendEvent(EventSeverity::Info,
                MaintenanceProgramAuditEvent{
                    p->id, MaintenanceAuditKind::Amended, 0,
                    "Maintenance charter amended; prior service and material expenditure retained"});
    return CommandResult::success("Maintenance charter amended");
}
CommandResult Simulation::setMaintenanceLifecycle(MaintenanceProgramId id,
                                                  MaintenanceProgramLifecycle target) {
    auto* p = find(state_.maintenancePrograms, id);
    if (!p || p->lifecycle == MaintenanceProgramLifecycle::Closed || p->lifecycle == target ||
        (target == MaintenanceProgramLifecycle::Authorized &&
         p->lifecycle != MaintenanceProgramLifecycle::Suspended)) {
        const std::string reason = "Maintenance lifecycle action is unavailable for this state";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    const MaintenanceExecutionHooks hooks{
        [this](EventSeverity severity, SimEventPayload event) { appendEvent(severity, std::move(event)); }};
    if (target != MaintenanceProgramLifecycle::Authorized) {
        withdrawMaintenanceJob(state_, *p,
                               target == MaintenanceProgramLifecycle::Closed ? "Provider cancelled"
                                                                             : "Provider suspended",
                               hooks);
        releaseMaintenanceAssets(state_, *p);
    }
    p->lifecycle = target;
    p->issue.acknowledged = true;
    auto kind = MaintenanceAuditKind::Resumed;
    std::string detail = "Maintenance provider resumed; real resources will be reacquired";
    if (target == MaintenanceProgramLifecycle::Suspended) {
        kind = MaintenanceAuditKind::Suspended;
        detail = "Maintenance suspended; partial restoration retained";
    }
    if (target == MaintenanceProgramLifecycle::Closed) {
        p->closedDay = state_.date.day;
        p->issue = {};
        kind = MaintenanceAuditKind::Cancelled;
        std::ostringstream summary;
        summary << "Maintenance closed; restored instrument duty=" << p->restoredDuty
                << "; team-workdays=" << p->teamWorkdays << "; consumed=";
        for (double amount : p->consumed.amount)
            summary << amount << ' ';
        detail = summary.str();
    }
    appendEvent(EventSeverity::Info, MaintenanceProgramAuditEvent{p->id, kind, 0, detail});
    publishMaintenanceReportIfDue(state_, *p, hooks);
    return CommandResult::success(detail);
}
CommandResult Simulation::acknowledgeMaintenanceIssue(const AcknowledgeMaintenanceIssueCommand& command) {
    auto* p = find(state_.maintenancePrograms, command.programId);
    if (!p || p->issue.signature.empty() || p->issue.acknowledged ||
        p->issue.signature != command.signature) {
        const std::string reason = "Maintenance issue identity is not pending";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    p->issue.acknowledged = true;
    appendEvent(
        EventSeverity::Info,
        MaintenanceProgramAuditEvent{p->id, MaintenanceAuditKind::IssueAcknowledged, 0, command.signature});
    return CommandResult::success("Maintenance issue acknowledged; physical limits unchanged");
}
} // namespace deep
