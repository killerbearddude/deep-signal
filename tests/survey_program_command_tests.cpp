#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/TransitPlanning.h"

// Command-boundary P3A checks: durable unready intent, ordinary rejection
// effects, exclusive fleet control, and stop requests that preserve transit.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error{message};
}

deep::SurveyProgramCharter readyCharter(const deep::GameState& state, const deep::BodyId target) {
    const auto home = std::find_if(state.colonies.begin(), state.colonies.end(),
        [](const deep::Colony& row) { return row.name == "P3A Survey Base"; });
    require(home != state.colonies.end(), "dedicated home exists");
    return deep::SurveyProgramCharter{
        .name = "Delegated survey proof",
        .homeColonyId = home->id,
        .requestedFleetId = state.fleets.back().id,
        .requestedLeaderId = state.people.at(2).id,
        .requestedTeamId = state.surveyTeams.front().id,
        .targets = {{target, 3, 1}},
        .policy = {}
    };
}

void testUnreadyIntentAndReport() {
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const auto charter = deep::SurveyProgramCharter{
        .name = "Unready but authorized",
        .homeColonyId = sim.state().colonies.front().id,
        .targets = {{sim.state().bodies.back().id, 2, 1}}
    };
    require(sim.execute(deep::CreateSurveyProgramCommand{charter}).ok,
            "well-formed program accepts absent requested assignments");
    require(sim.state().surveyPrograms.size() == 1 &&
            !sim.state().surveyPrograms.front().leasedFleetId.has_value() &&
            sim.state().surveyPrograms.front().receipts.empty(),
            "authorization creates durable intent without phantom work or lease");
    const auto result = sim.advanceDaysDetailed(30);
    require(result.advancedDays == 30 && !result.interrupted &&
            sim.state().surveyPrograms.front().reports.size() == 1 &&
            sim.state().surveyPrograms.front().reports.front().endDay == 30,
            "routine unready condition does not block time or due reporting");
    const auto rejected = std::count_if(sim.state().eventLog.begin(), sim.state().eventLog.end(),
        [](const deep::SimEvent& event) { return std::holds_alternative<deep::CommandRejectedEvent>(event.payload); });
    require(rejected == 0, "waiting program emits no daily rejection spam");
}

void testRejectedAmendmentAndExclusiveFleetControl() {
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    const auto target = sim.state().bodies.back().id;
    const auto charter = readyCharter(sim.state(), target);
    require(sim.execute(deep::CreateSurveyProgramCommand{charter}).ok, "ready charter is authorized");
    const auto programId = sim.state().surveyPrograms.front().id;
    auto invalid = charter;
    invalid.targets.push_back(invalid.targets.front());
    const auto priorCounter = sim.state().ids.nextSurveyProgramId;
    const auto priorRevision = sim.state().surveyPrograms.front().charterRevision;
    require(!sim.execute(deep::AmendSurveyProgramCommand{programId, invalid}).ok,
            "duplicate target amendment rejects");
    require(sim.state().surveyPrograms.front().charterRevision == priorRevision &&
            sim.state().surveyPrograms.front().charter.targets.size() == 1 &&
            sim.state().ids.nextSurveyProgramId == priorCounter,
            "rejected amendment leaves charter and entity IDs unchanged");

    sim.advanceDays(1); // Acquire assets and transfer real home fuel.
    const auto fleetId = charter.requestedFleetId.value();
    require(sim.state().surveyPrograms.front().leasedFleetId == fleetId,
            "ready program obtains actual fleet lease");
    const auto targetId = charter.targets.front().bodyId;
    for (const auto& command : {
             deep::SimCommand{deep::MoveFleetCommand{fleetId, targetId}},
             deep::SimCommand{deep::QueueFleetMoveOrderCommand{fleetId, targetId}},
             deep::SimCommand{deep::ClearFleetOrderQueueCommand{fleetId}},
             deep::SimCommand{deep::CancelFleetOrderCommand{fleetId}},
             deep::SimCommand{deep::ResourceSurveyCommand{fleetId, targetId}}
         }) {
        const auto result = sim.execute(command);
        require(!result.ok && result.message.find("controlled by survey program") != std::string::npos,
                "manual fleet mutation reports controlling program");
    }
    require(sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None &&
            sim.state().fleets.back().queuedOrders.empty(),
            "rejected manual commands do not mutate leased fleet");
}

void testSuspendAndCancelPreserveCommittedTransit() {
    for (const bool cancel : {false, true}) {
        deep::GameState state = deep::createDelegatedSurveyScenario();
        const deep::BodyId target = state.bodies.back().id;
        state.bodies.back().x = 20.0; // A multi-day fixture route under retained physics.
        deep::Simulation sim{std::move(state)};
        const auto charter = readyCharter(sim.state(), target);
        require(sim.execute(deep::CreateSurveyProgramCommand{charter}).ok,
                "program accepts distant fixed target");
        const auto programId = sim.state().surveyPrograms.front().id;
        sim.advanceDays(1); // Refill only.
        sim.advanceDays(1); // Depart.
        const auto beforeStop = sim.state().fleets.back().activeOrder;
        require(beforeStop.type == deep::FleetOrderType::MoveToBody && beforeStop.daysRemaining > 1,
                "fixture provides a committed multi-day outbound transit");
        const auto fuelAfterDeparture = sim.state().ships.back().fuel;
        const auto stopped = cancel
            ? sim.execute(deep::CancelSurveyProgramCommand{programId})
            : sim.execute(deep::SuspendSurveyProgramCommand{programId});
        require(stopped.ok && sim.state().fleets.back().activeOrder.arrivalDay == beforeStop.arrivalDay &&
                sim.state().ships.back().fuel == fuelAfterDeparture &&
                sim.state().surveyPrograms.front().leasedFleetId == charter.requestedFleetId,
                "stop records intent while preserving active route, fuel debit, and lease");
        sim.advanceDays(beforeStop.daysRemaining);
        require(sim.state().fleets.back().currentBodyId == target &&
                sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None &&
                sim.state().surveyPrograms.front().receipts.empty() &&
                !sim.state().surveyPrograms.front().leasedFleetId.has_value(),
                "arrival releases lease at real remote location without extra survey");
        require(sim.state().surveyTeams.front().locationKind == deep::SurveyTeamLocationKind::Fleet &&
                sim.state().surveyTeams.front().fleetId == charter.requestedFleetId,
                "remote team remains aboard rather than teleporting home");
        require(sim.state().surveyPrograms.front().lifecycle ==
                    (cancel ? deep::SurveyProgramLifecycle::Closed : deep::SurveyProgramLifecycle::Suspended),
                "cancel closes and suspension pauses after safe arrival");
    }
}

void testEverySimulationAdvancePathStopsAtPendingIssue() {
    deep::Simulation setup{deep::createHomeSystemScenario()};
    require(setup.execute(deep::CreateSurveyProgramCommand{deep::SurveyProgramCharter{
        .name = "Issue boundary proof",
        .homeColonyId = setup.state().colonies.front().id,
        .targets = {{setup.state().bodies.back().id, 1, 1}}
    }}).ok, "program exists before issue-boundary proof");
    deep::GameState pending = setup.state();
    pending.surveyPrograms.front().issue = deep::SurveyProgramIssue{
        .signature = "proof:authorization:1",
        .message = "Fuel authorization decision is pending",
        .acknowledged = false
    };
    deep::Simulation sim{std::move(pending)};
    const auto detailed = sim.advanceDaysDetailed(90);
    require(detailed.interrupted && detailed.advancedDays == 0 &&
            detailed.issueProgramId == deep::ProgramController{sim.state().surveyPrograms.front().id} &&
            sim.state().date.day == 0,
            "detailed advancement cannot bypass an unresolved issue");
    require(sim.advanceDays(5).empty() && sim.state().date.day == 0,
            "legacy event-vector advance delegates to the same interruption boundary");
    const auto command = sim.execute(deep::AdvanceDaysCommand{30});
    require(command.ok && command.message.find("Advanced 0 day(s); stopped") != std::string::npos &&
            sim.state().date.day == 0,
            "AdvanceDaysCommand truthfully reports zero elapsed days as accepted interruption");
    require(sim.execute(deep::AcknowledgeSurveyProgramIssueCommand{
        .programId = sim.state().surveyPrograms.front().id,
        .signature = "proof:authorization:1"
    }).ok, "acknowledgment records a choice without relaxing the charter");
    const auto resumed = sim.advanceDaysDetailed(1);
    require(resumed.advancedDays == 1 && !resumed.interrupted,
            "same world clock resumes after the identified issue is acknowledged");
}

void testQuiescentAssetAmendmentReleasesOldLease() {
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    auto charter = readyCharter(sim.state(), sim.state().bodies.back().id);
    require(sim.execute(deep::CreateSurveyProgramCommand{charter}).ok,
            "quiescent amendment proof authorizes a ready program");
    const auto programId = sim.state().surveyPrograms.front().id;
    sim.advanceDays(1); // Real refill holds a lease, with no committed task yet.
    require(sim.state().surveyPrograms.front().leasedTeamId == charter.requestedTeamId &&
            sim.state().surveyPrograms.front().task == deep::SurveyProgramTask::None,
            "first refill leaves a quiescent leased planning boundary");
    charter.requestedTeamId.reset();
    require(sim.execute(deep::AmendSurveyProgramCommand{programId, charter}).ok,
            "amendment records removal of future team request");
    sim.advanceDays(1);
    require(!sim.state().surveyPrograms.front().leasedFleetId.has_value() &&
            !sim.state().surveyPrograms.front().leasedTeamId.has_value() &&
            sim.state().surveyTeams.front().locationKind == deep::SurveyTeamLocationKind::Colony &&
            sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "quiescent amendment releases old assets before another sortie");
}

void testExclusiveArbitrationAndManualQueuePreservation() {
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    auto first = readyCharter(sim.state(), sim.state().bodies.back().id);
    auto second = first;
    first.name = "First in program vector";
    second.name = "Second requester";
    require(sim.execute(deep::CreateSurveyProgramCommand{first}).ok &&
            sim.execute(deep::CreateSurveyProgramCommand{second}).ok,
            "two programs may request the same finite fleet/team");
    const auto firstId = sim.state().surveyPrograms.front().id;
    sim.advanceDays(1);
    require(sim.state().surveyPrograms.front().leasedFleetId == first.requestedFleetId &&
            !sim.state().surveyPrograms.at(1).leasedFleetId.has_value(),
            "program vector order grants exactly one actual lease");
    require(sim.execute(deep::SuspendSurveyProgramCommand{firstId}).ok,
            "first program can safely release a stationary lease");
    sim.advanceDays(1);
    require(!sim.state().surveyPrograms.front().leasedFleetId.has_value() &&
            sim.state().surveyPrograms.at(1).leasedFleetId == second.requestedFleetId,
            "waiting second program acquires next day without stealing");

    deep::GameState queued = deep::createDelegatedSurveyScenario();
    const auto destination = queued.bodies.back().id;
    queued.fleets.back().queuedOrders.push_back(deep::QueuedFleetOrder{
        .type = deep::FleetOrderType::MoveToBody, .targetBodyId = destination
    });
    deep::Simulation blocked{std::move(queued)};
    const auto requested = readyCharter(blocked.state(), destination);
    require(blocked.execute(deep::CreateSurveyProgramCommand{requested}).ok,
            "existing manual queue is a readiness condition, not invalid intent");
    blocked.advanceDays(1);
    require(!blocked.state().surveyPrograms.front().leasedFleetId.has_value() &&
            blocked.state().fleets.back().queuedOrders.size() == 1 &&
            blocked.state().fleets.back().queuedOrders.front().targetBodyId == destination,
            "program waits without deleting or reordering prior manual queue");
}

} // namespace

int main() {
    try {
        testUnreadyIntentAndReport();
        testRejectedAmendmentAndExclusiveFleetControl();
        testSuspendAndCancelPreserveCommittedTransit();
        testEverySimulationAdvancePathStopsAtPendingIssue();
        testQuiescentAssetAmendmentReleasesOldLease();
        testExclusiveArbitrationAndManualQueuePreservation();
        std::cout << "Survey program command tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Survey program command test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
