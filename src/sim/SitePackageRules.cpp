// Explicit P4B fixture arithmetic. These rules know definitions and submitted
// module counts only; usefulness and physical work belong to later execution.
#include "sim/SitePackageRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace deep {
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
double add(double before, double value) {
    const double after = before + value;
    check(std::isfinite(value) && value >= 0 && std::isfinite(after) && (value == 0 || after > before),
          "Site package total is not representable");
    return after;
}
ProcessedMaterialSet cost(double alloys, double electronics, double composites) {
    ProcessedMaterialSet result;
    result.set(ProcessedMaterial::StructuralAlloys, alloys);
    result.set(ProcessedMaterial::Electronics, electronics);
    result.set(ProcessedMaterial::IndustrialComposites, composites);
    return result;
}
} // namespace
std::vector<SiteModuleDefinition> referenceSiteModuleCatalog() {
    return {{.kind = SiteModuleKind::IceExtraction,
             .cost = cost(80, 20, 10),
             .assemblyWorkdays = 4,
             .powerDemand = 10,
             .extractionPerDay = 10},
            {.kind = SiteModuleKind::Power,
             .cost = cost(60, 20, 10),
             .assemblyWorkdays = 3,
             .powerGeneration = 30,
             .reactorFuelPerDuty = 1},
            {.kind = SiteModuleKind::BulkHandling,
             .cost = cost(30, 10, 10),
             .assemblyWorkdays = 2,
             .powerDemand = 5,
             .handlingPerDay = 50},
            {.kind = SiteModuleKind::BulkStorage,
             .cost = cost(40, 0, 20),
             .assemblyWorkdays = 2,
             .rawStorage = 200},
            {.kind = SiteModuleKind::AutomationSupport,
             .cost = cost(30, 20, 10),
             .assemblyWorkdays = 3,
             .powerDemand = 5,
             .supportedExtractors = 1,
             .compositesPerDuty = 1}};
}
std::vector<SiteModuleInstall> referenceSitePackage() {
    std::vector<SiteModuleInstall> rows;
    for (std::size_t i = 0; i < siteModuleKindCount; ++i)
        rows.push_back({static_cast<SiteModuleKind>(i), 1});
    return rows;
}
void validateSiteModuleCatalog(std::span<const SiteModuleDefinition> definitions) {
    std::set<std::pair<int, SiteModuleKind>> keys;
    for (const auto& d : definitions) {
        check(d.kind >= SiteModuleKind::IceExtraction && d.kind < SiteModuleKind::Count &&
                  d.version == kSiteModuleCatalogVersion && keys.emplace(d.version, d.kind).second,
              "Invalid or duplicate site module identity");
        for (double value :
             {d.assemblyWorkdays, d.powerGeneration, d.powerDemand, d.extractionPerDay, d.handlingPerDay,
              d.rawStorage, d.supportedExtractors, d.reactorFuelPerDuty, d.compositesPerDuty})
            check(std::isfinite(value) && value >= 0, "Invalid site module numeric definition");
        check(d.assemblyWorkdays > 0, "Site module requires positive assembly work");
        for (double value : d.cost.amount)
            check(std::isfinite(value) && value >= 0, "Invalid site module cost");
        check(d.cost.get(ProcessedMaterial::Propellant) == 0 &&
                  d.cost.get(ProcessedMaterial::ReactorFuel) == 0,
              "Site assembly costs cannot include operating fuel supplies");
    }
}
SitePackageEvaluation evaluateSitePackage(std::span<const SiteModuleDefinition> definitions, int version,
                                          std::span<const SiteModuleInstall> package) {
    validateSiteModuleCatalog(definitions);
    check(version > 0 && !package.empty(), "Site package requires a version and at least one module row");
    SitePackageEvaluation result;
    std::set<SiteModuleKind> kinds;
    double extractorRate = 0, supported = 0;
    for (const auto& row : package) {
        check(row.quantity > 0 && kinds.insert(row.kind).second,
              "Site package quantity is invalid or kind repeated");
        const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& d) {
            return d.version == version && d.kind == row.kind;
        });
        check(found != definitions.end(), "Site package references an unknown module kind/version");
        const auto& d = *found;
        const double n = row.quantity;
        result.counts.at(static_cast<std::size_t>(row.kind)) = row.quantity;
        for (std::size_t i = 0; i < processedMaterialCount(); ++i)
            result.cost.amount[i] = add(result.cost.amount[i], d.cost.amount[i] * n);
        result.assemblyWorkdays = add(result.assemblyWorkdays, d.assemblyWorkdays * n);
        result.powerGeneration = add(result.powerGeneration, d.powerGeneration * n);
        result.powerDemand = add(result.powerDemand, d.powerDemand * n);
        result.rawHandling = add(result.rawHandling, d.handlingPerDay * n);
        result.rawStorage = add(result.rawStorage, d.rawStorage * n);
        result.reactorFuelPerDuty = add(result.reactorFuelPerDuty, d.reactorFuelPerDuty * n);
        result.compositesPerDuty = add(result.compositesPerDuty, d.compositesPerDuty * n);
        supported = add(supported, d.supportedExtractors * n);
        if (row.kind == SiteModuleKind::IceExtraction)
            extractorRate = d.extractionPerDay;
    }
    result.powerMargin = result.powerGeneration - result.powerDemand;
    result.ratedExtraction =
        extractorRate *
        std::min(static_cast<double>(result.counts[static_cast<std::size_t>(SiteModuleKind::IceExtraction)]),
                 supported);
    check(std::isfinite(result.powerMargin) && std::isfinite(result.ratedExtraction) &&
              std::isfinite(result.assemblyWorkdays + result.commissioningWorkdays),
          "Site package totals overflow");
    return result;
}
} // namespace deep
