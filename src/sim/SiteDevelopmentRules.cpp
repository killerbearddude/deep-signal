// Pure construction admission/readiness shared by execution and projections.
// Unfinished rows are independent; missing stock in an earlier row is no lock.
#include "sim/SiteDevelopmentRules.h"
#include "sim/ProgramControl.h"
#include "sim/TransitPlanning.h"
#include "sim/ShipDesignRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace deep {
namespace {
template <class T, class I> const T* find(const std::vector<T>& v, I id) {
    auto i = std::find_if(v.begin(), v.end(), [&](const T& x) { return x.id == id; });
    return i == v.end() ? nullptr : &*i;
}
bool valid(double x) {
    return std::isfinite(x) && x >= 0;
}
bool valid(const ProcessedMaterialSet& x) {
    return std::all_of(x.amount.begin(), x.amount.end(), [](double v) { return valid(v); });
}
// Distinguish an absent installation from one whose own hull cannot power it.
// A started row cannot borrow a healthy workshop on another hull.
std::string workshopBlocker(const GameState& s, const Fleet& f, std::optional<ShipId> pinned) {
    if (!s.siteConstructionFamilyId)
        return "Site Construction equipment family is unassigned";
    for (auto id : f.shipIds) {
        if (pinned && id != *pinned)
            continue;
        const auto* ship = find(s.ships, id);
        const auto* cls = ship ? find(s.shipClasses, ship->shipClassId) : nullptr;
        if (!cls)
            continue;
        const auto design = evaluateShipDesign(s.shipComponents, cls->components);
        for (const auto& rate : design.workshopRates)
            if (rate.familyId == *s.siteConstructionFamilyId && rate.teamWorkdaysPerDay > 0)
                return pinned ? "Pinned construction workshop hull is unpowered"
                              : "Installed construction workshop hull is unpowered";
    }
    return pinned ? "Pinned construction workshop is unavailable on retained builder"
                  : "Builder has no Site Construction workshop";
}
std::pair<ShipId, double> workshop(const GameState& s, const Fleet& f, std::optional<ShipId> pinned) {
    if (!s.siteConstructionFamilyId)
        return {};
    for (auto id : f.shipIds) {
        if (pinned && id != *pinned)
            continue;
        const auto* ship = find(s.ships, id);
        const double rate = ship ? operationalWorkshopRate(s, *ship, *s.siteConstructionFamilyId) : 0;
        if (rate > 0)
            return {id, rate};
    }
    return {};
}
} // namespace
std::optional<std::string> validateSiteDevelopmentCharter(const GameState& s, const SiteDevelopmentCharter& c,
                                                          bool registeringSite) {
    const auto& a = c.assignments;
    const auto& p = a.policy;
    if (a.name.find_first_not_of(" \r\n\t") == std::string::npos)
        return "Development name must be nonempty";
    if ((!registeringSite && !find(s.resourceSites, c.siteId)) || !find(s.colonies, c.supportColonyId))
        return "Development site or support colony does not exist";
    if (a.builderId && !find(s.fleets, *a.builderId))
        return "Requested builder does not exist";
    if (a.teamId && !find(s.maintenanceTeams, *a.teamId))
        return "Requested engineer does not exist";
    if (a.leaderId && !find(s.people, *a.leaderId))
        return "Responsible leader does not exist";
    if (!valid(p.homePropellantFloor) || !valid(p.returnContingencyFraction) ||
        (p.additionalPropellantAllowance && !valid(*p.additionalPropellantAllowance)) ||
        !valid(p.constructionFloors) || (p.materialAllowances && !valid(*p.materialAllowances)))
        return "Construction policy requires finite nonnegative limits";
    try {
        (void)evaluateSitePackage(s.siteModuleCatalog, c.catalogVersion, c.package);
    } catch (const std::exception& e) {
        return e.what();
    }
    return std::nullopt;
}
SiteDevelopmentWorkPlan planSiteDevelopmentWork(const GameState& s, const SiteDevelopmentProgram& p,
                                                const OpeningProgramContext* ctx) {
    SiteDevelopmentWorkPlan result;
    const auto fail = [&](std::string text) {
        result.readiness.explanation = std::move(text);
        return result;
    };
    if (!p.charter.assignments.leaderId) {
        result.readiness.cause = SiteWorkCause::NoLeader;
        return fail("Waiting for responsible leader");
    }
    const auto fid = p.taskBuilderId ? p.taskBuilderId : p.charter.assignments.builderId;
    const auto tid = p.taskTeamId ? p.taskTeamId : p.charter.assignments.teamId;
    const auto* f = fid ? find(s.fleets, *fid) : nullptr;
    const auto* t = tid ? find(s.maintenanceTeams, *tid) : nullptr;
    const auto* site = find(s.resourceSites, p.charter.siteId);
    if (!f || !t || !site) {
        result.readiness.cause = SiteWorkCause::NoTeamRate;
        return fail("Waiting for builder and engineering team");
    }
    if (!s.siteConstructionFamilyId || !maintenanceTeamQualified(*t, *s.siteConstructionFamilyId)) {
        result.readiness.cause = SiteWorkCause::NoTeamRate;
        return fail("Engineering team lacks Site Construction qualification");
    }
    ProcessedMaterialSet spent;
    for (const auto& row : p.rows)
        spent.addSet(row.consumed);
    bool unfinished = false;
    std::string blockers;
    for (std::size_t i = 0; i < p.charter.package.size(); ++i) {
        const auto eval = evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion,
                                              std::span<const SiteModuleInstall>(&p.charter.package[i], 1));
        const double done = i < p.rows.size() ? p.rows[i].workCompleted : 0;
        if (done >= eval.assemblyWorkdays)
            continue;
        unfinished = true;
        const auto [hull, rate] =
            workshop(s, *f, i < p.rows.size() ? p.rows[i].workshopShipId : std::nullopt);
        SiteAssemblyInputs in;
        in.requiredWork = eval.assemblyWorkdays;
        in.completedWork = done;
        in.totalCost = eval.cost;
        in.teamRate = t->workdaysPerDay;
        in.workshopRate = rate;
        in.currentStock = site->processedStock;
        in.openingStock = site->processedStock;
        in.floors = p.charter.assignments.policy.constructionFloors;
        in.lifetimeAllowances = p.charter.assignments.policy.materialAllowances;
        in.lifetimeConsumed = spent;
        if (ctx)
            for (std::size_t m = 0; m < processedMaterialCount(); ++m)
                in.openingStock.amount[m] = ctx->available(s, StockLocation{site->id},
                                                           Commodity{static_cast<ProcessedMaterial>(m)}, 0);
        auto prepared = prepareSiteAssembly(in);
        if (rate <= 0)
            prepared.explanation =
                workshopBlocker(s, *f, i < p.rows.size() ? p.rows[i].workshopShipId : std::nullopt);
        if (prepared.canWork)
            return {prepared, static_cast<int>(i), hull, false};
        if (blockers.empty())
            result.readiness.cause = prepared.cause;
        if (!blockers.empty())
            blockers += "; ";
        blockers += "row " + std::to_string(i + 1) + ": " + prepared.explanation;
    }
    if (unfinished)
        return fail(blockers);
    if (t->workdaysPerDay <= 0) {
        result.readiness.cause = SiteWorkCause::NoTeamRate;
        return fail("Waiting for positive engineering-team throughput");
    }
    const auto [hull, rate] = workshop(s, *f, p.commissioningWorkshopId);
    const double work = std::min({kSiteCommissioningWorkdays - p.commissioningWork, t->workdaysPerDay, rate});
    if (work <= 0) {
        result.readiness.cause = SiteWorkCause::NoWorkshopRate;
        return fail(workshopBlocker(s, *f, p.commissioningWorkshopId));
    }
    result.commissioning = true;
    result.workshopShipId = hull;
    result.readiness = {true, SiteWorkCause::Ready, "Commissioning", work, {}};
    return result;
}
std::string siteDevelopmentExecutionCondition(const GameState& s, const SiteDevelopmentProgram& p) {
    if (p.lifecycle == SiteDevelopmentLifecycle::Closed)
        return "Construction closed; installed assets and site operation retained";
    if (p.lifecycle == SiteDevelopmentLifecycle::Suspended)
        return "Construction suspended; physical custody retained";
    if (p.closure != SiteDevelopmentClosure::None) {
        if (!p.taskBuilderId)
            return "Ready for construction closeout; no physical builders remain committed";
        const auto* f = find(s.fleets, *p.taskBuilderId);
        const auto* home = find(s.colonies, p.charter.supportColonyId);
        if (!f || !home)
            return "Returning builder or support colony is unavailable";
        if (f->destinationBodyId)
            return "Builders in paid transit to body #" + std::to_string(f->destinationBodyId->value) +
                   "; arrival day " + std::to_string(f->activeOrder.arrivalDay) +
                   "; engineering custody retained";
        if (f->currentBodyId == home->bodyId)
            return "Ready to disembark original engineering team at support colony";
        double aboard = 0;
        for (auto id : f->shipIds)
            if (const auto* ship = find(s.ships, id))
                aboard += ship->fuel;
        const auto day = s.date.day == std::numeric_limits<std::int64_t>::max() ? s.date.day : s.date.day + 1;
        const double cost = adjustedFleetMoveFuelCost(s, *f, f->currentBodyId, home->bodyId, day);
        if (!std::isfinite(cost))
            return "Waiting for a representable physical return route";
        if (aboard < cost)
            return "Waiting for return engine Propellant: aboard " + std::to_string(aboard) + ", required " +
                   std::to_string(cost) + ", shortfall " + std::to_string(cost - aboard) +
                   "; builder and engineer remain remote";
        return "Ready for physical return to support colony; onboard Propellant " + std::to_string(aboard) +
               ", estimated next-opening burn " + std::to_string(cost);
    }
    if (!p.taskBuilderId) {
        const auto& a = p.charter.assignments;
        if (!a.leaderId)
            return "Waiting for responsible construction leader";
        const auto* f = a.builderId ? find(s.fleets, *a.builderId) : nullptr;
        const auto* t = a.teamId ? find(s.maintenanceTeams, *a.teamId) : nullptr;
        const auto* home = find(s.colonies, p.charter.supportColonyId);
        if (!f || !t || !home)
            return "Waiting for builder and engineering team";
        // Ownership explains why a requested person is aboard another fleet;
        // report that binding before its resulting physical location constraint.
        if (const auto owner = controllingProgram(s, f->id))
            return "Builder controlled by " + programControllerLabel(s, *owner);
        if (const auto owner = controllingEngineeringTeam(s, t->id))
            return "Engineer controlled by " + programControllerLabel(s, *owner);
        if (const auto owner = controllingSiteConstruction(s, p.charter.siteId))
            return "Site construction controlled by " + programControllerLabel(s, ProgramController{*owner});
        if (!s.siteConstructionFamilyId || !maintenanceTeamQualified(*t, *s.siteConstructionFamilyId) ||
            t->workdaysPerDay <= 0)
            return "Waiting for qualified construction engineering throughput";
        if (workshop(s, *f, {}).second <= 0)
            return workshopBlocker(s, *f, {});
        if (f->activeOrder.type != FleetOrderType::None || f->destinationBodyId || !f->queuedOrders.empty() ||
            f->currentBodyId != home->bodyId)
            return "Builder must be stationary at the support colony";
        if (!((t->location == MaintenanceTeamLocation::Colony && t->colonyId == home->id) ||
              (t->location == MaintenanceTeamLocation::Fleet && t->fleetId == f->id)))
            return "Engineer must be at the exact support colony or aboard the requested builder";
        return "Ready to embark construction engineers at support colony";
    }
    if (p.task == SiteDevelopmentTask::Outbound)
        return "Builder deployment";
    if (p.task == SiteDevelopmentTask::Preparing)
        return "Preparing real round-trip engine fuel";
    return planSiteDevelopmentWork(s, p).readiness.explanation;
}
double siteDevelopmentRoundTripFuel(const GameState& s, const SiteDevelopmentProgram& p, const Fleet& f,
                                    std::int64_t day) {
    const double inf = std::numeric_limits<double>::infinity();
    const auto* home = find(s.colonies, p.charter.supportColonyId);
    const auto* site = find(s.resourceSites, p.charter.siteId);
    const auto tid = p.taskTeamId ? p.taskTeamId : p.charter.assignments.teamId;
    const auto* team = tid ? find(s.maintenanceTeams, *tid) : nullptr;
    if (!home || !site || !team)
        return inf;
    long double days = 1;
    // Each row owns an opening; fractional work cannot spill into another row.
    for (std::size_t i = 0; i < p.charter.package.size(); ++i) {
        auto eval = evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion,
                                        std::span<const SiteModuleInstall>(&p.charter.package[i], 1));
        const double remaining = eval.assemblyWorkdays - p.rows[i].workCompleted;
        if (remaining <= 0)
            continue;
        auto [hull, rate] = workshop(s, f, p.rows[i].workshopShipId);
        (void)hull;
        rate = std::min(rate, team->workdaysPerDay);
        if (rate <= 0)
            return inf;
        days += std::ceil(remaining / rate);
    }
    auto [hull, rate] = workshop(s, f, p.commissioningWorkshopId);
    (void)hull;
    rate = std::min(rate, team->workdaysPerDay);
    if (rate <= 0)
        return inf;
    days += std::ceil(std::max(0.0, kSiteCommissioningWorkdays - p.commissioningWork) / rate);
    if (home->bodyId == site->bodyId)
        return 0;
    const auto out = planFleetTransit(s, home->bodyId, site->bodyId, day);
    if (out.type != FleetOrderType::MoveToBody || out.arrivalDay <= day ||
        days > static_cast<long double>(std::numeric_limits<std::int64_t>::max() - out.arrivalDay))
        return inf;
    const auto backDay = out.arrivalDay + static_cast<std::int64_t>(days);
    return adjustedFleetMoveFuelCost(s, f, home->bodyId, site->bodyId, day) +
           adjustedFleetMoveFuelCost(s, f, site->bodyId, home->bodyId, backDay) *
               (1 + p.charter.assignments.policy.returnContingencyFraction);
}
} // namespace deep
