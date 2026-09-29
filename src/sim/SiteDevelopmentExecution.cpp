// Physical construction custody and work. A site registration owns no hardware;
// only paid assembly followed by real commissioning appends installed groups.
#include "sim/SiteDevelopmentExecution.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/SiteEvents.h"
#include "sim/FreightProgramRules.h"
#include "sim/ShipDesignRules.h"
#include "sim/SurveyProgramRules.h"
#include "sim/TransitPlanning.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace deep {
namespace {
template <class T, class I> T* find(std::vector<T>& v, I id) {
    auto i = std::find_if(v.begin(), v.end(), [&](const T& x) { return x.id == id; });
    return i == v.end() ? nullptr : &*i;
}
bool stationary(const Fleet& f) {
    return f.activeOrder.type == FleetOrderType::None && !f.destinationBodyId && f.queuedOrders.empty();
}
void audit(const SiteDevelopmentProgram& p, SiteDevelopmentAuditKind kind,
           const SiteDevelopmentExecutionHooks& h, std::string detail, double amount = 0, int row = -1,
           std::optional<ShipId> hull = {}) {
    if (h.emit)
        h.emit(EventSeverity::Info,
               SiteDevelopmentAuditEvent{p.id, kind, p.charter.siteId, p.taskBuilderId, p.taskTeamId, hull,
                                         p.charter.assignments.leaderId, p.charterRevision, row, amount,
                                         std::move(detail)});
}
// A positive accounting delta must survive both the debit and credit; tolerance
// can compare rounding residue but never approve a completely lost transfer.
bool represents(double before, double after, double amount) {
    return std::isfinite(after) && after >= 0 &&
           (amount == 0 ||
            (std::abs(after - before) > 0 && freightNearlyEqual(std::abs(after - before), amount)));
}
// Stable typed causes distinguish accepted waiting intent from regression after
// actual work. Signature serialization preserves acknowledgment through pauses.
enum class WaitCause {
    Participants,
    Ownership,
    Qualification,
    Workshop,
    Location,
    Leader,
    Fuel,
    Materials,
    Floor,
    Allowance,
    Numeric
};
const char* waitKey(WaitCause cause) {
    switch (cause) {
    case WaitCause::Participants:
        return "development/participants";
    case WaitCause::Ownership:
        return "development/ownership";
    case WaitCause::Qualification:
        return "development/qualification";
    case WaitCause::Workshop:
        return "development/workshop";
    case WaitCause::Location:
        return "development/location";
    case WaitCause::Leader:
        return "development/leader";
    case WaitCause::Fuel:
        return "development/fuel";
    case WaitCause::Materials:
        return "development/material-stock";
    case WaitCause::Floor:
        return "development/material-floor";
    case WaitCause::Allowance:
        return "development/material-allowance";
    case WaitCause::Numeric:
        return "development/numeric";
    }
    return "development/numeric";
}
WaitCause workCause(SiteWorkCause cause) {
    switch (cause) {
    case SiteWorkCause::MaterialFloor:
        return WaitCause::Floor;
    case SiteWorkCause::MaterialAllowance:
        return WaitCause::Allowance;
    case SiteWorkCause::NoTeamRate:
        return WaitCause::Qualification;
    case SiteWorkCause::NoWorkshopRate:
        return WaitCause::Workshop;
    case SiteWorkCause::NoLeader:
        return WaitCause::Leader;
    case SiteWorkCause::NumericLimit:
        return WaitCause::Numeric;
    default:
        return WaitCause::Materials;
    }
}
void release(SiteDevelopmentProgram& p) {
    p.leasedBuilderId.reset();
    p.leasedTeamId.reset();
    p.holdsSiteConstruction = false;
    p.taskBuilderId.reset();
    p.taskTeamId.reset();
    p.task = SiteDevelopmentTask::None;
}
void close(GameState& s, SiteDevelopmentProgram& p, const SiteDevelopmentExecutionHooks& h) {
    if (h.prepareEvents)
        h.prepareEvents(1);
    p.lifecycle = SiteDevelopmentLifecycle::Closed;
    p.closedDay = s.date.day;
    p.issue = {};
    audit(p, SiteDevelopmentAuditKind::Closed, h,
          "Physical construction closeout complete; site assets retained");
}
// Returns true only when engine tanks actually received a finite transfer.
bool refill(GameState& s, SiteDevelopmentProgram& p, Fleet& f, Colony& home, double required,
            OpeningProgramContext& ctx, const SiteDevelopmentExecutionHooks& h) {
    const auto& policy = p.charter.assignments.policy;
    const double allowance = policy.additionalPropellantAllowance
                                 ? std::max(0.0, *policy.additionalPropellantAllowance - p.fuelLoaded)
                                 : std::numeric_limits<double>::infinity();
    double left =
        std::min({std::max(0.0, required - freightFleetFuel(s, f)), allowance,
                  ctx.available(s, home.id, ProcessedMaterial::Propellant, policy.homePropellantFloor)});
    std::vector<std::pair<Ship*, double>> changes;
    double total = 0;
    for (auto id : f.shipIds) {
        auto* ship = find(s.ships, id);
        auto* cls = ship ? find(s.shipClasses, ship->shipClassId) : nullptr;
        if (!cls)
            continue;
        const double capacity = evaluateShipDesign(s.shipComponents, cls->components).propellantCapacity;
        const double q = std::min(left, std::max(0.0, capacity - ship->fuel));
        if (!represents(ship->fuel, ship->fuel + q, q))
            return false;
        if (q > 0)
            changes.push_back({ship, q});
        total += q;
        left -= q;
    }
    const double stock = home.processedStockpile.get(ProcessedMaterial::Propellant);
    if (total <= 0 || !represents(stock, stock - total, total) ||
        !represents(p.fuelLoaded, p.fuelLoaded + total, total))
        return false;
    if (h.prepareEvents)
        h.prepareEvents(1);
    for (auto [ship, q] : changes)
        ship->fuel += q;
    home.processedStockpile.set(ProcessedMaterial::Propellant, stock - total);
    ctx.debit(home.id, ProcessedMaterial::Propellant, total);
    p.fuelLoaded += total;
    p.issue = {};
    audit(p, SiteDevelopmentAuditKind::Refueled, h,
          "Support colony Propellant transferred into real engine tanks", total);
    return true;
}
} // namespace
void runSiteDevelopmentOpeningDay(GameState& s, SiteDevelopmentProgram& p, OpeningProgramContext& ctx,
                                  const SiteDevelopmentExecutionHooks& h) {
    if (p.lifecycle == SiteDevelopmentLifecycle::Closed)
        return;
    auto* home = find(s.colonies, p.charter.supportColonyId);
    auto* site = find(s.resourceSites, p.charter.siteId);
    if (!home || !site)
        return;
    const auto wait = [&](WaitCause cause, std::string reason) {
        const std::string key = waitKey(cause);
        if (p.issue.signature != key) {
            // Known limits at admission/amendment are accepted waiting intent.
            const bool acknowledged =
                p.workReceipts.empty() || p.workReceipts.back().charterRevision != p.charterRevision;
            if (!acknowledged && h.prepareEvents)
                h.prepareEvents(1);
            p.issue.signature = key;
            p.issue.acknowledged = acknowledged;
            if (!acknowledged)
                audit(p, SiteDevelopmentAuditKind::IssueRaised, h, reason);
        }
        p.issue.message = std::move(reason);
    };
    // Actual assets remain stable throughout deployment. Requested replacements
    // start only after the original pair physically returns and disembarks.
    if (!p.taskBuilderId) {
        if (p.closure != SiteDevelopmentClosure::None) {
            close(s, p, h);
            return;
        }
        if (p.lifecycle == SiteDevelopmentLifecycle::Suspended)
            return;
        const auto& a = p.charter.assignments;
        auto* f = a.builderId ? find(s.fleets, *a.builderId) : nullptr;
        auto* t = a.teamId ? find(s.maintenanceTeams, *a.teamId) : nullptr;
        if (!a.leaderId || !f || !t) {
            wait(WaitCause::Participants, "Waiting for leader, builder, and engineering team");
            return;
        }
        if (controllingProgram(s, f->id) || controllingEngineeringTeam(s, t->id) ||
            controllingSiteConstruction(s, site->id)) {
            wait(WaitCause::Ownership, siteDevelopmentExecutionCondition(s, p));
            return;
        }
        if (ctx.occupiedFleets.contains(f->id.value) || ctx.occupiedMaintenanceTeams.contains(t->id.value) ||
            ctx.occupiedConstructionSites.contains(site->id.value)) {
            wait(WaitCause::Ownership,
                 "Builder, engineering team, or construction site was already occupied this opening");
            return;
        }
        if (!s.siteConstructionFamilyId || !maintenanceTeamQualified(*t, *s.siteConstructionFamilyId) ||
            t->workdaysPerDay <= 0) {
            wait(WaitCause::Qualification,
                 "Waiting for a construction-qualified engineering team with positive throughput");
            return;
        }
        bool powered = false;
        for (auto id : f->shipIds) {
            const auto* ship = find(s.ships, id);
            powered |= ship && operationalWorkshopRate(s, *ship, *s.siteConstructionFamilyId) > 0;
        }
        if (!powered) {
            wait(WaitCause::Workshop, siteDevelopmentExecutionCondition(s, p));
            return;
        }
        if (!stationary(*f) || f->currentBodyId != home->bodyId) {
            wait(WaitCause::Location, "Builder must be stationary at the support colony");
            return;
        }
        const bool aboard = t->location == MaintenanceTeamLocation::Fleet && t->fleetId == f->id;
        const bool atHome = t->location == MaintenanceTeamLocation::Colony && t->colonyId == home->id;
        if (!aboard && !atHome) {
            wait(WaitCause::Location,
                 "Engineering team must be at the exact support colony or aboard this builder");
            return;
        }
        if (h.prepareEvents)
            h.prepareEvents(2);
        p.issue = {};
        p.leasedBuilderId = f->id;
        p.leasedTeamId = t->id;
        p.holdsSiteConstruction = true;
        p.taskBuilderId = f->id;
        p.taskTeamId = t->id;
        p.task = SiteDevelopmentTask::Preparing;
        ctx.occupiedFleets.insert(f->id.value);
        ctx.occupiedMaintenanceTeams.insert(t->id.value);
        ctx.occupiedConstructionSites.insert(site->id.value);
        audit(p, SiteDevelopmentAuditKind::LeaseAcquired, h,
              "Exclusive physical construction participants acquired");
        t->location = MaintenanceTeamLocation::Fleet;
        t->colonyId.reset();
        t->fleetId = f->id;
        audit(p, SiteDevelopmentAuditKind::Embarked, h,
              aboard ? "Engineering team already aboard retained builder"
                     : "Engineering team embarked at support colony");
        return;
    }
    auto* f = find(s.fleets, *p.taskBuilderId);
    auto* t = p.taskTeamId ? find(s.maintenanceTeams, *p.taskTeamId) : nullptr;
    if (!f || !t)
        return;
    const auto depart = [&](BodyId target, const char* detail) {
        const double cost = adjustedFleetMoveFuelCost(s, *f, f->currentBodyId, target, s.date.day);
        if (std::isfinite(cost) && freightFleetFuel(s, *f) < cost) {
            wait(WaitCause::Fuel,
                 "Waiting for real onboard Propellant for the authorized physical return/departure");
            return false;
        }
        if (!std::isfinite(cost) || !represents(p.fuelBurned, p.fuelBurned + cost, cost) ||
            !freightFuelDebitRepresentable(s, *f, cost)) {
            wait(WaitCause::Numeric,
                 "Return/departure fuel accounting cannot represent a conservative debit");
            return false;
        }
        // Reserve the common movement audit and this program's departure audit
        // before the shared movement primitive touches fuel or the fleet order.
        if (h.prepareEvents)
            h.prepareEvents(2);
        double charged = 0;
        if (h.startProgramMove && h.startProgramMove(p.id, f->id, target, charged)) {
            p.issue = {};
            p.fuelBurned += charged;
            audit(p, SiteDevelopmentAuditKind::Departed, h, detail, charged);
            return true;
        }
        wait(WaitCause::Fuel, "Waiting for real onboard fuel for the authorized physical departure");
        return false;
    };
    if (!stationary(*f))
        return;
    const bool homeward = p.closure != SiteDevelopmentClosure::None ||
                          p.task == SiteDevelopmentTask::Return || p.task == SiteDevelopmentTask::Disembark;
    if (f->currentBodyId == home->bodyId &&
        (homeward || p.lifecycle == SiteDevelopmentLifecycle::Suspended)) {
        if (h.prepareEvents)
            h.prepareEvents(2);
        t->location = MaintenanceTeamLocation::Colony;
        t->colonyId = home->id;
        t->fleetId.reset();
        audit(p, SiteDevelopmentAuditKind::Disembarked, h,
              "Engineering team physically disembarked at support colony");
        release(p);
        if (p.closure != SiteDevelopmentClosure::None)
            close(s, p, h);
        return;
    }
    if (p.lifecycle == SiteDevelopmentLifecycle::Suspended)
        return;
    if (homeward) {
        p.task = SiteDevelopmentTask::Return;
        (void)depart(home->bodyId, "Builders departed for support colony");
        return;
    }
    if (!p.charter.assignments.leaderId) {
        wait(WaitCause::Leader, "Waiting for responsible construction leader");
        return;
    }
    if (f->currentBodyId == home->bodyId && p.task == SiteDevelopmentTask::Preparing) {
        if (s.date.day == std::numeric_limits<std::int64_t>::max()) {
            wait(WaitCause::Numeric, "Construction timetable exceeds date limits");
            return;
        }
        // Refueling itself consumes this opening. Price that hypothetical later
        // departure, then reprice again on the actual departure boundary.
        double required = siteDevelopmentRoundTripFuel(s, p, *f, s.date.day);
        if (!std::isfinite(required)) {
            wait(WaitCause::Numeric, "No representable construction round-trip estimate");
            return;
        }
        if (freightFleetFuel(s, *f) < required) {
            required = siteDevelopmentRoundTripFuel(s, p, *f, s.date.day + 1);
            if (std::isfinite(required) && refill(s, p, *f, *home, required, ctx, h))
                return;
            wait(WaitCause::Fuel,
                 "Waiting for support Propellant, tank room, stock floor, or lifetime fuel allowance");
            return;
        }
        if (home->bodyId == site->bodyId) {
            if (h.prepareEvents)
                h.prepareEvents(1);
            p.issue = {};
            p.task = SiteDevelopmentTask::Outbound;
            audit(p, SiteDevelopmentAuditKind::Departed, h, "Builder deployed to site on the same body");
            return;
        }
        if (depart(site->bodyId, "Builder departed for construction site"))
            p.task = SiteDevelopmentTask::Outbound;
        return;
    }
    if (f->currentBodyId != site->bodyId) {
        wait(WaitCause::Location, "Builder is not at the authorized construction site");
        return;
    }
    const auto plan = planSiteDevelopmentWork(s, p, &ctx);
    if (!plan.readiness.canWork) {
        wait(workCause(plan.readiness.cause), plan.readiness.explanation);
        return;
    }
    const double work = plan.readiness.work;
    // Allocate every persistent result before consuming physical inventory.
    p.workReceipts.reserve(p.workReceipts.size() + 1);
    if (h.prepareEvents)
        h.prepareEvents(2);
    if (plan.commissioning) {
        if (!represents(p.commissioningWork, p.commissioningWork + work, work)) {
            wait(WaitCause::Numeric, "Commissioning delta is not representable");
            return;
        }
        const bool complete = p.commissioningWork + work >= kSiteCommissioningWorkdays;
        if (complete)
            site->installed.reserve(site->installed.size() + p.charter.package.size());
        p.issue = {};
        p.commissioningWorkshopId = plan.workshopShipId;
        p.commissioningWork += work;
        p.task = SiteDevelopmentTask::Commissioning;
        p.workReceipts.push_back({s.date.day,
                                  p.charterRevision,
                                  SiteDevelopmentWorkKind::Commissioning,
                                  -1,
                                  f->id,
                                  plan.workshopShipId,
                                  t->id,
                                  *p.charter.assignments.leaderId,
                                  work,
                                  {}});
        audit(p, SiteDevelopmentAuditKind::CommissioningWork, h, "Actual engineering commissioning work",
              work, -1, plan.workshopShipId);
        if (complete) {
            for (std::size_t i = 0; i < p.charter.package.size(); ++i) {
                const auto& row = p.charter.package[i];
                site->installed.push_back({p.id, static_cast<int>(i), p.charter.catalogVersion, row.kind,
                                           row.quantity, s.date.day});
            }
            p.commissionedDay = s.date.day;
            p.closure = SiteDevelopmentClosure::Completed;
            p.lifecycle = SiteDevelopmentLifecycle::Closing;
            p.task = SiteDevelopmentTask::Return;
            audit(p, SiteDevelopmentAuditKind::Commissioned, h,
                  "Package commissioned; capacity becomes eligible next opening");
        }
        return;
    }
    auto& row = p.rows[static_cast<std::size_t>(plan.packageRow)];
    if (!represents(row.workCompleted, row.workCompleted + work, work)) {
        wait(WaitCause::Numeric, "Assembly delta is not representable");
        return;
    }
    for (std::size_t m = 0; m < processedMaterialCount(); ++m) {
        const double q = plan.readiness.consumed.amount[m];
        if (!represents(site->processedStock.amount[m], site->processedStock.amount[m] - q, q) ||
            !represents(row.consumed.amount[m], row.consumed.amount[m] + q, q)) {
            wait(WaitCause::Numeric, "Assembly material delta is not representable");
            return;
        }
    }
    for (std::size_t m = 0; m < processedMaterialCount(); ++m) {
        const double q = plan.readiness.consumed.amount[m];
        site->processedStock.amount[m] -= q;
        row.consumed.amount[m] += q;
        ctx.debit(StockLocation{site->id}, Commodity{static_cast<ProcessedMaterial>(m)}, q);
    }
    p.issue = {};
    row.workCompleted += work;
    row.workshopShipId = plan.workshopShipId;
    p.task = SiteDevelopmentTask::Assembly;
    p.workReceipts.push_back({s.date.day, p.charterRevision, SiteDevelopmentWorkKind::Assembly,
                              plan.packageRow, f->id, plan.workshopShipId, t->id,
                              *p.charter.assignments.leaderId, work, plan.readiness.consumed});
    audit(p, SiteDevelopmentAuditKind::AssemblyWork, h,
          "Delivered material consumed by actual engineering work", work, plan.packageRow,
          plan.workshopShipId);
}
void finishSiteDevelopmentsDay(GameState& s, const SiteDevelopmentExecutionHooks& h) {
    for (auto& p : s.siteDevelopmentPrograms) {
        if (p.lifecycle == SiteDevelopmentLifecycle::Closed)
            continue;
        if (s.date.day < p.nextReportDay)
            continue;
        SiteDevelopmentReport report;
        report.startDay = p.reportStartDay;
        report.endDay = s.date.day;
        report.isNinetyDayReview = s.date.day % 90 == 0;
        report.charterRevision = p.charterRevision;
        report.policy = p.charter.assignments.policy;
        report.fuelLoaded = p.fuelLoaded;
        report.fuelBurned = p.fuelBurned;
        report.builderId = p.taskBuilderId;
        report.teamId = p.taskTeamId;
        if (p.taskBuilderId) {
            const auto* f = find(s.fleets, *p.taskBuilderId);
            if (f)
                report.bodyId = f->currentBodyId;
        }
        report.auditThroughId = s.ids.nextEventId - 1;
        report.commissioned = p.commissionedDay.has_value();
        report.waitingReason = siteDevelopmentExecutionCondition(s, p);
        for (const auto& receipt : p.workReceipts)
            if (receipt.day > report.startDay && receipt.day <= report.endDay) {
                if (receipt.kind == SiteDevelopmentWorkKind::Assembly)
                    report.assemblyWork += receipt.work;
                else
                    report.commissioningWork += receipt.work;
                report.consumed.addSet(receipt.consumed);
            }
        p.reports.reserve(p.reports.size() + 1);
        if (h.prepareEvents)
            h.prepareEvents(1);
        p.reports.push_back(std::move(report));
        p.reportStartDay = s.date.day;
        p.nextReportDay = s.date.day > std::numeric_limits<std::int64_t>::max() - 30
                              ? std::numeric_limits<std::int64_t>::max()
                              : nextGlobalSurveyBoundary(s.date.day, 30);
        audit(p, SiteDevelopmentAuditKind::ReportPublished, h, "Construction oversight report published");
    }
}
} // namespace deep
