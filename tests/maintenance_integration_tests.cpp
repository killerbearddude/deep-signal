// Three-controller arbitration, bounded service days and actual P3B supply.
// Material totals are checked independently across real colony/cargo/repair data.
#include "sim/GameStateValidation.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) { require(std::isfinite(a) && std::abs(a - b) < 1e-8, why); }
MaintenanceProgramCharter provider(const GameState& s, ColonyId home) {
    MaintenanceProgramCharter c;
    c.name = "Base service";
    c.serviceColonyId = home;
    c.requestedTenderId = s.fleets.at(1).id;
    c.requestedTeamId = s.maintenanceTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.clients = {s.fleets.front().id};
    return c;
}
SurveyProgramCharter client(const GameState& s, ColonyId home) {
    SurveyProgramCharter c;
    c.name = "Standing client";
    c.homeColonyId = home;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, 6}};
    c.policy.maintenanceProgramId = s.maintenancePrograms.front().id;
    return c;
}
Simulation supplied() {
    Simulation sim(createMaintenanceSupplyScenario());
    const auto home = sim.state().colonies.at(sim.state().colonies.size() - 2).id;
    require(sim.execute(CreateMaintenanceProgramCommand{provider(sim.state(), home)}).ok,
            "parts-dependent provider accepted");
    require(sim.execute(CreateSurveyProgramCommand{client(sim.state(), home)}).ok,
            "same worn survey waits on provider");
    for (int i = 0; i < 2; ++i) {
        FreightProgramCharter c;
        c.name = i == 0 ? "Electronics" : "Composites";
        c.sourceColonyId = sim.state().colonies.back().id;
        c.destinationColonyId = home;
        c.material = i == 0 ? ProcessedMaterial::Electronics : ProcessedMaterial::IndustrialComposites;
        c.totalQuantity = 20;
        c.requestedFleetId = sim.state().fleets.at(static_cast<std::size_t>(i) + 2).id;
        c.requestedLeaderId = sim.state().people.front().id;
        require(sim.execute(CreateFreightProgramCommand{c}).ok, "single-commodity supply authorized");
    }
    return sim;
}
void supply_and_bulk_daily_equivalence() {
    auto sim = supplied();
    const auto initial = sim.state();
    std::int64_t firstSupply = -1, firstRepair = -1;
    for (int i = 0; i < 180; ++i) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1,
                "ordinary servicing and parts wait do not trap time");
        const auto& state = sim.state();
        const auto& p = state.maintenancePrograms.front();
        if (firstSupply < 0 && state.freightPrograms[0].cargoDelivered > 0 &&
            state.freightPrograms[1].cargoDelivered > 0) {
            firstSupply = state.date.day;
            require(p.receipts.empty(), "unloading opening cannot spend new credit on repair");
        }
        if (firstRepair < 0 && !p.receipts.empty())
            firstRepair = p.receipts.front().day;
        for (auto material : {ProcessedMaterial::Electronics, ProcessedMaterial::IndustrialComposites}) {
            double total = state.colonies.back().processedStockpile.get(material) +
                           state.colonies.at(state.colonies.size() - 2).processedStockpile.get(material) +
                           p.consumed.get(material);
            for (const auto& ship : state.ships)
                if (ship.cargo && ship.cargo->material == material)
                    total += ship.cargo->quantity;
            near(
                total, 20,
                "source plus destination plus cargo plus actual repair consumption conserves supplied parts");
        }
        if (firstSupply < 0)
            near(state.ships.front().equipmentCondition.front().usedDuty, 10,
                 "contract cargo cannot service before actual unload");
        validateGameState(state);
    }
    require(firstSupply > 0 && firstRepair == firstSupply + 1,
            "existing service begins exactly next opening after supply");
    require(sim.state().surveyPrograms.front().receipts.size() == 6,
            "original client continues six passes without another authorization");
    near(sim.state().maintenancePrograms.front().consumed.get(ProcessedMaterial::Electronics), 15,
         "initial overdue service plus two later services consume15");
    const auto& reports=sim.state().maintenancePrograms.front().reports;
    require(std::any_of(reports.begin(),reports.end(),[](const auto& r){return r.endDay==90&&r.isNinetyDayReview;})&&
            std::any_of(reports.begin(),reports.end(),[](const auto& r){return r.endDay==30&&!r.isNinetyDayReview;}),
            "maintenance uses actual global thirty-day reports and ninety-day review markers");
    Simulation bulk(initial);
    require(bulk.advanceDaysDetailed(180).advancedDays == 180, "bulk supply execution completes");
    const auto& a = sim.state();
    const auto& b = bulk.state();
    require(a.eventLog.size() == b.eventLog.size() &&
                a.surveyPrograms.front().receipts.size() == b.surveyPrograms.front().receipts.size(),
            "bulk/daily event and survey counts match");
    for (std::size_t i = 0; i < a.eventLog.size(); ++i)
        require(a.eventLog[i].id == b.eventLog[i].id && a.eventLog[i].day == b.eventLog[i].day &&
                    a.eventLog[i].payload.index() == b.eventLog[i].payload.index(),
                "mixed meaningful event order matches");
    for (std::size_t i = 0; i < a.ships.size(); ++i) {
        near(a.ships[i].fuel, b.ships[i].fuel, "mixed fuel continuation matches");
        require(a.ships[i].cargo.has_value() == b.ships[i].cargo.has_value(), "mixed cargo custody matches");
    }
    require(a.maintenancePrograms.front().consumed.amount == b.maintenancePrograms.front().consumed.amount,
            "mixed repair debits match");
}
void typed_arbitration_and_waiting_intent() {
    auto state = createTenderMaintenanceScenario();
    SurveyProgram s1, s2;
    s1.id = {1};
    s1.createdDay = 8;
    s2.id = {2};
    s2.createdDay = 1;
    FreightProgram f;
    f.id = {1};
    f.createdDay = 8;
    MaintenanceProgram m1, m2;
    m1.id = {1};
    m1.createdDay = 2;
    m2.id = {2};
    m2.createdDay = 8;
    state.surveyPrograms = {s1, s2};
    state.freightPrograms = {f};
    state.maintenancePrograms = {m1, m2};
    require(programOpeningOrder(state) == std::vector<ProgramController>{m1.id, s1.id, s2.id, f.id, m2.id},
            "three-way stable head merge preserves each vector and old tie precedence");
    state = createTenderMaintenanceScenario();
    Simulation sim(state);
    auto c = provider(state, state.colonies.back().id);
    require(sim.execute(CreateMaintenanceProgramCommand{c}).ok, "maintenance intent accepted");
    sim.advanceDays(1);
    require(controllingProgram(sim.state(), *c.requestedTenderId) ==
                ProgramController{MaintenanceProgramId{1}},
            "typed maintenance owner holds tender");
    auto sc = client(sim.state(), c.serviceColonyId);
    sc.requestedFleetId = c.requestedTenderId;
    require(sim.execute(CreateSurveyProgramCommand{sc}).ok, "survey request for busy tender remains intent");
    FreightProgramCharter fc;
    fc.name = "Busy tender freight";
    fc.sourceColonyId = c.serviceColonyId;
    fc.destinationColonyId = state.colonies.front().id;
    fc.requestedFleetId = c.requestedTenderId;
    fc.requestedLeaderId = c.requestedLeaderId;
    fc.totalQuantity = 1;
    require(sim.execute(CreateFreightProgramCommand{fc}).ok,
            "freight request for busy tender remains intent");
    sim.advanceDays(1);
    require(!sim.state().surveyPrograms.front().leasedFleetId &&
                !sim.state().freightPrograms.front().leasedFleetId,
            "other program kinds never steal maintenance tender");
    const auto fleet = *c.requestedTenderId;
    const auto body = state.bodies.back().id;
    require(!sim.execute(MoveFleetCommand{fleet, body}).ok &&
                !sim.execute(QueueFleetMoveOrderCommand{fleet, body}).ok &&
                !sim.execute(CancelFleetOrderCommand{fleet}).ok &&
                !sim.execute(ClearFleetOrderQueueCommand{fleet}).ok &&
                !sim.execute(ResourceSurveyCommand{fleet, state.fleets.back().currentBodyId}).ok,
            "all manual paths protect maintenance ownership");
    auto unready = c;
    unready.name = "Missing resources";
    unready.requestedTenderId.reset();
    unready.requestedTeamId.reset();
    unready.requestedLeaderId.reset();
    require(sim.execute(CreateMaintenanceProgramCommand{unready}).ok,
            "missing optional readiness still accepts standing intent");
    require(sim.execute(CreateMaintenanceProgramCommand{c}).ok,
            "already leased finite engineer remains accepted wait");
    sim.advanceDays(2);
    require(!sim.state().maintenancePrograms.back().leasedTeamId,
            "busy engineering team cannot duplicate daily capacity");
    auto malformed = c;
    malformed.clients.push_back(malformed.clients.front());
    const auto count = sim.state().maintenancePrograms.size();
    require(!sim.execute(CreateMaintenanceProgramCommand{malformed}).ok &&
                sim.state().maintenancePrograms.size() == count,
            "duplicate client admission has no partial mutation");
    malformed=c;malformed.requestedTenderId=FleetId{999999};
    const auto nextId=sim.state().ids.nextMaintenanceProgramId;
    require(!sim.execute(CreateMaintenanceProgramCommand{malformed}).ok&&sim.state().ids.nextMaintenanceProgramId==nextId&&
            sim.state().maintenancePrograms.size()==count,"invalid supplied identity rejects without allocating or partially creating intent");
}

void client_priority_and_shared_supply_floors() {
    // One finite team selects the first charter client, independent of survey
    // creation/ID order, and cannot donate leftover work to the second client.
    auto s = createTenderMaintenanceScenario();
    s.ships.front().equipmentCondition.front().usedDuty = 10;
    auto clone = s.ships.front();
    clone.id = ShipId{s.ids.nextShipId++};
    clone.fleetId = FleetId{s.ids.nextFleetId++};
    clone.name = "Second worn client";
    s.ships.push_back(clone);
    auto fleet = s.fleets.front();
    fleet.id = clone.fleetId;
    fleet.name = "Priority client";
    fleet.shipIds = {clone.id};
    s.fleets.push_back(fleet);
    auto surveyTeam = s.surveyTeams.front();
    surveyTeam.id = SurveyTeamId{s.ids.nextSurveyTeamId++};
    surveyTeam.name = "Second survey team";
    s.surveyTeams.push_back(surveyTeam);
    Simulation sim(s);
    auto p = provider(s, s.colonies.back().id);
    p.clients = {clone.fleetId, s.fleets.front().id};
    p.policy.floors.set(ProcessedMaterial::Electronics, 98.0);
    require(sim.execute(CreateMaintenanceProgramCommand{p}).ok, "ordered two-client provider accepted");
    auto first = client(sim.state(), p.serviceColonyId);
    require(sim.execute(CreateSurveyProgramCommand{first}).ok, "first survey accepted");
    auto second = first;
    second.name = "Higher charter priority";
    second.requestedFleetId = clone.fleetId;
    second.requestedTeamId = surveyTeam.id;
    require(sim.execute(CreateSurveyProgramCommand{second}).ok, "second survey accepted");
    sim.advanceDays(2);
    const auto& program = sim.state().maintenancePrograms.front();
    require(program.jobs.size() == 1 && program.jobs.front().clientFleetId == clone.fleetId &&
                program.receipts.size() == 1,
            "stored client order wins, only one client works");
    near(program.receipts.front().restoredDuty, 4.0, "two available Electronics bound restored duty tofour");
    near(program.receipts.front().teamWorkdays, 0.8, "only actual partial engineering work spent");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 10,
         "second-in-priority client remains untouched");
    near(sim.state().colonies.back().processedStockpile.get(ProcessedMaterial::Electronics), 98,
         "protected stock floor remains");
    auto amendment = maintenanceAmendmentFromCharter(program.charter);
    amendment.policy.floors.set(ProcessedMaterial::Electronics, 0);
    amendment.policy.lifetimeAllowances = ProcessedMaterialSet{};
    amendment.policy.lifetimeAllowances->set(ProcessedMaterial::Electronics, 1);
    amendment.policy.lifetimeAllowances->set(ProcessedMaterial::IndustrialComposites, 100);
    require(sim.execute(AmendMaintenanceProgramCommand{program.id, amendment}).ok,
            "allowance may fall below already consumed history");
    require(sim.advanceDaysDetailed(3).advancedDays == 3,
            "explicitly accepted shortage does not immediately reopen same issue");
    near(sim.state().maintenancePrograms.front().consumed.get(ProcessedMaterial::Electronics), 2,
         "lowered limits do not refund prior consumption");
    require(sim.execute(SuspendMaintenanceProgramCommand{program.id}).ok &&
                sim.execute(CancelMaintenanceProgramCommand{program.id}).ok,
            "unfinished limited work remains suspendable and cancelable");
    validateGameState(sim.state());

    // Different colonies at the same body are not aliases: service consumes
    // only its fixed stockpile while survey refueling remains at its own home.
    auto separate = createTenderMaintenanceScenario();
    separate.ships.front().equipmentCondition.front().usedDuty = 10;
    auto serviceColony = separate.colonies.back();
    serviceColony.id = ColonyId{separate.ids.nextColonyId++};
    serviceColony.name = "Separate service stockpile";
    separate.colonies.push_back(serviceColony);
    separate.maintenanceTeams.front().colonyId = serviceColony.id;
    Simulation split(separate);
    auto support = provider(separate, serviceColony.id);
    require(split.execute(CreateMaintenanceProgramCommand{support}).ok,
            "same-body distinct service colony accepted");
    auto survey = client(split.state(), separate.colonies.at(separate.colonies.size() - 2).id);
    require(split.execute(CreateSurveyProgramCommand{survey}).ok, "survey retains separate home colony");
    split.advanceDays(2);
    near(split.state()
             .colonies.at(split.state().colonies.size() - 2)
             .processedStockpile.get(ProcessedMaterial::Electronics),
         100, "repair does not debit survey-home alias");
    near(split.state().colonies.back().processedStockpile.get(ProcessedMaterial::Electronics), 97.5,
         "repair debits actual selected service colony");
}

void remote_assets_and_closed_reporting() {
    auto s = createTenderMaintenanceScenario();
    s.maintenanceTeams.front().colonyId = s.colonies.front().id;
    Simulation remote(s);
    auto c = provider(s, s.colonies.back().id);
    require(remote.execute(CreateMaintenanceProgramCommand{c}).ok,
            "remote engineering team does not reject intent");
    remote.advanceDays(10);
    require(!remote.state().maintenancePrograms.front().leasedTenderId &&
                !remote.state().maintenancePrograms.front().leasedTeamId &&
                remote.state().maintenanceTeams.front().colonyId == s.colonies.front().id,
            "pair acquisition never takes half a lease or teleports remote team");
    s = createTenderMaintenanceScenario();
    Simulation closed(s);
    require(closed.execute(CreateMaintenanceProgramCommand{provider(s, s.colonies.back().id)}).ok,
            "standing provider accepted");
    require(closed.advanceDaysDetailed(30).advancedDays == 30,
            "empty standing provider does not require periodic reauthorization");
    require(closed.state().maintenancePrograms.front().reports.size() == 1, "day30 report published once");
    require(closed.execute(CancelMaintenanceProgramCommand{MaintenanceProgramId{1}}).ok,
            "closure on report day accepted");
    closed.advanceDays(90);
    require(closed.state().maintenancePrograms.front().reports.size() == 1 &&
                closed.state().maintenancePrograms.front().closedDay == 30,
            "closure-day report is not duplicated and future provider reports stop");
    require(closed.state().maintenanceTeams.front().location == MaintenanceTeamLocation::Colony &&
                closed.state().maintenanceTeams.front().colonyId == s.colonies.back().id,
            "stationary team disembarks at actual service colony");
    validateGameState(closed.state());
}

void all_three_kinds_share_one_opening_stock() {
    // Fixture-only recipe adds Propellant, making service, survey refueling and
    // freight refueling compete for the very same 9 units on one opening.
    auto s = createTenderMaintenanceScenario();
    s.ships.front().equipmentCondition.front().usedDuty = 10;
    s.shipComponents.at(3).serviceProfile->materialsPerDuty.set(ProcessedMaterial::Propellant, 1.0);
    const auto home = s.colonies.back().id;
    const auto body = s.colonies.back().bodyId;
    s.colonies.back().processedStockpile.set(ProcessedMaterial::Propellant, 9.0);
    auto depot = s.colonies.back();
    depot.id = ColonyId{s.ids.nextColonyId++};
    depot.bodyId = s.bodies.at(s.bodies.size() - 2).id;
    depot.name = "Fixed half-unit freight destination";
    depot.processedStockpile = {};
    s.colonies.push_back(depot);
    const auto addFleet = [&](ShipClassId cls, const char* name) {
        const FleetId fleet{s.ids.nextFleetId++};
        const ShipId ship{s.ids.nextShipId++};
        s.ships.push_back(Ship{.id = ship, .shipClassId = cls, .name = name, .fleetId = fleet, .fuel = 0});
        initializeShipEquipmentCondition(s, s.ships.back());
        s.fleets.push_back(Fleet{.id = fleet,
                                 .name = name,
                                 .currentBodyId = body,
                                 .destinationBodyId = std::nullopt,
                                 .shipIds = {ship},
                                 .activeOrder = {},
                                 .queuedOrders = {},
                                 .ownerInstitutionId = s.institutions.front().id});
        return fleet;
    };
    const auto freighter = addFleet(s.shipClasses.at(1).id, "Fuel-sharing freighter");
    const auto surveyFleet = addFleet(s.shipClasses.front().id, "Fuel-sharing survey");
    auto team = s.surveyTeams.front();
    team.id = SurveyTeamId{s.ids.nextSurveyTeamId++};
    team.name = "Fuel-sharing team";
    s.surveyTeams.push_back(team);
    Simulation sim(s);
    require(sim.execute(CreateMaintenanceProgramCommand{provider(s, home)}).ok, "older provider accepted");
    require(sim.execute(CreateSurveyProgramCommand{client(sim.state(), home)}).ok,
            "worn service client accepted");
    sim.advanceDays(1);
    SurveyProgramCharter other;
    other.name = "Unassisted refueling survey";
    other.homeColonyId = home;
    other.requestedFleetId = surveyFleet;
    other.requestedTeamId = team.id;
    other.requestedLeaderId = s.people.front().id;
    other.targets = {{s.bodies.back().id, 0, 1}};
    require(sim.execute(CreateSurveyProgramCommand{other}).ok, "later survey fuel consumer accepted");
    FreightProgramCharter f;
    f.name = "Later freight fuel consumer";
    f.sourceColonyId = home;
    f.destinationColonyId = depot.id;
    f.material = ProcessedMaterial::Electronics;
    f.totalQuantity = 1;
    f.requestedFleetId = freighter;
    f.requestedLeaderId = s.people.front().id;
    require(sim.execute(CreateFreightProgramCommand{f}).ok, "later freight consumer accepted");
    require(sim.advanceDaysDetailed(1).advancedDays == 1, "shared opening executes normally");
    near(sim.state().maintenancePrograms.front().consumed.get(ProcessedMaterial::Propellant), 5,
         "earlier provider consumes five actual recipe units");
    near(sim.state().surveyPrograms.back().fuelLoaded, 2,
         "next survey transfers actual two minimum-price legs");
    near(sim.state().freightPrograms.front().fuelLoaded, 2, "last freight transfers final two fuel units");
    for (const auto& colony : sim.state().colonies)
        if (colony.id == home)
            near(colony.processedStockpile.get(ProcessedMaterial::Propellant), 0,
                 "no actor overspends the common9-unit stock");
    validateGameState(sim.state());
}
void industry_supply_is_available_next_opening() {
    // The service job already exists when an existing processing command enables
    // parts production. Its output cannot retroactively fund that day's opening.
    auto s=createTenderMaintenanceScenario();s.ships.front().equipmentCondition.front().usedDuty=10;
    auto& home=s.colonies.back();home.processedStockpile.set(ProcessedMaterial::Electronics,0);
    home.processorCapacity=10;home.processingPolicy=ProcessingPolicy::Manual;
    home.manualProcessingAllocations={{ProcessedMaterial::StructuralAlloys,1}};
    home.stockpile.set(Mineral::Copper,10);home.stockpile.set(Mineral::Silicon,10);home.stockpile.set(Mineral::RareEarthElements,2);
    Simulation sim(s);require(sim.execute(CreateMaintenanceProgramCommand{provider(s,home.id)}).ok,"industry-dependent provider accepted");
    require(sim.execute(CreateSurveyProgramCommand{client(sim.state(),home.id)}).ok,"industry-dependent client accepted");sim.advanceDays(2);
    require(activeServiceJob(sim.state().maintenancePrograms.front())&&sim.state().maintenancePrograms.front().receipts.empty(),"actual service waits for missing ingredient");
    require(sim.execute(SetColonyProcessingPolicyCommand{home.id,ProcessingPolicy::Manual,{{ProcessedMaterial::Electronics,1}}}).ok,"existing processing allocation enables real supplies");
    sim.advanceDays(1);
    require(sim.state().maintenancePrograms.front().receipts.empty(),"today's processing cannot supply earlier opening maintenance");
    near(sim.state().colonies.back().processedStockpile.get(ProcessedMaterial::Electronics),10,"ten real Electronics produced after opening");
    sim.advanceDays(1);
    require(sim.state().maintenancePrograms.front().receipts.size()==1&&sim.state().maintenancePrograms.front().receipts.front().day==4,
            "original held service uses produced parts at next opening without reauthorization");
    validateGameState(sim.state());
}
} // namespace
int main() {
    try {
        supply_and_bulk_daily_equivalence();
        typed_arbitration_and_waiting_intent();
        client_priority_and_shared_supply_floors();
        remote_assets_and_closed_reporting();
        all_three_kinds_share_one_opening_stock();
        industry_supply_is_available_next_opening();
        std::cout << "Maintenance integration tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
