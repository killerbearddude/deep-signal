#include "sim/Commands.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

// Focused P2 proof: data-only drafts, immutable revisions, physical build
// demand, real commissioning fuel, and component-driven survey eligibility.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}

void near(double actual, double expected, const std::string& message) {
    require(std::abs(actual - expected) < 1.0e-8, message);
}

void testReferenceAndDraftEvaluation() {
    deep::GameState state = deep::createHomeSystemScenario();
    const auto original = state.shipClasses.front().components;
    const auto reference = deep::evaluateShipDesign(state.shipComponents, original);
    require(reference.constructible, "reference composition is constructible");
    near(reference.dryMass, 500.0, "reference mass derives from five components");
    near(reference.usedVolume, 350.0, "reference used volume derives from components");
    near(reference.availableVolume, 650.0, "reference available volume derives from hull");
    near(reference.powerGeneration, 120.0, "reference power generation derives from reactor");
    near(reference.powerDemand, 60.0, "reference power demand derives from systems and sensor");
    near(reference.propellantCapacity, 1000.0, "reference tankage derives from tank");
    near(reference.surveyCapability, 1.0, "reference survey capability derives from sensor");
    near(reference.buildPoints, 500.0, "reference BP derives from components");
    near(reference.buildCost.get(deep::ProcessedMaterial::StructuralAlloys), 250.0,
         "reference alloy demand derives from components");
    near(reference.buildCost.get(deep::ProcessedMaterial::Propellant), 0.0,
         "tank filling is not hull construction cost");

    auto draft = original;
    draft.at(2).quantity = 2;
    const auto extraTank = deep::evaluateShipDesign(state.shipComponents, draft);
    near(extraTank.dryMass, 550.0, "extra tank adds mass");
    near(extraTank.usedVolume, 450.0, "extra tank uses volume");
    near(extraTank.propellantCapacity, 2000.0, "extra tank adds tankage");
    near(extraTank.buildPoints, 580.0, "extra tank adds BP");
    near(extraTank.buildCost.get(deep::ProcessedMaterial::StructuralAlloys), 280.0,
         "extra tank adds material cost");
    require(extraTank.buildPoints == deep::evaluateShipDesign(state.shipComponents, draft).buildPoints,
            "draft evaluation is deterministic");
    require(state.shipClasses.front().components == original,
            "draft evaluation does not mutate the authoritative class");

    draft = original;
    draft.erase(draft.begin() + 3);
    require(deep::evaluateShipDesign(state.shipComponents, draft).surveyCapability == 0.0,
            "removing sensor removes survey capability");
    draft = original;
    draft.erase(draft.begin() + 2);
    const auto noTank = deep::evaluateShipDesign(state.shipComponents, draft);
    require(noTank.constructible && noTank.propellantCapacity == 0.0,
            "zero tankage is a valid construction choice");
    draft = original;
    draft.erase(draft.begin() + 1);
    const auto powerless = deep::evaluateShipDesign(state.shipComponents, draft);
    require(powerless.constructible && powerless.powerMargin < 0.0 && powerless.surveyCapability > 0.0,
            "power deficit is a valid design with unavailable powered equipment");
    draft = original;
    draft.at(2).quantity = 8;
    const auto overflow = deep::evaluateShipDesign(state.shipComponents, draft);
    require(!overflow.constructible && overflow.usedVolume > overflow.volumeCapacity &&
            !overflow.constraints.empty(), "volume overflow has an explicit physical blocker");

    state.shipComponents.front().mass = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try { deep::validateGameState(state); } catch (const std::exception&) { rejected = true; }
    require(rejected, "malformed numeric component data is rejected");
    state.shipComponents.front().mass = 300.0;
    state.shipComponents.front().kind = static_cast<deep::ShipComponentKind>(99);
    rejected = false;
    try { deep::validateGameState(state); } catch (const std::exception&) { rejected = true; }
    require(rejected, "unknown component kind is rejected");
}

void testRevisionBindingAndDesignBlocker() {
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().processorCapacity = 0.0;
    deep::Simulation sim{std::move(state)};
    const auto colonyId = sim.state().colonies.front().id;
    const deep::ShipClass original = sim.state().shipClasses.front();
    require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, original.id, 1}).ok,
            "original revision can be ordered");
    sim.advanceDays(5);
    require(sim.state().ships.size() == 1, "original revision completes");
    require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, original.id, 1}).ok,
            "second original order is accepted");

    auto extraTank = original.components;
    extraTank.at(2).quantity = 2;
    const auto beforeId = sim.state().ids.nextShipClassId;
    require(sim.execute(deep::CreateShipClassRevisionCommand{
        .name = "Survey Cutter Long Range", .role = deep::ShipRole::Escort,
        .basedOnClassId = original.id, .components = extraTank
    }).ok, "new immutable revision is accepted");
    const auto& revised = sim.state().shipClasses.back();
    require(revised.id.value == beforeId && revised.revision == 2 &&
            revised.basedOnClassId == original.id, "new revision has typed ID and deterministic lineage");
    require(sim.state().shipClasses.front().id == original.id &&
            sim.state().shipClasses.front().name == original.name &&
            sim.state().shipClasses.front().revision == original.revision &&
            sim.state().shipClasses.front().components == original.components &&
            sim.state().shipClasses.front().role == original.role &&
            sim.state().shipClasses.front().basedOnClassId == original.basedOnClassId &&
            sim.state().shipClasses.front().speedKmPerDay == original.speedKmPerDay,
            "source revision remains field-equivalent");
    require(sim.state().ships.front().shipClassId == original.id &&
            sim.state().shipyardOrders.at(1).shipClassId == original.id,
            "completed ship and existing order remain bound to old revision");
    const auto* event = std::get_if<deep::ShipClassRevisionCreatedEvent>(&sim.state().eventLog.back().payload);
    require(event != nullptr && event->shipClassId == revised.id && event->revision == 2 &&
            event->basedOnClassId == original.id, "revision creation has exact audit identity");

    const auto classCount = sim.state().shipClasses.size();
    const auto nextId = sim.state().ids.nextShipClassId;
    for (const auto& invalid : {
             std::vector<deep::ShipComponentInstall>{{deep::ShipComponentId{999}, 1}},
             std::vector<deep::ShipComponentInstall>{{deep::ShipComponentId{1}, 0}},
             std::vector<deep::ShipComponentInstall>{{deep::ShipComponentId{1}, 1}, {deep::ShipComponentId{1}, 1}}
         }) {
        require(!sim.execute(deep::CreateShipClassRevisionCommand{
            .name = "Invalid", .role = deep::ShipRole::Survey,
            .basedOnClassId = original.id, .components = invalid
        }).ok, "invalid or duplicate installations reject");
        require(sim.state().shipClasses.size() == classCount && sim.state().ids.nextShipClassId == nextId,
                "rejected revision does not partially allocate a class");
    }

    sim.advanceDays(5);
    require(sim.state().shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Completed,
            "old revision order completes before isolated overflow proof");

    const auto revisedId = sim.state().shipClasses.back().id;
    const auto alloysBefore = sim.state().colonies.front().processedStockpile.get(
        deep::ProcessedMaterial::StructuralAlloys);
    const auto shipCountBefore = sim.state().ships.size();
    require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, revisedId, 1}).ok,
            "revised class can be ordered by exact ID");
    sim.advanceDays(5);
    require(sim.state().ships.size() == shipCountBefore &&
            sim.state().shipyardOrders.back().status == deep::ShipyardOrderStatus::Active,
            "extra tank's derived 580 BP cannot complete in five 110-BP days");
    near(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys),
         alloysBefore, "materials are not paid before revised hull completes");
    sim.advanceDays(1);
    require(sim.state().ships.size() == shipCountBefore + 1 &&
            sim.state().ships.back().shipClassId == revisedId,
            "derived BP completes the exact revision on day six");
    near(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys),
         alloysBefore - 280.0, "revised component cost is paid once per hull");

    auto overflow = original.components;
    overflow.at(2).quantity = 8;
    require(sim.execute(deep::CreateShipClassRevisionCommand{
        .name = "Overflow", .role = deep::ShipRole::Survey,
        .basedOnClassId = original.id, .components = overflow
    }).ok, "overflow revision may be saved");
    const auto overflowId = sim.state().shipClasses.back().id;
    require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, overflowId, 1}).ok,
            "overflow revision may be ordered as intent");
    const auto materials = sim.state().colonies.front().processedStockpile;
    const auto ships = sim.state().ships.size();
    sim.advanceDays(30);
    const auto& order = sim.state().shipyardOrders.back();
    require(order.shipClassId == overflowId && order.accumulatedBuildPoints == 0.0 &&
            order.quantityCompleted == 0 && order.status == deep::ShipyardOrderStatus::Active &&
            sim.state().ships.size() == ships, "non-constructible revision makes zero physical progress");
    for (std::size_t i = 0; i < deep::processedMaterialCount(); ++i) {
        near(sim.state().colonies.front().processedStockpile.amount[i], materials.amount[i],
             "non-constructible order consumes no construction materials");
    }
}

void testCommissioningUsesColonyPropellant() {
    for (const double available : {1500.0, 350.0, 0.0}) {
        deep::GameState state = deep::createHomeSystemScenario();
        state.colonies.front().processorCapacity = 0.0;
        state.colonies.front().processedStockpile.set(deep::ProcessedMaterial::Propellant, available);
        deep::Simulation sim{std::move(state)};
        const auto colonyId = sim.state().colonies.front().id;
        const auto classId = sim.state().shipClasses.front().id;
        require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, classId, 1}).ok,
                "hull order accepts any commissioning propellant level");
        sim.advanceDays(5);
        require(sim.state().ships.size() == 1, "fuel shortage does not block hull completion");
        const double transfer = std::min(1000.0, available);
        near(sim.state().ships.front().fuel, transfer, "ship receives only real transferred propellant");
        near(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Propellant),
             available - transfer, "colony is debited exactly one tank transfer");
    }

    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().processorCapacity = 0.0;
    deep::Simulation sim{std::move(state)};
    auto tankless = sim.state().shipClasses.front().components;
    tankless.erase(tankless.begin() + 2);
    const auto baseId = sim.state().shipClasses.front().id;
    require(sim.execute(deep::CreateShipClassRevisionCommand{
        .name = "Tankless", .role = deep::ShipRole::Survey,
        .basedOnClassId = baseId, .components = tankless
    }).ok, "tankless design revision is valid");
    const auto colonyId = sim.state().colonies.front().id;
    const auto classId = sim.state().shipClasses.back().id;
    const double colonyFuel = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Propellant);
    require(sim.execute(deep::AssignShipyardBuildCommand{colonyId, classId, 1}).ok,
            "tankless design may be ordered");
    sim.advanceDays(4);
    require(sim.state().ships.size() == 1 && sim.state().ships.front().fuel == 0.0,
            "tankless hull commissions with empty fuel state");
    near(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Propellant),
         colonyFuel, "tankless hull transfers no propellant");
}

void testSurveyUsesInstalledPoweredEquipment() {
    for (const int mode : {0, 1, 2}) {
        deep::GameState state = deep::createHomeSystemScenario();
        auto components = state.shipClasses.front().components;
        if (mode == 0) components.erase(components.begin() + 3); // Survey role, no sensor.
        if (mode == 2) components.erase(components.begin() + 1); // Sensor, power deficit.
        state.shipClasses.front().components = components;
        if (mode == 1) state.shipClasses.front().role = deep::ShipRole::Escort;
        const auto target = std::find_if(state.mineralDeposits.begin(), state.mineralDeposits.end(),
            [](const deep::MineralDeposit& deposit) { return !deep::isDepositKnown(deposit); });
        require(target != state.mineralDeposits.end(), "scenario provides low-confidence target");
        const deep::ShipId shipId{state.ids.nextShipId++};
        const deep::FleetId fleetId{state.ids.nextFleetId++};
        state.ships.push_back(deep::Ship{.id = shipId, .shipClassId = state.shipClasses.front().id,
            .name = "Survey proof", .fleetId = fleetId, .fuel = 0.0});
        state.fleets.push_back(deep::Fleet{.id = fleetId, .name = "Survey proof fleet",
            .currentBodyId = target->bodyId, .destinationBodyId = std::nullopt,
            .shipIds = {shipId}, .activeOrder = {}, .queuedOrders = {},
            .ownerInstitutionId = std::nullopt});
        deep::Simulation sim{std::move(state)};
        const auto result = sim.execute(deep::ResourceSurveyCommand{fleetId, target->bodyId});
        require(result.ok == (mode == 1), "survey permission follows powered sensor, not role");
        if (mode == 0) require(result.message.find("no installed") != std::string::npos,
                               "missing sensor reason is explicit");
        if (mode == 2) require(result.message.find("power deficit") != std::string::npos,
                               "power deficit reason is explicit");
    }
}

} // namespace

int main() {
    try {
        testReferenceAndDraftEvaluation();
        testRevisionBindingAndDesignBlocker();
        testCommissioningUsesColonyPropellant();
        testSurveyUsesInstalledPoweredEquipment();
        std::cout << "Ship design tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ship design test failed: " << error.what() << '\n';
        return 1;
    }
}
