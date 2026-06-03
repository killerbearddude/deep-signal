#include "app/ForecastService.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"

// Self-contained regression tests for app-layer forecast DTOs.
// These tests protect the future UI contract: panels should receive explainable
// projections from src/app instead of recalculating against raw GameState data.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void requireNear(const double actual, const double expected, const std::string_view message) {
    constexpr double kTolerance = 1.0e-9;
    const double delta = actual > expected ? actual - expected : expected - actual;
    if (delta > kTolerance) {
        throw TestFailure{message};
    }
}

void test_mineral_income_per_day_uses_current_mining_formula() {
    // Verifies the app forecast mirrors the prototype mining rule without UI
    // code reimplementing colony/deposit joins.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};

    const auto income = forecasts.mineralIncomePerDay();

    require(income.size() == 2, "home scenario has two income forecast rows");
    require(income.front().colonyName == "Terra Directorate", "income forecast resolves colony name");
    require(income.front().bodyName == "Terra", "income forecast resolves body name");
    require(income.front().mineralName == "Structural", "income forecast exposes mineral name");
    requireNear(income.front().incomePerDay, 10.0, "structural income uses mines times accessibility");
    requireNear(income.at(1).incomePerDay, 4.5, "propulsion income uses accessibility multiplier");
    require(!income.front().explanation.empty(), "income forecast includes explanation text");
}

void test_mineral_income_per_day_shares_deposits_between_colonies() {
    // Verifies per-colony income rows use the same shared-deposit rule as the
    // simulation tick. This prevents UI forecasts from overstating income when
    // multiple colonies mine one body and an early colony exhausts the deposit.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = state.bodies.front().id;
    const deep::ColonyId firstColonyId = state.colonies.front().id;
    const deep::ColonyId secondColonyId{state.ids.nextColonyId++};

    for (deep::MineralDeposit& deposit : state.mineralDeposits) {
        if (deposit.bodyId == terraId && deposit.mineral == deep::Mineral::Structural) {
            deposit.remaining = 10.0;
        }
    }

    state.colonies.push_back(deep::Colony{
        .id = secondColonyId,
        .bodyId = terraId,
        .name = "Second Mining Office",
        .stockpile = deep::MineralSet{},
        .mines = 10.0,
        .shipyardCapacity = 0.0
    });

    const deep::SimulationService service{std::move(state)};
    const deep::ForecastService forecasts{service};
    const auto income = forecasts.mineralIncomePerDay();

    double structuralIncomeTotal = 0.0;
    std::optional<double> firstColonyStructuralIncome;
    std::optional<double> secondColonyStructuralIncome;
    for (const deep::MineralIncomeForecast& row : income) {
        if (row.bodyId != terraId || row.mineral != deep::Mineral::Structural) {
            continue;
        }

        structuralIncomeTotal += row.incomePerDay;
        if (row.colonyId == firstColonyId) {
            firstColonyStructuralIncome = row.incomePerDay;
        } else if (row.colonyId == secondColonyId) {
            secondColonyStructuralIncome = row.incomePerDay;
        }
    }

    require(firstColonyStructuralIncome.has_value(), "first colony structural forecast row exists");
    require(secondColonyStructuralIncome.has_value(), "second colony structural forecast row exists");
    requireNear(structuralIncomeTotal, 10.0, "combined income does not exceed shared deposit remaining");
    requireNear(*firstColonyStructuralIncome, 10.0, "first colony receives the capped remaining deposit");
    requireNear(*secondColonyStructuralIncome, 0.0, "later colony receives no income after deposit exhaustion");
}

void test_deposit_exhaustion_estimate_uses_current_income_rate() {
    // Verifies deposit lifetime estimates are explainable capacity projections.
    // This catches drift if Simulation mining formulas change later.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};

    const auto deposits = forecasts.depositExhaustionEstimates();

    require(deposits.size() == 2, "home scenario has two deposit exhaustion rows");
    require(deposits.front().exhaustionDays.has_value(), "positive income yields exhaustion estimate");
    require(*deposits.front().exhaustionDays == 100000, "structural deposit exhaustion is rounded up by days");
    require(deposits.at(1).exhaustionDays.has_value(), "propulsion deposit also has exhaustion estimate");
    require(*deposits.at(1).exhaustionDays == 55556, "propulsion exhaustion uses accessibility-adjusted income");
    require(!deposits.front().explanation.empty(), "deposit exhaustion forecast includes explanation text");
}

void test_shipyard_order_eta_uses_capacity_and_accumulated_progress() {
    // Verifies active production orders expose capacity-only ETA and progress.
    // Future UI can display this without duplicating shipyard completion math.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 2
    }).ok, "build order is accepted before ETA forecast");

    static_cast<void>(service.advanceDays(2));

    const deep::ForecastService forecasts{service};
    const auto orders = forecasts.shipyardOrderEtas();

    require(orders.size() == 1, "one shipyard ETA forecast is returned");
    require(orders.front().colonyName == "Terra Directorate", "shipyard ETA resolves colony name");
    require(orders.front().shipClassName == "Survey Cutter", "shipyard ETA resolves ship-class name");
    require(orders.front().shipsRemaining == 2, "order still has two ships remaining after two days");
    requireNear(orders.front().buildPointsRemaining, 800.0, "ETA accounts for accumulated build points");
    require(orders.front().etaDays.has_value(), "positive capacity yields shipyard ETA");
    require(*orders.front().etaDays == 8, "shipyard ETA rounds remaining build points over capacity");
    require(orders.front().explanation.find("mineral shortages") != std::string::npos,
            "shipyard ETA explains capacity-only limitation");
}

void test_fleet_arrival_eta_reports_active_move_order() {
    // Verifies fleet forecasts expose movement arrival timing and destination
    // names without future map panels reading raw Fleet records directly.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before fleet ETA setup");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;

    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before fleet ETA forecast");

    const deep::ForecastService forecasts{service};
    const auto fleets = forecasts.fleetArrivalEtas();

    require(fleets.size() == 1, "one fleet ETA forecast is returned");
    require(fleets.front().fleetName.find("Survey Cutter Fleet") != std::string::npos,
            "fleet ETA includes fleet name");
    require(fleets.front().currentBodyName == "Terra", "fleet ETA resolves current body name");
    require(fleets.front().destinationBodyName == "Mars", "fleet ETA resolves destination body name");
    require(fleets.front().etaDays.has_value(), "active movement yields arrival ETA");
    require(*fleets.front().etaDays == 5, "fleet ETA uses active order days remaining");
    require(!fleets.front().explanation.empty(), "fleet ETA includes explanation text");
}

} // namespace

int main() {
    try {
        test_mineral_income_per_day_uses_current_mining_formula();
        test_mineral_income_per_day_shares_deposits_between_colonies();
        test_deposit_exhaustion_estimate_uses_current_income_rate();
        test_shipyard_order_eta_uses_capacity_and_accumulated_progress();
        test_fleet_arrival_eta_reports_active_move_order();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal forecast tests passed.\n";
    return EXIT_SUCCESS;
}
