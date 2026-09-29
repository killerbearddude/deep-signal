// P3C independent ten-duty/six-pass proof through actual commands and workdays.
// Literal consumption expectations prevent a shared preview/executor error
// from declaring fabricated repairs or double-counted final workdays correct.
#include "sim/EquipmentServiceRules.h"
#include "sim/GameStateValidation.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) { require(std::abs(a - b) < 1e-8, why); }
deep::MaintenanceProgramCharter supportFor(const deep::GameState& s) {
    deep::MaintenanceProgramCharter c;
    c.name = "Instrument support";
    c.serviceColonyId = s.colonies.back().id;
    c.requestedTenderId = s.fleets.back().id;
    c.requestedTeamId = s.maintenanceTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.clients = {s.fleets.front().id};
    return c;
}
deep::SurveyProgramCharter surveyFor(const deep::GameState& s, int passes = 6) {
    deep::SurveyProgramCharter c;
    c.name = "Supported client";
    c.homeColonyId = s.colonies.back().id;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, passes}};
    return c;
}
void authorizePair(deep::Simulation& sim) {
    require(sim.execute(deep::CreateMaintenanceProgramCommand{supportFor(sim.state())}).ok,
            "provider authorized");
    auto c = surveyFor(sim.state());
    c.policy.maintenanceProgramId = sim.state().maintenancePrograms.front().id;
    require(sim.execute(deep::CreateSurveyProgramCommand{c}).ok, "survey selects provider");
}
void step(deep::Simulation& sim) {
    auto result = sim.advanceDaysDetailed(1);
    if (result.advancedDays != 1 || result.interrupted)
        throw std::runtime_error("unexpected interruption: " + result.stopReason);
    deep::validateGameState(sim.state());
}
template <class Predicate> void until(deep::Simulation& sim, Predicate predicate) {
    for (int i = 0; i < 300 && !predicate(sim.state()); ++i)
        step(sim);
    require(predicate(sim.state()), "expected bounded phase reached");
}
void repeated_service() {
    using namespace deep;
    Simulation sim(createTenderMaintenanceScenario());
    MaintenanceProgramCharter support;
    support.name = "Standard instrument support";
    support.serviceColonyId = sim.state().colonies.back().id;
    support.requestedTenderId = sim.state().fleets.back().id;
    support.requestedTeamId = sim.state().maintenanceTeams.front().id;
    support.requestedLeaderId = sim.state().people.front().id;
    support.clients = {sim.state().fleets.front().id};
    require(sim.execute(CreateMaintenanceProgramCommand{support}).ok, "support charter authorizes");
    SurveyProgramCharter survey;
    survey.name = "Six passes with ten-duty instruments";
    survey.homeColonyId = support.serviceColonyId;
    survey.requestedFleetId = sim.state().fleets.front().id;
    survey.requestedTeamId = sim.state().surveyTeams.front().id;
    survey.requestedLeaderId = sim.state().people.front().id;
    survey.targets = {{sim.state().bodies.back().id, 0, 6}};
    survey.policy.maintenanceProgramId = sim.state().maintenancePrograms.front().id;
    require(sim.execute(CreateSurveyProgramCommand{survey}).ok, "client survey authorizes");
    for (int day = 0;
         day < 300 && sim.state().surveyPrograms.front().lifecycle != SurveyProgramLifecycle::Closed; ++day) {
        const auto result = sim.advanceDaysDetailed(1);
        if (result.interrupted)
            throw std::runtime_error("unexpected issue: " + result.stopReason);
        validateGameState(sim.state());
    }
    const auto& p = sim.state().maintenancePrograms.front();
    const auto& client = sim.state().surveyPrograms.front();
    require(client.lifecycle == SurveyProgramLifecycle::Closed && client.receipts.size() == 6,
            "six passes and physical return finish");
    require(p.jobs.size() == 2 && p.receipts.size() == 4,
            "two complete jobs each require two actual service days");
    near(p.restoredDuty, 20.0, "twenty physical instrument-duty units restored");
    near(p.teamWorkdays, 4.0, "four real engineering team-workdays consumed");
    near(p.consumed.get(ProcessedMaterial::Electronics), 10.0, "ten actual Electronics consumed");
    near(p.consumed.get(ProcessedMaterial::IndustrialComposites), 10.0, "ten actual Composites consumed");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 10.0,
         "last completed pass retains wear without unsolicited overhaul");
    int dutyEvents = 0;
    for (const auto& event : sim.state().eventLog)
        if (const auto* used = std::get_if<EquipmentDutyUsedEvent>(&event.payload)) {
            near(used->duty, 1.0, "timed work consumes exactly one duty");
            ++dutyEvents;
        }
    require(dutyEvents == 30, "final workday and zero-information publication never double-charge duty");
}

void partial_service_stops_and_policy_withdrawal() {
    // P3C19/25/26/27: crossing the trigger cannot end an active full-service
    // job, while explicit commands can withdraw it without undoing real work.
    using namespace deep;
    auto state = createTenderMaintenanceScenario();
    state.ships.front().equipmentCondition.front().usedDuty = 10;
    Simulation sim(state);
    authorizePair(sim);
    step(sim);
    require(sim.state().maintenancePrograms.front().receipts.empty(),
            "new client lease cannot service in its acquisition opening");
    step(sim);
    const auto provider = sim.state().maintenancePrograms.front().id;
    const auto client = sim.state().surveyPrograms.front().id;
    require(activeServiceJob(sim.state().maintenancePrograms.front()), "first restoration retains full job");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 5,
         "first step restores five duty only");
    require(sim.state().fleets.front().activeOrder.type == FleetOrderType::None,
            "client cannot depart after early same-opening repair");
    const double consumed =
        sim.state().maintenancePrograms.front().consumed.get(ProcessedMaterial::Electronics);
    require(sim.execute(SuspendMaintenanceProgramCommand{provider}).ok, "provider can stop partial repair");
    require(!activeServiceJob(sim.state().maintenancePrograms.front()) &&
                !sim.state().maintenancePrograms.front().leasedTenderId,
            "provider suspension withdraws job before releasing actual resources");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 5,
         "suspension does not erase actual restoration");
    near(sim.state().maintenancePrograms.front().consumed.get(ProcessedMaterial::Electronics), consumed,
         "parts are not refunded");
    // Raising the client's trigger keeps its remaining wear requested after the
    // explicitly interrupted job. Resume uses that actual remaining debt.
    auto charter = sim.state().surveyPrograms.front().charter;
    charter.policy.remainingDutyTrigger = 0.6;
    require(sim.execute(AmendSurveyProgramCommand{client, charter}).ok,
            "partial debt can remain explicitly requested");
    step(sim);
    require(sim.state().fleets.front().activeOrder.type == FleetOrderType::None,
            "unready selected provider holds due client");
    require(sim.execute(ResumeMaintenanceProgramCommand{provider}).ok, "provider resume accepted");
    step(sim);
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 0,
         "fresh job repairs remaining five rather than originalten");
    require(sim.state().maintenancePrograms.front().jobs.size() == 2,
            "resume creates new provenance episode");
    near(sim.state().maintenancePrograms.front().teamWorkdays, 2,
         "resumption cannot charge prior work twice");
    require(sim.state().fleets.front().activeOrder.type == FleetOrderType::None,
            "completion holds client until next opening");
    step(sim);
    require(sim.state().fleets.front().activeOrder.type == FleetOrderType::MoveToBody,
            "next opening permits actual sortie");

    for (int action = 0; action < 3; ++action) {
        Simulation paused(state);
        authorizePair(paused);
        step(paused);
        step(paused);
        auto policy = paused.state().surveyPrograms.front().charter;
        if (action == 0)
            require(paused.execute(SuspendSurveyProgramCommand{SurveyProgramId{1}}).ok,
                    "client suspension allowed");
        if (action == 1)
            require(paused.execute(CancelSurveyProgramCommand{SurveyProgramId{1}}).ok,
                    "client cancellation allowed");
        if (action == 2) {
            policy.policy.maintenanceProgramId.reset();
            require(paused.execute(AmendSurveyProgramCommand{SurveyProgramId{1}, policy}).ok,
                    "client can remove preventive support");
        }
        require(!activeServiceJob(paused.state().maintenancePrograms.front()),
                "client stop/policy change withdraws stale service claim");
        near(paused.state().ships.front().equipmentCondition.front().usedDuty, 5,
             "withdrawal is not free healing");
        validateGameState(paused.state());
    }
    Simulation replacement(state);
    authorizePair(replacement);
    step(replacement);
    step(replacement);
    const auto original = replacement.state().maintenancePrograms.front().jobs.front().leaderId;
    auto amend = maintenanceAmendmentFromCharter(replacement.state().maintenancePrograms.front().charter);
    amend.requestedLeaderId = replacement.state().people.at(1).id;
    require(replacement.execute(AmendMaintenanceProgramCommand{MaintenanceProgramId{1}, amend}).ok,
            "leader replacement accepted");
    require(!activeServiceJob(replacement.state().maintenancePrograms.front()) &&
                replacement.state().maintenancePrograms.front().jobs.front().leaderId == original,
            "replacement does not rewrite work provenance");
}

void exhausted_partial_visit_detour_and_return_fuel() {
    // P3C29/30: deliberate no-support operation exhausts a partial field pass.
    // A later policy amendment returns physically, services, and resumes it.
    using namespace deep;
    auto state = createTenderMaintenanceScenario();
    state.ships.front().equipmentCondition.front().usedDuty = 8;
    Simulation sim(state);
    require(sim.execute(CreateMaintenanceProgramCommand{supportFor(state)}).ok, "detour provider accepted");
    require(sim.execute(CreateSurveyProgramCommand{surveyFor(state, 1)}).ok,
            "unassisted duty-limited survey accepted");
    const auto result = sim.advanceDaysDetailed(100);
    require(result.interrupted && sim.state().surveyPrograms.front().workDaysCompleted == 2,
            "exhaustion stops after two real fieldwork days");
    const auto original = sim.state().surveyPrograms.front();
    auto charter = original.charter;
    charter.policy.maintenanceProgramId = MaintenanceProgramId{1};
    auto blocked = sim.state();
    blocked.ships.front().fuel = 0;
    Simulation noFuel(blocked);
    require(noFuel.execute(AmendSurveyProgramCommand{original.id, charter}).ok,
            "unsupported return remains accepted intent");
    const auto stopped = noFuel.advanceDaysDetailed(5);
    require(stopped.interrupted && noFuel.state().ships.front().fuel == 0 &&
                noFuel.state().fleets.front().activeOrder.type == FleetOrderType::None,
            "maintenance cannot fabricate return fuel or use manual cancellation teleport");
    require(sim.execute(AmendSurveyProgramCommand{original.id, charter}).ok,
            "support amendment enables same retained visit recovery");
    step(sim);
    require(sim.state().surveyPrograms.front().maintenanceReturn &&
                sim.state().fleets.front().activeOrder.type == FleetOrderType::MoveToBody,
            "maintenance return uses real paid transit");
    require(sim.state().surveyPrograms.front().taskBodyId == original.taskBodyId &&
                sim.state().surveyPrograms.front().firstWorkDay == original.firstWorkDay &&
                sim.state().surveyPrograms.front().workDaysCompleted == 2,
            "detour retains original partial work and identities");
    until(sim,
          [](const auto& s) { return s.surveyPrograms.front().lifecycle == SurveyProgramLifecycle::Closed; });
    require(sim.state().surveyPrograms.front().receipts.size() == 1 &&
                sim.state().surveyPrograms.front().totalWorkDays == 5,
            "resumption earns only three remaining days and produces one receipt");
    require(sim.state().observations.size()==1 && sim.state().observations.front().workDates.size()==5 &&
        sim.state().observations.front().instruments.front().exposure.workdays==5 &&
        sim.state().observations.front().workDates[2]>sim.state().observations.front().workDates[1]+1,
        "P4A exposure retains real interrupted fieldwork through physical service detour");
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 3,
         "initial8 plus actual5 minus restored10 equals3");
    std::int64_t arrivedHome = -1;
    for (const auto& event : sim.state().eventLog)
        if (const auto* arrival = std::get_if<FleetArrivedEvent>(&event.payload);
            arrival && arrival->fleetId == state.fleets.front().id &&
            arrival->destinationBodyId == state.colonies.back().bodyId) {
            arrivedHome = event.day;
            break;
        }
    require(arrivedHome >= 0 &&
                sim.state().maintenancePrograms.front().receipts.front().day == arrivedHome + 1,
            "maintenance-return arrival cannot receive service before the next opening");
}

void early_provider_completion_and_design_envelope() {
    // P3C19/20: provider is older, so its final repair executes BEFORE the
    // survey's head. Frozen opening holds prevent same-day refuel or departure.
    using namespace deep;
    auto state = createTenderMaintenanceScenario();
    // Three remaining duty is above the 25% trigger but below a full pass.
    state.ships.front().equipmentCondition.front().usedDuty = 7;
    state.ships.front().fuel = 0;
    Simulation sim(state);
    require(sim.execute(CreateMaintenanceProgramCommand{supportFor(state)}).ok, "older provider accepted");
    step(sim);
    auto survey = surveyFor(state, 1);
    survey.policy.maintenanceProgramId = MaintenanceProgramId{1};
    require(sim.execute(CreateSurveyProgramCommand{survey}).ok, "later worn client accepted");
    step(sim);
    step(sim);
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 2,
         "older provider first service step");
    near(sim.state().surveyPrograms.front().fuelLoaded, 0, "due preventive policy precedes refueling");
    step(sim);
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 0,
         "older provider completes full job");
    near(sim.state().surveyPrograms.front().fuelLoaded, 0,
         "early repair cannot enable later same-opening refueling");
    require(sim.state().fleets.front().activeOrder.type == FleetOrderType::None,
            "early repair cannot enable later same-opening launch");
    step(sim);
    require(sim.state().surveyPrograms.front().fuelLoaded > 0, "following opening can refuel normally");

    auto shortLife = createTenderMaintenanceScenario();
    shortLife.shipComponents.at(3).serviceProfile->dutyCapacity = 3;
    Simulation limited(shortLife);
    authorizePair(limited);
    limited.advanceDays(20);
    require(limited.state().maintenancePrograms.front().jobs.empty() &&
                limited.state().surveyPrograms.front().totalWorkDays == 0,
            "fresh short-life design never creates an infinite zero-work repair loop");
    auto unassisted = limited.state().surveyPrograms.front().charter;
    unassisted.policy.maintenanceProgramId.reset();
    require(limited.execute(AmendSurveyProgramCommand{SurveyProgramId{1}, unassisted}).ok,
            "player may remove preventive support");
    const auto stopped = limited.advanceDaysDetailed(100);
    require(stopped.interrupted && limited.state().surveyPrograms.front().workDaysCompleted == 3,
            "without support the actual three available duty days remain usable");
    near(limited.state().ships.front().equipmentCondition.front().usedDuty, 3,
         "removing policy never heals instruments");
}

void groups_compatibility_and_pinned_workshop() {
    // A partially compatible full job retains unresolved groups. Surplus team
    // capacity never starts a second group on the same opening day.
    using namespace deep;
    auto state = createTenderMaintenanceScenario();
    state.shipClasses.front().components.push_back({ShipComponentId{7}, 1});
    state.ships.front().equipmentCondition.clear();
    initializeShipEquipmentCondition(state, state.ships.front());
    state.ships.front().equipmentCondition[0].usedDuty = 1;
    state.ships.front().equipmentCondition[1].usedDuty = 10;
    state.shipClasses.back().components.push_back({ShipComponentId{9}, 1});
    state.maintenanceTeams.front().qualifiedFamilies = {EquipmentFamilyId{1}, EquipmentFamilyId{2}};
    Simulation groups(state);
    authorizePair(groups);
    step(groups);
    step(groups);
    near(groups.state().ships.front().equipmentCondition[0].usedDuty, 0, "first one-duty group finishes");
    near(groups.state().ships.front().equipmentCondition[1].usedDuty, 10,
         "unused first-group capacity does not repair second group today");
    near(groups.state().maintenancePrograms.front().teamWorkdays, 0.2,
         "only actual first-group labor consumed");
    step(groups);
    near(groups.state().ships.front().equipmentCondition[1].usedDuty, 5,
         "specialist group starts on later opening");
    require(groups.state().maintenancePrograms.front().receipts.back().componentId == ShipComponentId{7},
            "family-specific second group has exact receipt identity");

    auto incompatible = state;
    incompatible.maintenanceTeams.front().qualifiedFamilies = {EquipmentFamilyId{1}};
    Simulation lacking(incompatible);
    authorizePair(lacking);
    lacking.advanceDays(6);
    near(lacking.state().ships.front().equipmentCondition[1].usedDuty, 10,
         "matching workshop without qualified team cannot service specialist array");
    require(activeServiceJob(lacking.state().maintenancePrograms.front()),
            "unresolved incompatible group is not discarded or markedcomplete");
    incompatible = state;
    incompatible.shipClasses.back().components.pop_back();
    Simulation noWorkshop(incompatible);
    authorizePair(noWorkshop);
    noWorkshop.advanceDays(6);
    near(noWorkshop.state().ships.front().equipmentCondition[1].usedDuty, 10,
         "qualified team without matching workshop cannot service specialist array");

    auto pinnedState = createTenderMaintenanceScenario();
    pinnedState.ships.front().equipmentCondition.front().usedDuty = 10;
    auto spare = pinnedState.ships.back();
    spare.id = ShipId{pinnedState.ids.nextShipId++};
    spare.name = "Second powered workshop hull";
    pinnedState.ships.push_back(spare);
    pinnedState.fleets.back().shipIds.push_back(spare.id);
    Simulation pinned(pinnedState);
    authorizePair(pinned);
    step(pinned);
    step(pinned);
    const auto originalWorkshop = pinned.state().maintenancePrograms.front().jobs.back().workshopShipId;
    require(originalWorkshop == pinnedState.fleets.back().shipIds.front(),
            "first eligible workshop hull is pinned in persisted roster order");
    auto impaired = pinned.state();
    auto powerless = impaired.shipClasses.back();
    powerless.id = ShipClassId{impaired.ids.nextShipClassId++};
    powerless.name = "Detached power-loss fixture";
    std::erase_if(powerless.components, [](const auto& r) { return r.componentId == ShipComponentId{2}; });
    impaired.shipClasses.push_back(powerless);
    impaired.ships.at(1).shipClassId = powerless.id;
    Simulation waiting(impaired);
    const auto interrupted = waiting.advanceDaysDetailed(5);
    require(interrupted.interrupted &&
                waiting.state().maintenancePrograms.front().jobs.back().workshopShipId == originalWorkshop,
            "started group waits on its original impaired workshop and raises changed-equipment issue");
    near(waiting.state().ships.front().equipmentCondition.front().usedDuty, 5,
         "another powered hull cannot silently take over pinned group");
}
void powerless_client_and_fractional_group_completion() {
    // Three arrays exceed client power, but an external powered workshop can
    // still restore their condition. Thirds must finish without stranded dust.
    using namespace deep;
    auto state = createTenderMaintenanceScenario();
    state.shipClasses.front().components.at(3).quantity = 3;
    state.ships.front().equipmentCondition.front().usedDuty = 10;
    Simulation sim(state);
    authorizePair(sim);
    auto limits = maintenanceAmendmentFromCharter(sim.state().maintenancePrograms.front().charter);
    limits.policy.lifetimeAllowances = ProcessedMaterialSet{};
    limits.policy.lifetimeAllowances->set(ProcessedMaterial::Electronics, 15);
    limits.policy.lifetimeAllowances->set(ProcessedMaterial::IndustrialComposites, 15);
    require(sim.execute(AmendMaintenanceProgramCommand{MaintenanceProgramId{1}, limits}).ok,
            "exact full-service recipe allowance accepted");
    sim.advanceDays(12);
    near(sim.state().ships.front().equipmentCondition.front().usedDuty, 0,
         "fractional quantity-scaled service finishes exactly at zero duty debt");
    const auto& provider = sim.state().maintenancePrograms.front();
    require(provider.jobs.size() == 1 && provider.jobs.front().outcome == ServiceJobOutcome::Completed &&
                provider.receipts.size() == 6,
            "three units consume six daily team actions without an extra rounding-residue job");
    near(provider.teamWorkdays, 6, "N3 labor is six real team-workdays");
    near(provider.consumed.get(ProcessedMaterial::Electronics), 15, "N3 materials are fifteen actual units");
    require(sim.state().surveyPrograms.front().totalWorkDays == 0 &&
                prepareSurveyDuty(sim.state(), sim.state().fleets.front(), 1).poweredCapability == 0,
            "service repairs duty but does not cure the client's power deficit");
    validateGameState(sim.state());
}
} // namespace
int main() {
    try {
        repeated_service();
        partial_service_stops_and_policy_withdrawal();
        exhausted_partial_visit_detour_and_return_fuel();
        early_provider_completion_and_design_envelope();
        groups_compatibility_and_pinned_workshop();
        powerless_client_and_fractional_group_completion();
        std::cout << "Maintenance execution tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
