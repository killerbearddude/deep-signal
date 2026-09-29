#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"

// Focused app-boundary checks: program views are owned values, charter advice
// accepts deliberate unready intent, and report buttons use global day marks.

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void programQueriesAndBoundaries() {
    deep::SimulationService service;
    deep::SimulationQueries queries{service};
    const auto initialTime = queries.surveyTime();
    require(initialTime.day == 0 && initialTime.nextThirtyDay == 30 && initialTime.nextNinetyDay == 90,
            "initial global boundaries");
    require(initialTime.daysUntilThirty == 30 && initialTime.daysUntilNinety == 90,
            "initial boundary controls request exact elapsed days");

    deep::SurveyProgramCharter charter;
    require(!queries.previewSurveyProgramCharter(charter).structurallyValid,
            "missing structural inputs stay invalid");
    const auto colonies = queries.colonies();
    const auto bodies = queries.strategicBodies();
    require(!colonies.empty() && bodies.size() >= 2, "default scenario supports a charter draft");
    charter.name = "Boundary Survey";
    charter.homeColonyId = colonies.front().id;
    charter.targets.push_back({.bodyId = bodies[1].id, .priority = 3, .requestedPasses = 2});
    const auto preview = queries.previewSurveyProgramCharter(charter);
    require(preview.structurallyValid && preview.waitingReasons.size() == 3,
            "missing optional assignments are accepted and explained");
    require(!preview.firstTargetChoiceReason.empty(), "target order is explained from known charter data");
    require(preview.executionCondition.find("no fleet requested") != std::string::npos,
            "draft feasibility outlook uses shared known-data program rules");

    deep::GameState altered = service.state();
    for (deep::MineralDeposit& deposit : altered.mineralDeposits) {
        deposit.remaining = 0.0;
        deposit.accessibility = 0.0;
    }
    deep::SimulationService alteredService{std::move(altered)};
    const auto alteredPreview = deep::SimulationQueries{alteredService}.previewSurveyProgramCharter(charter);
    require(alteredPreview.structurallyValid == preview.structurallyValid &&
            alteredPreview.waitingReasons == preview.waitingReasons &&
            alteredPreview.firstTargetChoiceReason == preview.firstTargetChoiceReason &&
            alteredPreview.executionCondition == preview.executionCondition,
            "charter advice does not use unobserved mineral quantities or accessibility");

    require(service.execute(deep::CreateSurveyProgramCommand{.charter = charter}).ok,
            "unready program is authorized");
    const auto snapshot = queries.surveyPrograms();
    require(snapshot.size() == 1 && snapshot.front().charter.name == "Boundary Survey",
            "query returns authorized program");
    require(snapshot.front().requestedVisits == 2 && snapshot.front().completedVisits == 0,
            "visit totals come from charter and receipts");
    require(snapshot.front().condition.find("Waiting") != std::string::npos,
            "condition explains why the unready program waits");

    charter.name = "Revised Survey";
    require(service.execute(deep::AmendSurveyProgramCommand{
        .programId = snapshot.front().id, .charter = charter
    }).ok, "charter amendment is accepted");
    require(queries.surveyPrograms().front().charter.name == "Revised Survey" &&
            snapshot.front().charter.name == "Boundary Survey",
            "returned DTO remains an owned snapshot across mutation");

    require(service.execute(deep::AdvanceDaysCommand{.days = 31}).ok,
            "unready waiting program does not block ordinary time");
    const auto later = queries.surveyTime();
    require(later.day == 31 && later.nextThirtyDay == 60 && later.nextNinetyDay == 90,
            "next boundaries are strictly future global multiples");
    require(later.daysUntilThirty == 29 && later.daysUntilNinety == 59,
            "boundary controls use the live absolute day");

    deep::GameState issueState = service.state();
    issueState.surveyPrograms.front().issue = deep::SurveyProgramIssue{
        .signature = "app:decision:1", .message = "Program decision pending", .acknowledged = false
    };
    deep::SimulationService blocked{std::move(issueState)};
    const auto dayBefore = blocked.state().date.day;
    require(blocked.advanceDays(30).empty() && blocked.state().date.day == dayBefore,
            "direct service advance shares the interruption boundary");
    const auto partial = blocked.advanceDaysDetailed(90);
    require(partial.interrupted && partial.advancedDays == 0 &&
            partial.issueProgramId == deep::ProgramController{blocked.state().surveyPrograms.front().id},
            "service exposes actual elapsed days and controlling issue");
    const auto commandResult = blocked.execute(deep::AdvanceDaysCommand{.days = 30});
    require(commandResult.ok && commandResult.message.find("Advanced 0 day(s); stopped") != std::string::npos,
            "command entry point reports the same accepted interruption");

    deep::GameState nearLimitState = deep::createHomeSystemScenario();
    nearLimitState.date.day = std::numeric_limits<std::int64_t>::max() - 5;
    deep::SimulationService nearLimit{std::move(nearLimitState)};
    const auto unavailable = deep::SimulationQueries{nearLimit}.surveyTime();
    require(!unavailable.nextThirtyDay && !unavailable.nextNinetyDay &&
            !unavailable.daysUntilThirty && !unavailable.daysUntilNinety,
            "unrepresentable future reporting boundaries disable controls without throwing");
}

void cancelledAmendedProgramKeepsPhysicalAssetLocation() {
    deep::SimulationService service{deep::createDelegatedSurveyScenario()};
    deep::SimulationQueries queries{service};
    const deep::GameState& initial = service.state();
    const deep::BodyId target = initial.bodies.at(initial.bodies.size() - 3U).id;
    const deep::FleetId fieldFleet = initial.fleets.back().id;
    const deep::SurveyTeamId fieldTeam = initial.surveyTeams.front().id;
    deep::SurveyProgramCharter charter;
    charter.name = "Physical disposition";
    charter.homeColonyId = initial.colonies.back().id;
    charter.requestedFleetId = fieldFleet;
    charter.requestedTeamId = fieldTeam;
    charter.requestedLeaderId = initial.people.at(2).id;
    charter.targets.push_back({.bodyId = target, .priority = 1, .requestedPasses = 1});
    require(service.execute(deep::CreateSurveyProgramCommand{.charter = charter}).ok,
            "ready fixture program is authorized");
    const deep::SurveyProgramId programId = service.state().surveyPrograms.back().id;

    for (int day = 0; day < 15; ++day) {
        const deep::Fleet& fleet = service.state().fleets.back();
        if (fleet.currentBodyId == target && fleet.activeOrder.type == deep::FleetOrderType::None) break;
        require(service.execute(deep::AdvanceDaysCommand{.days = 1}).ok,
                "program can reach its first target");
    }
    const deep::Fleet& arrived = service.state().fleets.back();
    require(arrived.currentBodyId == target && arrived.activeOrder.type == deep::FleetOrderType::None,
            "field fleet is stationary at the target before cancellation");
    require(service.state().surveyPrograms.back().taskFleetId == fieldFleet &&
            service.state().surveyPrograms.back().taskTeamId == fieldTeam,
            "committed task retains actual asset identities");

    charter.requestedFleetId.reset();
    charter.requestedTeamId.reset();
    require(service.execute(deep::AmendSurveyProgramCommand{
        .programId = programId, .charter = charter
    }).ok, "amendment can remove future requested assets during committed work");
    require(service.execute(deep::CancelSurveyProgramCommand{.programId = programId}).ok,
            "stationary cancellation releases the lease");
    const auto summaries = queries.surveyPrograms();
    require(summaries.size() == 1 && !summaries.front().leasedFleetId && !summaries.front().leasedTeamId,
            "cancelled program has no active lease");
    require(summaries.front().currentLocationName.find("P3A Target 1") != std::string::npos &&
            summaries.front().currentLocationName.find("Former task fleet") != std::string::npos &&
            summaries.front().teamLocationName.find("P3A Survey Fleet") != std::string::npos,
            "disposition names the actual field assets and their location after requested IDs change");
}

} // namespace

int main() {
    try {
        programQueriesAndBoundaries();
        cancelledAmendedProgramKeepsPhysicalAssetLocation();
        std::cout << "Survey program app queries: 2 scenarios passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Survey program app query failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
