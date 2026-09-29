// Operating receipts pin the equipment eligible on their own dates. Expansion
// cannot retroactively alter support cost or turn a past observation into truth.
#include "sim/SiteOperationValidation.h"
#include "sim/SiteOperationRules.h"
#include "sim/EquipmentServiceRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
namespace deep {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool valid(double x) {
    return std::isfinite(x) && x >= 0;
}
bool near(double a, double b) {
    return equipmentNearlyEqual(a, b);
}
bool bounded(double x, double upper) {
    return x <= upper || near(x, upper);
}
template <class T, class I> bool exists(const std::vector<T>& v, I id) {
    return std::any_of(v.begin(), v.end(), [&](const T& x) { return x.id == id; });
}
} // namespace
void validateSiteOperationState(const GameState& s) {
    for (const auto& site : s.resourceSites) {
        require(site.id.value > 0 && exists(s.bodies, site.bodyId) && !site.name.empty() &&
                    site.createdDay >= 0 && site.createdDay <= s.date.day,
                "Invalid resource site registration");
        require(!site.institutionId || exists(s.institutions, *site.institutionId),
                "Site institution missing");
        require(site.operatingRevision > 0 && !validateSiteOperatingPolicy(s, site.operatingPolicy),
                "Invalid standing site operating policy");
        for (double q : site.processedStock.amount)
            require(valid(q), "Site processed staging stock invalid");
        for (double q : site.rawStock.amount)
            require(valid(q), "Site raw stock invalid");
        std::int64_t commissioned = site.createdDay;
        for (const auto& group : site.installed) {
            require(group.commissionedDay >= commissioned && group.commissionedDay <= s.date.day &&
                        group.quantity > 0 && group.packageRow >= 0,
                    "Invalid installed group date/order/count");
            commissioned = group.commissionedDay;
        }
        // Current passive storage includes today's commissioning, although its
        // handling/extraction remains unavailable until a later opening.
        const auto current = siteCapabilities(
            s, site, s.date.day == std::numeric_limits<std::int64_t>::max() ? s.date.day : s.date.day + 1);
        require(bounded(siteRawOccupied(site), current.equipment.rawStorage),
                "Site raw stock exceeds shared installed storage");
        // An operating revision has one immutable value wherever it appears:
        // current authority, supported-day receipts and published report snapshots.
        std::map<int, SiteOperatingPolicy> policies;
        policies.emplace(site.operatingRevision, site.operatingPolicy);
        const auto rememberPolicy = [&](int revision, const SiteOperatingPolicy& policy) {
            const auto [entry, inserted] = policies.emplace(revision, policy);
            require(inserted || entry->second == policy,
                    "Site policy snapshots disagree within one operating revision");
        };
        double spent = 0;
        std::int64_t day = site.createdDay;
        int revision = 0;
        for (const auto& r : site.dutyReceipts) {
            require(r.day > day && r.day <= s.date.day && r.operatingRevision >= revision &&
                        r.operatingRevision <= site.operatingRevision && r.operatingRevision > 0,
                    "Invalid site duty chronology");
            day = r.day;
            revision = r.operatingRevision;
            rememberPolicy(r.operatingRevision, r.policy);
            require(exists(s.people, r.leaderId) && r.policy.leaderId == r.leaderId && r.policy.enabled &&
                        !validateSiteOperatingPolicy(s, r.policy),
                    "Invalid historical site duty authority");
            require(valid(r.duty) && r.duty > 0 && r.duty <= 1 && valid(r.reactorFuel) &&
                        valid(r.composites) && valid(r.rawHandling),
                    "Invalid site duty quantities");
            const auto cap = siteCapabilities(s, site, r.day);
            require(r.installedGroupCutoff == cap.installedGroupCutoff && r.installedGroupCutoff > 0,
                    "Site duty used future or mismatched installed equipment");
            require(cap.equipment.powerGeneration > 0 && cap.equipment.powerMargin >= 0 &&
                        cap.equipment.counts[static_cast<std::size_t>(SiteModuleKind::Power)] > 0 &&
                        cap.equipment.counts[static_cast<std::size_t>(SiteModuleKind::AutomationSupport)] >
                            0 &&
                        cap.equipment.reactorFuelPerDuty > 0 && cap.equipment.compositesPerDuty > 0,
                    "Site duty ran without supported all-active power and automation");
            require(near(r.reactorFuel, r.duty * cap.equipment.reactorFuelPerDuty) &&
                        near(r.composites, r.duty * cap.equipment.compositesPerDuty) &&
                        near(r.rawHandling, r.duty * cap.equipment.rawHandling),
                    "Site duty support cost/handling does not match historical hardware");
            spent += r.duty;
            require(valid(spent), "Site lifetime duty overflow");
            if (r.policy.lifetimeDutyAllowance)
                require(bounded(spent, *r.policy.lifetimeDutyAllowance),
                        "Site historical duty exceeded then-authorized lifetime allowance");
        }
        day = site.createdDay;
        revision = 0;
        for (const auto& r : site.extractionReceipts) {
            require(r.day > day && r.day <= s.date.day && r.operatingRevision >= revision &&
                        r.operatingRevision <= site.operatingRevision && r.operatingRevision > 0,
                    "Invalid site extraction chronology");
            day = r.day;
            revision = r.operatingRevision;
            const auto duty = std::find_if(site.dutyReceipts.begin(), site.dutyReceipts.end(),
                                           [&](const auto& d) { return d.day == r.day; });
            require(duty != site.dutyReceipts.end() && duty->leaderId == r.leaderId &&
                        duty->operatingRevision == r.operatingRevision &&
                        duty->installedGroupCutoff == r.installedGroupCutoff,
                    "Extraction has no corresponding physical supported day");
            require(valid(r.nominalAttempt) && r.nominalAttempt > 0 && valid(r.recoveredIce) &&
                        valid(r.freeRawRoom) && valid(r.availableHandling) && !r.observation.empty(),
                    "Invalid observed extraction quantities");
            const auto cap = siteCapabilities(s, site, r.day);
            require(near(r.nominalAttempt, duty->duty * std::min(cap.equipment.ratedExtraction,
                                                                 duty->policy.requestedIcePerDay)) &&
                        r.freeRawRoom > 0 && r.availableHandling > 0 &&
                        bounded(r.recoveredIce, r.nominalAttempt) && bounded(r.recoveredIce, r.freeRawRoom) &&
                        bounded(r.recoveredIce, r.availableHandling),
                    "Extraction exceeded authorized physical attempt/recovery bounds");
            require(bounded(r.availableHandling, duty->rawHandling) &&
                        bounded(r.freeRawRoom, cap.equipment.rawStorage),
                    "Extraction available room/handling exceeds installed equipment");
        }
        require(site.issue.cause >= SiteOperatingIssueCause::None &&
                    site.issue.cause <= SiteOperatingIssueCause::DutyAllowance &&
                    site.issue.episodeStartedDay >= 0 && site.issue.episodeStartedDay <= s.date.day,
                "Invalid site issue episode");
        if (site.issue.cause == SiteOperatingIssueCause::None)
            require(site.issue.acknowledged, "Empty site issue is pending");
        else
            require(!site.issue.message.empty(), "Observed site issue lacks explanation");
        require(site.reportStartDay >= site.createdDay && site.reportStartDay <= s.date.day &&
                    site.nextReportDay > site.reportStartDay,
                "Invalid site report schedule");
        std::int64_t end = site.createdDay;
        for (const auto& r : site.reports) {
            require(r.startDay == end && r.endDay > r.startDay && r.endDay <= s.date.day &&
                        r.operatingRevision > 0 && r.operatingRevision <= site.operatingRevision,
                    "Invalid site report chronology/revision");
            rememberPolicy(r.operatingRevision, r.policy);
            require(!validateSiteOperatingPolicy(s, r.policy) &&
                        r.installedGroupCutoff <= site.installed.size() && r.auditThroughId >= 0 &&
                        r.auditThroughId < s.ids.nextEventId,
                    "Invalid site report historical cutoff/policy");
            for (double q : {r.supportedDuty, r.reactorFuel, r.composites, r.recoveredIce, r.rawOccupancy,
                             r.rawCapacity, r.deliveredIce})
                require(valid(q), "Invalid site report quantity");
            require(std::isfinite(r.exportedIce), "Site net export is not finite");
            require(r.attempts >= 0 && bounded(r.rawOccupancy, r.rawCapacity),
                    "Invalid site report attempts/raw room");
            double duty = 0, reactor = 0, composites = 0, ice = 0;
            int attempts = 0;
            for (const auto& d : site.dutyReceipts)
                if (d.day > r.startDay && d.day <= r.endDay) {
                    duty += d.duty;
                    reactor += d.reactorFuel;
                    composites += d.composites;
                }
            for (const auto& e : site.extractionReceipts)
                if (e.day > r.startDay && e.day <= r.endDay) {
                    ++attempts;
                    ice += e.recoveredIce;
                }
            require(near(duty, r.supportedDuty) && near(reactor, r.reactorFuel) &&
                        near(composites, r.composites) && near(ice, r.recoveredIce) && attempts == r.attempts,
                    "Site report does not reconcile operating receipts");
            const auto cutoff = siteCapabilities(
                s, site, r.endDay == std::numeric_limits<std::int64_t>::max() ? r.endDay : r.endDay + 1);
            require(r.installedGroupCutoff == cutoff.installedGroupCutoff &&
                        near(r.rawCapacity, cutoff.equipment.rawStorage) &&
                        r.isNinetyDayReview == (r.endDay % 90 == 0),
                    "Site report historical capacity/review cutoff mismatch");
            double exported = 0, delivered = 0;
            for (const auto& freight : s.freightPrograms)
                if (freight.charter.source == StockLocation{site.id} &&
                    freight.charter.commodity == Commodity{Mineral::WaterIce})
                    for (const auto& transfer : freight.receipts)
                        if (transfer.day > r.startDay && transfer.day <= r.endDay) {
                            if (transfer.kind == FreightTransferKind::Load)
                                exported += transfer.amount;
                            if (transfer.kind == FreightTransferKind::SourceReturn)
                                exported -= transfer.amount;
                            if (transfer.kind == FreightTransferKind::Delivery)
                                delivered += transfer.amount;
                        }
            require(near(exported, r.exportedIce) && near(delivered, r.deliveredIce),
                    "Site report freight totals disagree with physical transfer receipts");
            end = r.endDay;
        }
        require(end == site.reportStartDay, "Site report boundary disagrees with saved reports");
    }
}
} // namespace deep
