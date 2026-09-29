#include "sim/Commands.h"
#include "sim/FreightProgramRules.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/TransitPlanning.h"

// Independent P3B-03/09/12/13/26 edge evidence. Assertions measure dated real
// transfers and movement rather than comparing two wrappers of one preview.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(const bool value, const std::string_view message) {
    if (!value) throw std::runtime_error{std::string{message}};
}
void near(const double actual, const double expected, const std::string_view message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 1e-7, message);
}
deep::Colony& source(deep::GameState& state) { return state.colonies.at(state.colonies.size() - 2); }
const deep::Colony& source(const deep::GameState& state) { return state.colonies.at(state.colonies.size() - 2); }
deep::FreightProgramCharter charter(const deep::GameState& state, const double quantity = 500.0) {
    deep::FreightProgramCharter c;
    c.name = "Freight edge evidence";
    c.sourceColonyId = source(state).id;
    c.destinationColonyId = state.colonies.back().id;
    c.totalQuantity = quantity;
    c.requestedFleetId = state.fleets.back().id;
    c.requestedLeaderId = state.people.front().id;
    return c;
}
deep::FreightProgramId authorize(deep::Simulation& sim, const deep::FreightProgramCharter& c) {
    require(sim.execute(deep::CreateFreightProgramCommand{c}).ok, "coherent edge charter authorizes");
    return sim.state().freightPrograms.back().id;
}
const deep::FreightProgram& program(const deep::Simulation& sim) { return sim.state().freightPrograms.back(); }
void step(deep::Simulation& sim) {
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1 && !result.interrupted, "expected one uninterrupted physical day");
    deep::validateGameState(sim.state());
}
template <typename Predicate>
void until(deep::Simulation& sim, Predicate done) {
    for (int i = 0; i < 300 && !done(sim.state()); ++i) step(sim);
    require(done(sim.state()), "expected physical condition reached within finite fixture bound");
}
void fixedFiveUnitRoute(deep::GameState& state) {
    const auto target = state.colonies.back().bodyId;
    for (auto& body : state.bodies) if (body.id == target) { body.x = 5.0; body.y = 0.0; }
}

void unreadyEquipmentAndBusyFleetRemainIntent() {
    // These are valid physical designs. Removing capability must never turn
    // role labels or command admission into substitute cargo equipment.
    for (int mode = 0; mode < 5; ++mode) {
        auto state = deep::createDelegatedFreightScenario();
        auto& design = state.shipClasses.back();
        const auto removeKind = [&](const deep::ShipComponentKind kind) {
            std::erase_if(design.components, [&](const auto& install) {
                const auto component = std::find_if(state.shipComponents.begin(), state.shipComponents.end(),
                    [&](const auto& row) { return row.id == install.componentId; });
                return component != state.shipComponents.end() && component->kind == kind;
            });
        };
        if (mode == 0) removeKind(deep::ShipComponentKind::CargoBay);
        if (mode == 1) removeKind(deep::ShipComponentKind::Reactor);
        if (mode == 2) removeKind(deep::ShipComponentKind::PropellantTank);
        if (mode == 3) for (auto& component : state.shipComponents) {
            if (component.kind == deep::ShipComponentKind::CargoBay) component.cargoHandlingPerDay = 0.0;
        }
        if (mode == 4) { state.ships.back().fuel = 200.0; state.bodies.back().x = 100.0; }
        deep::Simulation sim{state};
        if (mode == 4) {
            require(sim.execute(deep::MoveFleetCommand{state.fleets.back().id, state.bodies.back().id}).ok,
                    "busy fixture has a real paid manual leg");
            require(sim.state().fleets.back().activeOrder.arrivalDay > 1, "manual leg stays busy through checked day");
        }
        const auto id = authorize(sim, charter(sim.state()));
        const auto before = sim.state();
        const int days = mode == 4 ? 1 : 7;
        const auto advanced = sim.advanceDaysDetailed(days);
        require(advanced.advancedDays == days && !advanced.interrupted && program(sim).id == id,
                "known unready condition retains original intention without clock trap");
        require(program(sim).receipts.empty() && program(sim).cargoLoaded == 0.0 && program(sim).fuelLoaded == 0.0,
                "unready fleet fabricates no cargo or operating transfer");
        near(source(sim.state()).processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys), 500.0,
             "unready operation leaves actual source material intact");
        for (std::size_t i = before.eventLog.size(); i < sim.state().eventLog.size(); ++i) {
            require(!std::holds_alternative<deep::CommandRejectedEvent>(sim.state().eventLog[i].payload),
                    "known lack of readiness produces no daily rejection spam");
        }
        if (mode == 4) {
            require(!program(sim).leasedFleetId && sim.state().fleets.back().activeOrder.arrivalDay == before.fleets.back().activeOrder.arrivalDay,
                    "freight neither steals nor rewrites the busy manual leg");
        }
    }
}

void twoConsumersUseOneRealStockBudget() {
    auto state = deep::createDelegatedFreightScenario();
    source(state).processedStockpile.set(deep::ProcessedMaterial::StructuralAlloys, 120.0);
    state.ships.back().fuel = 20.0;
    auto secondFleet = state.fleets.back();
    secondFleet.id = deep::FleetId{state.ids.nextFleetId++};
    secondFleet.name = "Second competing freighter";
    auto secondShip = state.ships.back();
    secondShip.id = deep::ShipId{state.ids.nextShipId++};
    secondShip.fleetId = secondFleet.id;
    secondFleet.shipIds = {secondShip.id};
    state.ships.push_back(secondShip);
    state.fleets.push_back(secondFleet);
    auto first = charter(state, 150.0);
    first.requestedFleetId = state.fleets.at(state.fleets.size() - 2).id;
    first.policy.sourceCargoFloor = 60.0;
    auto second = charter(state, 150.0);
    second.name = "Second source consumer";
    second.policy.sourceCargoFloor = 20.0;
    deep::Simulation sim{state};
    authorize(sim, first);
    authorize(sim, second);
    step(sim);
    // Consumer one leaves 70, respecting its floor60. Consumer two may use
    // another50 down to its own floor20: floors do not reserve stock globally.
    near(sim.state().freightPrograms.front().cargoLoaded, 50.0, "first consumer receives one hull handling day");
    near(sim.state().freightPrograms.back().cargoLoaded, 50.0, "second consumer sees only remaining real stock");
    near(source(sim.state()).processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys), 20.0,
         "shared withdrawals debit stock once and stop at second applicable floor");
    near(sim.state().ships.at(sim.state().ships.size() - 2).cargo->quantity + sim.state().ships.back().cargo->quantity +
         source(sim.state()).processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys), 120.0,
         "source and two physical holds conserve all120 units");
}

void operatingAllowanceCountsOnlyRealAdditionalFuel() {
    auto preloaded = deep::createDelegatedFreightScenario();
    fixedFiveUnitRoute(preloaded);
    preloaded.ships.back().fuel = 10.0;
    auto prepaidCharter = charter(preloaded, 25.0);
    prepaidCharter.policy.maxAdditionalPropellant = 0.0;
    deep::Simulation prepaid{preloaded};
    authorize(prepaid, prepaidCharter);
    until(prepaid, [](const auto& s) { return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed; });
    near(program(prepaid).fuelLoaded, 0.0, "already onboard10 fuel does not consume a zero additional allowance");
    near(program(prepaid).fuelBurned, 10.0, "actual two fixed5-unit legs burn10 once");

    auto partial = deep::createDelegatedFreightScenario();
    fixedFiveUnitRoute(partial);
    partial.ships.back().fuel = 3.0;
    source(partial).processedStockpile.set(deep::ProcessedMaterial::Propellant, 120.0);
    auto payload = charter(partial, 90.0);
    payload.material = deep::ProcessedMaterial::Propellant;
    payload.policy.sourceCargoFloor = 20.0;
    payload.policy.sourcePropellantFloor = 10.0;
    payload.policy.maxAdditionalPropellant = 7.0;
    deep::Simulation delivery{partial};
    authorize(delivery, payload);
    until(delivery, [](const auto& s) { return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed; });
    near(program(delivery).fuelLoaded, 7.0, "partial tanks receive exactly missing7 operating units");
    near(program(delivery).cargoDelivered, 90.0, "90 payload units are not charged against operating allowance7");
    near(source(delivery.state()).processedStockpile.get(payload.material), 23.0, "source120 pays7 engine plus90 cargo exactly once");
    near(source(delivery.state()).processedStockpile.get(payload.material) + delivery.state().colonies.back().processedStockpile.get(payload.material) +
         delivery.state().ships.back().fuel + program(delivery).fuelBurned, 123.0,
         "source120 plus initial engine3 equals final stock, tanks and actual burn");

    auto limited = deep::createDelegatedFreightScenario();
    fixedFiveUnitRoute(limited);
    auto finite = charter(limited, 400.0);
    finite.policy.maxAdditionalPropellant = 10.0;
    deep::Simulation exhausted{limited};
    const auto id = authorize(exhausted, finite);
    const auto result = exhausted.advanceDaysDetailed(200);
    require(result.interrupted && result.advancedDays > 0 && result.advancedDays < 200,
            "new operating allowance exhaustion after real progress interrupts bulk advancement");
    near(program(exhausted).cargoDelivered, 200.0, "first committed200 delivered before second-cycle cap issue");
    near(program(exhausted).fuelLoaded, 10.0, "allowance counts actual first-cycle refill");
    require(program(exhausted).issue.signature == "allowance", "stable issue names exhausted lifetime fuel authority");
    require(exhausted.execute(deep::AcknowledgeFreightProgramIssueCommand{id, program(exhausted).issue.signature}).ok,
            "cap issue can be acknowledged without changing resources");
    require(exhausted.advanceDaysDetailed(20).advancedDays == 20, "unchanged acknowledged allowance wait does not trap clock");
    near(program(exhausted).fuelLoaded, 10.0, "acknowledgment transfers no additional fuel");
    auto amendment = deep::freightAmendmentFromCharter(program(exhausted).charter);
    amendment.policy.maxAdditionalPropellant = 20.0;
    require(exhausted.execute(deep::AmendFreightProgramCommand{id, amendment}).ok, "additional real authority may resume existing commitment");
    until(exhausted, [](const auto& s) { return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed; });
    near(program(exhausted).cargoDelivered, 400.0, "same program completes second batch after policy amendment");
    near(program(exhausted).fuelLoaded, 20.0, "amendment retains prior spending instead of resetting lifetime history");
}

void movingDestinationPricesReturnAfterUnloading() {
    auto state = deep::createDelegatedFreightScenario();
    const auto sourceBody = source(state).bodyId;
    const auto destinationBody = state.colonies.back().bodyId;
    const auto railCenter = state.bodies.back().id;
    for (auto& body : state.bodies) {
        if (body.id == sourceBody) body.x = -10.0;
        if (body.id == destinationBody) {
            body.parentBodyId = railCenter;
            body.orbitalRadiusKm = 15.0 * deep::kKilometersPerMapUnit;
            body.orbitalPeriodDays = 17.0;
            body.phaseRadians = 0.5;
        }
    }
    deep::FreightProgram candidate;
    candidate.id = deep::FreightProgramId{1};
    candidate.charter = charter(state, 200.0);
    candidate.charter.policy.returnContingencyFraction = 0.25;
    const auto plan = deep::planFreightShipment(state, candidate, state.fleets.back());
    require(plan.ready && plan.unloadingDays == 4, "moving target supports independently known four-day unload");
    const auto outbound = deep::planFleetTransit(state, sourceBody, destinationBody, plan.departureDay);
    const auto returnDay = outbound.arrivalDay + 4 + 1;
    const double outboundFuel = deep::adjustedFleetMoveFuelCost(state, state.fleets.back(), sourceBody, destinationBody, plan.departureDay);
    const double returnFuel = deep::adjustedFleetMoveFuelCost(state, state.fleets.back(), destinationBody, sourceBody, returnDay);
    const double arrivalDayReturn = deep::adjustedFleetMoveFuelCost(state, state.fleets.back(), destinationBody, sourceBody, outbound.arrivalDay);
    near(plan.requiredFuel, outboundFuel + returnFuel * 1.25, "budget independently prices return after all4 unload days plus departure boundary");
    require(plan.returnDepartureDay == returnDay && std::abs(returnFuel - arrivalDayReturn) > 1e-5,
            "moving geometry distinguishes actual dated return from arrival-day shortcut");
    require(std::abs(plan.requiredFuel - 2.0 * outboundFuel) > 1e-5, "fixture distinguishes round trip from twice outward price");
    deep::Simulation sim{state};
    authorize(sim, candidate.charter);
    until(sim, [](const auto& s) { return s.freightPrograms.back().task == deep::FreightProgramTask::Outbound; });
    const auto actualArrival = sim.state().fleets.back().activeOrder.arrivalDay;
    until(sim, [](const auto& s) { return s.freightPrograms.back().task == deep::FreightProgramTask::Return; });
    require(sim.state().date.day == actualArrival + 4, "real unloading occupies four days following physical arrival");
    const double beforeBurn = program(sim).fuelBurned;
    const double actualReturnPrice = deep::adjustedFleetMoveFuelCost(sim.state(), sim.state().fleets.back(), destinationBody, sourceBody, sim.state().date.day + 1);
    step(sim);
    require(sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "empty return starts on separate following opening boundary");
    near(program(sim).fuelBurned - beforeBurn, actualReturnPrice, "return pays independently repriced current-date fuel once");
}
} // namespace

int main() {
    try {
        unreadyEquipmentAndBusyFleetRemainIntent();
        twoConsumersUseOneRealStockBudget();
        operatingAllowanceCountsOnlyRealAdditionalFuel();
        movingDestinationPricesReturnAfterUnloading();
        std::cout << "Freight edge tests: 4 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Freight edge test failure: " << error.what() << '\n';
        return 1;
    }
}
