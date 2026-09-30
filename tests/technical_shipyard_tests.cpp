// Developed-component production regressions: local prototype completeness,
// frozen current-hull plans, D+1 serial processes, locality, FIFO, and no cloning.
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Key {
    ColonyId colony;
    TechnicalFacilityId facility;
    MaintenanceTeamId team;
    PersonId leader;
    TechnologyOpportunityId opportunity;
};
Key key(const GameState& state) {
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    return {state.technicalFacilities.front().colonyId, state.technicalFacilities.front().id, team->id,
            state.people.front().id, state.technologyOpportunities.front().id};
}
TechnicalDevelopmentCharter charter(const GameState& state, TechnicalDevelopmentScope scope) {
    const auto k = key(state);
    return {"Shipyard technology work", k.opportunity, k.colony, k.facility, k.team, k.leader, scope, {}};
}
GameState fixture() {
    auto state = createHomeSystemScenario();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == key(state).colony; });
    colony.processedStockpile.amount.fill(5'000.0);
    colony.processorCapacity = 0.0;
    return state;
}
void step(Simulation& simulation) {
    const auto result = simulation.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "one technical shipyard day advances");
    validateGameState(simulation.state());
}
GameState demonstrated() {
    auto state = fixture();
    Simulation simulation(state);
    require(simulation
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(state, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "demonstration accepted");
    for (int day = 0; day < 13; ++day)
        step(simulation);
    return simulation.state();
}
ShipClassId precisionClass(Simulation& simulation, int sensorQuantity = 1) {
    auto installs = referenceSurveyCutterComponents();
    installs.at(3) = {simulation.state().developedComponentRevisions.front().componentId, sensorQuantity};
    require(simulation
                .execute(CreateShipClassRevisionCommand{"Precision yard test", ShipRole::Survey, std::nullopt,
                                                        installs})
                .ok,
            "precision class saved before serial readiness");
    return simulation.state().shipClasses.back().id;
}
void locality_and_complete_plan() {
    auto state = demonstrated();
    const auto terra = key(state).colony;
    const auto mars = std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
                          return row.id != terra && row.shipyardCapacity > 0;
                      })->id;
    for (auto& colony : state.colonies) {
        colony.shipyardCapacity = colony.id == terra || colony.id == mars ? 100.0 : 0.0;
        colony.processedStockpile.amount.fill(5'000.0);
    }
    Simulation simulation(state);
    const auto single = precisionClass(simulation);
    require(simulation.execute(AssignShipyardBuildCommand{mars, single, 1}).ok,
            "remote developed-component order remains valid intent");
    step(simulation);
    require(simulation.state().shipyardOrders.back().accumulatedBuildPoints == 0.0 &&
                !simulation.state().shipyardOrders.back().currentHullSupplyPlan,
            "global demonstration does not make another colony serially capable");

    const auto doubled = precisionClass(simulation, 2);
    require(simulation.execute(AssignShipyardBuildCommand{terra, doubled, 1}).ok,
            "two-component class order accepted");
    step(simulation);
    require(simulation.state().shipyardOrders.back().accumulatedBuildPoints == 0.0 &&
                simulation.state().prototypeComponentUnits.front().state ==
                    PrototypeComponentState::Available,
            "one prototype cannot bind an incomplete two-unit hull plan");
}
void frozen_prototype_then_serial_quantity() {
    auto state = demonstrated();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == key(state).colony; });
    colony.shipyardCapacity = 50.0;
    Simulation simulation(state);
    const auto cls = precisionClass(simulation);
    require(simulation.execute(AssignShipyardBuildCommand{colony.id, cls, 2}).ok,
            "two-hull order accepted with one prototype");
    step(simulation);
    const auto orderId = simulation.state().shipyardOrders.back().id;
    require(simulation.state().shipyardOrders.back().currentHullSupplyPlan &&
                simulation.state().prototypeComponentUnits.front().state ==
                    PrototypeComponentState::ReservedForShipyard,
            "first positive work freezes the prototype plan");
    require(simulation
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(simulation.state(), TechnicalDevelopmentScope::ProductionReady)})
                .ok,
            "local process qualification begins while prototype hull is in progress");
    for (int day = 0; day < 4; ++day)
        step(simulation);
    require(simulation.state().shipyardOrders.back().currentHullSupplyPlan &&
                simulation.state()
                        .shipyardOrders.back()
                        .currentHullSupplyPlan->developedComponents.front()
                        .kind == DevelopedComponentSupplyKind::PrototypeUnit,
            "new process qualification cannot rewrite current prototype plan");
    Simulation continued(simulation.state());
    for (int day = 0; day < 12 && continued.state().shipyardOrders.back().quantityCompleted == 0; ++day)
        step(continued);
    const auto& afterFirst =
        *std::find_if(continued.state().shipyardOrders.begin(), continued.state().shipyardOrders.end(),
                      [&](const auto& row) { return row.id == orderId; });
    require(afterFirst.quantityCompleted == 1 &&
                continued.state().prototypeComponentUnits.front().state == PrototypeComponentState::Consumed,
            "process availability does not rewrite the in-progress prototype hull");
    step(continued);
    const auto& secondPlan = continued.state().shipyardOrders.back().currentHullSupplyPlan;
    require(secondPlan && secondPlan->developedComponents.front().kind ==
                              DevelopedComponentSupplyKind::SerialProduction,
            "next quantity binds the now-effective serial process");
}
void waiting_order_recovers_on_d_plus_one() {
    auto state = demonstrated();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == key(state).colony; });
    colony.shipyardCapacity = 1'000.0;
    Simulation simulation(state);
    const auto cls = precisionClass(simulation);
    require(simulation.execute(AssignShipyardBuildCommand{key(state).colony, cls, 1}).ok,
            "first experimental hull authorized");
    step(simulation);
    require(simulation.state().prototypeComponentUnits.front().state == PrototypeComponentState::Consumed,
            "first real hull consumes the only prototype");
    require(simulation.execute(AssignShipyardBuildCommand{key(state).colony, cls, 1}).ok,
            "serial-unready order accepted");
    step(simulation);
    require(simulation.state().shipyardOrders.back().accumulatedBuildPoints == 0.0,
            "order waits without prototype or local process");
    require(simulation
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(simulation.state(), TechnicalDevelopmentScope::ProductionReady)})
                .ok,
            "local production qualification authorized later");
    for (int day = 0; day < 4; ++day)
        step(simulation);
    require(simulation.state().componentProductionCapabilities.size() == 1 &&
                simulation.state().shipyardOrders.back().accumulatedBuildPoints == 0.0,
            "D qualification cannot fund the later same-day shipyard phase");
    const auto electronicsBefore =
        std::find_if(simulation.state().colonies.begin(), simulation.state().colonies.end(),
                     [&](const auto& row) { return row.id == key(state).colony; })
            ->processedStockpile.get(ProcessedMaterial::Electronics);
    step(simulation);
    require(simulation.state().shipyardOrders.back().accumulatedBuildPoints > 0.0 ||
                simulation.state().shipyardOrders.back().quantityCompleted == 1,
            "same waiting order resumes on process available day D+1");
    if (simulation.state().shipyardOrders.back().quantityCompleted == 1) {
        const auto electronicsAfter =
            std::find_if(simulation.state().colonies.begin(), simulation.state().colonies.end(),
                         [&](const auto& row) { return row.id == key(state).colony; })
                ->processedStockpile.get(ProcessedMaterial::Electronics);
        require(std::abs((electronicsBefore - electronicsAfter) - 100.0) < 1e-8,
                "serial hull pays the full developed component Electronics cost");
    }
}
void second_colony_requires_local_qualification() {
    auto state = demonstrated();
    const auto terra = key(state).colony;
    auto mars = std::find_if(state.colonies.begin(), state.colonies.end(),
                             [&](const auto& row) { return row.id != terra; });
    mars->processedStockpile.amount.fill(1'000.0);
    mars->processorCapacity = 0.0;
    const TechnicalFacilityId secondFacility{state.ids.nextTechnicalFacilityId++};
    state.technicalFacilities.push_back({secondFacility, mars->id, "Mars Instrument Process Laboratory", 1.0,
                                         TechnicalFacilityCapability::PrototypeInstrumentation});
    auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                             [&](const auto& row) { return row.id == key(state).team; });
    team->colonyId = mars->id;
    TechnicalDevelopmentCharter local{"Mars local process",
                                      key(state).opportunity,
                                      mars->id,
                                      secondFacility,
                                      team->id,
                                      key(state).leader,
                                      TechnicalDevelopmentScope::ProductionReady,
                                      {}};
    Simulation simulation(state);
    require(simulation.execute(CreateTechnicalDevelopmentCommand{local}).ok,
            "second-colony process qualification intent accepted");
    for (int day = 0; day < 4; ++day)
        step(simulation);
    const auto component = simulation.state().developedComponentRevisions.front().componentId;
    require(
        !serialProductionAvailable(simulation.state(), component, terra, simulation.state().date.day + 1) &&
            serialProductionAvailable(simulation.state(), component, mars->id,
                                      simulation.state().date.day + 1),
        "same demonstrated component becomes producible only at locally qualified colony");
}
} // namespace

int main() {
    try {
        locality_and_complete_plan();
        frozen_prototype_then_serial_quantity();
        waiting_order_recovers_on_d_plus_one();
        second_colony_requires_local_qualification();
        std::cout << "Technical shipyard: 3 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical shipyard failure: " << error.what() << '\n';
        return 1;
    }
}
