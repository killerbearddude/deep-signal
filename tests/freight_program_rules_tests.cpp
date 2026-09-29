#include "sim/FreightProgramRules.h"
#include "sim/ProgramControl.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/TransitPlanning.h"

// Independent P3B-01/02/04/10/13/14 rules evidence. Fixture expectations below
// are literal hand-calculated catalog/stock/rate outcomes, not preview echoes.
#include <algorithm>
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
    p.charter.source = s.colonies[s.colonies.size() - 2].id;
    p.charter.operatingBaseColonyId = std::get<deep::ColonyId>(p.charter.source);
    p.charter.destination = s.colonies.back().id;
    p.charter.requestedFleetId = s.fleets.back().id;
    p.charter.requestedLeaderId = s.people.front().id;
    p.charter.totalQuantity = target;
    return p;
}
void catalog_and_capability() {
    auto s = deep::createDelegatedFreightScenario();
    const auto& freighter = *std::find_if(s.shipClasses.begin(), s.shipClasses.end(), [&](const auto& cls) {
        return cls.id == s.ships.back().shipClassId;
    });
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
    std::find_if(s.shipClasses.begin(), s.shipClasses.end(), [&](const auto& cls) {
        return cls.id == s.ships.back().shipClassId;
    })->role = deep::ShipRole::Survey;
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
    p.charter.destination = p.charter.source;
    require(deep::validateFreightProgramCharter(s, p.charter).has_value(), "same colony malformed");
    p = program(s);
    p.charter.commodity = deep::ProcessedMaterial::Count;
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
    p.charter.commodity = deep::ProcessedMaterial::Propellant;
    p.charter.policy.sourceCargoFloor = 20;
    p.charter.policy.basePropellantFloor = 10;
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
// Detached site hardware is appropriate here: this verifies pure logistics
// arithmetic, while earned construction/commissioning is covered end to end.
void typed_collection_and_shared_handling() {
    auto s = deep::createDelegatedFreightScenario();
    auto p = program(s, 200);
    s.siteModuleCatalog = deep::referenceSiteModuleCatalog();
    deep::ResourceSite site;
    site.id = {1};
    site.bodyId = s.colonies.back().bodyId;
    site.name = "Raw source";
    site.operatingPolicy.leaderId = s.people.front().id;
    site.processedStock.set(deep::ProcessedMaterial::ReactorFuel, 100);
    site.processedStock.set(deep::ProcessedMaterial::IndustrialComposites, 100);
    site.rawStock.set(deep::Mineral::WaterIce, 200);
    for (const auto& row : deep::referenceSitePackage())
        site.installed.push_back(
            {{1}, static_cast<int>(site.installed.size()), 1, row.kind, row.quantity, 0});
    s.resourceSites.push_back(site);
    s.date.day = 1;
    p.charter.source = site.id;
    p.charter.destination = p.charter.operatingBaseColonyId;
    p.charter.commodity = deep::Mineral::WaterIce;
    p.charter.policy.sourceCargoFloor = 30;
    p.charter.policy.basePropellantFloor = 7;
    require(!deep::validateFreightProgramCharter(s, p.charter),
            "raw collection admitted without geological evidence");
    near(deep::freightEffectiveFloor(p, site.id, deep::Mineral::WaterIce), 30,
         "raw cargo floor belongs to raw source");
    near(deep::freightEffectiveFloor(p, p.charter.operatingBaseColonyId, deep::ProcessedMaterial::Propellant),
         7, "raw cargo floor never reduces base fuel");
    const auto plan = deep::planFreightShipment(s, p, s.fleets.back());
    require(plan.ready && deep::freightIsCollection(p), "real raw stock supports empty-first collection");
    near(plan.quantity, 170, "manifest uses real stock above raw floor");
    require(plan.loadingDays == 4, "site shared handling prices remote loading");
    // Greedy first hull handles 100 at rate100, second 100 at rate10,
    // against one site pool50: two first-hull days plus ten second-hull days.
    auto slowClass = *std::find_if(s.shipClasses.begin(), s.shipClasses.end(),
                                   [&](const auto& cls) { return cls.id == s.ships.back().shipClassId; });
    slowClass.id = {900};
    for (auto& component : s.shipComponents)
        if (component.kind == deep::ShipComponentKind::CargoBay)
            component.cargoHandlingPerDay = 50;
    for (auto& row : slowClass.components) {
        const auto component = std::find_if(s.shipComponents.begin(), s.shipComponents.end(),
                                            [&](const auto& x) { return x.id == row.componentId; });
        if (component != s.shipComponents.end() && component->kind == deep::ShipComponentKind::CargoBay) {
            auto slow = *component;
            slow.id = {901};
            slow.cargoHandlingPerDay = 5;
            s.shipComponents.push_back(slow);
            row.componentId = slow.id;
            break;
        }
    }
    s.shipClasses.push_back(slowClass);
    auto slowShip = s.ships.back();
    slowShip.id = {900};
    slowShip.shipClassId = slowClass.id;
    s.ships.push_back(slowShip);
    s.fleets.back().shipIds.push_back(slowShip.id);
    const std::vector<deep::FreightManifestRow> manifest{{s.fleets.back().shipIds.front(), 100},
                                                         {slowShip.id, 100}};
    const auto constrained = deep::evaluateFreightManifest(s, p, s.fleets.back(), manifest, 2, true);
    require(constrained.ready && constrained.loadingDays == 12,
            "shared site timetable follows actual roster allocation, not max aggregate shortcut");
    s.resourceSites.back().rawStock.set(deep::Mineral::WaterIce, 0);
    require(!deep::planFreightShipment(s, p, s.fleets.back()).ready,
            "hidden reserves never substitute for actual source stock");
    p.charter.commodity = deep::ProcessedMaterial::StructuralAlloys;
    require(deep::Commodity{deep::ProcessedMaterial::StructuralAlloys} !=
                deep::Commodity{
                    static_cast<deep::Mineral>(static_cast<int>(deep::ProcessedMaterial::StructuralAlloys))},
            "equal enum ordinals retain commodity kind");
    // Cold packaged stores remain admissible and executable without site duty.
    s.resourceSites.back().installed.clear();
    s.resourceSites.back().processedStock.set(deep::ProcessedMaterial::StructuralAlloys, 100);
    require(deep::planFreightShipment(s, p, s.fleets.back()).ready,
            "ship handling retrieves sealed processed staging without raw port");
}
} // namespace
int main() {
    try {
        catalog_and_capability();
        malformed_and_readiness();
        shared_propellant_and_dated_return();
        typed_collection_and_shared_handling();
        std::cout << "freight rules tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
