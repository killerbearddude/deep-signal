// Independent integration proofs for mixed authority, phase-local stock and
// receiving operations. Assertions inspect real stock/leases/dates, not preview
// arithmetic, so the same bug in a planner and executor cannot satisfy them.
#include "sim/FreightProgramRules.h"
#include "sim/GameStateValidation.h"
#include "sim/ProgramControl.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/ShipDesignRules.h"
#include "sim/SurveyProgramExecution.h"
#include "sim/EquipmentServiceRules.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(double a, double b, const char* message) { require(std::abs(a-b)<1e-7, message); }

FreightProgramCharter freight(const GameState& state, ProcessedMaterial material = ProcessedMaterial::StructuralAlloys) {
    FreightProgramCharter charter;
    charter.name = "Independent supply proof";
    charter.source = state.colonies.at(state.colonies.size()-2).id;
    charter.operatingBaseColonyId = std::get<deep::ColonyId>(charter.source);
    charter.destination = state.colonies.back().id;
    charter.commodity = material;
    charter.totalQuantity = 500.0;
    charter.requestedFleetId = state.fleets.back().id;
    charter.requestedLeaderId = state.people.front().id;
    return charter;
}
SurveyProgramCharter receivingSurvey(const GameState& state) {
    SurveyProgramCharter charter;
    charter.name = "Waiting receiver";
    charter.homeColonyId = state.colonies.back().id;
    charter.requestedFleetId = state.fleets.front().id;
    charter.requestedLeaderId = state.people.front().id;
    charter.requestedTeamId = state.surveyTeams.front().id;
    charter.targets = {{state.bodies.back().id, 0, 1}};
    return charter;
}

void test_stable_merge_and_budget() {
    // Compare heads, not all creation dates: each stored vector remains ordered
    // as authored even when a valid scenario places an older row behind a newer.
    auto state = createDelegatedFreightScenario();
    SurveyProgram s1, s2;
    s1.id = SurveyProgramId{7}; s1.createdDay = 10;
    s2.id = SurveyProgramId{2}; s2.createdDay = 0;
    FreightProgram f1, f2;
    f1.id = FreightProgramId{7}; f1.createdDay = 5;
    f2.id = FreightProgramId{2}; f2.createdDay = 10;
    state.surveyPrograms = {s1,s2}; state.freightPrograms = {f1,f2};
    require(programOpeningOrder(state) == std::vector<ProgramController>{f1.id,s1.id,s2.id,f2.id},
            "stable merge preserves vector order and survey wins equal-date heads");
    auto& source = state.colonies.at(state.colonies.size()-2);
    source.processedStockpile.set(ProcessedMaterial::Propellant,120.0);
    OpeningProgramContext opening(state);
    near(opening.available(state,source.id,ProcessedMaterial::Propellant,20.0),100.0,"floor leaves 100 spendable");
    source.processedStockpile.set(ProcessedMaterial::Propellant,110.0);
    opening.debit(source.id,ProcessedMaterial::Propellant,10.0);
    near(opening.available(state,source.id,ProcessedMaterial::Propellant,20.0),90.0,"ten fuel leaves ninety payload");
    source.processedStockpile.set(ProcessedMaterial::Propellant,1110.0);
    near(opening.available(state,source.id,ProcessedMaterial::Propellant,20.0),90.0,"inbound thousand not respent this opening");
    OpeningProgramContext tomorrow(state);
    near(tomorrow.available(state,source.id,ProcessedMaterial::Propellant,20.0),1090.0,"actual credits spendable next opening");
}

void test_six_kind_tie_and_technical_facility_budget() {
    auto state = createHomeSystemScenario();
    SurveyProgram survey; survey.id = SurveyProgramId{1};
    FreightProgram freightProgram; freightProgram.id = FreightProgramId{1};
    MaintenanceProgram maintenance; maintenance.id = MaintenanceProgramId{1};
    AnalysisProgram analysis; analysis.id = AnalysisProgramId{1};
    SiteDevelopmentProgram site; site.id = SiteDevelopmentProgramId{1};
    TechnicalDevelopmentProgram technical; technical.id = TechnicalDevelopmentProgramId{1};
    state.surveyPrograms = {survey}; state.freightPrograms = {freightProgram};
    state.maintenancePrograms = {maintenance}; state.analysisPrograms = {analysis};
    state.siteDevelopmentPrograms = {site}; state.technicalDevelopmentPrograms = {technical};
    require(programOpeningOrder(state) == std::vector<ProgramController>{
                survey.id, freightProgram.id, maintenance.id, analysis.id, site.id, technical.id},
            "equal-day program heads preserve Survey/Freight/Maintenance/Analysis/Site/Technical order");
    require(ProgramController{maintenance.id} != ProgramController{technical.id},
            "equal numeric maintenance and technical IDs remain typed identities");
    // Detached ordering records above are intentionally not validated as full
    // programs; facility scratch is independently derived from authored state.
    state.surveyPrograms.clear(); state.freightPrograms.clear(); state.maintenancePrograms.clear();
    state.analysisPrograms.clear(); state.siteDevelopmentPrograms.clear();
    state.technicalDevelopmentPrograms.clear();
    OpeningProgramContext opening(state);
    const auto facility = state.technicalFacilities.front();
    near(opening.availableTechnicalFacility(facility.id), 1.0,
         "opening facility begins with one actual workday");
    opening.debitTechnicalFacility(facility.id, 0.25);
    near(opening.availableTechnicalFacility(facility.id), 0.75,
         "technical facility throughput cannot be double-spent");
}

void test_report_date_limit_stops_before_physical_tick() {
    auto state=createDelegatedFreightScenario();
    const auto finalBoundary=std::numeric_limits<std::int64_t>::max()/30*30;
    state.date.day=finalBoundary-32;
    Simulation sim(state);
    auto fc=freight(state);fc.requestedFleetId.reset();fc.requestedLeaderId.reset();
    require(sim.execute(CreateFreightProgramCommand{fc}).ok,"late-date waiting charter accepted");
    const auto result=sim.advanceDaysDetailed(40);
    require(result.interrupted && result.advancedDays==31 && sim.state().date.day==finalBoundary-1,
            "unrepresentable future report cursor stops before mutating the due day");
    require(sim.state().freightPrograms.front().receipts.empty(),"date limit fabricates no work");
    validateGameState(sim.state());
}

void test_scheduled_closure_report_once() {
    // Same-body handling takes three boundaries in this fixture: load, commit
    // disposition, unload. Align closure with a monthly/quarterly report and
    // verify closure publishes it once, then stops future freight reports.
    for (const std::int64_t start : {27,87}) {
        auto state=createDelegatedFreightScenario();
        state.date.day=start;
        state.colonies.back().bodyId=state.colonies.at(state.colonies.size()-2).bodyId;
        auto fc=freight(state);fc.totalQuantity=25.0;
        Simulation sim(state);
        require(sim.execute(CreateFreightProgramCommand{fc}).ok,"report-boundary fixture authorized");
        require(sim.advanceDaysDetailed(90).advancedDays==90,"routine reports and closed history do not stop time");
        const auto& p=sim.state().freightPrograms.front();
        require(p.closedDay==start+3 && p.reports.size()==1 && p.reports.front().endDay==start+3,
                "scheduled report published exactly once on closure boundary");
        require(p.reports.front().isNinetyDayReview==(start==87),"global quarter marker remains date-derived");
        near(p.reports.front().cargoDelivered,25.0,"closure report includes actual final unload");
        validateGameState(sim.state());
    }
}

void test_typed_arbitration_and_manual_guards() {
    auto state = createDelegatedFreightScenario();
    const auto sourceId = state.colonies.at(state.colonies.size()-2).id;
    state.surveyTeams.front().colonyId = sourceId;
    auto sc = receivingSurvey(state);
    sc.homeColonyId = sourceId;
    sc.requestedFleetId = state.fleets.back().id;
    // Both leaders/program kinds ask for one real hull. A role cannot grant it
    // parallel control even though survey and freight both allocate numeric ID1.
    Simulation sim(state);
    require(sim.execute(CreateFreightProgramCommand{freight(state)}).ok,"freight accepted");
    require(sim.execute(CreateSurveyProgramCommand{sc}).ok,"survey accepted");
    sim.advanceDays(1);
    const auto fleetId = state.fleets.back().id;
    require(controllingProgram(sim.state(),fleetId) == ProgramController{SurveyProgramId{1}},"tie gives survey typed owner");
    require(!sim.state().freightPrograms.front().leasedFleetId,"losing freight retains only intent");
    require(!sim.execute(MoveFleetCommand{fleetId,state.bodies.back().id}).ok,"manual move guarded");
    require(!sim.execute(QueueFleetMoveOrderCommand{fleetId,state.bodies.back().id}).ok,"queue append guarded");
    require(!sim.execute(ClearFleetOrderQueueCommand{fleetId}).ok,"queue clear guarded");
    require(!sim.execute(CancelFleetOrderCommand{fleetId}).ok,"manual cancel guarded");
    require(!sim.execute(ResourceSurveyCommand{fleetId,state.fleets.back().currentBodyId}).ok,"manual survey guarded");

    // A quiescent amended survey releases inside opening, but freight must wait
    // until tomorrow even if its merged head occurs after that release.
    auto detached = sim.state();
    auto& sp = detached.surveyPrograms.front();
    sp.task = SurveyProgramTask::None;
    sp.taskBodyId.reset(); sp.taskFleetId.reset(); sp.taskTeamId.reset(); sp.taskLeaderId.reset();
    sp.taskPassNumber = 0; sp.workDaysCompleted = 0; sp.firstWorkDay = 0;
    sp.charter.requestedFleetId.reset();
    Simulation released(detached);
    released.advanceDays(1);
    require(!controllingProgram(released.state(),fleetId),"release unavailable for rest of same opening");
    released.advanceDays(1);
    require(controllingProgram(released.state(),fleetId) == ProgramController{FreightProgramId{1}},"freight acquires next day with its own kind");
    require(!released.execute(CancelFleetOrderCommand{fleetId}).ok,"freight also blocks manual cancellation shortcut");
    validateGameState(released.state());
}

void test_waiting_survey_uses_delivery_next_opening() {
    auto state = createDelegatedFreightScenario();
    Simulation sim(state);
    require(sim.execute(CreateSurveyProgramCommand{receivingSurvey(state)}).ok,"unfueled receiver accepted");
    auto fc = freight(state,ProcessedMaterial::Propellant);
    fc.totalQuantity = 100.0;
    require(sim.execute(CreateFreightProgramCommand{fc}).ok,"propellant supply accepted");
    const auto surveyId = sim.state().surveyPrograms.front().id;
    std::int64_t firstDeliveryDay = -1;
    for (int day=0;day<100 && firstDeliveryDay<0;++day) {
        require(sim.advanceDaysDetailed(1).advancedDays==1,"normal supply loop advances");
        if (sim.state().freightPrograms.front().cargoDelivered>0.0) firstDeliveryDay=sim.state().date.day;
        require(sim.state().surveyPrograms.front().fuelLoaded==0.0,"incoming fuel cannot fund same-opening survey");
    }
    require(firstDeliveryDay>0,"real unloading occurred");
    sim.advanceDays(1);
    require(sim.state().surveyPrograms.front().id==surveyId && sim.state().surveyPrograms.front().fuelLoaded>0.0,
            "same survey intention draws real delivery at next opening");
    for (int day=0;day<100 && sim.state().surveyPrograms.front().receipts.empty();++day) sim.advanceDays(1);
    require(sim.state().surveyPrograms.front().receipts.size()==1,"receiving survey completes without reauthorization");
    validateGameState(sim.state());
}

void test_survey_condition_and_report_name_freight_owner() {
    // Both cases reach the report boundary with a real freight lease. The
    // dual-capability hull, co-located team and prepaid fuel make ownership the
    // only stationary readiness blocker; transit must not be called manual.
    for (const bool moving : {false, true}) {
        auto state = createDelegatedFreightScenario();
        state.date.day = moving ? 24 : 28;
        const auto sourceId = state.colonies.at(state.colonies.size() - 2).id;
        const auto fleetId = state.fleets.back().id;
        state.surveyTeams.front().colonyId = sourceId;
        std::find_if(state.shipClasses.begin(),state.shipClasses.end(),[&](const auto& cls){return cls.id==state.ships.back().shipClassId;})->components.push_back({ShipComponentId{4}, 1});
        // This authored dual-capability hull now has a managed instrument row.
        initializeShipEquipmentCondition(state, state.ships.back());
        state.ships.back().fuel = 1000.0;
        for (auto& body : state.bodies) {
            if (body.id == state.colonies.back().bodyId) body.x = 100.0;
        }
        require(evaluateFleetSurvey(state, state.fleets.back()).operationalCapability > 0.0,
                "freight fixture has powered survey equipment as well as cargo bays");

        Simulation sim(state);
        auto fc = freight(state);
        fc.name = "Supply Delivery";
        require(sim.execute(CreateFreightProgramCommand{fc}).ok, "freight owner authorized first");
        const int setupDays = moving ? 5 : 1;
        require(sim.advanceDaysDetailed(setupDays).advancedDays == setupDays && sim.state().date.day == 29,
                "freight owns the requested fleet before the survey report boundary");
        require((sim.state().fleets.back().activeOrder.type == FleetOrderType::MoveToBody) == moving,
                "fixture exercises the intended stationary or moving ownership case");
        require(sim.state().ships.back().cargo.has_value(), "freight ownership includes real loaded cargo");

        auto sc = receivingSurvey(state);
        sc.homeColonyId = sourceId;
        sc.requestedFleetId = fleetId;
        require(sim.execute(CreateSurveyProgramCommand{sc}).ok, "competing survey intention remains valid");
        const auto surveyId = sim.state().surveyPrograms.front().id;
        const auto freightId = sim.state().freightPrograms.front().id;
        require(surveyId.value == freightId.value && ProgramController{surveyId} != ProgramController{freightId},
                "equal numeric IDs remain distinct typed owners");
        const std::string expected = "Waiting: requested fleet is controlled by freight program Supply Delivery (#1)";
        require(surveyProgramExecutionCondition(sim.state(), sim.state().surveyPrograms.front()) == expected,
                "survey primary condition names the actual freight controller before movement or readiness");

        require(sim.advanceDaysDetailed(1).advancedDays == 1, "ordinary ownership wait does not interrupt time");
        const auto& survey = sim.state().surveyPrograms.front();
        require(survey.id == surveyId && survey.lifecycle == SurveyProgramLifecycle::Authorized &&
                survey.charter.requestedFleetId == fleetId && !survey.leasedFleetId && !survey.leasedTeamId &&
                survey.task == SurveyProgramTask::None && survey.receipts.empty(),
                "survey retains its intent without acquiring or using freight assets");
        require(controllingProgram(sim.state(), fleetId) == ProgramController{freightId},
                "freight retains exclusive fleet control");
        require(surveyProgramExecutionCondition(sim.state(), survey) == expected,
                "ownership remains the primary survey condition on the report day");
        require(survey.reports.size() == 1 && survey.reports.front().endDay == 30 &&
                survey.reports.front().waitingReason == expected,
                "durable due survey report preserves the correct freight ownership explanation");
        validateGameState(sim.state());
    }
}

void test_older_freight_and_mixed_bulk_equivalence() {
    // A freight head created earlier than survey takes the shared fleet; there
    // is no unconditional survey-first collection pass hidden behind the merge.
    auto state = createDelegatedFreightScenario();
    state.surveyTeams.front().colonyId = state.colonies.at(state.colonies.size()-2).id;
    auto fc = freight(state);
    const auto leader = fc.requestedLeaderId;
    fc.requestedLeaderId.reset();
    Simulation arbitration(state);
    require(arbitration.execute(CreateFreightProgramCommand{fc}).ok,"older unready freight created");
    arbitration.advanceDays(1);
    auto sc = receivingSurvey(state);
    sc.homeColonyId = std::get<ColonyId>(fc.source);
    sc.requestedFleetId = fc.requestedFleetId;
    require(arbitration.execute(CreateSurveyProgramCommand{sc}).ok,"later survey created");
    auto amended = freightAmendmentFromCharter(fc);
    amended.requestedLeaderId = leader;
    require(arbitration.execute(AmendFreightProgramCommand{FreightProgramId{1},amended}).ok,"older freight gains readiness");
    arbitration.advanceDays(1);
    require(controllingProgram(arbitration.state(),*fc.requestedFleetId)==ProgramController{FreightProgramId{1}},
            "older freight wins against later survey without changing either vector");

    // Advance mixed dependent supply/survey work in bulk and as identical daily
    // commands. Audit identity/order, physical stocks and leases must agree.
    state = createDelegatedFreightScenario();
    Simulation setup(state);
    require(setup.execute(CreateSurveyProgramCommand{receivingSurvey(state)}).ok,"mixed survey created");
    require(setup.execute(CreateFreightProgramCommand{freight(state,ProcessedMaterial::Propellant)}).ok,"mixed supply created");
    Simulation bulk(setup.state()), daily(setup.state());
    require(bulk.advanceDaysDetailed(90).advancedDays==90,"bulk mixed advancement completes");
    for (int i=0;i<90;++i) require(daily.advanceDaysDetailed(1).advancedDays==1,"daily mixed advancement completes");
    const auto& a = bulk.state(); const auto& b = daily.state();
    require(a.date.day==b.date.day && a.eventLog.size()==b.eventLog.size(),"same elapsed days and audit count");
    for (std::size_t i=0;i<a.eventLog.size();++i) require(a.eventLog[i].id==b.eventLog[i].id &&
        a.eventLog[i].day==b.eventLog[i].day && a.eventLog[i].payload.index()==b.eventLog[i].payload.index(),
        "same meaningful audit identity and order");
    for (std::size_t i=0;i<a.colonies.size();++i) require(a.colonies[i].processedStockpile.amount==b.colonies[i].processedStockpile.amount,
        "same actual inventories across daily chunking");
    for (std::size_t i=0;i<a.ships.size();++i) require(a.ships[i].fuel==b.ships[i].fuel && a.ships[i].cargo.has_value()==b.ships[i].cargo.has_value(),
        "same engine fuel and cargo presence");
    const auto& af=a.freightPrograms.front(); const auto& bf=b.freightPrograms.front();
    require(af.cargoDelivered==500.0 && af.cargoDelivered==bf.cargoDelivered && af.fuelBurned==bf.fuelBurned &&
        af.lifecycle==bf.lifecycle && af.leasedFleetId==bf.leasedFleetId && af.receipts.size()==bf.receipts.size() && af.reports.size()==bf.reports.size(),
        "same completed freight history and final custody");
    require(a.surveyPrograms.front().receipts.size()==1 && a.surveyPrograms.front().receipts.size()==b.surveyPrograms.front().receipts.size(),
        "same dependent survey completion");
    validateGameState(a); validateGameState(b);
}

void test_inbound_stock_supplies_later_shipyard_phase() {
    auto state = createDelegatedFreightScenario();
    auto& destination = state.colonies.back();
    destination.shipyardCapacity = 1000.0;
    destination.processedStockpile.set(ProcessedMaterial::Electronics,80.0);
    destination.processedStockpile.set(ProcessedMaterial::ReactorFuel,20.0);
    destination.processedStockpile.set(ProcessedMaterial::IndustrialComposites,50.0);
    Simulation sim(state);
    require(sim.execute(AssignShipyardBuildCommand{destination.id,state.shipClasses.front().id,1}).ok,"waiting yard intent accepted");
    require(sim.execute(CreateFreightProgramCommand{freight(state)}).ok,"alloy supply accepted");
    const auto initialShips = sim.state().ships.size();
    bool completed = false;
    for (int day=0;day<150 && !completed;++day) {
        sim.advanceDays(1);
        const double delivered = sim.state().freightPrograms.front().cargoDelivered;
        if (delivered < 250.0) require(sim.state().ships.size()==initialShips,"arrival or insufficient unloading cannot satisfy yard");
        else {
            require(sim.state().ships.size()==initialShips+1,"opening unload pays later same-day hull completion");
            near(sim.state().colonies.back().processedStockpile.get(ProcessedMaterial::StructuralAlloys),
                 delivered-250.0,"derived hull cost debited once from actual deliveries");
            completed = true;
        }
    }
    require(completed,"existing yard completed from deliveries");
    validateGameState(sim.state());
}

void test_processing_supply_and_retained_empty_return() {
    // Today's processing cannot fund today's opening. Tomorrow the original
    // intention loads the fifty units actually made from finite raw inventory.
    auto state = createDelegatedFreightScenario();
    auto& source = state.colonies.at(state.colonies.size()-2);
    source.processedStockpile.set(ProcessedMaterial::StructuralAlloys,0.0);
    source.processorCapacity=50.0;
    source.processingPolicy=ProcessingPolicy::Manual;
    source.manualProcessingAllocations={{ProcessedMaterial::StructuralAlloys,1.0}};
    source.stockpile.set(Mineral::Iron,50.0);
    source.stockpile.set(Mineral::Nickel,25.0);
    source.stockpile.set(Mineral::Titanium,12.5);
    state.ships.back().fuel=1000.0;
    Simulation production(state);
    auto fc=freight(state); fc.totalQuantity=50.0;
    require(production.execute(CreateFreightProgramCommand{fc}).ok,"waiting production-backed freight accepted");
    production.advanceDays(1);
    require(!production.state().ships.back().cargo,"later processing cannot retroactively supply opening");
    near(production.state().colonies.at(state.colonies.size()-2).processedStockpile.get(ProcessedMaterial::StructuralAlloys),50.0,
         "existing industry produces fifty real alloys");
    production.advanceDays(1);
    near(freightCargoAboard(production.state(),FreightProgramId{1}),50.0,"same intent loads real production next day");
    for (int i=0;i<100 && production.state().fleets.back().activeOrder.type==FleetOrderType::None;++i) production.advanceDays(1);
    for (int i=0;i<100;++i) {
        const auto& p=production.state().freightPrograms.front();
        if (p.task==FreightProgramTask::Return && production.state().fleets.back().activeOrder.type==FleetOrderType::MoveToBody) break;
        require(production.advanceDaysDetailed(1).advancedDays==1,"delivery reaches actual empty return transit");
    }
    const auto route=production.state().fleets.back().activeOrder;
    require(route.type==FleetOrderType::MoveToBody && !production.state().ships.back().cargo,"empty return is paid active transit");
    const double fuel=production.state().ships.back().fuel;
    require(production.execute(SuspendFreightProgramCommand{FreightProgramId{1}}).ok,"empty return suspends");
    require(production.execute(CancelFreightProgramCommand{FreightProgramId{1}}).ok,"cancel during empty return accepted");
    require(production.state().fleets.back().activeOrder.arrivalDay==route.arrivalDay,"empty return cancellation retains stored arrival");
    near(production.state().ships.back().fuel,fuel,"empty transit lifecycle does not charge or refund again");
    production.advanceDays(route.daysRemaining+1);
    require(production.state().fleets.back().currentBodyId==source.bodyId &&
            production.state().freightPrograms.front().lifecycle==FreightProgramLifecycle::Closed &&
            production.state().freightPrograms.front().closure==FreightProgramClosure::Cancelled,
            "cancelled empty leg physically arrives and closes at actual location");
    validateGameState(production.state());
}

void test_replacement_fleet_waits_for_committed_return() {
    // The next requested fleet is ready at source, but it cannot take over the
    // old shipment or start another pickup before the original empty return.
    auto state=createDelegatedFreightScenario();
    const auto sourceBody=state.colonies.at(state.colonies.size()-2).bodyId;
    state.fleets.front().currentBodyId=sourceBody;
    state.ships.front().shipClassId=state.ships.back().shipClassId;
    // The detached replacement fixture is a newly authored sensorless hull.
    state.ships.front().equipmentCondition.clear();
    initializeShipEquipmentCondition(state, state.ships.front());
    state.ships.front().fuel=1000.0;
    Simulation sim(state);
    auto fc=freight(state); fc.totalQuantity=200.0;
    require(sim.execute(CreateFreightProgramCommand{fc}).ok,"replacement proof starts");
    for (int i=0;i<100 && sim.state().freightPrograms.front().task!=FreightProgramTask::Outbound;++i) sim.advanceDays(1);
    require(sim.state().freightPrograms.front().task==FreightProgramTask::Outbound,"original fleet carries committed cargo");
    auto amendment=freightAmendmentFromCharter(fc);
    amendment.totalQuantity=300.0;
    amendment.requestedFleetId=state.fleets.front().id;
    require(sim.execute(AmendFreightProgramCommand{FreightProgramId{1},amendment}).ok,"new future fleet and larger target accepted");
    bool sawReplacement=false;
    for (int i=0;i<180;++i) {
        sim.advanceDays(1);
        const auto& program=sim.state().freightPrograms.front();
        if (program.shipment && program.shipment->number==2) {
            require(program.shipment->fleetId==state.fleets.front().id,"second shipment uses requested new fleet");
            require(sim.state().fleets.back().currentBodyId==sourceBody &&
                    sim.state().fleets.back().activeOrder.type==FleetOrderType::None && !sim.state().ships.back().cargo,
                    "new pickup begins only after original physical return");
            sawReplacement=true;
        }
        if (program.lifecycle==FreightProgramLifecycle::Closed) break;
    }
    require(sawReplacement && sim.state().freightPrograms.front().cargoDelivered==300.0,"amended future work completes using replacement");
    validateGameState(sim.state());
}
}

int main() {
    try {
        test_stable_merge_and_budget();
        test_six_kind_tie_and_technical_facility_budget();
        test_report_date_limit_stops_before_physical_tick();
        test_scheduled_closure_report_once();
        test_typed_arbitration_and_manual_guards();
        test_waiting_survey_uses_delivery_next_opening();
        test_survey_condition_and_report_name_freight_owner();
        test_older_freight_and_mixed_bulk_equivalence();
        test_inbound_stock_supplies_later_shipyard_phase();
        test_processing_supply_and_retained_empty_return();
        test_replacement_fleet_waits_for_committed_return();
        std::cout << "Mixed program control and stock phase tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
