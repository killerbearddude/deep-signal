#pragma once

// Shared deterministic rail and sustained-burn transit helpers.
// Simulation, app queries, and forecasts use this single planning surface so
// command execution and UI previews use the same route model. Helpers borrow
// state without mutation; command eligibility and fuel payment live in Simulation.

#include "sim/Domain.h"
#include "sim/GameState.h"

#include <cstdint>
#include <optional>

namespace deep {

// Computes the display/map-space position for a body on its simple circular
// rail at the given game day. Returns nullopt for an unresolved body/parent or a
// chain deeper than 16. Stars, unparented bodies, and bodies with inactive rail
// metadata use their fixed x/y coordinates. Callers provide finite valid metadata.
[[nodiscard]] std::optional<MapPosition> bodyPositionAtDay(const GameState& state,
                                                           BodyId bodyId,
                                                           std::int64_t day) noexcept;

// Euclidean distance in rendered map units. Transit planning intentionally uses
// chord distance between projected positions, not true orbital mechanics.
[[nodiscard]] double mapDistance(MapPosition origin, MapPosition destination) noexcept;

// Control point for the presentation curve between two transit endpoints. The
// curve is render-language only; sustained-burn timing remains chord-based.
[[nodiscard]] MapPosition routeCurveControlPoint(MapPosition departure, MapPosition arrival) noexcept;

// Flip-and-burn travel estimate for a sustained-burn transit using T=2*sqrt(d/a).
// Distance is kilometers and acceleration is expressed in Earth gravities.
// Returns fractional days; non-positive distance or acceleration returns zero.
// Assumes rest-to-rest motion with equal accelerating/decelerating halves and no
// gravity, speed limit, or coasting. Callers must reject non-finite inputs.
[[nodiscard]] double sustainedBurnTravelDays(double transitDistanceKm, double accelerationG) noexcept;

// Produces the authoritative v1 fleet transit plan. The destination position is
// projected at arrival day using a fixed small iteration count because the
// arrival day depends on distance to the destination's future rail position.
// Preconditions: resolvable distinct endpoints, finite positive acceleration, and
// travel dates/duration representable by the integer fields. This is a geometric
// planner, not command validation: an unresolved departure returns None, while
// unresolved destination projections retain a fallback position. Durations round
// up to whole days with a minimum of one; iteration does not guarantee convergence.
[[nodiscard]] FleetOrder planFleetTransit(const GameState& state,
                                          BodyId departureBodyId,
                                          BodyId destinationBodyId,
                                          std::int64_t departureDay,
                                          double burnAccelerationG = kPrototypeBurnAccelerationG) noexcept;

// Base v1 fuel requirement for a planned body-to-body move before personnel or
// future logistics modifiers are applied. Shares planFleetTransit's preconditions;
// same-body requests return zero, and None/non-positive-distance plans return
// infinity. Positive routes cost at least one abstract fuel unit for the fleet,
// independent of hull count or mass.
[[nodiscard]] double moveFuelCost(const GameState& state,
                                  BodyId originBodyId,
                                  BodyId destinationBodyId,
                                  std::int64_t departureDay,
                                  double burnAccelerationG = kPrototypeBurnAccelerationG) noexcept;

} // namespace deep
