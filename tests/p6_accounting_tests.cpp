// Reconciles subsystem boundaries from physical stock deltas, immutable
// designs/recipes and action receipts rather than calling executor forecasts.
#include "app/P6ProvingFixture.h"
#include "app/SiteDevelopmentFixture.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/ShipDesignRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/GameStateValidation.h"
#include "sim/Simulation.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double left, double right, const char* message) {
    if (!std::isfinite(left) || !std::isfinite(right) ||
        std::abs(left - right) > 1e-7 * std::max({1.0, std::abs(left), std::abs(right)}))
        throw std::runtime_error(message);
}
const ShipClass& shipClass(const GameState& state, ShipClassId id) {
    const auto row = std::find_if(state.shipClasses.begin(), state.shipClasses.end(),
                                  [&](const auto& value) { return value.id == id; });
    if (row == state.shipClasses.end())
        throw std::runtime_error("P6 order references missing class");
    return *row;
}

void ordinary_hulls_and_commissioning_fuel() {
    const auto run = earnP6EstablishedLoop();
    const auto& before = run.starting.colonies.back();
    const auto& built = run.shipsBuilt.colonies.back();
    ProcessedMaterialSet expectedHullCost;
    double expectedBuildPoints = 0;
    for (const auto& order : run.shipsBuilt.shipyardOrders) {
        const auto& cls = shipClass(run.shipsBuilt, order.shipClassId);
        const auto evaluation = evaluateShipDesign(run.shipsBuilt.shipComponents, cls.components);
        require(evaluation.constructible && order.quantityCompleted == order.quantityRequested,
                "each P6 order completes only a constructible immutable revision");
        expectedBuildPoints += evaluation.buildPoints * order.quantityCompleted;
        for (std::size_t index = 0; index < processedMaterialCount(); ++index)
            expectedHullCost.amount[index] += evaluation.buildCost.amount[index] * order.quantityCompleted;
    }
    require(expectedBuildPoints > 0 && expectedBuildPoints <= before.shipyardCapacity,
            "one-day hull output fits the actual finite yard work budget");
    for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
        const auto material = static_cast<ProcessedMaterial>(index);
        if (material != ProcessedMaterial::Propellant)
            near(before.processedStockpile.amount[index] - built.processedStockpile.amount[index],
                 expectedHullCost.amount[index],
                 "ordinary hull material debit equals evaluated immutable design cost");
    }
    double commissionedFuel = 0;
    for (const auto& ship : run.shipsBuilt.ships) {
        const auto evaluation = evaluateShipDesign(run.shipsBuilt.shipComponents,
                                                   shipClass(run.shipsBuilt, ship.shipClassId).components);
        require(ship.fuel >= 0 && ship.fuel <= evaluation.propellantCapacity,
                "commissioned tank is bounded by installed tankage");
        commissionedFuel += ship.fuel;
    }
    near(before.processedStockpile.get(ProcessedMaterial::Propellant) -
             built.processedStockpile.get(ProcessedMaterial::Propellant),
         commissionedFuel,
         "commissioning tank fill is an exact colony-to-ship transfer separate from hull cost");
}

void freight_site_and_processing() {
    const auto run = earnP6EstablishedLoop();
    const auto& state = run.mature;
    const auto& site = state.resourceSites.front();
    double netIceLoaded = 0;
    double deliveredIce = 0;
    double suppliedFuel = 0, suppliedComposites = 0;
    for (const auto& program : state.freightPrograms) {
        double aboard = 0;
        for (const auto& ship : state.ships)
            if (ship.cargo && ship.cargo->programId == program.id) {
                require(ship.cargo->commodity == program.charter.commodity,
                        "exact hull cargo commodity retains the program identity");
                aboard += ship.cargo->quantity;
            }
        near(program.cargoLoaded, program.cargoDelivered + program.cargoReturned + aboard,
             "freight loaded equals delivered plus returned plus exact onboard custody");
        double receiptLoaded = 0, receiptDelivered = 0, receiptReturned = 0;
        for (const auto& receipt : program.receipts) {
            if (receipt.kind == FreightTransferKind::Load)
                receiptLoaded += receipt.amount;
            if (receipt.kind == FreightTransferKind::Delivery)
                receiptDelivered += receipt.amount;
            if (receipt.kind == FreightTransferKind::SourceReturn)
                receiptReturned += receipt.amount;
        }
        near(program.cargoLoaded, receiptLoaded, "loaded counter matches dated physical load receipts");
        near(program.cargoDelivered, receiptDelivered,
             "delivered counter matches dated physical destination receipts");
        near(program.cargoReturned, receiptReturned,
             "returned counter matches dated physical source-return receipts");
        if (program.charter.commodity == Commodity{Mineral::WaterIce}) {
            netIceLoaded += receiptLoaded - receiptReturned;
            deliveredIce += receiptDelivered;
        }
        if (program.charter.commodity == Commodity{ProcessedMaterial::ReactorFuel})
            suppliedFuel += receiptDelivered;
        if (program.charter.commodity == Commodity{ProcessedMaterial::IndustrialComposites})
            suppliedComposites += receiptDelivered;
    }
    const BodyId target = site.bodyId;
    const auto deposit =
        std::find_if(state.mineralDeposits.begin(), state.mineralDeposits.end(), [&](const auto& row) {
            return row.bodyId == target && row.mineral == Mineral::WaterIce;
        });
    require(deposit != state.mineralDeposits.end(), "useful target has authored physical Ice");
    double recovered = 0, dutyFuel = 0, dutyComposites = 0;
    for (const auto& receipt : site.extractionReceipts)
        recovered += receipt.recoveredIce;
    for (const auto& receipt : site.dutyReceipts) {
        dutyFuel += receipt.reactorFuel;
        dutyComposites += receipt.composites;
    }
    near(100'000.0 - deposit->remaining, recovered,
         "physical hidden Ice depletion equals actual recorded recovery");
    near(recovered, site.rawStock.get(Mineral::WaterIce) + netIceLoaded,
         "recovered Ice equals current site stock plus net physical export");
    double assemblyComposites = 0;
    for (const auto& receipt : state.siteDevelopmentPrograms.front().workReceipts)
        assemblyComposites += receipt.consumed.get(ProcessedMaterial::IndustrialComposites);
    near(suppliedFuel, site.processedStock.get(ProcessedMaterial::ReactorFuel) + dutyFuel,
         "delivered site Reactor Fuel equals retained stock plus supported-duty debit");
    near(suppliedComposites,
         site.processedStock.get(ProcessedMaterial::IndustrialComposites) + assemblyComposites +
             dutyComposites,
         "delivered Composites equals assembly plus supported-duty plus retained stock");

    const auto& home = state.colonies.back();
    const auto& original = run.starting.colonies.back();
    const double grossPropellant = home.processedProductionTotals.get(ProcessedMaterial::Propellant) -
                                   original.processedProductionTotals.get(ProcessedMaterial::Propellant);
    near(deliveredIce - home.stockpile.get(Mineral::WaterIce), grossPropellant,
         "actual delivered Ice not in colony stock became gross Propellant output");
    near(original.stockpile.get(Mineral::Volatiles) - home.stockpile.get(Mineral::Volatiles),
         0.5 * grossPropellant, "normal Propellant recipe consumes half as much Volatiles as gross output");
    require(grossPropellant > 0 && netIceLoaded >= deliveredIce,
            "gross production is positive and in-transit Ice remains physical cargo");
}

void zero_yield_still_pays_supported_duty() {
    const auto state = earnSiteDevelopmentFixture(90, false);
    const auto& site = state.resourceSites.front();
    double fuel = 0, composites = 0;
    for (const auto& receipt : site.dutyReceipts) {
        fuel += receipt.reactorFuel;
        composites += receipt.composites;
    }
    require(site.extractionReceipts.size() >= 5 && fuel > 0 && composites > 0,
            "genuine zero-yield supported attempts still pay real duty inputs");
    for (const auto& receipt : site.extractionReceipts)
        near(receipt.recoveredIce, 0, "zero-yield attempt cannot credit physical site stock");
}

void first_delivery_obeys_daily_processing_order() {
    const auto run = earnP6EstablishedLoop();
    const auto& before = run.beforeFirstIceDelivery;
    const auto& after = run.firstIceDelivery;
    require(after.date.day == before.date.day + 1 && before.freightPrograms.back().cargoDelivered == 0 &&
                after.freightPrograms.back().cargoDelivered > 0,
            "checkpoint crosses first real site-to-colony Ice delivery opening");
    const double delivered = after.freightPrograms.back().cargoDelivered;
    const double gross = after.colonies.back().processedProductionTotals.get(ProcessedMaterial::Propellant) -
                         before.colonies.back().processedProductionTotals.get(ProcessedMaterial::Propellant);
    require(gross > 0, "same-day later processing can use actually unloaded opening Ice");
    near(delivered,
         gross + after.colonies.back().stockpile.get(Mineral::WaterIce) -
             before.colonies.back().stockpile.get(Mineral::WaterIce),
         "opening Ice delivery splits between current raw stock and actual later processing");
    near(before.colonies.back().stockpile.get(Mineral::Volatiles) -
             after.colonies.back().stockpile.get(Mineral::Volatiles),
         0.5 * gross, "same-day processor uses exact Volatiles recipe input");

    auto isolated = createDelegatedFreightScenario();
    auto& source = isolated.colonies.at(isolated.colonies.size() - 2);
    source.processedStockpile = {};
    source.processedStockpile.set(ProcessedMaterial::Electronics, 10);
    source.stockpile = {};
    source.stockpile.set(Mineral::WaterIce, 100);
    source.stockpile.set(Mineral::Volatiles, 50);
    source.processorCapacity = 100;
    source.processingPolicy = ProcessingPolicy::Manual;
    source.manualProcessingAllocations = {{ProcessedMaterial::Propellant, 1}};
    FreightProgramCharter charter;
    charter.name = "P6 pre-existing fuel-limited intent";
    charter.source = source.id;
    charter.destination = isolated.colonies.back().id;
    charter.operatingBaseColonyId = source.id;
    charter.commodity = ProcessedMaterial::Electronics;
    charter.totalQuantity = 10;
    charter.requestedFleetId = isolated.fleets.back().id;
    charter.requestedLeaderId = isolated.people.front().id;
    Simulation waiting(isolated);
    require(waiting.execute(CreateFreightProgramCommand{charter}).ok,
            "fuel-limited operation is valid durable intent before processing");
    const auto programId = waiting.state().freightPrograms.front().id;
    require(waiting.advanceDaysDetailed(1).advancedDays == 1,
            "later processing output does not stop the day");
    require(waiting.state().freightPrograms.front().fuelLoaded == 0 &&
                waiting.state()
                        .colonies.at(isolated.colonies.size() - 2)
                        .processedProductionTotals.get(ProcessedMaterial::Propellant) == 100,
            "new Propellant produced later today cannot fund an already passed opening");
    for (int day = 0; day < 12 && waiting.state().freightPrograms.front().fuelLoaded == 0; ++day)
        require(waiting.advanceDaysDetailed(1).advancedDays == 1,
                "existing freight intent keeps advancing toward next eligible opening");
    require(waiting.state().freightPrograms.front().id == programId &&
                waiting.state().freightPrograms.front().fuelLoaded > 0,
            "same operation takes newly produced fuel later without reauthorization");
}

void optional_technical_and_service_costs() {
    const auto state = earnTechnicalDevelopmentFixture(6.0);
    const auto& program = state.technicalDevelopmentPrograms.front();
    double work[5]{};
    ProcessedMaterialSet spent;
    for (const auto& receipt : program.receipts) {
        const auto index = static_cast<std::size_t>(receipt.stage);
        require(index < 5, "only real development stages spend work");
        work[index] += receipt.work;
        spent.addSet(receipt.consumed);
    }
    for (std::size_t index = 0; index < 5; ++index)
        near(work[index], technicalStageRequiredWork(static_cast<TechnicalDevelopmentStage>(index)),
             "each demonstrated technical stage paid its full finite work bill");
    near(spent.get(ProcessedMaterial::StructuralAlloys), 40, "local production spent exact technical Alloys");
    near(spent.get(ProcessedMaterial::Electronics), 145,
         "design prototype test process and support spent exact technical Electronics");
    near(spent.get(ProcessedMaterial::IndustrialComposites), 61,
         "design prototype test process and support spent exact technical Composites");
    require(state.technicalTestRecords.size() == 3 && state.developedComponentRevisions.size() == 1 &&
                state.prototypeIntegrationReceipts.size() == 1 &&
                state.prototypeComponentUnits.front().consumedShipId ==
                    state.prototypeIntegrationReceipts.front().shipId,
            "one tested prototype is integrated into one actual commissioned hull");
    const auto advanced = state.developedComponentRevisions.front().componentId;
    const auto component = std::find_if(state.shipComponents.begin(), state.shipComponents.end(),
                                        [&](const auto& row) { return row.id == advanced; });
    require(component != state.shipComponents.end(), "demonstrated component exists in catalog");
    near(component->buildCost.get(ProcessedMaterial::Electronics), 60,
         "prototype credit is the immutable component's embodied Electronics");
    near(component->buildPoints, 90, "prototype credit is the immutable component's embodied build work");
    double maintenanceWork = 0, maintenanceElectronics = 0;
    ProcessedMaterialSet maintenanceConsumed;
    for (const auto& provider : state.maintenancePrograms)
        for (const auto& receipt : provider.receipts) {
            maintenanceWork += receipt.teamWorkdays;
            maintenanceElectronics += receipt.consumed.get(ProcessedMaterial::Electronics);
            maintenanceConsumed.addSet(receipt.consumed);
            require(receipt.componentId == advanced && receipt.restoredDuty > 0,
                    "normal maintenance acts on worn advanced instrument identity");
        }
    require(maintenanceWork > 0 && maintenanceElectronics > 0,
            "advanced service spends actual team work and specialist parts");
    ProcessedMaterialSet serialHullDemand;
    for (const auto& ship : state.ships) {
        const auto evaluation =
            evaluateShipDesign(state.shipComponents, shipClass(state, ship.shipClassId).components);
        serialHullDemand.addSet(evaluation.buildCost);
    }
    const auto colony = std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
        return row.id == state.technicalFacilities.front().colonyId;
    });
    require(colony != state.colonies.end(), "P5 technical accounting colony exists");
    for (const auto material : {ProcessedMaterial::StructuralAlloys, ProcessedMaterial::Electronics,
                                ProcessedMaterial::IndustrialComposites}) {
        const double actual = 10'000.0 - colony->processedStockpile.get(material);
        const double expected = spent.get(material) + serialHullDemand.get(material) -
                                component->buildCost.get(material) + maintenanceConsumed.get(material);
        near(actual, expected,
             "colony stock reconciles development, serial hulls, one prototype credit and service");
    }
}

void prototype_hull_credit_is_one_physical_build_plan() {
    auto state = createHomeSystemScenario();
    const auto facility = state.technicalFacilities.front();
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& row) { return row.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(10'000);
    colony->processorCapacity = 0;
    colony->mines = 0;
    colony->shipyardCapacity = 50;
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    TechnicalDevelopmentCharter charter{"P6 embodied prototype accounting",
                                        state.technologyOpportunities.front().id,
                                        facility.colonyId,
                                        facility.id,
                                        team->id,
                                        state.people.front().id,
                                        TechnicalDevelopmentScope::DemonstratePrototype,
                                        {}};
    Simulation sim(state);
    require(sim.execute(CreateTechnicalDevelopmentCommand{charter}).ok,
            "technical evidence path authorizes with real engineering inputs");
    for (int day = 0; day < 25 && sim.state().developedComponentRevisions.empty(); ++day)
        require(sim.advanceDaysDetailed(1).advancedDays == 1,
                "prototype work and three tests are earned through time");
    require(sim.state().developedComponentRevisions.size() == 1 &&
                sim.state().componentProductionCapabilities.empty(),
            "demonstration exists while serial process remains unavailable");
    const auto componentId = sim.state().developedComponentRevisions.front().componentId;
    const auto component = std::find_if(sim.state().shipComponents.begin(), sim.state().shipComponents.end(),
                                        [&](const auto& row) { return row.id == componentId; });
    auto installs = referenceSurveyCutterComponents();
    installs.at(3).componentId = componentId;
    const auto serial = evaluateShipDesign(sim.state().shipComponents, installs);
    require(sim.execute(CreateShipClassRevisionCommand{"P6 prototype cost witness", ShipRole::Survey,
                                                       std::nullopt, installs})
                .ok,
            "immutable prototype-capable class is saved before production qualification");
    require(
        sim.execute(AssignShipyardBuildCommand{facility.colonyId, sim.state().shipClasses.back().id, 1}).ok,
        "same exact revision is ordered without a serial process");
    for (int day = 0; day < 5 && !sim.state().shipyardOrders.back().currentHullSupplyPlan; ++day)
        require(sim.advanceDaysDetailed(1).advancedDays == 1,
                "positive yard work binds the actual local prototype");
    require(sim.state().shipyardOrders.back().currentHullSupplyPlan.has_value() &&
                sim.state().shipyardOrders.back().accumulatedBuildPoints > 0,
            "prototype becomes committed only after a real positive work allocation");
    const auto plan = *sim.state().shipyardOrders.back().currentHullSupplyPlan;
    near(plan.effectiveBuildPoints, serial.buildPoints - component->buildPoints,
         "frozen prototype hull plan credits component build points once");
    for (std::size_t index = 0; index < processedMaterialCount(); ++index)
        near(plan.effectiveBuildCost.amount[index],
             serial.buildCost.amount[index] - component->buildCost.amount[index],
             "frozen prototype hull plan credits only embodied component material once");
    const auto beforeHull =
        std::find_if(sim.state().colonies.begin(), sim.state().colonies.end(), [&](const auto& row) {
            return row.id == facility.colonyId;
        })->processedStockpile;
    for (int day = 0; day < 30 && sim.state().shipyardOrders.back().quantityCompleted == 0; ++day)
        require(sim.advanceDaysDetailed(1).advancedDays == 1,
                "partially built prototype hull completes through finite yard work");
    require(sim.state().shipyardOrders.back().quantityCompleted == 1 &&
                sim.state().prototypeIntegrationReceipts.size() == 1 &&
                sim.state().prototypeComponentUnits.front().state == PrototypeComponentState::Consumed,
            "one reserved physical prototype integrates into one completed hull");
    const auto afterHull =
        std::find_if(sim.state().colonies.begin(), sim.state().colonies.end(), [&](const auto& row) {
            return row.id == facility.colonyId;
        })->processedStockpile;
    for (std::size_t index = 0; index < processedMaterialCount(); ++index)
        if (static_cast<ProcessedMaterial>(index) != ProcessedMaterial::Propellant)
            near(beforeHull.amount[index] - afterHull.amount[index], plan.effectiveBuildCost.amount[index],
                 "actual final hull debit equals frozen prototype-credit material plan");
    validateGameState(sim.state());
}
} // namespace

int main() {
    try {
        ordinary_hulls_and_commissioning_fuel();
        freight_site_and_processing();
        zero_yield_still_pays_supported_duty();
        first_delivery_obeys_daily_processing_order();
        optional_technical_and_service_costs();
        prototype_hull_credit_is_one_physical_build_plan();
        std::cout << "P6 independent resource ledgers passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 accounting proof failed: " << error.what() << '\n';
        return 1;
    }
}
