#include "app/ForecastService.h"

// Implements lightweight app-layer forecasts over the current simulation state.
// These projections intentionally mirror current Prototype 0.1 formulas without
// moving forecast logic into src/sim or introducing UI dependencies.

#include "sim/GameState.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
// mutating state. availableRemaining is the shared amount left after earlier
// colonies in deterministic state order have taken their forecast extraction.
[[nodiscard]] double dailyExtraction(const Colony& colony,
                                     const MineralDeposit& deposit,
                                     const double availableRemaining) noexcept {
    if (availableRemaining <= 0.0) {
        return 0.0;
    }

    const double potentialExtraction = colony.mines * deposit.accessibility;
    return std::min(availableRemaining, std::max(0.0, potentialExtraction));
}

[[nodiscard]] std::string mineralIncomeExplanation(const Colony& colony, const MineralDeposit& deposit, const double incomePerDay) {
    std::ostringstream out;
    out << colony.mines << " mines * " << deposit.accessibility
        << " accessibility = " << incomePerDay << " per day";
    return out.str();
}

using MineralAmountTotals = std::array<double, mineralCount()>;

void addMineralSet(MineralAmountTotals& totals, const MineralSet& minerals, const double scale = 1.0) noexcept {
    for (std::size_t i = 0; i < totals.size(); ++i) {
        totals[i] += minerals.amount[i] * scale;
    }
}

[[nodiscard]] Mineral mineralFromIndex(const std::size_t index) noexcept {
    return static_cast<Mineral>(index);
}

[[nodiscard]] std::optional<int> stockpileRunoutDays(const double stockpile, const double netPerDay) {
    if (netPerDay >= -kMineralComparisonEpsilon) {
        return std::nullopt;
    }

    if (stockpile <= 0.0) {
        return 0;
    }

    return ceilToNonNegativeDays(stockpile / -netPerDay);
}

[[nodiscard]] std::string miningCauseExplanation(const double incomePerDay) {
    std::ostringstream out;
    out << incomePerDay << " per day from current mines and accessible deposits";
    return out.str();
}

[[nodiscard]] std::string shipyardDemandCauseExplanation(const double demandPerDay) {
    std::ostringstream out;
    out << demandPerDay << " per day committed to active shipyard orders; "
        << "demand is amortized over capacity-only order ETAs";
    return out.str();
}

[[nodiscard]] MineralAmountTotals totalColonyStockpiles(const GameState& state) noexcept {
    MineralAmountTotals totals{};
    for (const Colony& colony : state.colonies) {
        addMineralSet(totals, colony.stockpile);
    }
    return totals;
}

[[nodiscard]] MineralAmountTotals miningIncomeByMineral(const std::vector<MineralIncomeForecast>& incomeRows) noexcept {
    MineralAmountTotals totals{};
    for (const MineralIncomeForecast& row : incomeRows) {
        totals[mineralIndex(row.mineral)] += row.incomePerDay;
    }
    return totals;
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

[[nodiscard]] MineralAmountTotals activeShipyardDemandByMineral(const GameState& state) {
    MineralAmountTotals totals{};

    for (const ShipyardOrder& order : state.shipyardOrders) {
        if (order.status != ShipyardOrderStatus::Active || order.quantityCompleted >= order.quantityRequested) {
            continue;
        }

        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        if (colony == nullptr || shipClass == nullptr) {
            continue;
        }

        const std::optional<int> etaDays = shipyardEtaDays(order, *shipClass, *colony);
        if (!etaDays.has_value() || *etaDays <= 0) {
            continue;
        }

        // Prototype v1 explains committed production as an amortized demand flow,
        // not exact per-day spending. Simulation still spends minerals only when
        // a ship completes; this projection spreads the remaining committed ship
        // costs across the capacity-only ETA so empire forecasts show pressure.
        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double perDayScale = static_cast<double>(shipsRemaining) / static_cast<double>(*etaDays);
        addMineralSet(totals, shipClass->buildCost, perDayScale);
    }

    return totals;
}

[[nodiscard]] std::string shipyardOrderStatusName(const ShipyardOrderStatus status) {
    switch (status) {
    case ShipyardOrderStatus::Active:
        return "Active";
    case ShipyardOrderStatus::Completed:
        return "Completed";
    }

    return "Unknown";
}

[[nodiscard]] MineralSet scaledMineralSet(const MineralSet& minerals, const int scale) noexcept {
    MineralSet result{};
    if (scale <= 0) {
        return result;
    }

    for (std::size_t i = 0; i < result.amount.size(); ++i) {
        result.amount[i] = minerals.amount[i] * static_cast<double>(scale);
    }
    return result;
}

[[nodiscard]] std::optional<Mineral> firstBlockingMineral(const MineralSet& stockpile, const MineralSet& required) noexcept {
    for (std::size_t i = 0; i < required.amount.size(); ++i) {
        if (stockpile.amount[i] + kMineralComparisonEpsilon < required.amount[i]) {
            return mineralFromIndex(i);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int> queueAwareShipyardEtaDays(const double buildPointsAhead,
                                                           const double orderBuildPointsRemaining,
                                                           const double colonyCapacity) {
    if (orderBuildPointsRemaining <= 0.0) {
        return 0;
    }

    if (colonyCapacity <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays((buildPointsAhead + orderBuildPointsRemaining) / colonyCapacity);
}

[[nodiscard]] std::string productionBacklogStatusName(const ShipyardOrder& order,
                                                      const bool blockedByMineral,
                                                      const std::string& blockingMineralName) {
    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return "Completed";
    }

    if (blockedByMineral) {
        return "Blocked: " + blockingMineralName;
    }

    return shipyardOrderStatusName(order.status);
}

[[nodiscard]] std::string productionBacklogExplanation(const int queuePosition,
                                                       const double buildPointsAhead,
                                                       const double orderBuildPointsRemaining,
                                                       const double colonyCapacity,
                                                       const std::optional<int> etaDays,
                                                       const bool blockedByMineral,
                                                       const std::string& blockingMineralName) {
    std::ostringstream out;
    out << "Queue position " << queuePosition << "; "
        << buildPointsAhead << " build points ahead + "
        << orderBuildPointsRemaining << " order build points remaining";

    if (etaDays.has_value()) {
        out << " / " << colonyCapacity << " colony capacity per day = " << *etaDays << " day(s)";
    } else {
        out << "; no positive colony shipyard capacity, so ETA cannot be estimated";
    }

    if (blockedByMineral) {
        out << "; current stockpiles are short of " << blockingMineralName;
    }

    return out.str();
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

    std::vector<double> remainingByDeposit;
    remainingByDeposit.reserve(state.mineralDeposits.size());
    for (const MineralDeposit& deposit : state.mineralDeposits) {
        remainingByDeposit.push_back(deposit.remaining);
    }

    for (const Colony& colony : state.colonies) {
        for (std::size_t depositIndex = 0; depositIndex < state.mineralDeposits.size(); ++depositIndex) {
            const MineralDeposit& deposit = state.mineralDeposits[depositIndex];
            if (deposit.bodyId != colony.bodyId) {
                continue;
            }

            // Forecast extraction must share each deposit in the same deterministic
            // colony/deposit order used by Simulation::simulateMining. Without
            // this shared running balance, multiple colonies on one body would
            // each cap against the full deposit and overstate total income.
            const double income = dailyExtraction(colony, deposit, remainingByDeposit[depositIndex]);
            remainingByDeposit[depositIndex] -= income;

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

std::vector<MineralForecastCauseChain> ForecastService::mineralForecastCauseChains() const {
    const GameState& state = service_.state();
    const MineralAmountTotals stockpiles = totalColonyStockpiles(state);
    const MineralAmountTotals miningIncome = miningIncomeByMineral(mineralIncomePerDay());
    const MineralAmountTotals shipyardDemand = activeShipyardDemandByMineral(state);

    std::vector<MineralForecastCauseChain> forecasts;
    forecasts.reserve(mineralCount());

    for (std::size_t i = 0; i < mineralCount(); ++i) {
        const Mineral mineral = mineralFromIndex(i);
        const double netPerDay = miningIncome[i] - shipyardDemand[i];

        forecasts.push_back(MineralForecastCauseChain{
            .mineral = mineral,
            .mineralName = mineralName(mineral),
            .stockpile = stockpiles[i],
            .miningIncomePerDay = miningIncome[i],
            .activeShipyardDemandPerDay = shipyardDemand[i],
            .netPerDay = netPerDay,
            .stockpileRunoutDays = stockpileRunoutDays(stockpiles[i], netPerDay),
            .causes = {
                MineralForecastCauseRow{
                    .label = "Mining",
                    .amountPerDay = miningIncome[i],
                    .explanation = miningCauseExplanation(miningIncome[i])
                },
                MineralForecastCauseRow{
                    .label = "Active shipyard orders",
                    .amountPerDay = -shipyardDemand[i],
                    .explanation = shipyardDemandCauseExplanation(shipyardDemand[i])
                }
            }
        });
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

std::vector<ProductionBacklogForecast> ForecastService::productionBacklog() const {
    const GameState& state = service_.state();

    struct ColonyQueueForecastState {
        ColonyId colonyId;
        int nextQueuePosition = 1;
        double buildPointsAhead = 0.0;
    };

    std::vector<ColonyQueueForecastState> queueStates;
    queueStates.reserve(state.colonies.size());
    for (const Colony& colony : state.colonies) {
        queueStates.push_back(ColonyQueueForecastState{
            .colonyId = colony.id,
            .nextQueuePosition = 1,
            .buildPointsAhead = 0.0
        });
    }

    std::vector<ProductionBacklogForecast> forecasts;
    forecasts.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double buildPointsRemaining = shipClass == nullptr ? 0.0 : totalBuildPointsRemaining(order, *shipClass);
        const MineralSet requiredMinerals = shipClass == nullptr
            ? MineralSet{}
            : scaledMineralSet(shipClass->buildCost, shipsRemaining);

        int queuePosition = 0;
        double buildPointsAhead = 0.0;
        double colonyCapacity = colony == nullptr ? 0.0 : std::max(0.0, colony->shipyardCapacity);
        std::optional<int> etaDays;

        if (order.status == ShipyardOrderStatus::Completed || shipsRemaining == 0) {
            etaDays = 0;
        } else if (colony != nullptr && shipClass != nullptr) {
            auto queueIt = std::find_if(queueStates.begin(), queueStates.end(), [order](const ColonyQueueForecastState& queueState) {
                return queueState.colonyId == order.colonyId;
            });

            if (queueIt != queueStates.end()) {
                queuePosition = queueIt->nextQueuePosition;
                buildPointsAhead = queueIt->buildPointsAhead;
                etaDays = queueAwareShipyardEtaDays(buildPointsAhead, buildPointsRemaining, colonyCapacity);

                // Match simulation's FIFO capacity rule: this order's remaining
                // build-point need is queued before later active orders at the
                // same colony, regardless of whether minerals later delay it.
                ++queueIt->nextQueuePosition;
                queueIt->buildPointsAhead += buildPointsRemaining;
            }
        }

        const std::optional<Mineral> blockingMineral = colony == nullptr
            ? std::nullopt
            : firstBlockingMineral(colony->stockpile, requiredMinerals);
        const bool blockedByMineral = blockingMineral.has_value()
            && order.status == ShipyardOrderStatus::Active
            && shipsRemaining > 0;
        const std::string blockerName = blockingMineral.has_value() ? mineralName(*blockingMineral) : std::string{};

        forecasts.push_back(ProductionBacklogForecast{
            .orderId = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .quantityRequested = order.quantityRequested,
            .quantityCompleted = order.quantityCompleted,
            .shipsRemaining = shipsRemaining,
            .queuePosition = queuePosition,
            .colonyShipyardCapacity = colonyCapacity,
            .accumulatedBuildPoints = order.accumulatedBuildPoints,
            .buildPointsRemaining = buildPointsRemaining,
            .requiredMineralsRemaining = requiredMinerals,
            .blockedByMineral = blockedByMineral,
            .blockingMineral = blockedByMineral ? blockingMineral : std::nullopt,
            .blockingMineralName = blockedByMineral ? blockerName : std::string{},
            .etaDays = etaDays,
            .statusName = productionBacklogStatusName(order, blockedByMineral, blockerName),
            .explanation = productionBacklogExplanation(queuePosition,
                                                        buildPointsAhead,
                                                        buildPointsRemaining,
                                                        colonyCapacity,
                                                        etaDays,
                                                        blockedByMineral,
                                                        blockerName)
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
