#include "app/ForecastService.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"

// Self-contained regression tests for app-layer forecast DTOs.
// These tests protect the future UI contract: panels should receive explainable
// projections from src/app instead of recalculating against raw GameState data.

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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



deep::PersonId personIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Person& person : state.people) {
        if (person.name == name) {
            return person.id;
        }
    }
    throw TestFailure{"expected person was not present in scenario"};
}

deep::BodyId bodyIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Body& body : state.bodies) {
        if (body.name == name) {
            return body.id;
        }
    }
    throw TestFailure{"expected body was not present in scenario"};
}

deep::FleetId addTestFleetAt(deep::GameState& state, const deep::BodyId bodyId) {
    // Survey forecast tests need an already-positioned fleet so they can isolate
    // confidence changes from movement and fuel mechanics. The ship/fleet pair
    // is fully valid for GameState validation.
    const deep::FleetId fleetId{state.ids.nextFleetId++};
    const deep::ShipId shipId{state.ids.nextShipId++};
    const deep::ShipClass& shipClass = state.shipClasses.front();

    state.fleets.push_back(deep::Fleet{
        .id = fleetId,
        .name = "Forecast Survey Fleet",
        .currentBodyId = bodyId,
        .destinationBodyId = std::nullopt,
        .shipIds = {shipId},
        .activeOrder = deep::FleetOrder{},
        .queuedOrders = {},
        .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(deep::Ship{
        .id = shipId,
        .shipClassId = shipClass.id,
        .name = "Forecast Survey Cutter",
        .fleetId = fleetId,
        .fuel = shipClass.fuelCapacity
    });

    return fleetId;
}

const deep::MineralForecastCauseChain& requireCauseChain(const std::vector<deep::MineralForecastCauseChain>& chains,
                                                         const deep::Mineral mineral) {
    const auto it = std::find_if(chains.begin(), chains.end(), [mineral](const deep::MineralForecastCauseChain& chain) {
        return chain.mineral == mineral;
    });
    if (it == chains.end()) {
        throw TestFailure{"expected mineral forecast cause chain was not returned"};
    }
    return *it;
}

const deep::ProcessedMaterialForecastCauseChain& requireMaterialCauseChain(
    const std::vector<deep::ProcessedMaterialForecastCauseChain>& chains,
    const deep::ProcessedMaterial material) {
    const auto it = std::find_if(chains.begin(), chains.end(), [material](const deep::ProcessedMaterialForecastCauseChain& chain) {
        return chain.material == material;
    });
    if (it == chains.end()) {
        throw TestFailure{"expected processed-material forecast cause chain was not returned"};
    }
    return *it;
}

void test_mineral_income_per_day_uses_current_mining_formula() {
    // Verifies the app forecast mirrors the prototype mining rule without UI
    // code reimplementing colony/deposit joins.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};

    const auto income = forecasts.mineralIncomePerDay();

    require(income.size() == 19, "home scenario has mature-system income forecast rows");
    require(income.front().colonyName == "Terra Directorate", "income forecast resolves colony name");
    require(income.front().bodyName == "Terra", "income forecast resolves body name");
    require(income.front().mineralName == "Iron", "income forecast exposes mineral name");
    requireNear(income.front().incomePerDay, 10.0, "iron income uses mines times accessibility");
    requireNear(income.at(1).incomePerDay, 8.0, "nickel income uses accessibility multiplier");
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
        if (deposit.bodyId == terraId && deposit.mineral == deep::Mineral::Iron) {
            deposit.remaining = 10.0;
        }
    }

    state.colonies.push_back(deep::Colony{
        .id = secondColonyId,
        .bodyId = terraId,
        .name = "Second Mining Office",
        .stockpile = deep::MineralSet{},
        .processedStockpile = deep::ProcessedMaterialSet{},
        .mines = 10.0,
        .processorCapacity = 0.0,
        .shipyardCapacity = 0.0,
        .processingPolicy = deep::ProcessingPolicy::Balanced,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = state.colonies.front().ownerInstitutionId
    });

    const deep::SimulationService service{std::move(state)};
    const deep::ForecastService forecasts{service};
    const auto income = forecasts.mineralIncomePerDay();

    double ironIncomeTotal = 0.0;
    std::optional<double> firstColonyIronIncome;
    std::optional<double> secondColonyIronIncome;
    for (const deep::MineralIncomeForecast& row : income) {
        if (row.bodyId != terraId || row.mineral != deep::Mineral::Iron) {
            continue;
        }

        ironIncomeTotal += row.incomePerDay;
        if (row.colonyId == firstColonyId) {
            firstColonyIronIncome = row.incomePerDay;
        } else if (row.colonyId == secondColonyId) {
            secondColonyIronIncome = row.incomePerDay;
        }
    }

    require(firstColonyIronIncome.has_value(), "first colony iron forecast row exists");
    require(secondColonyIronIncome.has_value(), "second colony iron forecast row exists");
    requireNear(ironIncomeTotal, 10.0, "combined income does not exceed shared deposit remaining");
    requireNear(*firstColonyIronIncome, 10.0, "first colony receives the capped remaining deposit");
    requireNear(*secondColonyIronIncome, 0.0, "later colony receives no income after deposit exhaustion");
}

void test_mineral_forecast_cause_chains_report_processing_demand() {
    // Verifies raw mineral cause chains explain the new extraction-to-processing
    // bridge: mining income is offset by raw inputs consumed by recipes.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};
    const auto chains = forecasts.mineralForecastCauseChains();
    const deep::MineralForecastCauseChain& iron = requireCauseChain(chains, deep::Mineral::Iron);

    require(chains.size() == deep::mineralCount(), "one cause chain is returned for every mineral");
    require(iron.mineralName == "Iron", "cause chain exposes mineral name");
    requireNear(iron.stockpile, 28'000.0, "cause chain sums mature-system colony raw stockpiles");
    const double expectedIronIncome = 10.0 + 3.6 + 13.3;
    requireNear(iron.miningIncomePerDay, expectedIronIncome, "cause chain includes mature-system mining income per day");
    const double expectedIronDemand = 50.0 / static_cast<double>(deep::processedMaterialCount());
    requireNear(iron.committedDemandPerDay, expectedIronDemand, "cause chain includes policy-weighted processing raw demand");
    requireNear(iron.netPerDay, expectedIronIncome - expectedIronDemand, "cause chain computes net raw mineral flow");
    require(!iron.stockpileRunoutDays.has_value(), "positive net flow has no stockpile runout");
    require(iron.confirmedDepositQuantity > 0.0, "cause chain separates confirmed deposit quantity");
    require(iron.uncertainDepositQuantity >= 0.0, "cause chain separates uncertain deposit quantity");
    require(iron.causes.size() == 4, "cause chain contains deposit certainty plus flow explanation rows");
    require(iron.causes.front().label == "Confirmed deposits", "first cause row explains confirmed reserves");
    require(iron.causes.at(1).label == "Uncertain deposits", "second cause row explains uncertain reserves");
    require(iron.causes.at(2).label == "Mining", "third cause row explains mining");
    require(iron.causes.at(3).label == "Processing recipes", "fourth cause row explains processing demand");
    requireNear(iron.causes.at(3).amountPerDay, -expectedIronDemand, "processing cause row reports demand as a negative contribution");
}

void test_processed_material_forecast_cause_chains_report_shipyard_demand() {
    // Verifies processed materials are now the shipyard-facing resource layer.
    // Active orders create processed-material demand instead of raw mineral demand.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before processed-material forecast");

    const deep::ForecastService forecasts{service};
    const auto chains = forecasts.processedMaterialForecastCauseChains();
    const deep::ProcessedMaterialForecastCauseChain& electronics =
        requireMaterialCauseChain(chains, deep::ProcessedMaterial::Electronics);

    require(chains.size() == deep::processedMaterialCount(), "one cause chain is returned for every processed material");
    require(electronics.materialName == "Electronics", "processed cause chain exposes material name");
    requireNear(electronics.stockpile, 750.0, "cause chain sums mature-system colony processed stockpiles");
    const double balancedElectronicsOutput = 50.0 / static_cast<double>(deep::processedMaterialCount());
    requireNear(electronics.processingIncomePerDay, balancedElectronicsOutput, "balanced policy assigns capacity to electronics");
    requireNear(electronics.committedDemandPerDay, 16.0, "shipyard demand is amortized over ETA");
    requireNear(electronics.netPerDay, balancedElectronicsOutput - 16.0, "processed material net flow includes shipyard demand");
    require(electronics.stockpileRunoutDays.has_value(), "negative material net flow yields runout");
    require(*electronics.stockpileRunoutDays == 98, "processed material runout rounds up by days");
    require(electronics.causes.at(1).label == "Active shipyard orders", "processed demand names shipyard orders");
}


void test_processed_material_forecast_respects_processing_policy() {
    // Verifies forecasts use the same processing policy allocation as the
    // simulation. Manual electronics focus should forecast electronics output
    // without also assigning capacity to structural alloys.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;

    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 100.0}
        }
    }).ok, "manual electronics processing policy is accepted before forecast");

    const deep::ForecastService forecasts{service};
    const auto chains = forecasts.processedMaterialForecastCauseChains();
    const auto& electronics = requireMaterialCauseChain(chains, deep::ProcessedMaterial::Electronics);
    const auto& alloys = requireMaterialCauseChain(chains, deep::ProcessedMaterial::StructuralAlloys);

    require(electronics.processingIncomePerDay > 0.0, "manual electronics policy forecasts electronics output");
    requireNear(alloys.processingIncomePerDay, 0.0, "manual electronics policy forecasts no structural alloy output");
}

void test_processed_material_forecast_normalizes_manual_weights() {
    // Verifies manual forecast allocations are relative weights rather than
    // literal percentages. The resulting material income should divide one
    // processor-capacity pool by normalized share.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;

    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::StructuralAlloys, .weight = 3.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 1.0}
        }
    }).ok, "manual weighted processing policy is accepted before forecast");

    const deep::ForecastService forecasts{service};
    const auto chains = forecasts.processedMaterialForecastCauseChains();
    const auto& alloys = requireMaterialCauseChain(chains, deep::ProcessedMaterial::StructuralAlloys);
    const auto& electronics = requireMaterialCauseChain(chains, deep::ProcessedMaterial::Electronics);

    requireNear(alloys.processingIncomePerDay, 37.5, "weight 3 forecasts 75 percent of processor output");
    requireNear(electronics.processingIncomePerDay, 12.5, "weight 1 forecasts 25 percent of processor output");
}

void test_mineral_forecast_cause_chains_include_processing_demand() {
    // Verifies raw minerals include processing demand even without active
    // shipyard orders, because processors now consume raw inputs daily.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};
    const auto chains = forecasts.mineralForecastCauseChains();
    const deep::MineralForecastCauseChain& iron = requireCauseChain(chains, deep::Mineral::Iron);

    const double balancedAlloyOutput = 50.0 / static_cast<double>(deep::processedMaterialCount());
    const double expectedIronIncome = 10.0 + 3.6 + 13.3;
    requireNear(iron.miningIncomePerDay, expectedIronIncome, "surplus forecast includes mature-system mining income");
    requireNear(iron.committedDemandPerDay, balancedAlloyOutput, "surplus forecast includes policy-weighted processing demand");
    requireNear(iron.netPerDay, expectedIronIncome - balancedAlloyOutput, "raw forecast includes processing demand");
    require(!iron.stockpileRunoutDays.has_value(), "positive raw flow has no runout day");
}

void test_deposit_forecasts_expose_confidence_and_uncertainty() {
    // Deposit forecasts distinguish confirmed reserves from survey estimates so
    // future exploration orders can explain what uncertainty they are reducing.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};
    const auto deposits = forecasts.depositExhaustionEstimates();

    const auto frontierIt = std::find_if(deposits.begin(), deposits.end(), [](const deep::DepositExhaustionForecast& row) {
        return row.bodyName == "Helios Far Survey Object" && row.mineral == deep::Mineral::Lithium;
    });

    require(frontierIt != deposits.end(), "frontier rare-earth deposit forecast exists");
    requireNear(frontierIt->confidence, 0.15, "frontier deposit preserves low confidence");
    require(frontierIt->surveyStateName == "Estimated", "low-confidence frontier deposit is marked estimated");
    requireNear(frontierIt->confirmedDeposit, frontierIt->remainingDeposit * frontierIt->confidence,
                "confirmed deposit equals confidence-weighted reserve");
    requireNear(frontierIt->uncertainDeposit, frontierIt->remainingDeposit - frontierIt->confirmedDeposit,
                "uncertain deposit is the unconfirmed reserve remainder");
}

void test_resource_survey_updates_confirmed_and_estimated_forecasts() {
    // Accepted survey commands should immediately change forecast certainty: the
    // physical reserve was already in state, but its confirmed/estimated split
    // must reflect the improved confidence.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId frontierId = bodyIdByName(state, "Helios Far Survey Object");
    const deep::FleetId fleetId = addTestFleetAt(state, frontierId);
    deep::SimulationService service{std::move(state)};

    const auto beforeRows = deep::ForecastService{service}.depositExhaustionEstimates();
    const auto beforeIt = std::find_if(beforeRows.begin(), beforeRows.end(), [](const deep::DepositExhaustionForecast& row) {
        return row.bodyName == "Helios Far Survey Object" && row.mineral == deep::Mineral::RareEarthElements;
    });
    require(beforeIt != beforeRows.end(), "frontier rare-earth forecast exists before survey");
    requireNear(beforeIt->confidence, 0.0, "rare-earth frontier deposit starts hidden");
    requireNear(beforeIt->confirmedDeposit, 0.0, "hidden deposit has no confirmed reserve before survey");
    requireNear(beforeIt->estimatedDeposit, 0.0, "hidden deposit has no displayed estimate before survey");

    require(service.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = frontierId
    }).ok, "resource survey command is accepted before forecast update");

    const auto afterRows = deep::ForecastService{service}.depositExhaustionEstimates();
    const auto afterIt = std::find_if(afterRows.begin(), afterRows.end(), [](const deep::DepositExhaustionForecast& row) {
        return row.bodyName == "Helios Far Survey Object" && row.mineral == deep::Mineral::RareEarthElements;
    });
    require(afterIt != afterRows.end(), "frontier rare-earth forecast exists after survey");
    require(afterIt->confidence >= deep::kResourceSurveyMinimumRevealedConfidence,
            "survey raises hidden deposit confidence into estimated range");
    require(afterIt->confirmedDeposit > beforeIt->confirmedDeposit,
            "survey increases confirmed reserve forecast");
    require(afterIt->estimatedDeposit > beforeIt->estimatedDeposit,
            "survey exposes estimated reserve forecast");
    require(afterIt->uncertainDeposit < afterIt->remainingDeposit,
            "survey reduces uncertain reserve share");
}

void test_deposit_exhaustion_estimate_uses_current_income_rate() {
    // Verifies deposit lifetime estimates are explainable capacity projections.
    // This catches drift if Simulation mining formulas change later.
    const deep::SimulationService service;
    const deep::ForecastService forecasts{service};

    const auto deposits = forecasts.depositExhaustionEstimates();

    require(deposits.size() == 31, "home scenario has mature-system deposit exhaustion rows");
    require(deposits.front().exhaustionDays.has_value(), "positive income yields exhaustion estimate");
    require(*deposits.front().exhaustionDays == 100000, "iron deposit exhaustion is rounded up by days");
    require(deposits.at(1).exhaustionDays.has_value(), "nickel deposit also has exhaustion estimate");
    require(*deposits.at(1).exhaustionDays == 75000, "nickel exhaustion uses accessibility-adjusted income");
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
    requireNear(orders.front().buildPointsRemaining, 780.0, "ETA accounts for appointment-modified accumulated build points");
    require(orders.front().etaDays.has_value(), "positive capacity yields shipyard ETA");
    require(*orders.front().etaDays == 8, "shipyard ETA rounds remaining build points over effective capacity");
    requireNear(orders.front().effectiveShipyardCapacity, 110.0, "shipyard ETA exposes appointment-modified capacity");
    requireNear(orders.front().shipyardModifierPercent, 10.0, "shipyard ETA exposes capped appointment modifier");
    require(!orders.front().shipyardModifierBreakdown.empty(), "shipyard ETA exposes modifier breakdown rows");
    require(orders.front().explanation.find("processed material shortages") != std::string::npos,
            "shipyard ETA explains capacity-only limitation");
}

void test_production_backlog_uses_fifo_colony_capacity() {
    // Verifies backlog ETAs account for the single shared colony capacity pool.
    // The second active order waits behind the first instead of receiving a
    // duplicate full allocation in the same forecast window.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "first build order is accepted before backlog forecast");
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "second build order is accepted before backlog forecast");

    const deep::ForecastService forecasts{service};
    const auto backlog = forecasts.productionBacklog();

    require(backlog.size() == 2, "two backlog forecast rows are returned");
    require(backlog.front().queuePosition == 1, "first order is first in colony queue");
    require(backlog.at(1).queuePosition == 2, "second order is second in colony queue");
    requireNear(backlog.front().buildPointsRemaining, 500.0, "first order has one ship of BP remaining");
    requireNear(backlog.at(1).buildPointsRemaining, 500.0, "second order has one ship of BP remaining");
    require(backlog.front().etaDays.has_value(), "first order has capacity ETA");
    require(backlog.at(1).etaDays.has_value(), "second order has queue-aware ETA");
    require(*backlog.front().etaDays == 5, "first order ETA uses direct colony capacity");
    require(*backlog.at(1).etaDays == 10, "second order ETA includes first order backlog ahead");
    require(backlog.front().statusName == "Building", "unblocked active order reports building status");
    require(backlog.at(1).explanation.find("build points ahead") != std::string::npos,
            "backlog explanation exposes queue capacity math");
}

void test_production_backlog_reports_blocking_material() {
    // Verifies the backlog forecast identifies the first processed material
    // preventing an order from completing with current stockpiles. This is an
    // app-layer explanation only; it does not change simulation production rules.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().processedStockpile.set(deep::ProcessedMaterial::StructuralAlloys, 100.0);

    deep::SimulationService service{std::move(state)};
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before blocker forecast");

    const deep::ForecastService forecasts{service};
    const auto backlog = forecasts.productionBacklog();

    require(backlog.size() == 1, "one blocked backlog row is returned");
    require(backlog.front().blockedByMaterial, "backlog row marks processed-material blocker");
    require(backlog.front().blockingMaterial.has_value(), "blocking material enum is set");
    require(*backlog.front().blockingMaterial == deep::ProcessedMaterial::StructuralAlloys,
            "structural alloys are the blocking material");
    require(backlog.front().blockingMaterialName == "Structural Alloys", "blocking material name is display-ready");
    requireNear(backlog.front().requiredMaterialsRemaining.get(deep::ProcessedMaterial::StructuralAlloys), 250.0,
                "required remaining materials include one Survey Cutter structural alloy cost");
    require(backlog.front().statusName == "Waiting for materials", "status reports material wait state");
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
    require(*fleets.front().etaDays == service.state().fleets.front().activeOrder.daysRemaining,
            "fleet ETA uses active order days remaining");
    require(!fleets.front().explanation.empty(), "fleet ETA includes explanation text");
}


void test_fleet_fuel_forecast_reports_range_after_move_start() {
    // Verifies the app forecast layer exposes propellant state for UI panels.
    // Movement consumes fuel at order start, so the forecast should show the
    // reduced range immediately after the command is accepted.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before fleet fuel setup");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before fleet fuel forecast");

    const deep::ForecastService forecasts{service};
    const auto fuels = forecasts.fleetFuelForecasts();

    require(fuels.size() == 1, "one fleet fuel forecast is returned");
    requireNear(fuels.front().fuelCapacity, 1000.0, "fuel forecast includes fleet capacity");
    const double expectedFuel = 1000.0 - service.state().fleets.front().activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit;
    requireNear(fuels.front().currentFuel, expectedFuel, "fuel forecast includes movement-start consumption");
    requireNear(fuels.front().currentRange, expectedFuel, "fuel forecast maps current fuel to v1 range");
    require(!fuels.front().explanation.empty(), "fuel forecast includes explanation text");
}


void test_fleet_fuel_forecast_exposes_commander_modifier() {
    // Forecasts should explain appointment-driven fuel efficiency using the same
    // capped modifier that movement consumes in the simulation layer.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;
    const deep::PersonId commanderId = personIdByName(service.state(), "Commodore Elias Voss");

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before commander fuel forecast setup");
    static_cast<void>(service.advanceDays(5));

    const deep::FleetId fleetId = service.state().fleets.front().id;
    require(service.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::FleetCommander,
        .scopeType = deep::AppointmentScopeType::Fleet,
        .scopeId = fleetId.value,
        .personId = commanderId
    }).ok, "fleet commander appointment is accepted before fuel forecast");
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "commander-modified move is accepted before fuel forecast");

    const deep::ForecastService forecasts{service};
    const auto fuels = forecasts.fleetFuelForecasts();

    const double expectedFuel = 1000.0 - (service.state().fleets.front().activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit * 0.9);
    requireNear(fuels.front().currentFuel, expectedFuel, "forecast reflects commander-reduced movement fuel cost");
    requireNear(fuels.front().fuelEfficiencyModifierPercent, 10.0, "forecast exposes capped commander fuel modifier");
    requireNear(fuels.front().currentRange, expectedFuel / 0.9, "forecast range uses effective fuel cost multiplier");
    require(!fuels.front().fuelModifierBreakdown.empty(), "forecast exposes commander fuel modifier breakdown");
}

} // namespace

int main() {
    try {
        test_mineral_income_per_day_uses_current_mining_formula();
        test_mineral_income_per_day_shares_deposits_between_colonies();
        test_mineral_forecast_cause_chains_report_processing_demand();
        test_processed_material_forecast_cause_chains_report_shipyard_demand();
        test_processed_material_forecast_respects_processing_policy();
        test_processed_material_forecast_normalizes_manual_weights();
        test_mineral_forecast_cause_chains_include_processing_demand();
        test_deposit_forecasts_expose_confidence_and_uncertainty();
        test_resource_survey_updates_confirmed_and_estimated_forecasts();
        test_deposit_exhaustion_estimate_uses_current_income_rate();
        test_shipyard_order_eta_uses_capacity_and_accumulated_progress();
        test_production_backlog_uses_fifo_colony_capacity();
        test_production_backlog_reports_blocking_material();
        test_fleet_arrival_eta_reports_active_move_order();
        test_fleet_fuel_forecast_reports_range_after_move_start();
        test_fleet_fuel_forecast_exposes_commander_modifier();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal forecast tests passed.\n";
    return EXIT_SUCCESS;
}
