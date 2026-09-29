#pragma once
// Knowledge-limited site capability and support projections. These functions
// read installed groups and real stocks only, never physical mineral deposits.
#include "sim/GameState.h"
#include "sim/SiteWorkRules.h"
namespace deep {
struct SiteCapabilities {
    SitePackageEvaluation equipment;
    std::size_t installedGroupCutoff = 0;
};
// Only groups commissioned before openingDay contribute to this opening.
[[nodiscard]] SiteCapabilities siteCapabilities(const GameState&, const ResourceSite&,
                                                std::int64_t openingDay);
// All raw channels share one physical bin; throws for invalid/nonfinite stock.
[[nodiscard]] double siteRawOccupied(const ResourceSite&);
// Actual supported-duty receipts are the only lifetime expenditure authority.
[[nodiscard]] double siteDutySpent(const ResourceSite&);
// Supplied opening stock is required during execution; absent uses current
// stock for a conditional read-only preview at the supplied opening date.
[[nodiscard]] SiteWorkReadiness siteDutyPreview(const GameState&, const ResourceSite&,
                                                std::int64_t openingDay,
                                                const ProcessedMaterialSet* openingStock = nullptr);
[[nodiscard]] double siteRawHandlingPreview(const GameState&, SiteId, std::int64_t openingDay);
[[nodiscard]] double siteRawRoom(const GameState&, SiteId, std::int64_t openingDay);
// Policy validity is structural. Missing stock, hardware or leader is not invalid.
[[nodiscard]] std::optional<std::string> validateSiteOperatingPolicy(const GameState&,
                                                                     const SiteOperatingPolicy&);
} // namespace deep
