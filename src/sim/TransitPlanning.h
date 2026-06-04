#pragma once

// Shared deterministic rail and sustained-burn transit helpers.
// Simulation, app queries, and forecasts use this single planning surface so
// command execution and UI previews cannot drift apart as route rules evolve.

#include "sim/Domain.h"
#include "sim/GameState.h"

#include <cstdint>
#include <optional>

namespace deep {

// Computes the display/map-space position for a body on its simple circular
// rail at the given game day. Returns nullopt when the body ID or parent chain
// cannot be resolved; validated GameState snapshots should always resolve.
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
[[nodiscard]] double sustainedBurnTravelDays(double transitDistanceKm, double accelerationG) noexcept;

// Produces the authoritative v1 fleet transit plan. The destination position is
// projected at arrival day using a fixed small iteration count because the
// arrival day depends on distance to the destination's future rail position.
[[nodiscard]] FleetOrder planFleetTransit(const GameState& state,
                                          BodyId departureBodyId,
                                          BodyId destinationBodyId,
                                          std::int64_t departureDay,
                                          double burnAccelerationG = kPrototypeBurnAccelerationG) noexcept;

// Base v1 fuel requirement for a planned body-to-body move before personnel or
// future logistics modifiers are applied. Invalid plans return infinity so
// command validators fail closed instead of treating bad routes as free travel.
[[nodiscard]] double moveFuelCost(const GameState& state,
                                  BodyId originBodyId,
                                  BodyId destinationBodyId,
                                  std::int64_t departureDay,
                                  double burnAccelerationG = kPrototypeBurnAccelerationG) noexcept;

} // namespace deep
