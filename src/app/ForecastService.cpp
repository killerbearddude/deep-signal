#include "app/ForecastService.h"

// Implements lightweight app-layer forecasts over the current simulation state.
// These projections intentionally mirror current Prototype 0.1 formulas without
// moving forecast logic into src/sim or introducing UI dependencies.

#include "sim/GameState.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

namespace deep {
namespace {

template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

[[nodiscard]] std::string bodyName(const GameState& state, const BodyId id) {
    const Body* body = findById(state.bodies, id);
    return body == nullptr ? std::string{"<unknown body>"} : body->name;
}

[[nodiscard]] std::string colonyName(const GameState& state, const ColonyId id) {
    const Colony* colony = findById(state.colonies, id);
    return colony == nullptr ? std::string{"<unknown colony>"} : colony->name;
}

[[nodiscard]] std::string shipClassName(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? std::string{"<unknown ship class>"} : shipClass->name;
}

[[nodiscard]] std::string mineralName(const Mineral mineral) {
    return std::string{toString(mineral)};
}

[[nodiscard]] int ceilToNonNegativeDays(const double days) noexcept {
    if (days <= 0.0) {
        return 0;
    }

    return static_cast<int>(std::ceil(days));
}

// Mirrors Simulation::simulateMining for a single colony/deposit pair without
// mutating state. The last extraction day is capped by remaining deposit amount.
[[nodiscard]] double dailyExtraction(const Colony& colony, const MineralDeposit& deposit) noexcept {
    if (deposit.remaining <= 0.0) {
        return 0.0;
    }

    const double potentialExtraction = colony.mines * deposit.accessibility;
    return std::min(deposit.remaining, std::max(0.0, potentialExtraction));
}

[[nodiscard]] std::string mineralIncomeExplanation(const Colony& colony, const MineralDeposit& deposit, const double incomePerDay) {
    std::ostringstream out;
    out << colony.mines << " mines * " << deposit.accessibility
        << " accessibility = " << incomePerDay << " per day";
    return out.str();
}

[[nodiscard]] double totalDailyExtraction(const GameState& state, const MineralDeposit& deposit) noexcept {
    double potentialIncome = 0.0;
    for (const Colony& colony : state.colonies) {
        if (colony.bodyId == deposit.bodyId) {
            potentialIncome += std::max(0.0, colony.mines * deposit.accessibility);
        }
    }

    return deposit.remaining <= 0.0 ? 0.0 : std::min(deposit.remaining, potentialIncome);
}

[[nodiscard]] std::optional<int> exhaustionDays(const MineralDeposit& deposit, const double incomePerDay) {
    if (deposit.remaining <= 0.0) {
        return 0;
    }

    if (incomePerDay <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays(deposit.remaining / incomePerDay);
}

[[nodiscard]] std::string exhaustionExplanation(const MineralDeposit& deposit, const double incomePerDay, const std::optional<int> etaDays) {
    if (deposit.remaining <= 0.0) {
        return "Deposit is already exhausted";
    }

    if (!etaDays.has_value()) {
        return "No positive daily extraction rate; exhaustion cannot be estimated";
    }

    std::ostringstream out;
    out << deposit.remaining << " remaining / " << incomePerDay
        << " per day = " << *etaDays << " day(s)";
    return out.str();
}

[[nodiscard]] double totalBuildPointsRemaining(const ShipyardOrder& order, const ShipClass& shipClass) noexcept {
    const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
    if (shipsRemaining == 0) {
        return 0.0;
    }

    // accumulatedBuildPoints applies to the next ship in the order. Treat it as
    // progress against the remaining total capacity requirement, then clamp away
    // small or corrupted over-progress so projections never report negative work.
    const double remaining = static_cast<double>(shipsRemaining) * shipClass.buildPoints - order.accumulatedBuildPoints;
    return std::max(0.0, remaining);
}

[[nodiscard]] std::optional<int> shipyardEtaDays(const ShipyardOrder& order, const ShipClass& shipClass, const Colony& colony) {
    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return 0;
    }

    if (shipClass.buildPoints <= 0.0 || colony.shipyardCapacity <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays(totalBuildPointsRemaining(order, shipClass) / colony.shipyardCapacity);
}

[[nodiscard]] std::string shipyardEtaExplanation(const ShipyardOrder& order,
                                                 const ShipClass* shipClass,
                                                 const Colony* colony,
                                                 const std::optional<int> etaDays) {
    if (shipClass == nullptr || colony == nullptr) {
        return "Order references missing colony or ship class; ETA cannot be estimated";
    }

    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return "Order is already complete";
    }

    if (!etaDays.has_value()) {
        return "No positive shipyard capacity or build-point requirement; ETA cannot be estimated";
    }

    std::ostringstream out;
    out << totalBuildPointsRemaining(order, *shipClass) << " build points remaining / "
        << colony->shipyardCapacity << " capacity per day = " << *etaDays
        << " day(s); mineral shortages may pause completion";
    return out.str();
}

[[nodiscard]] std::optional<int> fleetEtaDays(const Fleet& fleet) noexcept {
    if (fleet.activeOrder.type != FleetOrderType::MoveToBody) {
        return std::nullopt;
    }

    if (!fleet.activeOrder.targetBodyId.has_value()) {
        return std::nullopt;
    }

    return std::max(0, fleet.activeOrder.daysRemaining);
}

[[nodiscard]] std::string fleetEtaExplanation(const Fleet& fleet, const std::optional<int> etaDays) {
    if (fleet.activeOrder.type == FleetOrderType::None) {
        return "Fleet is idle; no arrival ETA";
    }

    if (!fleet.activeOrder.targetBodyId.has_value()) {
        return "Fleet has movement order without a target body; ETA cannot be estimated";
    }

    std::ostringstream out;
    out << "Fleet will arrive in " << *etaDays << " day(s) if the active order continues";
    return out.str();
}

} // namespace

ForecastService::ForecastService(const SimulationService& service) noexcept
    : service_{service} {}

std::vector<MineralIncomeForecast> ForecastService::mineralIncomePerDay() const {
    const GameState& state = service_.state();
    std::vector<MineralIncomeForecast> forecasts;
    forecasts.reserve(state.colonies.size() * state.mineralDeposits.size());

    for (const Colony& colony : state.colonies) {
        for (const MineralDeposit& deposit : state.mineralDeposits) {
            if (deposit.bodyId != colony.bodyId) {
                continue;
            }

            const double income = dailyExtraction(colony, deposit);
            forecasts.push_back(MineralIncomeForecast{
                .colonyId = colony.id,
                .bodyId = colony.bodyId,
                .mineral = deposit.mineral,
                .colonyName = colony.name,
                .bodyName = bodyName(state, colony.bodyId),
                .mineralName = mineralName(deposit.mineral),
                .incomePerDay = income,
                .explanation = mineralIncomeExplanation(colony, deposit, income)
            });
        }
    }

    return forecasts;
}

std::vector<DepositExhaustionForecast> ForecastService::depositExhaustionEstimates() const {
    const GameState& state = service_.state();
    std::vector<DepositExhaustionForecast> forecasts;
    forecasts.reserve(state.mineralDeposits.size());

    for (const MineralDeposit& deposit : state.mineralDeposits) {
        // Sum all colony extraction on this body so the deposit forecast remains
        // correct if later scenarios add multiple settlements to one body.
        const double income = totalDailyExtraction(state, deposit);
        const std::optional<int> eta = exhaustionDays(deposit, income);

        forecasts.push_back(DepositExhaustionForecast{
            .bodyId = deposit.bodyId,
            .mineral = deposit.mineral,
            .bodyName = bodyName(state, deposit.bodyId),
            .mineralName = mineralName(deposit.mineral),
            .remainingDeposit = deposit.remaining,
            .incomePerDay = income,
            .exhaustionDays = eta,
            .explanation = exhaustionExplanation(deposit, income, eta)
        });
    }

    return forecasts;
}

std::vector<ShipyardOrderEtaForecast> ForecastService::shipyardOrderEtas() const {
    const GameState& state = service_.state();
    std::vector<ShipyardOrderEtaForecast> forecasts;
    forecasts.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double remainingBuildPoints = shipClass == nullptr ? 0.0 : totalBuildPointsRemaining(order, *shipClass);
        const std::optional<int> eta = (colony == nullptr || shipClass == nullptr)
            ? std::nullopt
            : shipyardEtaDays(order, *shipClass, *colony);

        forecasts.push_back(ShipyardOrderEtaForecast{
            .orderId = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .shipsRemaining = shipsRemaining,
            .buildPointsRemaining = remainingBuildPoints,
            .etaDays = eta,
            .explanation = shipyardEtaExplanation(order, shipClass, colony, eta)
        });
    }

    return forecasts;
}

std::vector<FleetArrivalEtaForecast> ForecastService::fleetArrivalEtas() const {
    const GameState& state = service_.state();
    std::vector<FleetArrivalEtaForecast> forecasts;
    forecasts.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const std::optional<int> eta = fleetEtaDays(fleet);
        forecasts.push_back(FleetArrivalEtaForecast{
            .fleetId = fleet.id,
            .fleetName = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .currentBodyName = bodyName(state, fleet.currentBodyId),
            .destinationBodyId = fleet.activeOrder.targetBodyId,
            .destinationBodyName = fleet.activeOrder.targetBodyId.has_value()
                ? bodyName(state, *fleet.activeOrder.targetBodyId)
                : std::string{},
            .etaDays = eta,
            .explanation = fleetEtaExplanation(fleet, eta)
        });
    }

    return forecasts;
}

} // namespace deep
