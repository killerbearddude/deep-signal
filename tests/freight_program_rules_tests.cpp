#include "sim/FreightProgramRules.h"
#include "sim/ProgramControl.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/TransitPlanning.h"

// Independent P3B-01/02/04/10/13/14 rules evidence. Fixture expectations below
// are literal hand-calculated catalog/stock/rate outcomes, not preview echoes.
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}
void near(double a, double b, const char* message) {
    require(std::isfinite(a) && std::abs(a - b) < 1.e-7, message);
}
deep::FreightProgram program(const deep::GameState& s, double target = 500.0) {
    deep::FreightProgram p;
    p.id = {1};
    p.charter.name = "Rules freight";
    p.charter.sourceColonyId = s.colonies[s.colonies.size() - 2].id;
    p.charter.destinationColonyId = s.colonies.back().id;
    p.charter.requestedFleetId = s.fleets.back().id;
    p.charter.requestedLeaderId = s.people.front().id;
    p.charter.totalQuantity = target;
    return p;
}
void catalog_and_capability() {
    auto s = deep::createDelegatedFreightScenario();
    const auto& freighter = s.shipClasses.back();
    const auto d = deep::evaluateShipDesign(s.shipComponents, freighter.components);
    near(d.dryMass, 590, "independent freighter mass");
    near(d.usedVolume, 670, "independent internal volume");
    near(d.volumeCapacity, 1000, "hull internal capacity");
    near(d.powerGeneration, 120, "reactor supply");
    near(d.powerDemand, 60, "two bay power demand");
    near(d.propellantCapacity, 1000, "engine tank capacity");
    near(d.cargoCapacity, 200, "two bays store 200 normalized units");
    near(d.cargoHandlingPerDay, 50, "two bays handle 50 units daily");
    near(d.buildPoints, 550, "derived construction BP");
    near(d.buildCost.get(deep::ProcessedMaterial::StructuralAlloys), 330, "alloy cost");
    near(d.buildCost.get(deep::ProcessedMaterial::Electronics), 60, "electronics cost");
    near(d.buildCost.get(deep::ProcessedMaterial::ReactorFuel), 20, "reactor fuel cost");
    near(d.buildCost.get(deep::ProcessedMaterial::IndustrialComposites), 90, "composites cost");
    near(d.surveyCapability, 0, "freighter has no sensor");
    auto rows = freighter.components;
    rows.back().quantity = 1;
    const auto one = deep::evaluateShipDesign(s.shipComponents, rows);
    near(d.dryMass - one.dryMass, 60, "added bay mass");
    near(d.usedVolume - one.usedVolume, 200, "added bay volume");
    near(d.cargoCapacity - one.cargoCapacity, 100, "added bay storage");
    near(d.buildPoints - one.buildPoints, 60, "added bay BP");
    const auto cutter = deep::evaluateShipDesign(s.shipComponents, s.shipClasses.front().components);
    near(cutter.buildPoints, 500, "Survey Cutter BP remains unchanged");
    near(cutter.propellantCapacity, 1000, "Survey Cutter tank unchanged");
    near(cutter.cargoCapacity, 0, "role alone does not create storage");
    s.shipClasses.back().role = deep::ShipRole::Survey;
    near(deep::freightHullCapabilities(s, s.fleets.back()).front().operationalHandlingPerDay, 50,
         "survey label does not remove bay handling");
    for (auto& component : s.shipComponents)
        if (component.kind == deep::ShipComponentKind::Reactor)
            component.powerGeneration = 0;
    const auto hull = deep::freightHullCapabilities(s, s.fleets.back()).front();
    near(hull.capacity, 200, "unpowered hull retains storage");
    near(hull.operationalHandlingPerDay, 0, "unpowered handling is unavailable");
}
void malformed_and_readiness() {
    auto s = deep::createDelegatedFreightScenario();
    auto p = program(s);
    require(!deep::validateFreightProgramCharter(s, p.charter), "coherent charter admitted");
    p.charter.requestedFleetId.reset();
    p.charter.requestedLeaderId.reset();
    require(!deep::validateFreightProgramCharter(s, p.charter), "missing readiness admitted");
    p.charter.destinationColonyId = p.charter.sourceColonyId;
    require(deep::validateFreightProgramCharter(s, p.charter).has_value(), "same colony malformed");
    p = program(s);
    p.charter.material = deep::ProcessedMaterial::Count;
    require(deep::validateFreightProgramCharter(s, p.charter).has_value(), "invalid material malformed");
    p = program(s);
    p.charter.totalQuantity = std::numeric_limits<double>::infinity();
    require(deep::validateFreightProgramCharter(s, p.charter).has_value(), "infinite target malformed");
    p = program(s);
    p.charter.policy.sourceCargoFloor = std::numeric_limits<double>::quiet_NaN();
    require(deep::validateFreightProgramCharter(s, p.charter).has_value(), "NaN policy malformed");
    p = program(s);
    p.charter.totalQuantity = 0;
    require(deep::validateFreightProgramCharter(s, p.charter).has_value() &&
                !deep::validateFreightProgramCharter(s, p.charter, true),
            "zero only permitted for amendment");
}
void shared_propellant_and_dated_return() {
    auto s = deep::createDelegatedFreightScenario();
    auto p = program(s);
    auto& source = s.colonies[s.colonies.size() - 2];
    auto& destination = s.colonies.back();
    p.charter.material = deep::ProcessedMaterial::Propellant;
    p.charter.policy.sourceCargoFloor = 20;
    p.charter.policy.sourcePropellantFloor = 10;
    source.processedStockpile.set(deep::ProcessedMaterial::Propellant, 120);
    // Fixed points five units apart cost five each way; no commander modifier
    // is attached to the dedicated freight fleet. The required 10-unit refill
    // therefore leaves exactly 90 useful units above the larger 20-unit floor.
    auto& body = s.bodies[s.bodies.size() - 3];
    body.x = 5;
    body.y = 0;
    const auto plan = deep::planFreightShipment(s, p, s.fleets.back());
    require(plan.ready, "smaller self-supported Propellant shipment found");
    near(plan.requiredFuel, 10, "independent round-trip fuel");
    near(plan.additionalFuel, 10, "empty tanks require real refill");
    near(plan.quantity, 90, "120 minus max floor20 minus engine refill10 yields payload90");
    require(plan.loadingDays == 2 && plan.unloadingDays == 2, "90 units require two workdays each way");
    const auto outbound = deep::planFleetTransit(s, source.bodyId, destination.bodyId, plan.departureDay);
    require(plan.returnDepartureDay == outbound.arrivalDay + 3,
            "return includes two unloading days plus separate departure boundary");
    p.charter.policy.maxAdditionalPropellant = 0;
    require(!deep::planFreightShipment(s, p, s.fleets.back()).ready,
            "zero allowance with empty tanks stays unready");
    s.ships.back().fuel = 10;
    const auto prepaid = deep::planFreightShipment(s, p, s.fleets.back());
    require(prepaid.ready, "already aboard fuel needs no new allowance");
    near(prepaid.additionalFuel, 0, "preexisting engine fuel not charged again");
    destination.bodyId = source.bodyId;
    s.ships.back().fuel = 0;
    const auto same = deep::planFreightShipment(s, p, s.fleets.back());
    require(same.ready, "distinct same-body colonies need no fictitious transit");
    near(same.requiredFuel, 0, "same-body route costs zero");
}
} // namespace
int main() {
    try {
        catalog_and_capability();
        malformed_and_readiness();
        shared_propellant_and_dated_return();
        std::cout << "freight rules tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
