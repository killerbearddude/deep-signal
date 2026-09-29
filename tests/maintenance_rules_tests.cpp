// Independent condition/workshop arithmetic and manual-action admission. These
// fixtures use literal duty/material totals, not an executor compared to itself.
#include "sim/EquipmentServiceRules.h"
#include "sim/GameStateValidation.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) { require(std::isfinite(a) && std::abs(a - b) < 1e-8, why); }
template <class Action> void rejects(Action action, const char* why) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, why);
}
void nominal_and_independent_hulls() {
    auto s = createTenderMaintenanceScenario();
    const auto composition = s.shipClasses.front().components;
    const auto design = evaluateShipDesign(s.shipComponents, composition);
    near(design.dryMass, 500, "nominal cutter mass unchanged");
    near(design.buildPoints, 500, "nominal cutter BP unchanged");
    near(design.propellantCapacity, 1000, "nominal cutter tank unchanged");
    near(design.surveyCapability, 1, "nominal cutter sensor unchanged");
    const auto tender = evaluateShipDesign(s.shipComponents, s.shipClasses.back().components);
    near(tender.dryMass, 570, "reference tender mass is independently570");
    near(tender.usedVolume, 470, "reference tender volume470");
    near(tender.powerGeneration, 120, "reference tender reactor120");
    near(tender.powerDemand, 50, "reference tender systems20 and workshop30");
    near(tender.buildPoints, 530, "reference tender work530");
    near(tender.buildCost.get(ProcessedMaterial::Electronics), 60, "reference tender electronics60");
    Ship clone = s.ships.front();
    clone.id = ShipId{s.ids.nextShipId++};
    clone.name = "Independent second cutter";
    s.ships.push_back(clone);
    s.fleets.front().shipIds.push_back(clone.id);
    s.ships.front().equipmentCondition.front().usedDuty = 10;
    const auto action = prepareSurveyDuty(s, s.fleets.front(), 1);
    near(action.nominalCapability, 2, "nominal fleet retains both arrays");
    near(action.usableCapability, 1, "only healthy instrument contributes");
    applySurveyDuty(s, action);
    near(s.ships.front().equipmentCondition.front().usedDuty, 10, "exhausted hull untouched");
    near(s.ships.back().equipmentCondition.front().usedDuty, 1, "second hull independently consumes duty");
    require(s.shipClasses.front().components == composition, "condition cannot mutate immutable revision");
    near(s.ships.front().fuel, 1000, "condition does not consume engine fuel");
    validateGameState(s);
}
void manual_duty_and_initialization() {
    auto s = createTenderMaintenanceScenario();
    s.ships.front().equipmentCondition.front().usedDuty = 3;
    Simulation sim(s);
    const auto fleet = s.fleets.front().id;
    const auto body = s.fleets.front().currentBodyId;
    require(sim.execute(ResourceSurveyCommand{fleet, body}).ok,
            "seven remaining duty supports one immediate five-duty pass");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 8,
         "manual pass consumes exactlyfive");
    require(!sim.execute(ResourceSurveyCommand{fleet, body}).ok,
            "two remaining duty cannot bypass manual five-duty contract");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 8,
         "rejected manual action consumes nothing");
    auto bad = s;
    bad.ships.front().equipmentCondition.clear();
    rejects([&] { validateGameState(bad); }, "missing condition rejected, not initialized");
    bad = s;
    bad.ships.front().equipmentCondition.push_back(bad.ships.front().equipmentCondition.front());
    rejects([&] { validateGameState(bad); }, "duplicate condition rejected");
    bad = s;
    bad.ships.front().equipmentCondition.front().usedDuty = 11;
    rejects([&] { validateGameState(bad); }, "over-capacity duty rejected");
    rejects([&] { initializeShipEquipmentCondition(s, s.ships.front()); },
            "initializer cannot heal an existing hull");
    auto home = createHomeSystemScenario();
    home.colonies.front().processorCapacity = 0;
    const double electronics = home.colonies.front().processedStockpile.get(ProcessedMaterial::Electronics);
    Simulation build(home);
    require(
        build.execute(AssignShipyardBuildCommand{home.colonies.front().id, home.shipClasses.front().id, 1})
            .ok,
        "exact revision order accepted");
    build.advanceDays(5);
    require(build.state().ships.size() == 1, "reference hull commissions");
    near(build.state().ships.front().equipmentCondition.front().usedDuty, 0,
         "new hull initializes healthy without servicing");
    near(build.state().colonies.front().processedStockpile.get(ProcessedMaterial::Electronics),
         electronics - 80, "commission pays construction only, no repair recipe");
}
void quantity_families_and_power() {
    auto s = createTenderMaintenanceScenario();
    s.shipClasses.front().components.at(3).quantity = 2;
    s.ships.front().equipmentCondition.front().usedDuty = 10;
    ProcessedMaterialSet supply;
    supply.set(ProcessedMaterial::Electronics, 3);
    supply.set(ProcessedMaterial::IndustrialComposites, 10);
    const auto work = planEquipmentService(s, s.ships.front().id, ShipComponentId{4}, 1, 1, supply);
    require(work.ready, "quantity-scaled service is possible");
    near(work.restoredDuty, 2.5, "N2 labor0.2 limits restoration to2.5");
    near(work.teamWorkdays, 1, "one engineer day spent");
    near(work.consumed.get(ProcessedMaterial::Electronics), 2.5, "N2 actual recipe material2.5");
    near(work.afterUsedDuty, 7.5, "only actual per-unit duty restored");
    near(operationalWorkshopRate(s, s.ships.back(), EquipmentFamilyId{1}), 1,
         "standard workshop family supported");
    near(operationalWorkshopRate(s, s.ships.back(), EquipmentFamilyId{2}), 0,
         "standard workshop cannot repair specialist family");
    require(!maintenanceTeamQualified(s.maintenanceTeams.front(), EquipmentFamilyId{2}),
            "standard team lacks specialist qualification");
    s.shipClasses.back().role = ShipRole::Escort;
    near(operationalWorkshopRate(s, s.ships.back(), EquipmentFamilyId{1}), 1,
         "role does not grant or block workshop");
    std::erase_if(s.shipClasses.back().components,
                  [](const auto& i) { return i.componentId == ShipComponentId{2}; });
    near(operationalWorkshopRate(s, s.ships.back(), EquipmentFamilyId{1}), 0,
         "another hull reactor cannot power tender workshop");
    auto malformed = createTenderMaintenanceScenario();
    malformed.shipComponents.at(3).serviceProfile->teamWorkdaysPerDuty = 0;
    rejects([&] { validateGameState(malformed); }, "zero repair labor rejected");
    malformed = createTenderMaintenanceScenario();
    malformed.shipComponents.at(3).serviceProfile->materialsPerDuty.amount[0] =
        std::numeric_limits<double>::quiet_NaN();
    rejects([&] { validateGameState(malformed); }, "nonfinite service recipe rejected");
    malformed = createTenderMaintenanceScenario();
    malformed.shipComponents.at(3).serviceProfile->familyId=EquipmentFamilyId{999};
    rejects([&] { validateGameState(malformed); }, "dangling equipment family reference rejected");
    malformed = createTenderMaintenanceScenario();
    malformed.maintenanceTeams.front().qualifiedFamilies={EquipmentFamilyId{999}};
    rejects([&] { validateGameState(malformed); }, "dangling engineering qualification rejected");
    malformed = createTenderMaintenanceScenario();
    malformed.shipComponents.at(7).workshopRates.push_back(
        malformed.shipComponents.at(7).workshopRates.front());
    rejects([&] { validateGameState(malformed); },
            "duplicate workshop family entries rejected deterministically");
}
void tiny_positive_repairs_keep_real_bounds() {
    // Relative-only execution normalization cannot turn a small material offer
    // into free restoration merely because both values are below absolute epsilon.
    auto s = createTenderMaintenanceScenario();
    s.ships.front().equipmentCondition.front().usedDuty = 1e-12;
    ProcessedMaterialSet stock;
    stock.set(ProcessedMaterial::Electronics, 1e-15);
    stock.set(ProcessedMaterial::IndustrialComposites, 1);
    const auto partial = planEquipmentService(s, s.ships.front().id, ShipComponentId{4}, 1, 1, stock);
    require(partial.ready && partial.restoredDuty <= 2.01e-15 && partial.afterUsedDuty > 9e-13,
            "tiny positive shortage is not normalized into full restoration");
    stock.set(ProcessedMaterial::Electronics, 0);
    require(!planEquipmentService(s, s.ships.front().id, ShipComponentId{4}, 1, 1, stock).ready,
            "zero real material cannot repair even tiny positive duty");
}
} // namespace
int main() {
    try {
        nominal_and_independent_hulls();
        manual_duty_and_initialization();
        quantity_families_and_power();
        tiny_positive_repairs_keep_real_bounds();
        std::cout << "Maintenance rules tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
