// Cross-check service authority and work history against physical equipment.
// Validation never repairs missing condition, shifts assets, or invents supplies.
#include "sim/MaintenanceProgramValidation.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ProgramControl.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace deep {
namespace {
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
bool nonnegative(double x) { return std::isfinite(x) && x >= 0.0; }
void validSet(const ProcessedMaterialSet& set) {
    for (double x : set.amount)
        check(nonnegative(x), "Maintenance material values must be finite and nonnegative");
}
bool sameSet(const ProcessedMaterialSet& a, const ProcessedMaterialSet& b) {
    for (std::size_t j = 0; j < processedMaterialCount(); ++j)
        if (!equipmentNearlyEqual(a.amount[j], b.amount[j]))
            return false;
    return true;
}
bool stationary(const Fleet& f) {
    return f.activeOrder.type == FleetOrderType::None && !f.destinationBodyId && f.queuedOrders.empty();
}
using TargetKey = std::pair<std::int64_t, std::int64_t>;
} // namespace

void validateMaintenanceState(const GameState& s) {
    std::set<std::int64_t> teamIds, programIds, leasedTeams, leasedFleets, heldClients;
    std::set<std::pair<std::int64_t, std::int64_t>> teamWorkDays, workshopWorkDays, clientWorkDays;
    check(s.ids.nextMaintenanceTeamId > 0 && s.ids.nextMaintenanceProgramId > 0,
          "Maintenance counters must be positive");
    for (const auto& p : s.surveyPrograms)
        if (p.leasedFleetId)
            leasedFleets.insert(p.leasedFleetId->value);
    for (const auto& p : s.freightPrograms)
        if (p.leasedFleetId)
            leasedFleets.insert(p.leasedFleetId->value);
    for (const auto& team : s.maintenanceTeams) {
        check(team.id && team.id.value < s.ids.nextMaintenanceTeamId &&
                  teamIds.insert(team.id.value).second && !team.name.empty() &&
                  nonnegative(team.workdaysPerDay),
              "Maintenance team identity/name/capacity is invalid");
        std::set<std::int64_t> qualified;
        for (auto family : team.qualifiedFamilies)
            check(find(s.equipmentFamilies, family) && qualified.insert(family.value).second,
                  "Maintenance qualifications must be unique existing families");
        if (team.location == MaintenanceTeamLocation::Colony)
            check(team.colonyId && !team.fleetId && find(s.colonies, *team.colonyId),
                  "Maintenance team must have exactly one real colony location");
        else
            check(team.location == MaintenanceTeamLocation::Fleet && team.fleetId && !team.colonyId &&
                      find(s.fleets, *team.fleetId),
                  "Maintenance team must have exactly one real fleet location");
    }
    for (const auto& p : s.maintenancePrograms) {
        check(p.id && p.id.value < s.ids.nextMaintenanceProgramId && programIds.insert(p.id.value).second,
              "Maintenance program ID/counter invalid");
        check(!validateMaintenanceCharter(s, p.charter) && p.createdDay >= 0 && p.createdDay <= s.date.day &&
                  p.charterRevision > 0,
              "Maintenance charter/date invalid");
        check(p.lifecycle >= MaintenanceProgramLifecycle::Authorized &&
                  p.lifecycle <= MaintenanceProgramLifecycle::Closed,
              "Maintenance lifecycle invalid");
        check(p.leasedTenderId.has_value() == p.leasedTeamId.has_value(),
              "Maintenance acquires tender/team atomically");
        if (p.lifecycle == MaintenanceProgramLifecycle::Closed)
            check(p.closedDay && *p.closedDay >= p.createdDay && *p.closedDay <= s.date.day,
                  "Closed provider requires dated closure");
        else
            check(!p.closedDay, "Open provider cannot have closure date");
        if (p.lifecycle == MaintenanceProgramLifecycle::Closed)
            check(p.issue.signature.empty() && p.issue.acknowledged,
                  "Closed maintenance cannot retain a physical issue");
        if (p.lifecycle != MaintenanceProgramLifecycle::Authorized)
            check(!p.leasedTenderId && !activeServiceJob(p),
                  "Inactive maintenance cannot retain executable assets or job");
        if (p.leasedTenderId) {
            const auto* tender = find(s.fleets, *p.leasedTenderId);
            const auto* team = find(s.maintenanceTeams, *p.leasedTeamId);
            const auto* colony = find(s.colonies, p.charter.serviceColonyId);
            check(tender && team && colony && stationary(*tender) &&
                      tender->currentBodyId == colony->bodyId &&
                      team->location == MaintenanceTeamLocation::Fleet && team->fleetId == tender->id &&
                      p.charter.requestedTenderId == tender->id && p.charter.requestedTeamId == team->id &&
                      p.charter.requestedLeaderId,
                  "Maintenance lease must be a co-located authorized tender/team pair");
            check(leasedFleets.insert(tender->id.value).second && leasedTeams.insert(team->id.value).second,
                  "Duplicate program fleet or engineering-team ownership");
        }
        check(p.jobs.size() < static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                  p.nextJobNumber == static_cast<int>(p.jobs.size()) + 1,
              "Maintenance job sequence invalid");
        std::int64_t priorEnd = p.createdDay;
        for (std::size_t n = 0; n < p.jobs.size(); ++n) {
            const auto& job = p.jobs[n];
            const auto* survey = find(s.surveyPrograms, job.surveyProgramId);
            const auto* client = find(s.fleets, job.clientFleetId);
            const auto* tender = find(s.fleets, job.tenderFleetId);
            const auto* team = find(s.maintenanceTeams, job.teamId);
            const auto* colony = find(s.colonies, job.serviceColonyId);
            check(job.number == static_cast<int>(n) + 1 && survey && client && tender && team && colony &&
                      find(s.people, job.leaderId) && job.serviceColonyId == p.charter.serviceColonyId &&
                      job.clientFleetId != job.tenderFleetId && job.providerRevision > 0 &&
                      job.providerRevision <= p.charterRevision && job.clientRevision > 0 &&
                      job.clientRevision <= survey->charterRevision && job.startedDay >= priorEnd &&
                      job.startedDay <= s.date.day && !job.targets.empty(),
                  "Maintenance job provenance/order is invalid");
            check(job.outcome >= ServiceJobOutcome::Active && job.outcome <= ServiceJobOutcome::Withdrawn,
                  "Maintenance job outcome invalid");
            if (job.outcome == ServiceJobOutcome::Active) {
                check(n + 1 == p.jobs.size() && !job.endedDay && job.endReason.empty() &&
                          p.lifecycle == MaintenanceProgramLifecycle::Authorized &&
                          p.leasedTenderId == job.tenderFleetId && p.leasedTeamId == job.teamId &&
                          p.charter.requestedLeaderId == job.leaderId && eligibleServiceClient(s, *survey) &&
                          survey->leasedFleetId == job.clientFleetId &&
                          survey->charter.policy.maintenanceProgramId == p.id &&
                          client->currentBodyId == colony->bodyId &&
                          heldClients.insert(client->id.value).second &&
                          std::find(p.charter.clients.begin(), p.charter.clients.end(), client->id) !=
                              p.charter.clients.end(),
                      "Active service must hold one authorized stationary survey-owned client");
            } else {
                check(job.endedDay && *job.endedDay >= job.startedDay && *job.endedDay <= s.date.day &&
                          !job.endReason.empty() && !job.activeTarget && !job.workshopShipId,
                      "Ended service job retains active work or lacks history");
                priorEnd = *job.endedDay;
            }
            check(job.activeTarget.has_value() == job.workshopShipId.has_value(),
                  "Pinned service group/workshop identity must be paired");
            if (job.activeTarget)
                check(*job.activeTarget >= 0 &&
                          static_cast<std::size_t>(*job.activeTarget) < job.targets.size() &&
                          std::find(tender->shipIds.begin(), tender->shipIds.end(), *job.workshopShipId) !=
                              tender->shipIds.end(),
                      "Pinned service group/hull invalid");
            std::set<TargetKey> targets;
            std::size_t priorOrder = 0;
            bool first = true;
            for (const auto& target : job.targets) {
                const auto* ship = find(s.ships, target.shipId);
                const auto* component = find(s.shipComponents, target.componentId);
                const auto* cls = ship ? find(s.shipClasses, ship->shipClassId) : nullptr;
                check(ship && cls && component && component->serviceProfile && target.initialUsedDuty > 0.0 &&
                          std::isfinite(target.initialUsedDuty) &&
                          target.initialUsedDuty <= component->serviceProfile->dutyCapacity &&
                          targets.insert({ship->id.value, component->id.value}).second,
                      "Service job target debt/identity invalid");
                const auto hull = std::find(client->shipIds.begin(), client->shipIds.end(), ship->id);
                const auto install =
                    std::find_if(cls->components.begin(), cls->components.end(),
                                 [&](const auto& i) { return i.componentId == component->id; });
                check(hull != client->shipIds.end() && install != cls->components.end(),
                      "Service target must remain on original client revision/roster");
                // Flatten using the preceding hulls' installation counts, so
                // heterogeneous classes still preserve authored snapshot order.
                std::size_t order = static_cast<std::size_t>(install - cls->components.begin());
                for (auto h = client->shipIds.begin(); h != hull; ++h) {
                    const auto* previousShip = find(s.ships, *h);
                    const auto* previousClass =
                        previousShip ? find(s.shipClasses, previousShip->shipClassId) : nullptr;
                    check(previousClass != nullptr, "Missing service client roster class");
                    order += previousClass->components.size();
                }
                check(first || order > priorOrder,
                      "Service target order must match roster/installation order");
                first = false;
                priorOrder = order;
                double remaining = target.initialUsedDuty;
                for (const auto& receipt : p.receipts)
                    if (receipt.jobNumber == job.number && receipt.clientShipId == ship->id &&
                        receipt.componentId == component->id) {
                        check(equipmentNearlyEqual(receipt.beforeUsedDuty, remaining),
                              "Service target receipts do not form a physical condition chain");
                        remaining = receipt.afterUsedDuty;
                    }
                if (job.outcome == ServiceJobOutcome::Completed)
                    check(remaining == 0.0, "Full service job completed before all selected debt restored");
                if (job.outcome == ServiceJobOutcome::Active) {
                    const auto condition =
                        std::find_if(ship->equipmentCondition.begin(), ship->equipmentCondition.end(),
                                     [&](const auto& c) { return c.componentId == component->id; });
                    check(condition != ship->equipmentCondition.end() &&
                              equipmentNearlyEqual(condition->usedDuty, remaining),
                          "Active service debt disagrees with physical condition");
                }
            }
        }
        ProcessedMaterialSet consumed;
        double restored = 0.0, work = 0.0;
        std::int64_t previousDay = p.createdDay - 1;
        for (std::size_t n = 0; n < p.receipts.size(); ++n) {
            const auto& r = p.receipts[n];
            check(n < static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                      r.sequence == static_cast<int>(n) + 1 && r.jobNumber > 0 &&
                      r.jobNumber < p.nextJobNumber && r.day > previousDay && r.day <= s.date.day,
                  "Service receipts must be ordered positive daily actions, one per provider/day");
            previousDay = r.day;
            const auto& job = p.jobs[static_cast<std::size_t>(r.jobNumber - 1)];
            check(r.providerRevision >= job.providerRevision && r.providerRevision <= p.charterRevision &&
                      r.clientRevision >= job.clientRevision &&
                      r.clientRevision <= find(s.surveyPrograms, job.surveyProgramId)->charterRevision,
                  "Service receipt authority revisions are inconsistent");
            check(teamWorkDays.insert({job.teamId.value, r.day}).second &&
                      workshopWorkDays.insert({r.workshopShipId.value, r.day}).second &&
                      clientWorkDays.insert({job.clientFleetId.value, r.day}).second,
                  "Engineering team/workshop/client serviced twice in one opening day");
            const auto* ship = find(s.ships, r.clientShipId);
            const auto* cls = ship ? find(s.shipClasses, ship->shipClassId) : nullptr;
            const auto* component = find(s.shipComponents, r.componentId);
            const auto* workshop = find(s.ships, r.workshopShipId);
            check(cls && component && component->serviceProfile && workshop &&
                      workshop->fleetId == job.tenderFleetId && r.day >= job.startedDay &&
                      (!job.endedDay || r.day <= *job.endedDay) &&
                      std::any_of(job.targets.begin(), job.targets.end(),
                                  [&](const auto& t) {
                                      return t.shipId == r.clientShipId && t.componentId == r.componentId;
                                  }),
                  "Service receipt references invalid historical participants/target/date");
            const auto install = std::find_if(cls->components.begin(), cls->components.end(),
                                              [&](const auto& i) { return i.componentId == r.componentId; });
            check(
                install != cls->components.end() && install->quantity == r.installationQuantity &&
                    r.installationQuantity > 0 && nonnegative(r.beforeUsedDuty) &&
                    nonnegative(r.afterUsedDuty) && r.restoredDuty > 0.0 && std::isfinite(r.restoredDuty) &&
                    r.teamWorkdays > 0.0 && std::isfinite(r.teamWorkdays) &&
                    r.beforeUsedDuty <= component->serviceProfile->dutyCapacity &&
                    equipmentNearlyEqual(r.beforeUsedDuty - r.afterUsedDuty, r.restoredDuty) &&
                    equipmentNearlyEqual(r.teamWorkdays, r.restoredDuty * r.installationQuantity *
                                                             component->serviceProfile->teamWorkdaysPerDuty),
                "Service receipt condition/labor arithmetic is inconsistent");
            validSet(r.consumed);
            for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
                check(equipmentNearlyEqual(r.consumed.amount[j],
                                           r.restoredDuty * r.installationQuantity *
                                               component->serviceProfile->materialsPerDuty.amount[j]),
                      "Service receipt recipe debit disagrees with actual restoration");
                consumed.amount[j] += r.consumed.amount[j];
            }
            restored += r.restoredDuty * r.installationQuantity;
            work += r.teamWorkdays;
        }
        validSet(p.consumed);
        check(nonnegative(p.restoredDuty) && nonnegative(p.teamWorkdays) && sameSet(consumed, p.consumed) &&
                  equipmentNearlyEqual(restored, p.restoredDuty) &&
                  equipmentNearlyEqual(work, p.teamWorkdays),
              "Maintenance counters disagree with dated physical work");
        // Every positive work receipt and ended job must have its original
        // dated audit, preventing a second fabricated history from agreeing only
        // with its own duplicated counters while lacking actual action identity.
        for (const auto& r : p.receipts) {
            const auto count = std::count_if(s.eventLog.begin(), s.eventLog.end(), [&](const auto& event) {
                const auto* audit = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
                return audit && audit->programId == p.id && audit->jobNumber == r.jobNumber &&
                       audit->kind == MaintenanceAuditKind::WorkPerformed && event.day == r.day;
            });
            check(count == 1, "Maintenance receipt requires exactly one dated work audit");
        }
        for (const auto& job : p.jobs) {
            const auto auditCount = [&](MaintenanceAuditKind kind, std::int64_t day) {
                return std::count_if(s.eventLog.begin(), s.eventLog.end(), [&](const auto& event) {
                    const auto* a = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
                    return a && a->programId == p.id && a->jobNumber == job.number && a->kind == kind &&
                           event.day == day;
                });
            };
            check(job.startedDay > p.createdDay &&
                      auditCount(MaintenanceAuditKind::JobStarted, job.startedDay) == 1,
                  "Service job requires original dated commitment");
            if (job.endedDay)
                check(auditCount(job.outcome == ServiceJobOutcome::Completed
                                     ? MaintenanceAuditKind::JobCompleted
                                     : MaintenanceAuditKind::JobWithdrawn,
                                 *job.endedDay) == 1,
                      "Ended service job requires one dated outcome");
        }
        for (const auto& event : s.eventLog)
            if (const auto* a = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
                a && a->programId == p.id) {
                check(event.day >= p.createdDay && (!p.closedDay || event.day <= *p.closedDay),
                      "Maintenance audit outside provider lifetime");
                if (a->kind == MaintenanceAuditKind::JobStarted ||
                    a->kind == MaintenanceAuditKind::WorkPerformed ||
                    a->kind == MaintenanceAuditKind::JobCompleted ||
                    a->kind == MaintenanceAuditKind::JobWithdrawn) {
                    check(a->jobNumber > 0 && a->jobNumber < p.nextJobNumber,
                          "Maintenance action references missing job");
                    const auto& job = p.jobs[static_cast<std::size_t>(a->jobNumber - 1)];
                    if (a->kind == MaintenanceAuditKind::JobStarted)
                        check(event.day == job.startedDay, "Job start audit date mismatch");
                    if (a->kind == MaintenanceAuditKind::JobCompleted ||
                        a->kind == MaintenanceAuditKind::JobWithdrawn)
                        check(job.endedDay && event.day == *job.endedDay, "Job end audit date mismatch");
                    if (a->kind == MaintenanceAuditKind::WorkPerformed)
                        check(std::count_if(p.receipts.begin(), p.receipts.end(),
                                            [&](const auto& r) {
                                                return r.jobNumber == a->jobNumber && r.day == event.day;
                                            }) == 1,
                              "Work audit requires one positive physical receipt");
                }
            }
        const auto through = p.closedDay ? *p.closedDay : s.date.day;
        std::int64_t expected = nextGlobalSurveyBoundary(p.createdDay, 30), start = p.createdDay;
        std::int64_t previousAudit = 0;
        for (const auto& report : p.reports) {
            check(report.startDay == start && report.endDay == expected && report.endDay <= through &&
                      report.isNinetyDayReview == (expected % 90 == 0) && report.charterRevision > 0 &&
                      report.charterRevision <= p.charterRevision,
                  "Maintenance report boundary/revision invalid");
            check(report.auditThroughId >= previousAudit && report.auditThroughId < s.ids.nextEventId,
                  "Maintenance report audit boundary invalid");
            int publications = 0;
            for (std::size_t eventIndex = 0; eventIndex < s.eventLog.size(); ++eventIndex) {
                const auto& event = s.eventLog[eventIndex];
                const auto* audit = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
                if (audit && audit->programId == p.id &&
                    audit->kind == MaintenanceAuditKind::ReportPublished && event.day == report.endDay) {
                    ++publications;
                    check(report.auditThroughId ==
                              (eventIndex == 0 ? 0 : s.eventLog[eventIndex - 1].id.value),
                          "Maintenance report must retain its actual publication cutoff");
                }
            }
            check(publications == 1, "Maintenance report requires exactly one dated publication");
            ProcessedMaterialSet actual;
            double restoredInPeriod = 0.0, workInPeriod = 0.0;
            int completed = 0, withdrawn = 0;
            for (const auto& r : p.receipts)
                if (r.day >= start && r.day <= expected) {
                    for (std::size_t j = 0; j < processedMaterialCount(); ++j)
                        actual.amount[j] += r.consumed.amount[j];
                    restoredInPeriod += r.restoredDuty * r.installationQuantity;
                    workInPeriod += r.teamWorkdays;
                }
            for (const auto& event : s.eventLog) {
                if (event.id.value <= previousAudit || event.id.value > report.auditThroughId)
                    continue;
                const auto* audit = std::get_if<MaintenanceProgramAuditEvent>(&event.payload);
                if (!audit || audit->programId != p.id)
                    continue;
                if (audit->kind == MaintenanceAuditKind::JobCompleted)
                    ++completed;
                if (audit->kind == MaintenanceAuditKind::JobWithdrawn)
                    ++withdrawn;
            }
            check(sameSet(actual, report.consumed) &&
                      equipmentNearlyEqual(restoredInPeriod, report.restoredDuty) &&
                      equipmentNearlyEqual(workInPeriod, report.teamWorkdays) &&
                      report.jobsCompleted == completed && report.jobsWithdrawn == withdrawn &&
                      nonnegative(report.availableTeamRate) && nonnegative(report.compatibleWorkshopRate),
                  "Maintenance report disagrees with physical history");
            validSet(report.policy.floors);
            if (report.policy.lifetimeAllowances)
                validSet(*report.policy.lifetimeAllowances);
            start = expected + 1;
            previousAudit = report.auditThroughId;
            expected = nextGlobalSurveyBoundary(expected, 30);
        }
        check(expected > through && p.nextReportDay == expected && p.reportStartDay == start,
              "Maintenance due reports/cursors incomplete");
        check((p.issue.signature.empty() && p.issue.message.empty() && p.issue.acknowledged) ||
                  (!p.issue.signature.empty() && !p.issue.message.empty()),
              "Maintenance issue shape invalid");
    }
}
} // namespace deep
