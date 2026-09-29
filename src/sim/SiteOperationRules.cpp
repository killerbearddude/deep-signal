// Recorded hardware and known inventory define readiness independently from
// whether an actual geological recovery will later find usable Water Ice.
#include "sim/SiteOperationRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace deep {
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
double add(double a, double b) {
    check(std::isfinite(b) && b >= 0 && std::isfinite(a + b), "Site capability total overflow");
    return a + b;
}
const ResourceSite& site(const GameState& s, SiteId id) {
    auto it = std::find_if(s.resourceSites.begin(), s.resourceSites.end(),
                           [&](const auto& x) { return x.id == id; });
    check(it != s.resourceSites.end(), "Unknown site");
    return *it;
}
} // namespace
SiteCapabilities siteCapabilities(const GameState& s, const ResourceSite& site, std::int64_t day) {
    SiteCapabilities result;
    auto& e = result.equipment;
    e.commissioningWorkdays = 0;
    double extractorRate = 0, support = 0;
    for (std::size_t index = 0; index < site.installed.size(); ++index) {
        const auto& group = site.installed[index];
        if (group.commissionedDay >= day)
            continue;
        const auto it =
            std::find_if(s.siteModuleCatalog.begin(), s.siteModuleCatalog.end(), [&](const auto& d) {
                return d.kind == group.kind && d.version == group.catalogVersion;
            });
        check(it != s.siteModuleCatalog.end() && group.quantity > 0,
              "Unknown or invalid commissioned site module");
        const auto& d = *it;
        auto& count = e.counts.at(static_cast<std::size_t>(group.kind));
        check(count <= std::numeric_limits<std::int64_t>::max() - group.quantity,
              "Installed module count overflow");
        count += group.quantity;
        const double n = group.quantity;
        e.powerGeneration = add(e.powerGeneration, d.powerGeneration * n);
        e.powerDemand = add(e.powerDemand, d.powerDemand * n);
        e.rawHandling = add(e.rawHandling, d.handlingPerDay * n);
        e.rawStorage = add(e.rawStorage, d.rawStorage * n);
        e.reactorFuelPerDuty = add(e.reactorFuelPerDuty, d.reactorFuelPerDuty * n);
        e.compositesPerDuty = add(e.compositesPerDuty, d.compositesPerDuty * n);
        support = add(support, d.supportedExtractors * n);
        if (group.kind == SiteModuleKind::IceExtraction)
            extractorRate = d.extractionPerDay;
        result.installedGroupCutoff = index + 1;
    }
    e.powerMargin = e.powerGeneration - e.powerDemand;
    e.ratedExtraction = extractorRate * std::min(static_cast<double>(e.counts[0]), support);
    check(std::isfinite(e.powerMargin) && std::isfinite(e.ratedExtraction),
          "Site aggregate capability overflow");
    return result;
}
double siteRawOccupied(const ResourceSite& site) {
    double total = 0;
    for (double value : site.rawStock.amount)
        total = add(total, value);
    return total;
}
double siteDutySpent(const ResourceSite& site) {
    double total = 0;
    for (const auto& r : site.dutyReceipts)
        total = add(total, r.duty);
    return total;
}
SiteWorkReadiness siteDutyPreview(const GameState& s, const ResourceSite& site, std::int64_t day,
                                  const ProcessedMaterialSet* opening) {
    SiteDutyInputs inputs;
    inputs.enabled = site.operatingPolicy.enabled;
    inputs.hasLeader = site.operatingPolicy.leaderId.has_value();
    inputs.installed = siteCapabilities(s, site, day).equipment;
    inputs.currentStock = site.processedStock;
    inputs.openingStock = opening ? *opening : site.processedStock;
    inputs.reactorFuelFloor = site.operatingPolicy.reactorFuelFloor;
    inputs.compositesFloor = site.operatingPolicy.compositesFloor;
    inputs.lifetimeDutyAllowance = site.operatingPolicy.lifetimeDutyAllowance;
    inputs.dutySpent = siteDutySpent(site);
    return prepareSiteDuty(inputs);
}
double siteRawHandlingPreview(const GameState& s, SiteId id, std::int64_t day) {
    const auto& at = site(s, id);
    const auto work = siteDutyPreview(s, at, day);
    return work.canWork ? work.work * siteCapabilities(s, at, day).equipment.rawHandling : 0.0;
}
double siteRawRoom(const GameState& s, SiteId id, std::int64_t day) {
    const auto& at = site(s, id);
    return std::max(0.0, siteCapabilities(s, at, day).equipment.rawStorage - siteRawOccupied(at));
}
std::optional<std::string> validateSiteOperatingPolicy(const GameState& s, const SiteOperatingPolicy& p) {
    if (p.leaderId && std::none_of(s.people.begin(), s.people.end(),
                                   [&](const auto& person) { return person.id == *p.leaderId; }))
        return "Site operating leader does not exist";
    for (double value : {p.requestedIcePerDay, p.reactorFuelFloor, p.compositesFloor})
        if (!std::isfinite(value) || value < 0)
            return "Site operating targets/floors must be finite and nonnegative";
    if (p.lifetimeDutyAllowance && (!std::isfinite(*p.lifetimeDutyAllowance) || *p.lifetimeDutyAllowance < 0))
        return "Site duty allowance must be finite and nonnegative";
    return std::nullopt;
}
} // namespace deep
