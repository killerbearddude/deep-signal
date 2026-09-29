// Site operation consumes real support before any transfer, then recovers only
// physically available ice after colony mining. Receipts expose outcomes only.
#include "sim/SiteOperationExecution.h"
#include "sim/SiteOperationRules.h"
#include "sim/FreightProgramRules.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace deep {
namespace {
// Public previews at the terminal representable date remain read-only and
// cannot overflow while asking whether today's commissioned group is eligible.
std::int64_t nextOpeningDay(std::int64_t day) {
    return day == std::numeric_limits<std::int64_t>::max() ? day : day + 1;
}
ResourceSite& find(GameState& s, SiteId id) {
    auto it = std::find_if(s.resourceSites.begin(), s.resourceSites.end(),
                           [&](const auto& x) { return x.id == id; });
    if (it == s.resourceSites.end())
        throw std::logic_error("Opening site disappeared");
    return *it;
}
void audit(const ResourceSite& site, SiteOperatingAuditKind kind, double amount, std::string detail,
           const SiteOperationHooks& h) {
    h.emit(EventSeverity::Info,
           SiteOperatingAuditEvent{site.id, kind, site.operatingRevision, site.issue.cause,
                                   site.issue.episodeStartedDay, amount, std::move(detail)});
}
// A zero-recovery episode spans only genuine attempts since the latest positive
// outcome. Pauses and supply gaps do not erase the observed episode identity.
std::pair<int, std::int64_t> zeroTail(const ResourceSite& site) {
    int count = 0;
    std::int64_t start = 0;
    for (auto it = site.extractionReceipts.rbegin(); it != site.extractionReceipts.rend(); ++it) {
        if (it->recoveredIce > 0)
            break;
        if (count < std::numeric_limits<int>::max())
            ++count;
        start = it->day;
    }
    return {count, start};
}
bool acknowledgedBefore(const GameState& s, const ResourceSite& site, SiteOperatingIssueCause cause,
                        std::int64_t day) {
    for (const auto& event : s.eventLog)
        if (const auto* e = std::get_if<SiteOperatingAuditEvent>(&event.payload))
            if (e->siteId == site.id && e->kind == SiteOperatingAuditKind::IssueAcknowledged &&
                e->cause == cause && e->episodeStartedDay == day)
                return true;
    return false;
}
void issue(GameState& s, ResourceSite& site, SiteOperatingIssueCause cause, std::int64_t day,
           std::string message, const SiteOperationHooks& h) {
    if (site.issue.cause == cause && site.issue.episodeStartedDay == day)
        return;
    const bool ack = acknowledgedBefore(s, site, cause, day);
    h.prepareEvents(1);
    site.issue = {cause, day, std::move(message), ack};
    audit(site, SiteOperatingAuditKind::IssueRaised, 0, site.issue.message, h);
}
} // namespace
void runSitesOpeningDay(GameState& s, OpeningProgramContext& opening, const SiteOperationHooks& h) {
    for (auto& budget : opening.sites) {
        auto& site = find(s, budget.siteId);
        ProcessedMaterialSet stock;
        for (std::size_t i = 0; i < stock.amount.size(); ++i)
            stock.amount[i] = opening.available(s, site.id, static_cast<ProcessedMaterial>(i), 0);
        const auto work = siteDutyPreview(s, site, s.date.day, &stock);
        if (!work.canWork)
            continue;
        const auto cap = siteCapabilities(s, site, s.date.day);
        const double handling = cap.equipment.rawHandling * work.work;
        if (!std::isfinite(handling))
            throw std::runtime_error("Site handling overflow");
        SiteDutyReceipt receipt{s.date.day,
                                site.operatingRevision,
                                *site.operatingPolicy.leaderId,
                                budget.installedGroupCutoff,
                                work.work,
                                work.consumed.get(ProcessedMaterial::ReactorFuel),
                                work.consumed.get(ProcessedMaterial::IndustrialComposites),
                                handling,
                                site.operatingPolicy};
        site.dutyReceipts.reserve(site.dutyReceipts.size() + 1);
        h.prepareEvents(1);
        for (std::size_t i = 0; i < work.consumed.amount.size(); ++i)
            if (work.consumed.amount[i] > 0) {
                const auto material = static_cast<ProcessedMaterial>(i);
                opening.debit(site.id, material, work.consumed.amount[i]);
                site.processedStock.amount[i] -= work.consumed.amount[i];
            }
        site.dutyReceipts.push_back(std::move(receipt));
        budget.duty = work.work;
        budget.remainingHandling = handling;
        audit(site, SiteOperatingAuditKind::DutyPerformed, work.work, "Paid supported site duty", h);
    }
}
void runSitesExtractionDay(GameState& s, OpeningProgramContext& opening, const SiteOperationHooks& h) {
    // Stored site order follows all colony miners; every debit has one matching
    // receipt and inventory credit. No diagnostic reads this physical boundary.
    for (auto& budget : opening.sites) {
        auto& site = find(s, budget.siteId);
        if (budget.duty <= 0 || !site.operatingPolicy.leaderId)
            continue;
        const double room = std::max(0.0, budget.rawCapacity - siteRawOccupied(site));
        // Duty scales the authorized attempted rate, before physical recovery.
        // Positive space/handling permits an attempt but caps only recovered
        // output; applying these caps before accessibility would underproduce.
        const double nominal =
            budget.duty * std::min(budget.extractionRate, site.operatingPolicy.requestedIcePerDay);
        if (!(nominal > 0) || !std::isfinite(nominal) || room <= 0 || budget.remainingHandling <= 0)
            continue;
        auto deposit = std::find_if(s.mineralDeposits.begin(), s.mineralDeposits.end(), [&](const auto& d) {
            return d.bodyId == site.bodyId && d.mineral == Mineral::WaterIce;
        });
        const double recovered =
            deposit == s.mineralDeposits.end()
                ? 0
                : std::min({deposit->remaining, nominal * std::clamp(deposit->accessibility, 0.0, 1.0), room,
                            budget.remainingHandling});
        const double prior = site.rawStock.get(Mineral::WaterIce);
        if (recovered > 0) {
            const double after = prior + recovered;
            const double reserveAfter = deposit->remaining - recovered;
            const double credit = after - prior, debit = deposit->remaining - reserveAfter;
            // Paid support remains spent, but an unrepresentable transfer is
            // not a geological zero observation. Neither inventory end moves,
            // no handler is spent, and no attempted-recovery receipt is made.
            if (!std::isfinite(after) || reserveAfter < 0 || credit <= 0 || debit <= 0 ||
                !freightNearlyEqual(credit, recovered) || !freightNearlyEqual(debit, recovered))
                continue;
        }
        SiteExtractionReceipt receipt{s.date.day,
                                      site.operatingRevision,
                                      *site.operatingPolicy.leaderId,
                                      budget.installedGroupCutoff,
                                      nominal,
                                      recovered,
                                      room,
                                      budget.remainingHandling,
                                      recovered > 0 ? "Water Ice recovered by site operation"
                                                    : "No Water Ice recovered by this attempt"};
        site.extractionReceipts.reserve(site.extractionReceipts.size() + 1);
        h.prepareEvents(1);
        if (recovered > 0) {
            deposit->remaining -= recovered;
            site.rawStock.set(Mineral::WaterIce, prior + recovered);
            opening.debitSiteRawHandling(site.id, recovered);
        }
        site.extractionReceipts.push_back(std::move(receipt));
        audit(site, SiteOperatingAuditKind::ExtractionAttempted, recovered,
              site.extractionReceipts.back().observation, h);
    }
}
std::string siteOperatingCondition(const GameState& s, const ResourceSite& site) {
    const auto work = siteDutyPreview(s, site, nextOpeningDay(s.date.day));
    if (!work.canWork)
        return work.explanation;
    const auto cap = siteCapabilities(s, site, nextOpeningDay(s.date.day)).equipment;
    if (site.operatingPolicy.requestedIcePerDay == 0)
        return "Supported raw handling; extraction target is zero";
    if (cap.ratedExtraction == 0)
        return "Waiting for commissioned extraction capability";
    if (cap.rawStorage <= siteRawOccupied(site))
        return "Waiting for free shared raw storage";
    if (cap.rawHandling == 0)
        return "Waiting for supported raw handling";
    return work.work < 1 ? "Partial supported duty; extraction outcome remains unmeasured"
                         : "Ready for supported operation; extraction outcome remains unmeasured";
}
void finishSitesDay(GameState& s, const SiteOperationHooks& h) {
    for (auto& site : s.resourceSites) {
        // Resolve report cursor arithmetic before appending issue/report history.
        const auto nextReport =
            site.nextReportDay == s.date.day ? nextGlobalSurveyBoundary(s.date.day, 30) : site.nextReportDay;
        const auto [zeros, zeroStart] = zeroTail(site);
        const auto preview = siteDutyPreview(s, site, nextOpeningDay(s.date.day));
        // A newly observed loss of support is a distinct actionable cause even
        // if the operator already acknowledged an unproductive installation.
        // Once support returns, issue() restores the prior zero episode's ack
        // from its dated audit instead of interrupting for the same evidence.
        if (site.operatingPolicy.enabled && !site.dutyReceipts.empty() && !preview.canWork &&
            (preview.cause == SiteWorkCause::ReactorFuel || preview.cause == SiteWorkCause::Composites ||
             preview.cause == SiteWorkCause::DutyAllowance)) {
            const auto cause = preview.cause == SiteWorkCause::DutyAllowance
                                   ? SiteOperatingIssueCause::DutyAllowance
                                   : SiteOperatingIssueCause::SupplyLost;
            issue(s, site, cause, site.dutyReceipts.back().day, preview.explanation, h);
        } else if (zeros >= 5)
            issue(s, site, SiteOperatingIssueCause::ZeroRecovery, zeroStart,
                  "Five or more genuine site attempts recovered no Water Ice", h);
        else if (site.issue.cause != SiteOperatingIssueCause::None)
            site.issue = {};
        if (site.nextReportDay != s.date.day)
            continue;
        SiteOperatingReport r;
        r.startDay = site.reportStartDay;
        r.endDay = s.date.day;
        r.isNinetyDayReview = s.date.day % 90 == 0;
        r.operatingRevision = site.operatingRevision;
        r.policy = site.operatingPolicy;
        r.installedGroupCutoff = site.installed.size();
        for (const auto& d : site.dutyReceipts)
            if (d.day > r.startDay && d.day <= r.endDay) {
                r.supportedDuty += d.duty;
                r.reactorFuel += d.reactorFuel;
                r.composites += d.composites;
            }
        for (const auto& a : site.extractionReceipts)
            if (a.day > r.startDay && a.day <= r.endDay) {
                ++r.attempts;
                r.recoveredIce += a.recoveredIce;
            }
        for (const auto& p : s.freightPrograms)
            if (p.charter.source == StockLocation{site.id} &&
                p.charter.commodity == Commodity{Mineral::WaterIce})
                for (const auto& x : p.receipts)
                    if (x.day > r.startDay && x.day <= r.endDay) {
                        if (x.kind == FreightTransferKind::Load)
                            r.exportedIce += x.amount;
                        if (x.kind == FreightTransferKind::SourceReturn)
                            r.exportedIce -= x.amount;
                        if (x.kind == FreightTransferKind::Delivery)
                            r.deliveredIce += x.amount;
                    }
        r.rawOccupancy = siteRawOccupied(site);
        r.rawCapacity = siteCapabilities(s, site, nextOpeningDay(s.date.day)).equipment.rawStorage;
        r.waitingReason = site.issue.cause == SiteOperatingIssueCause::None ? siteOperatingCondition(s, site)
                                                                            : site.issue.message;
        r.auditThroughId = s.ids.nextEventId - 1;
        site.reports.reserve(site.reports.size() + 1);
        h.prepareEvents(1);
        site.reports.push_back(std::move(r));
        site.reportStartDay = s.date.day;
        site.nextReportDay = nextReport;
        audit(site, SiteOperatingAuditKind::ReportPublished, 0, "Site operating report published", h);
    }
}
} // namespace deep
