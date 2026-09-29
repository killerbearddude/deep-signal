#pragma once

// Known-state admission and one-row work preparation. No geological truth is
// consulted, and projections never acquire assets or spend material.
#include "sim/EquipmentServiceRules.h"
#include "sim/SiteWorkRules.h"
namespace deep {
struct OpeningProgramContext;
struct SiteDevelopmentWorkPlan {
    SiteWorkReadiness readiness;
    int packageRow = -1;
    ShipId workshopShipId;
    bool commissioning = false;
};
// Rejects malformed authority; missing physical capabilities remain waiting intent.
[[nodiscard]] std::optional<std::string>
validateSiteDevelopmentCharter(const GameState&, const SiteDevelopmentCharter&, bool registeringSite = false);
// Chooses the first executable unfinished row, with a pinned hull once started.
[[nodiscard]] SiteDevelopmentWorkPlan planSiteDevelopmentWork(const GameState&, const SiteDevelopmentProgram&,
                                                              const OpeningProgramContext* = nullptr);
// Returns an owned explanation using only public resources and physical custody.
[[nodiscard]] std::string siteDevelopmentExecutionCondition(const GameState&, const SiteDevelopmentProgram&);
// Conditional dated round trip, including remaining row-specific work openings.
// Infinity indicates a route/rate/date that cannot currently be represented.
[[nodiscard]] double siteDevelopmentRoundTripFuel(const GameState&, const SiteDevelopmentProgram&,
                                                  const Fleet&, std::int64_t departureDay);
} // namespace deep
