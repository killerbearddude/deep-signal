#include "sim/TransitPlanning.h"

// Implements the shared v1 transit model: fixed on-rails body positions plus a
// simplified sustained-burn route estimate. This module has no SQLite/UI
// dependencies and belongs to the deterministic simulation layer.

#include <algorithm>
#include <cmath>
#include <limits>

namespace deep {
namespace {

template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

[[nodiscard]] MapPosition fallbackBodyPosition(const Body& body) noexcept {
    return MapPosition{.x = body.x, .y = body.y};
}

[[nodiscard]] std::optional<MapPosition> bodyPositionAtDayRecursive(const GameState& state,
                                                                    const BodyId bodyId,
                                                                    const std::int64_t day,
                                                                    const int depth) noexcept {
    // Validation rejects deeper chains. The guard remains here so corrupted
    // states fail closed if this helper is called before validation.
    if (depth > 16) {
        return std::nullopt;
    }

    const Body* body = findById(state.bodies, bodyId);
    if (body == nullptr) {
        return std::nullopt;
    }

    if (body->type == BodyType::Star || !body->parentBodyId.has_value() ||
        body->orbitalRadiusKm <= 0.0 || body->orbitalPeriodDays <= 0.0) {
        return fallbackBodyPosition(*body);
    }

    const std::optional<MapPosition> parentPosition = bodyPositionAtDayRecursive(state, *body->parentBodyId, day, depth + 1);
    if (!parentPosition.has_value()) {
        return std::nullopt;
    }

    constexpr double kTwoPi = 6.28318530717958647692;
    const double angle = body->phaseRadians + (kTwoPi * static_cast<double>(day) / body->orbitalPeriodDays);
    const double radiusMapUnits = body->orbitalRadiusKm / kKilometersPerMapUnit;
    return MapPosition{
        .x = parentPosition->x + (std::cos(angle) * radiusMapUnits),
        .y = parentPosition->y + (std::sin(angle) * radiusMapUnits)
    };
}

} // namespace

std::optional<MapPosition> bodyPositionAtDay(const GameState& state,
                                             const BodyId bodyId,
                                             const std::int64_t day) noexcept {
    return bodyPositionAtDayRecursive(state, bodyId, day, 0);
}

double mapDistance(const MapPosition origin, const MapPosition destination) noexcept {
    return std::hypot(destination.x - origin.x, destination.y - origin.y);
}

MapPosition routeCurveControlPoint(const MapPosition departure, const MapPosition arrival) noexcept {
    const double dx = arrival.x - departure.x;
    const double dy = arrival.y - departure.y;
    const double length = std::hypot(dx, dy);
    if (length <= 1.0e-9) {
        return MapPosition{.x = departure.x, .y = departure.y};
    }

    // Bend the rendered route away from the chord so the visual language reads
    // as a planned sustained-burn transit rather than a straight targeting ray.
    const double bend = length * 0.18;
    return MapPosition{
        .x = (departure.x + arrival.x) * 0.5 + (dy / length) * bend,
        .y = (departure.y + arrival.y) * 0.5 - (dx / length) * bend
    };
}

double sustainedBurnTravelDays(const double transitDistanceKm, const double accelerationG) noexcept {
    if (transitDistanceKm <= 0.0 || accelerationG <= 0.0) {
        return 0.0;
    }

    const double distanceMeters = transitDistanceKm * 1000.0;
    const double accelerationMetersPerSecondSquared = accelerationG * kStandardGravityMetersPerSecondSquared;
    const double seconds = 2.0 * std::sqrt(distanceMeters / accelerationMetersPerSecondSquared);
    return seconds / kSecondsPerGameDay;
}

FleetOrder planFleetTransit(const GameState& state,
                            const BodyId departureBodyId,
                            const BodyId destinationBodyId,
                            const std::int64_t departureDay,
                            const double burnAccelerationG) noexcept {
    const std::optional<MapPosition> departurePosition = bodyPositionAtDay(state, departureBodyId, departureDay);
    if (!departurePosition.has_value()) {
        return FleetOrder{};
    }

    std::int64_t arrivalDay = departureDay + 1;
    MapPosition projectedArrivalPosition = bodyPositionAtDay(state, destinationBodyId, arrivalDay).value_or(*departurePosition);
    double transitDistanceKm = mapDistance(*departurePosition, projectedArrivalPosition) * kKilometersPerMapUnit;

    for (int i = 0; i < kTransitPlanningIterations; ++i) {
        const double travelDays = sustainedBurnTravelDays(transitDistanceKm, burnAccelerationG);
        arrivalDay = departureDay + std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(travelDays)));
        projectedArrivalPosition = bodyPositionAtDay(state, destinationBodyId, arrivalDay).value_or(projectedArrivalPosition);
        transitDistanceKm = mapDistance(*departurePosition, projectedArrivalPosition) * kKilometersPerMapUnit;
    }

    const int daysRemaining = static_cast<int>(std::max<std::int64_t>(1, arrivalDay - departureDay));
    return FleetOrder{
        .type = FleetOrderType::MoveToBody,
        .targetBodyId = destinationBodyId,
        .daysRemaining = daysRemaining,
        .departureBodyId = departureBodyId,
        .departureDay = departureDay,
        .arrivalDay = arrivalDay,
        .departurePosition = *departurePosition,
        .projectedArrivalPosition = projectedArrivalPosition,
        .transitDistanceKm = transitDistanceKm,
        .burnAccelerationG = burnAccelerationG,
        .routeCurveControlPoint = routeCurveControlPoint(*departurePosition, projectedArrivalPosition)
    };
}

double moveFuelCost(const GameState& state,
                    const BodyId originBodyId,
                    const BodyId destinationBodyId,
                    const std::int64_t departureDay,
                    const double burnAccelerationG) noexcept {
    if (originBodyId == destinationBodyId) {
        return 0.0;
    }

    const FleetOrder plan = planFleetTransit(state, originBodyId, destinationBodyId, departureDay, burnAccelerationG);
    if (plan.type != FleetOrderType::MoveToBody || plan.transitDistanceKm <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }

    // Fuel remains a v1 operational abstraction. It scales with planned transit
    // distance while route timing comes from the sustained-burn estimate.
    return std::max(1.0, (plan.transitDistanceKm / kKilometersPerMapUnit) * kPrototypeFuelPerMapUnit);
}

} // namespace deep
