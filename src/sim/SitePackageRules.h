#pragma once

// Pure, knowledge-independent package evaluation. Registration and preview do
// not create installed modules, spend materials or inspect geological deposits.
#include "sim/Minerals.h"
#include <cstdint>
#include <span>
#include <vector>

namespace deep {
inline constexpr int kSiteModuleCatalogVersion = 1;
inline constexpr double kSiteCommissioningWorkdays = 2.0;
enum class SiteModuleKind { IceExtraction, Power, BulkHandling, BulkStorage, AutomationSupport, Count };
inline constexpr std::size_t siteModuleKindCount = 5;

// Campaign-authored definitions are immutable within one catalog version. Costs
// are normalized processed units; labor is engineering team-workdays.
struct SiteModuleDefinition {
    SiteModuleKind kind = SiteModuleKind::IceExtraction;
    int version = kSiteModuleCatalogVersion;
    ProcessedMaterialSet cost;
    double assemblyWorkdays = 0;
    double powerGeneration = 0;
    double powerDemand = 0;
    double extractionPerDay = 0;
    double handlingPerDay = 0;
    double rawStorage = 0;
    double supportedExtractors = 0;
    double reactorFuelPerDuty = 0;
    double compositesPerDuty = 0;
};
struct SiteModuleInstall {
    SiteModuleKind kind = SiteModuleKind::IceExtraction;
    int quantity = 1;
    bool operator==(const SiteModuleInstall&) const = default;
};
struct SitePackageEvaluation {
    ProcessedMaterialSet cost;
    double assemblyWorkdays = 0;
    double commissioningWorkdays = kSiteCommissioningWorkdays;
    double powerGeneration = 0;
    double powerDemand = 0;
    double powerMargin = 0;
    double ratedExtraction = 0;
    double rawHandling = 0;
    double rawStorage = 0;
    double reactorFuelPerDuty = 0;
    double compositesPerDuty = 0;
    std::array<std::int64_t, siteModuleKindCount> counts{};
};

// Authors the five supplied prototype definitions; no build/unlock side effects.
[[nodiscard]] std::vector<SiteModuleDefinition> referenceSiteModuleCatalog();
// One of each kind in the declared construction order, without physical assets.
[[nodiscard]] std::vector<SiteModuleInstall> referenceSitePackage();
// Validates finite/nonnegative definition data and unique version/kind identities.
// Throws on malformed data; never repairs or normalizes submitted rows.
void validateSiteModuleCatalog(std::span<const SiteModuleDefinition>);
// Validates a nonempty, ordered, unique-kind package and derives finite demand
// and nominal capabilities. Missing power/support/storage remains valid intent.
[[nodiscard]] SitePackageEvaluation evaluateSitePackage(std::span<const SiteModuleDefinition>, int version,
                                                        std::span<const SiteModuleInstall>);
} // namespace deep
