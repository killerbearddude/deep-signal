#pragma once

// Pure site labor and support preparation. Callers supply known opening stocks
// and authority; these functions neither mutate inventories nor inspect geology.
#include "sim/SitePackageRules.h"
#include <optional>
#include <string>

namespace deep {
enum class SiteWorkCause {
    Ready,
    Complete,
    NoTeamRate,
    NoWorkshopRate,
    MaterialStock,
    MaterialFloor,
    MaterialAllowance,
    Disabled,
    NoLeader,
    NoPower,
    PowerDeficit,
    NoAutomation,
    DutyAllowance,
    ReactorFuel,
    Composites,
    NumericLimit
};
struct SiteWorkReadiness {
    bool canWork = false;
    SiteWorkCause cause = SiteWorkCause::Complete;
    std::string explanation;
    double work = 0;
    ProcessedMaterialSet consumed;
};
struct SiteAssemblyInputs {
    double requiredWork = 0;
    double completedWork = 0;
    ProcessedMaterialSet totalCost;
    double teamRate = 0;
    double workshopRate = 0;
    ProcessedMaterialSet currentStock;
    ProcessedMaterialSet openingStock;
    ProcessedMaterialSet floors;
    std::optional<ProcessedMaterialSet> lifetimeAllowances;
    ProcessedMaterialSet lifetimeConsumed;
};
// Bounds one row's work by both actual and opening stock, authority and rates.
// No materials are charged for a zero result. Throws for malformed inputs.
[[nodiscard]] SiteWorkReadiness prepareSiteAssembly(const SiteAssemblyInputs&);

struct SiteDutyInputs {
    bool enabled = true;
    bool hasLeader = false;
    SitePackageEvaluation installed;
    ProcessedMaterialSet currentStock;
    ProcessedMaterialSet openingStock;
    double reactorFuelFloor = 0;
    double compositesFloor = 0;
    std::optional<double> lifetimeDutyAllowance;
    double dutySpent = 0;
};
// One equivalent full-duty site-day at most. Positive duty pays support even
// with no extractor, no storage, a full bin, or a later measured zero recovery.
[[nodiscard]] SiteWorkReadiness prepareSiteDuty(const SiteDutyInputs&);
} // namespace deep
