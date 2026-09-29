// Strict construction snapshot validation. Durable rows must be explained by
// dated physical-work receipts, and installed groups must match completed work.
#include "sim/SiteDevelopmentValidation.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/EquipmentServiceRules.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <map>
#include <unordered_set>
namespace deep {
namespace {
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
bool valid(double x) {
    return std::isfinite(x) && x >= 0;
}
template <class T, class I> const T* find(const std::vector<T>& v, I id) {
    auto it = std::find_if(v.begin(), v.end(), [&](const T& x) { return x.id == id; });
    return it == v.end() ? nullptr : &*it;
}
bool equal(double a, double b) {
    return equipmentNearlyEqual(a, b);
}
} // namespace
void validateSiteDevelopmentState(const GameState& s) {
    std::unordered_set<std::int64_t> ids, fleets, teams, sites;
    for (const auto& p : s.siteDevelopmentPrograms) {
        require(p.id.value > 0 && ids.insert(p.id.value).second &&
                    p.id.value < s.ids.nextSiteDevelopmentProgramId,
                "Invalid development identity/counter");
        require(!validateSiteDevelopmentCharter(s, p.charter), "Invalid development charter");
        require(p.createdDay >= 0 && p.createdDay <= s.date.day && p.charterRevision > 0,
                "Invalid development creation/revision");
        require(p.lifecycle >= SiteDevelopmentLifecycle::Authorized &&
                    p.lifecycle <= SiteDevelopmentLifecycle::Closed &&
                    p.closure >= SiteDevelopmentClosure::None &&
                    p.closure <= SiteDevelopmentClosure::Cancelled && p.task >= SiteDevelopmentTask::None &&
                    p.task <= SiteDevelopmentTask::Disembark,
                "Invalid development lifecycle tag");
        require(p.rows.size() == p.charter.package.size(), "Development package/row count mismatch");
        require(valid(p.commissioningWork) && p.commissioningWork <= kSiteCommissioningWorkdays &&
                    valid(p.fuelLoaded) && valid(p.fuelBurned),
                "Invalid development cumulative work/fuel");
        require(p.leasedBuilderId.has_value() == p.leasedTeamId.has_value() &&
                    p.leasedBuilderId.has_value() == p.holdsSiteConstruction &&
                    p.taskBuilderId == p.leasedBuilderId && p.taskTeamId == p.leasedTeamId,
                "Development custody and leases disagree");
        require(p.taskBuilderId.has_value() == (p.task != SiteDevelopmentTask::None),
                "Development task has no retained participants");
        if (p.leasedBuilderId) {
            const auto* f = find(s.fleets, *p.leasedBuilderId);
            const auto* t = find(s.maintenanceTeams, *p.leasedTeamId);
            require(f && t && fleets.insert(p.leasedBuilderId->value).second &&
                        teams.insert(p.leasedTeamId->value).second &&
                        sites.insert(p.charter.siteId.value).second,
                    "Development participants double-booked or missing");
            require(t->location == MaintenanceTeamLocation::Fleet && t->fleetId == f->id && !t->colonyId,
                    "Leased construction team is not aboard actual builder");
            require(s.siteConstructionFamilyId && maintenanceTeamQualified(*t, *s.siteConstructionFamilyId),
                    "Actual construction team lacks qualification");
        }
        const bool closed = p.lifecycle == SiteDevelopmentLifecycle::Closed;
        require(closed == p.closedDay.has_value(), "Development close date/lifecycle mismatch");
        if (closed)
            require(!p.taskBuilderId && p.closure != SiteDevelopmentClosure::None &&
                        *p.closedDay >= p.createdDay && *p.closedDay <= s.date.day,
                    "Closed development retains physical task or invalid disposition");
        if (p.lifecycle == SiteDevelopmentLifecycle::Closing)
            require(p.closure != SiteDevelopmentClosure::None, "Closing development lacks disposition");
        if (p.lifecycle == SiteDevelopmentLifecycle::Authorized)
            require(p.closure == SiteDevelopmentClosure::None,
                    "Authorized development retains closure disposition");
        require(p.commissionedDay.has_value() == (p.closure == SiteDevelopmentClosure::Completed),
                "Commissioned development lost Completed disposition");
        if (p.commissionedDay)
            require(*p.commissionedDay >= p.createdDay && *p.commissionedDay <= s.date.day &&
                        equal(p.commissioningWork, kSiteCommissioningWorkdays),
                    "Invalid commissioning completion date/work");
        std::vector<double> work(p.rows.size());
        std::vector<ProcessedMaterialSet> consumed(p.rows.size());
        double commissioning = 0;
        std::int64_t last = p.createdDay;
        int revision = 0;
        for (const auto& r : p.workReceipts) {
            require(r.day > last && r.day <= s.date.day && r.charterRevision >= revision &&
                        r.charterRevision <= p.charterRevision && r.charterRevision > 0,
                    "Invalid development receipt chronology");
            last = r.day;
            revision = r.charterRevision;
            require(valid(r.work) && r.work > 0 && find(s.fleets, r.builderId) &&
                        find(s.ships, r.workshopShipId) && find(s.maintenanceTeams, r.teamId) &&
                        find(s.people, r.leaderId),
                    "Invalid development receipt participants/work");
            for (double q : r.consumed.amount)
                require(valid(q), "Invalid development receipt material");
            if (r.kind == SiteDevelopmentWorkKind::Assembly) {
                require(r.packageRow >= 0 && static_cast<std::size_t>(r.packageRow) < p.rows.size(),
                        "Invalid assembly receipt row");
                const auto i = static_cast<std::size_t>(r.packageRow);
                const auto eval =
                    evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion,
                                        std::span<const SiteModuleInstall>(&p.charter.package[i], 1));
                for (std::size_t m = 0; m < processedMaterialCount(); ++m)
                    require(equal(r.consumed.amount[m], r.work * eval.cost.amount[m] / eval.assemblyWorkdays),
                            "Assembly receipt does not conserve proportional material cost");
                require(p.rows[i].workshopShipId == r.workshopShipId,
                        "Started construction row changed pinned workshop");
                work[i] += r.work;
                consumed[i].addSet(r.consumed);
            } else {
                require(r.kind == SiteDevelopmentWorkKind::Commissioning && r.packageRow == -1 &&
                            p.commissioningWorkshopId == r.workshopShipId,
                        "Invalid commissioning receipt identity");
                for (double q : r.consumed.amount)
                    require(q == 0, "Commissioning charged assembly materials twice");
                for (std::size_t i = 0; i < p.rows.size(); ++i) {
                    const auto eval =
                        evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion,
                                            std::span<const SiteModuleInstall>(&p.charter.package[i], 1));
                    require(equal(work[i], eval.assemblyWorkdays),
                            "Commissioning preceded assembly completion");
                }
                commissioning += r.work;
            }
        }
        require(equal(commissioning, p.commissioningWork) &&
                    p.commissioningWorkshopId.has_value() == (commissioning > 0),
                "Commissioning receipt totals disagree");
        double loadedFuel = 0, burnedFuel = 0;
        for (const auto& event : s.eventLog)
            if (const auto* audit = std::get_if<SiteDevelopmentAuditEvent>(&event.payload);
                audit && audit->programId == p.id) {
                if (audit->kind == SiteDevelopmentAuditKind::Refueled)
                    loadedFuel += audit->amount;
                if (audit->kind == SiteDevelopmentAuditKind::Departed)
                    burnedFuel += audit->amount;
            }
        require(equal(loadedFuel, p.fuelLoaded) && equal(burnedFuel, p.fuelBurned),
                "Development fuel totals disagree with actual transfer/departure audits");
        const auto* site = find(s.resourceSites, p.charter.siteId);
        for (std::size_t i = 0; i < p.rows.size(); ++i) {
            const auto& row = p.rows[i];
            const auto eval =
                evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion,
                                    std::span<const SiteModuleInstall>(&p.charter.package[i], 1));
            require(valid(row.workCompleted) && row.workCompleted <= eval.assemblyWorkdays &&
                        equal(row.workCompleted, work[i]) && row.workshopShipId.has_value() == (work[i] > 0),
                    "Assembly row work/receipt mismatch");
            for (std::size_t m = 0; m < processedMaterialCount(); ++m)
                require(valid(row.consumed.amount[m]) && equal(row.consumed.amount[m], consumed[i].amount[m]),
                        "Assembly row material history mismatch");
            int installed = 0;
            for (const auto& group : site->installed)
                if (group.programId == p.id && group.packageRow == static_cast<int>(i)) {
                    ++installed;
                    require(p.commissionedDay && group.commissionedDay == *p.commissionedDay &&
                                group.catalogVersion == p.charter.catalogVersion &&
                                group.kind == p.charter.package[i].kind &&
                                group.quantity == p.charter.package[i].quantity,
                            "Installed group differs from commissioned package");
                }
            require(installed == (p.commissionedDay ? 1 : 0),
                    "Commissioned package installed zero or multiple times");
        }
        require(p.reportStartDay >= p.createdDay && p.reportStartDay <= s.date.day &&
                    p.nextReportDay > p.reportStartDay,
                "Invalid development report schedule");
        std::map<int, SiteDevelopmentPolicy> policies;
        policies.emplace(p.charterRevision, p.charter.assignments.policy);
        const auto samePolicy = [](const SiteDevelopmentPolicy& a, const SiteDevelopmentPolicy& b) {
            return a.homePropellantFloor == b.homePropellantFloor &&
                   a.additionalPropellantAllowance == b.additionalPropellantAllowance &&
                   a.returnContingencyFraction == b.returnContingencyFraction &&
                   a.constructionFloors.amount == b.constructionFloors.amount &&
                   a.materialAllowances.has_value() == b.materialAllowances.has_value() &&
                   (!a.materialAllowances || a.materialAllowances->amount == b.materialAllowances->amount);
        };
        std::int64_t end = p.createdDay;
        for (const auto& report : p.reports) {
            auto historical = p.charter;
            historical.assignments.policy = report.policy;
            require(!validateSiteDevelopmentCharter(s, historical),
                    "Invalid historical development report policy");
            require((!report.builderId || find(s.fleets, *report.builderId)) &&
                        (!report.teamId || find(s.maintenanceTeams, *report.teamId)) &&
                        (!report.bodyId || find(s.bodies, *report.bodyId)),
                    "Development report participant/location reference missing");
            const auto [policy, inserted] = policies.emplace(report.charterRevision, report.policy);
            require(inserted || samePolicy(policy->second, report.policy),
                    "Development policy snapshots disagree within one charter revision");
            require(report.startDay == end && report.endDay > report.startDay &&
                        report.endDay <= s.date.day && report.charterRevision > 0 &&
                        report.charterRevision <= p.charterRevision,
                    "Invalid development report chronology");
            require(valid(report.assemblyWork) && valid(report.commissioningWork) &&
                        valid(report.fuelLoaded) && valid(report.fuelBurned),
                    "Invalid development report accounting");
            require(report.auditThroughId >= 0 && report.auditThroughId < s.ids.nextEventId &&
                        report.isNinetyDayReview == (report.endDay % 90 == 0),
                    "Invalid development report audit/review cutoff");
            double assembly = 0, commission = 0, loaded = 0, burned = 0;
            ProcessedMaterialSet materials;
            for (const auto& r : p.workReceipts)
                if (r.day > report.startDay && r.day <= report.endDay) {
                    if (r.kind == SiteDevelopmentWorkKind::Assembly)
                        assembly += r.work;
                    else
                        commission += r.work;
                    materials.addSet(r.consumed);
                }
            for (const auto& event : s.eventLog)
                if (event.id.value <= report.auditThroughId)
                    if (const auto* audit = std::get_if<SiteDevelopmentAuditEvent>(&event.payload);
                        audit && audit->programId == p.id) {
                        if (audit->kind == SiteDevelopmentAuditKind::Refueled)
                            loaded += audit->amount;
                        if (audit->kind == SiteDevelopmentAuditKind::Departed)
                            burned += audit->amount;
                    }
            require(equal(assembly, report.assemblyWork) && equal(commission, report.commissioningWork) &&
                        equal(loaded, report.fuelLoaded) && equal(burned, report.fuelBurned),
                    "Development report does not reconcile work/fuel history");
            for (std::size_t m = 0; m < processedMaterialCount(); ++m)
                require(equal(materials.amount[m], report.consumed.amount[m]),
                        "Development report material totals disagree");
            end = report.endDay;
        }
        require(end == p.reportStartDay, "Development reporting boundary disagrees with history");
        require(!p.issue.signature.empty() || p.issue.acknowledged, "Empty development issue is pending");
    }
    // Reverse installation references must resolve as well: an otherwise valid
    // program cannot hide extra unmatched installed rows on a different site.
    for (const auto& site : s.resourceSites)
        for (const auto& group : site.installed) {
            const auto* p = find(s.siteDevelopmentPrograms, group.programId);
            require(p && p->charter.siteId == site.id && group.packageRow >= 0 &&
                        static_cast<std::size_t>(group.packageRow) < p->rows.size(),
                    "Installed group references wrong development/site/row");
        }
}
} // namespace deep
