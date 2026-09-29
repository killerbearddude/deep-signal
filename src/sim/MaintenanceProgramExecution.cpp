// Incremental service has no dismantled-state veto: every completed daily step
// is physical progress, and stopping preserves it while releasing job authority.
#include "sim/MaintenanceProgramExecution.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace deep {
namespace {
template <class T, class Id> T* find(std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
bool stationary(const Fleet& f) {
    return f.activeOrder.type == FleetOrderType::None && !f.destinationBodyId && f.queuedOrders.empty();
}
void audit(const MaintenanceProgram& p, MaintenanceAuditKind kind, int job, std::string detail,
           const MaintenanceExecutionHooks& hooks) {
    hooks.emit(EventSeverity::Info, MaintenanceProgramAuditEvent{p.id, kind, job, std::move(detail)});
}
bool represented(double before, double after, double change) {
    return std::isfinite(after) && after >= 0.0 &&
           (change == 0.0 || (after != before && equipmentNearlyEqual(std::abs(after - before), change)));
}
bool acquire(GameState& s, MaintenanceProgram& p, OpeningProgramContext& opening,
             const MaintenanceExecutionHooks& hooks) {
    if (p.leasedTenderId && p.leasedTeamId)
        return true;
    if (!p.charter.requestedTenderId || !p.charter.requestedTeamId || !p.charter.requestedLeaderId)
        return false;
    if (opening.occupiedFleets.contains(p.charter.requestedTenderId->value) ||
        opening.occupiedMaintenanceTeams.contains(p.charter.requestedTeamId->value))
        return false;
    auto* fleet = find(s.fleets, *p.charter.requestedTenderId);
    auto* team = find(s.maintenanceTeams, *p.charter.requestedTeamId);
    const auto* colony = find(s.colonies, p.charter.serviceColonyId);
    if (!fleet || !team || !colony || !stationary(*fleet) || fleet->currentBodyId != colony->bodyId)
        return false;
    if (!((team->location == MaintenanceTeamLocation::Colony && team->colonyId == colony->id) ||
          (team->location == MaintenanceTeamLocation::Fleet && team->fleetId == fleet->id)))
        return false;
    // Acquire both or neither; a workshop never manufactures its own team.
    opening.occupiedFleets.insert(fleet->id.value);
    opening.occupiedMaintenanceTeams.insert(team->id.value);
    p.leasedTenderId = fleet->id;
    p.leasedTeamId = team->id;
    team->location = MaintenanceTeamLocation::Fleet;
    team->colonyId.reset();
    team->fleetId = fleet->id;
    audit(p, MaintenanceAuditKind::LeaseAcquired, 0,
          "Tender and engineering team acquired together at service colony", hooks);
    return true;
}
bool jobComplete(const GameState& s, const ServiceJob& job) {
    for (const auto& target : job.targets) {
        const auto* ship = find(s.ships, target.shipId);
        if (!ship)
            return false;
        const auto row = std::find_if(ship->equipmentCondition.begin(), ship->equipmentCondition.end(),
                                      [&](const auto& c) { return c.componentId == target.componentId; });
        if (row == ship->equipmentCondition.end() || row->usedDuty > 0.0)
            return false;
    }
    return true;
}
std::optional<std::pair<std::string, std::string>> issueCause(const GameState& s,
                                                              const MaintenanceProgram& p) {
    const auto* job = activeServiceJob(p);
    if (p.lifecycle != MaintenanceProgramLifecycle::Authorized || !job || p.receipts.empty())
        return std::nullopt;
    const auto plan = planMaintenanceWork(s, p, *job);
    if (plan.ready)
        return std::nullopt;
    if (plan.condition.find("workshop") != std::string::npos ||
        plan.condition.find("Engineering") != std::string::npos) {
        // Initial incompatibility is a known wait. A started pinned group that
        // loses its real prerequisites after work is a changed consequence.
        if (job->activeTarget)
            return std::pair{"equipment", plan.condition};
    }
    if (p.charter.policy.lifetimeAllowances) {
        for (const auto& target : job->targets) {
            const auto* component = find(s.shipComponents, target.componentId);
            const auto* ship = find(s.ships, target.shipId);
            if (!component || !component->serviceProfile || !ship)
                continue;
            const auto row = std::find_if(ship->equipmentCondition.begin(), ship->equipmentCondition.end(),
                                          [&](const auto& c) { return c.componentId == target.componentId; });
            if (row == ship->equipmentCondition.end() || row->usedDuty <= 0.0)
                continue;
            for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
                if (component->serviceProfile->materialsPerDuty.amount[j] > 0.0 &&
                    p.charter.policy.lifetimeAllowances->amount[j] <= p.consumed.amount[j]) {
                    return std::pair{"allowance:" + std::to_string(j),
                                     "Maintenance material allowance exhausted during unfinished service"};
                }
            }
        }
    }
    // Ordinary recoverable stock shortages are reportable waits, not dialogs.
    return std::nullopt;
}
} // namespace

void withdrawMaintenanceJob(GameState& s, MaintenanceProgram& p, const std::string& reason,
                            const MaintenanceExecutionHooks& hooks) {
    auto* job = activeServiceJob(p);
    if (!job)
        return;
    job->outcome = ServiceJobOutcome::Withdrawn;
    job->endedDay = s.date.day;
    job->endReason = reason;
    job->activeTarget.reset();
    job->workshopShipId.reset();
    audit(p, MaintenanceAuditKind::JobWithdrawn, job->number, reason, hooks);
}
void withdrawClientService(GameState& s, SurveyProgramId id, const std::string& reason,
                           const MaintenanceExecutionHooks& hooks) {
    for (auto& p : s.maintenancePrograms) {
        const auto* job = activeServiceJob(p);
        if (job && job->surveyProgramId == id)
            withdrawMaintenanceJob(s, p, reason, hooks);
    }
}
void releaseMaintenanceAssets(GameState& s, MaintenanceProgram& p) {
    if (activeServiceJob(p))
        throw std::logic_error("Withdraw client service before releasing maintenance assets");
    auto* team = p.leasedTeamId ? find(s.maintenanceTeams, *p.leasedTeamId) : nullptr;
    const auto* fleet = p.leasedTenderId ? find(s.fleets, *p.leasedTenderId) : nullptr;
    const auto* colony = find(s.colonies, p.charter.serviceColonyId);
    if (fleet && !stationary(*fleet))
        throw std::logic_error("Maintenance lease cannot contain transit");
    if (team && fleet && colony && fleet->currentBodyId == colony->bodyId && team->fleetId == fleet->id) {
        team->location = MaintenanceTeamLocation::Colony;
        team->colonyId = colony->id;
        team->fleetId.reset();
    }
    p.leasedTenderId.reset();
    p.leasedTeamId.reset();
}

void runMaintenanceProgramOpeningDay(GameState& s, MaintenanceProgram& p, OpeningProgramContext& opening,
                                     const MaintenanceExecutionHooks& hooks) {
    if (p.lifecycle != MaintenanceProgramLifecycle::Authorized || !acquire(s, p, opening, hooks))
        return;
    if (!activeServiceJob(p)) {
        for (FleetId clientId : p.charter.clients) {
            const auto owner = controllingProgram(s, clientId);
            const auto* surveyId = owner ? std::get_if<SurveyProgramId>(&*owner) : nullptr;
            if (!surveyId || !opening.eligibleServiceClients.contains(surveyId->value))
                continue;
            const auto* client = find(s.surveyPrograms, *surveyId);
            const auto* home = client ? find(s.colonies, client->charter.homeColonyId) : nullptr;
            const auto* colony = find(s.colonies, p.charter.serviceColonyId);
            if (!client || client->charter.policy.maintenanceProgramId != p.id ||
                !eligibleServiceClient(s, *client) || !home || !colony || home->bodyId != colony->bodyId ||
                clientServiceJob(s, client->id))
                continue;
            const auto request = surveyServiceRequest(s, *client);
            if (!request.requested || request.targets.empty())
                continue;
            if (p.nextJobNumber == std::numeric_limits<int>::max())
                return;
            ServiceJob job;
            job.number = p.nextJobNumber;
            job.surveyProgramId = client->id;
            job.clientFleetId = clientId;
            job.serviceColonyId = colony->id;
            job.tenderFleetId = *p.leasedTenderId;
            job.teamId = *p.leasedTeamId;
            job.leaderId = *p.charter.requestedLeaderId;
            job.providerRevision = p.charterRevision;
            job.clientRevision = client->charterRevision;
            job.startedDay = s.date.day;
            job.targets = request.targets;
            p.jobs.push_back(std::move(job));
            ++p.nextJobNumber;
            audit(p, MaintenanceAuditKind::JobStarted, p.jobs.back().number,
                  "Full service job committed; client retains survey movement control", hooks);
            break;
        }
    }
    auto* job = activeServiceJob(p);
    if (!job)
        return;
    const auto plan = planMaintenanceWork(s, p, *job, &opening);
    if (!plan.ready)
        return;
    const auto& service = plan.service;
    auto* colony = find(s.colonies, job->serviceColonyId);
    auto* ship = find(s.ships, service.shipId);
    auto row = std::find_if(ship->equipmentCondition.begin(), ship->equipmentCondition.end(),
                            [&](const auto& c) { return c.componentId == service.componentId; });
    const double totalRestored = service.restoredDuty * service.quantity;
    if (!represented(p.restoredDuty, p.restoredDuty + totalRestored, totalRestored) ||
        !represented(p.teamWorkdays, p.teamWorkdays + service.teamWorkdays, service.teamWorkdays))
        return;
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        const double amount = service.consumed.amount[j];
        if (!represented(colony->processedStockpile.amount[j], colony->processedStockpile.amount[j] - amount,
                         amount) ||
            !represented(p.consumed.amount[j], p.consumed.amount[j] + amount, amount))
            return;
    }
    if (p.receipts.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return;
    // Allocate durable receipt storage before touching stock or condition.
    p.receipts.push_back(MaintenanceWorkReceipt{
        static_cast<int>(p.receipts.size()) + 1, job->number, s.date.day, service.shipId, service.componentId,
        plan.workshopShipId, service.quantity, service.beforeUsedDuty, service.afterUsedDuty,
        service.restoredDuty, service.teamWorkdays, service.consumed, p.charterRevision,
        find(s.surveyPrograms, job->surveyProgramId)->charterRevision});
    job->activeTarget = plan.targetIndex;
    job->workshopShipId = plan.workshopShipId;
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        colony->processedStockpile.amount[j] -= service.consumed.amount[j];
        p.consumed.amount[j] += service.consumed.amount[j];
        opening.debit(colony->id, static_cast<ProcessedMaterial>(j), service.consumed.amount[j]);
    }
    row->usedDuty = service.afterUsedDuty;
    p.restoredDuty += totalRestored;
    p.teamWorkdays += service.teamWorkdays;
    // Actual work proves the previous constraint resolved. A subsequent loss
    // may raise a new issue; idle/reacquisition gaps alone must not reopen it.
    p.issue = {};
    audit(p, MaintenanceAuditKind::WorkPerformed, job->number,
          "Actual instrument duty restored using colony materials and engineering work", hooks);
    if (row->usedDuty == 0.0) {
        job->activeTarget.reset();
        job->workshopShipId.reset();
    }
    if (jobComplete(s, *job)) {
        job->outcome = ServiceJobOutcome::Completed;
        job->endedDay = s.date.day;
        job->endReason = "Selected groups restored fully";
        audit(p, MaintenanceAuditKind::JobCompleted, job->number, job->endReason, hooks);
    }
    // Opening holds survive this completion; no second target/client starts.
}

void publishMaintenanceReportIfDue(GameState& s, MaintenanceProgram& p,
                                   const MaintenanceExecutionHooks& hooks) {
    if (s.date.day < p.nextReportDay || (p.closedDay && s.date.day > *p.closedDay))
        return;
    const auto next = nextGlobalSurveyBoundary(s.date.day, 30);
    MaintenanceProgramReport report;
    report.startDay = p.reportStartDay;
    report.endDay = s.date.day;
    report.isNinetyDayReview = s.date.day % 90 == 0;
    report.charterRevision = p.charterRevision;
    report.policy = p.charter.policy;
    for (const auto& receipt : p.receipts)
        if (receipt.day >= report.startDay && receipt.day <= report.endDay) {
            report.restoredDuty += receipt.restoredDuty * receipt.installationQuantity;
            report.teamWorkdays += receipt.teamWorkdays;
            for (std::size_t j = 0; j < processedMaterialCount(); ++j)
                report.consumed.amount[j] += receipt.consumed.amount[j];
        }
    report.auditThroughId = s.eventLog.empty() ? 0 : s.eventLog.back().id.value;
    const auto previousAudit = p.reports.empty() ? 0 : p.reports.back().auditThroughId;
    for (const auto& event : s.eventLog) {
        if (event.id.value <= previousAudit || event.id.value > report.auditThroughId)
            continue;
        const auto* outcome = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
        if (!outcome || outcome->programId != p.id)
            continue;
        if (outcome->kind == MaintenanceAuditKind::JobCompleted)
            ++report.jobsCompleted;
        if (outcome->kind == MaintenanceAuditKind::JobWithdrawn)
            ++report.jobsWithdrawn;
    }
    if (p.leasedTeamId)
        if (const auto* team = find(s.maintenanceTeams, *p.leasedTeamId))
            report.availableTeamRate = team->workdaysPerDay;
    if (const auto* job = activeServiceJob(p))
        report.compatibleWorkshopRate = planMaintenanceWork(s, p, *job).workshopRate;
    report.waitingReason = maintenanceExecutionCondition(s, p);
    p.reports.push_back(std::move(report));
    p.reportStartDay = s.date.day + 1;
    p.nextReportDay = next;
    audit(p, MaintenanceAuditKind::ReportPublished, 0,
          "Maintenance work and historical supply limits reported", hooks);
}
void acknowledgeKnownMaintenanceLimit(const GameState& s, MaintenanceProgram& p) {
    if (const auto cause = issueCause(s, p))
        p.issue = {"maintenance:" + std::to_string(p.id.value) + ":" + cause->first, cause->second, true};
    else if (!p.issue.signature.empty())
        p.issue.acknowledged = true;
}
void finishMaintenanceProgramsDay(GameState& s, const MaintenanceExecutionHooks& hooks) {
    for (auto& p : s.maintenancePrograms) {
        if (p.lifecycle == MaintenanceProgramLifecycle::Authorized) {
            if (const auto cause = issueCause(s, p)) {
                const auto signature = "maintenance:" + std::to_string(p.id.value) + ":" + cause->first;
                if (p.issue.signature != signature) {
                    p.issue = {signature, cause->second, false};
                    audit(p, MaintenanceAuditKind::IssueRaised, 0, cause->second, hooks);
                }
            } else if (!p.issue.acknowledged)
                p.issue = {};
        }
        publishMaintenanceReportIfDue(s, p, hooks);
    }
}
} // namespace deep
