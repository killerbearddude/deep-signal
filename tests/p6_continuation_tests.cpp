// Save/Load and time-chunk branches start at command-earned cross-system states.
// Full persisted-column comparison is independent of SaveGameRepository's SQL
// insertion order; live state checks cover physical quantities and decisions.
#include "app/P6ProvingFixture.h"
#include "app/SiteDevelopmentFixture.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"
#include "support/P6StateFingerprint.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace deep;
using Checkpoint = std::pair<std::string, GameState>;
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void accept(Simulation& sim, const SimCommand& command) {
    const auto result = sim.execute(command);
    require(result.ok, "P6 continuation command rejected: " + result.message);
}
void step(Simulation& sim) {
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1 && !result.interrupted,
            "P6 checkpoint preparation interrupted: " + result.stopReason);
    validateGameState(sim.state());
}
template <class Predicate> void until(Simulation& sim, Predicate done, int limit = 80) {
    for (int day = 0; day < limit && !done(); ++day)
        step(sim);
    require(done(), "P6 command-earned checkpoint did not occur in bounded time");
}

GameState waitingOrder() {
    auto state = createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 0;
    Simulation sim(state);
    accept(sim, AssignShipyardBuildCommand{state.colonies.front().id, state.shipClasses.front().id, 1});
    step(sim);
    require(sim.state().shipyardOrders.front().accumulatedBuildPoints == 0,
            "pending P1 order has no fabricated work");
    return sim.state();
}

GameState paidSurveyTransit() {
    auto state = createDelegatedSurveyScenario();
    SurveyProgramCharter charter;
    charter.name = "P6 paid survey transit";
    charter.homeColonyId = state.colonies.back().id;
    charter.requestedFleetId = state.fleets.back().id;
    charter.requestedTeamId = state.surveyTeams.front().id;
    charter.requestedLeaderId = state.people.front().id;
    charter.targets = {{state.bodies.back().id, 1, 1}};
    Simulation sim(state);
    accept(sim, CreateSurveyProgramCommand{charter});
    until(sim, [&] {
        return sim.state().surveyPrograms.front().fuelBurned > 0 &&
               sim.state().fleets.back().activeOrder.type == FleetOrderType::MoveToBody;
    });
    return sim.state();
}

GameState partialAnalysis() {
    auto state = createDelegatedSurveyScenario();
    state.colonies.back().analysisCapacity = 1.0;
    state.surveyTeams.push_back({SurveyTeamId{state.ids.nextSurveyTeamId++}, "P6 home analyst",
                                 SurveyTeamLocationKind::Colony, state.colonies.back().id, std::nullopt});
    Simulation sim(state);
    accept(sim, ResourceSurveyCommand{state.fleets.front().id, state.fleets.front().currentBodyId});
    AnalysisCharter analysis{"P6 partial analysis",
                             state.colonies.back().id,
                             FixedBatchInput{{sim.state().observations.back().id}},
                             state.surveyTeams.back().id,
                             state.people.front().id,
                             std::nullopt};
    accept(sim, CreateAnalysisProgramCommand{analysis});
    until(sim, [&] {
        return !sim.state().analysisPrograms.front().jobs.empty() &&
               !sim.state().analysisPrograms.front().receipts.empty();
    });
    require(sim.state().assessments.empty(), "checkpoint retains unfinished interpretation");
    return sim.state();
}

std::pair<GameState, GameState> technicalTestingAndReservedPrototype() {
    auto state = createHomeSystemScenario();
    const auto facility = state.technicalFacilities.front();
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& row) { return row.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(10'000);
    colony->processorCapacity = 0;
    colony->shipyardCapacity = 50;
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    TechnicalDevelopmentCharter charter{"P6 checkpoint prototype",
                                        state.technologyOpportunities.front().id,
                                        facility.colonyId,
                                        facility.id,
                                        team->id,
                                        state.people.front().id,
                                        TechnicalDevelopmentScope::DemonstratePrototype,
                                        {}};
    Simulation sim(state);
    accept(sim, CreateTechnicalDevelopmentCommand{charter});
    until(sim, [&] { return sim.state().technicalTestRecords.size() == 1; });
    const auto oneTest = sim.state();
    until(sim, [&] { return sim.state().developedComponentRevisions.size() == 1; });
    auto installs = referenceSurveyCutterComponents();
    installs.at(3).componentId = sim.state().developedComponentRevisions.front().componentId;
    accept(sim, CreateShipClassRevisionCommand{"P6 partial prototype hull", ShipRole::Survey, std::nullopt,
                                               installs});
    accept(sim, AssignShipyardBuildCommand{facility.colonyId, sim.state().shipClasses.back().id, 1});
    until(
        sim,
        [&] {
            return sim.state().shipyardOrders.front().currentHullSupplyPlan.has_value() &&
                   sim.state().shipyardOrders.front().accumulatedBuildPoints > 0;
        },
        8);
    require(sim.state().prototypeComponentUnits.front().state == PrototypeComponentState::ReservedForShipyard,
            "physical prototype reservation survives an in-progress hull");
    return {oneTest, sim.state()};
}

std::vector<Checkpoint> checkpoints() {
    const auto established = earnP6EstablishedLoop();
    const auto technical = technicalTestingAndReservedPrototype();
    std::vector<Checkpoint> result;
    result.emplace_back("waiting ship order", waitingOrder());
    result.emplace_back("paid survey transit", paidSurveyTransit());
    result.emplace_back("partial home analysis", partialAnalysis());
    result.emplace_back("loaded freight transit", established.freightInTransit);
    result.emplace_back("partial field assembly", established.sitePartiallyAssembled);
    result.emplace_back("acknowledged operating limitation", earnSiteDevelopmentFixture(90, false));
    result.emplace_back("one complete prototype test", technical.first);
    result.emplace_back("prototype reserved to in-progress hull", technical.second);
    result.emplace_back("advanced service history", earnTechnicalDevelopmentFixture(6.0));
    Simulation reported(established.mature);
    while (reported.state().date.day % 30 != 0)
        step(reported);
    require(reported.state().date.day % 30 == 0 && !reported.state().resourceSites.front().reports.empty(),
            "P6 same-day command begins after a global report boundary");
    const auto report = reported.state().resourceSites.front().reports.back();
    accept(reported, SetColonyProcessingPolicyCommand{
                         reported.state().colonies.back().id, ProcessingPolicy::Balanced, {}});
    const auto& retained = reported.state().resourceSites.front().reports.back();
    require(retained.endDay == report.endDay && retained.auditThroughId == report.auditThroughId &&
                retained.waitingReason == report.waitingReason &&
                retained.recoveredIce == report.recoveredIce,
            "same-day later authority does not rewrite published site report");
    result.emplace_back("report boundary followed by same-day command", reported.state());
    return result;
}

void compareLive(const GameState& a, const GameState& b, const std::string& name) {
    require(a.date.day == b.date.day && a.ids.nextEventId == b.ids.nextEventId &&
                a.eventLog.size() == b.eventLog.size() && a.ships.size() == b.ships.size() &&
                a.fleets.size() == b.fleets.size() && a.shipyardOrders.size() == b.shipyardOrders.size() &&
                a.surveyPrograms.size() == b.surveyPrograms.size() &&
                a.freightPrograms.size() == b.freightPrograms.size() &&
                a.resourceSites.size() == b.resourceSites.size() &&
                a.technicalDevelopmentPrograms.size() == b.technicalDevelopmentPrograms.size(),
            name + ": live durable entity counts/identities diverged");
    for (std::size_t index = 0; index < a.ships.size(); ++index)
        require(a.ships[index].id == b.ships[index].id &&
                    a.ships[index].shipClassId == b.ships[index].shipClassId &&
                    a.ships[index].fuel == b.ships[index].fuel &&
                    a.ships[index].cargo.has_value() == b.ships[index].cargo.has_value(),
                name + ": exact hull identity/fuel/custody diverged");
    for (std::size_t index = 0; index < a.colonies.size(); ++index)
        require(a.colonies[index].id == b.colonies[index].id &&
                    a.colonies[index].stockpile.amount == b.colonies[index].stockpile.amount &&
                    a.colonies[index].processedStockpile.amount ==
                        b.colonies[index].processedStockpile.amount &&
                    a.colonies[index].processedProductionTotals.amount ==
                        b.colonies[index].processedProductionTotals.amount,
                name + ": colony inventory or gross production diverged");
}

void comparePersisted(const GameState& a, const GameState& b, const p6test::TemporaryDirectory& temp,
                      const std::string& label) {
    compareLive(a, b, label);
    validateGameState(a);
    validateGameState(b);
    const auto left = p6test::durableSnapshot(a, temp.path / "left.sqlite");
    const auto right = p6test::durableSnapshot(b, temp.path / "right.sqlite");
    require(left == right, label + ": complete v18 logical table/column snapshot diverged");
}

void save_load_matrix() {
    p6test::TemporaryDirectory temp;
    for (const auto& [name, starting] : checkpoints()) {
        validateGameState(starting);
        save::SaveGameRepository::save(temp.path / "checkpoint.sqlite", starting);
        Simulation uninterrupted(starting);
        Simulation reloaded(save::SaveGameRepository::load(temp.path / "checkpoint.sqlite"));
        comparePersisted(uninterrupted.state(), reloaded.state(), temp, name + " at load");
        int elapsed = 0;
        bool reachedDecision = false;
        for (int day = 0; day < 5; ++day) {
            const auto left = uninterrupted.advanceDaysDetailed(1);
            const auto right = reloaded.advanceDaysDetailed(1);
            require(left.advancedDays == right.advancedDays && left.interrupted == right.interrupted &&
                        left.stopReason == right.stopReason && left.issueSource == right.issueSource,
                    name + ": first decision boundary differs after Save/Load");
            comparePersisted(uninterrupted.state(), reloaded.state(), temp,
                             name + " after day " + std::to_string(day + 1));
            elapsed += left.advancedDays;
            if (left.interrupted) {
                reachedDecision = true;
                break;
            }
        }
        std::cout << name << ": advanced=" << elapsed << " decision=" << (reachedDecision ? "yes" : "no")
                  << '\n';
    }
}

void block_and_daily_have_same_decision_boundary() {
    p6test::TemporaryDirectory temp;
    const auto established = earnP6EstablishedLoop();
    for (const auto& [name, starting] :
         std::vector<Checkpoint>{{"P1 waiting intention", waitingOrder()},
                                 {"mature standing site", established.mature},
                                 {"partial P5 test", technicalTestingAndReservedPrototype().first}}) {
        Simulation block(starting), daily(starting);
        const auto result = block.advanceDaysDetailed(30);
        for (int day = 0; day < result.advancedDays; ++day) {
            const auto one = daily.advanceDaysDetailed(1);
            require(one.advancedDays == 1, name + ": daily branch missed elapsed day");
            if (day + 1 == result.advancedDays)
                require(one.interrupted == result.interrupted && one.stopReason == result.stopReason &&
                            one.issueSource == result.issueSource,
                        name + ": block and daily disagree on first decision");
        }
        comparePersisted(block.state(), daily.state(), temp, name + " block/day");
    }
}
} // namespace

int main() {
    try {
        save_load_matrix();
        block_and_daily_have_same_decision_boundary();
        std::cout << "P6 continuation matrix passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 continuation proof failed: " << error.what() << '\n';
        return 1;
    }
}
