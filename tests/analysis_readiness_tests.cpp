// Regression for PR #11: deterministic laboratory allocation must be visible
// without persisting shares or changing physical dispatch. Use real batches,
// two local scientists, actual labor and non-ID program order across Save/Load.
#include "app/SimulationQueries.h"
#include "save/SaveGameRepository.h"
#include "sim/AnalysisProgramRules.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Temp {
    std::filesystem::path directory;
    Temp() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            auto candidate =
                std::filesystem::temp_directory_path() /
                ("deep_signal_analysis_readiness_" + std::to_string(stamp) + "_" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("No unique readiness test directory");
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
GameState twoPrograms(std::int64_t day, double capacity) {
    auto state = createDelegatedSurveyScenario();
    state.date.day = day;
    for (auto& colony : state.colonies) {
        colony.mines = 0;
        colony.processorCapacity = 0;
    }
    state.colonies.back().analysisCapacity = capacity;
    state.surveyTeams.push_back({SurveyTeamId{state.ids.nextSurveyTeamId++}, "Second real scientist",
                                 SurveyTeamLocationKind::Colony, state.colonies.back().id, std::nullopt});
    Simulation sim(state);
    require(sim.execute(ResourceSurveyCommand{state.fleets.front().id, state.colonies.back().bodyId}).ok,
            "Actual observation acquired");
    sim.advanceDays(1); // The batch is analyzable before either authorization.
    for (const auto& team : state.surveyTeams) {
        AnalysisCharter charter{"Scientific work " + std::to_string(team.id.value),
                                state.colonies.back().id,
                                FixedBatchInput{{ObservationBatchId{1}}},
                                team.id,
                                state.people.front().id,
                                std::nullopt};
        require(sim.execute(CreateAnalysisProgramCommand{charter}).ok, "Both valid intents accepted");
    }
    state = sim.state();
    std::reverse(state.analysisPrograms.begin(), state.analysisPrograms.end());
    validateGameState(state);
    return state;
}
void step(Simulation& sim) {
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1 && !result.interrupted, "Capacity contention is an ordinary wait");
    validateGameState(sim.state());
}
std::vector<AnalysisProgramSummary> summaries(const GameState& state) {
    const SimulationService service(state);
    return SimulationQueries(service).analysisPrograms();
}
void full_contention_recovery_and_report(const Temp& temp) {
    Simulation sim(twoPrograms(27, 1)); // Authorize at day 28; reporting boundary is day 30.
    const auto original = sim.state().analysisPrograms;
    for (const auto& team : sim.state().surveyTeams)
        require(team.locationKind == SurveyTeamLocationKind::Colony &&
                    team.colonyId == sim.state().colonies.back().id,
                "Both actual scientists are physically at the shared laboratory");
    require(sim.state().observations.front().availableDay <= sim.state().date.day,
            "Shared acquired input is already analyzable before the opening");
    require(original.front().id.value > original.back().id.value, "Fixture priority is stored order, not ID");
    for (const auto& p : original) {
        require(!validateAnalysisCharter(sim.state(), p.charter), "Both intents remain structurally valid");
        require(analysisReadiness(sim.state(), p).canAttemptWork,
                "Both have local staff, leader and delivered data");
        require(!p.leasedTeamId, "No speculative ownership at authorization");
    }
    const auto before = summaries(sim.state());
    const auto first = analysisNextOpeningReadiness(sim.state(), original.front());
    const auto second = analysisNextOpeningReadiness(sim.state(), original.back());
    require(first.canAttemptWork && first.workThisOpening == 1 && !second.canAttemptWork &&
                second.cause == AnalysisWaitCause::LaboratoryContention,
            "Only the first eligible head receives the one laboratory day");
    require(before.back().condition == second.explanation &&
                before.back().condition.find("committed to earlier analysis work") != std::string::npos &&
                !before.back().currentJobEta,
            "Query must identify contention and omit a blocked ETA");
    require(sim.state().analysisPrograms == original,
            "Readiness projection must not reserve or mutate intent");
    auto unready = sim.state();
    unready.analysisPrograms.front().charter.requestedLeaderId.reset();
    require(analysisNextOpeningReadiness(unready, unready.analysisPrograms.back()).workThisOpening == 1,
            "Ineligible earlier intent must not reserve laboratory throughput");

    step(sim);
    step(sim);
    const auto& waiting = sim.state().analysisPrograms.back();
    require(analysisWork(sim.state().analysisPrograms.front()) == 2 && analysisWork(waiting) == 0,
            "Earlier program consumes real work while later program remains at zero");
    require(waiting.lifecycle == AnalysisLifecycle::Authorized && !waiting.leasedTeamId &&
                waiting.charterRevision == 1,
            "Waiting preserves original intent without seizing its scientist");
    require(waiting.reports.size() == 1 && waiting.reports.front().endDay == 30 &&
                waiting.reports.front().waitingReason == analysisExecutionCondition(sim.state(), waiting) &&
                waiting.reports.front().waitingReason.find("committed to earlier analysis work") !=
                    std::string::npos,
            "Day-30 durable report must retain the real continuing laboratory blocker");

    const auto recordedReason = waiting.reports.front().waitingReason;
    const auto path = temp.directory / "blocked.sqlite";
    save::SaveGameRepository::save(path, sim.state());
    Simulation loaded(save::SaveGameRepository::load(path));
    require(loaded.state().analysisPrograms == sim.state().analysisPrograms &&
                programOpeningOrder(loaded.state()) == programOpeningOrder(sim.state()) &&
                summaries(loaded.state()).back().condition == summaries(sim.state()).back().condition,
            "Save/Load preserves stored priority, reports and resulting explanation");
    step(sim);
    step(loaded); // Earlier job completes; this opening still grants later work zero.
    require(analysisWork(sim.state().analysisPrograms.back()) == 0,
            "Completion cannot respent the consumed day");
    const auto ready = analysisNextOpeningReadiness(sim.state(), sim.state().analysisPrograms.back());
    require(ready.canAttemptWork && ready.cause == AnalysisWaitCause::None && ready.workThisOpening == 1,
            "Same waiting program becomes executable when earlier work releases next-opening capacity");
    step(sim);
    step(loaded);
    require(analysisWork(sim.state().analysisPrograms.back()) == 1 &&
                sim.state().analysisPrograms.back().id == original.back().id &&
                sim.state().analysisPrograms.back().charterRevision == 1 &&
                sim.state().analysisPrograms == loaded.state().analysisPrograms,
            "Recovery earns work without reauthorization and continues identically after Load");
    require(sim.state().analysisPrograms.back().reports.front().waitingReason == recordedReason,
            "Later readiness must not rewrite the historical report");
}
void partial_allocation(const Temp& temp) {
    Simulation sim(twoPrograms(0, 2));
    step(sim);
    step(sim); // Both real scientists have earned two days on their active jobs.
    auto state = sim.state();
    state.colonies.back().analysisCapacity = .75;
    sim = Simulation(state);
    step(sim); // Earlier job now needs .25; later still needs 1.0.
    state = sim.state();
    state.colonies.back().analysisCapacity = 1;
    sim = Simulation(state);
    const auto before = sim.state().analysisPrograms;
    const auto earlier = analysisNextOpeningReadiness(sim.state(), before.front());
    const auto later = analysisNextOpeningReadiness(sim.state(), before.back());
    require(earlier.workThisOpening == .25 && later.canAttemptWork && later.workThisOpening == .75 &&
                later.cause == AnalysisWaitCause::PartialLaboratoryShare,
            "A .25 remainder leaves an executable .75 allocation for the other actual scientist");
    const auto rows = summaries(sim.state());
    require(rows.back().condition == later.explanation &&
                rows.back().condition.find("0.75") != std::string::npos && !rows.back().currentJobEta,
            "Partial allocation must be explained without inventing a completion ETA");
    const auto path = temp.directory / "partial.sqlite";
    save::SaveGameRepository::save(path, sim.state());
    Simulation loaded(save::SaveGameRepository::load(path));
    const auto restored =
        analysisNextOpeningReadiness(loaded.state(), loaded.state().analysisPrograms.back());
    require(restored.cause == later.cause && restored.workThisOpening == later.workThisOpening &&
                restored.explanation == later.explanation,
            "Fractional allocation explanation survives Save/Load");
    step(sim);
    step(loaded);
    require(analysisWork(sim.state().analysisPrograms.front()) - analysisWork(before.front()) == .25 &&
                analysisWork(sim.state().analysisPrograms.back()) - analysisWork(before.back()) == .75 &&
                sim.state().analysisPrograms == loaded.state().analysisPrograms,
            "Executor spends the same .25/.75 shares as its structured readiness result");
}
} // namespace
int main() {
    try {
        Temp temp;
        full_contention_recovery_and_report(temp);
        partial_allocation(temp);
        std::cout << "Analysis readiness contention regressions passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
