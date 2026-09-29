// Physical field-versus-laboratory staffing and independent work totals. Every
// day is validated; inputs come from actual survey commands rather than archives.
#include "sim/Simulation.h"
#include "sim/ScenarioFactory.h"
#include "sim/AnalysisProgramRules.h"
#include "sim/GameStateValidation.h"
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void day(Simulation& sim) {
    const auto r = sim.advanceDaysDetailed(1);
    require(r.advancedDays == 1 && !r.interrupted, "Unexpected analysis interruption");
    validateGameState(sim.state());
}
GameState fixture() {
    auto s = createDelegatedSurveyScenario();
    s.colonies.back().analysisCapacity = 1;
    for (auto& c : s.colonies) {
        c.mines = 0;
        c.processorCapacity = 0;
    }
    s.ships.front().fuel = 1000;
    return s;
}
SurveyProgramCharter survey(const GameState& s) {
    SurveyProgramCharter c;
    c.name = "Two field passes";
    c.homeColonyId = s.colonies.back().id;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, 2}};
    return c;
}
AnalysisCharter follower(const GameState& s, SurveyTeamId team) {
    return {
        "Analyze acquired passes", s.colonies.back().id, FollowSurveyInput{s.surveyPrograms.front().id}, team,
        s.people.front().id,       std::nullopt};
}
void same_team_and_concurrency() {
    for (bool separate : {false, true}) {
        auto s = fixture();
        if (separate)
            s.surveyTeams.push_back({SurveyTeamId{s.ids.nextSurveyTeamId++}, "Actual independent analyst",
                                     SurveyTeamLocationKind::Colony, s.colonies.back().id, std::nullopt});
        Simulation sim(s);
        require(sim.execute(CreateSurveyProgramCommand{survey(s)}).ok, "Survey authorized");
        require(sim.execute(CreateAnalysisProgramCommand{follower(sim.state(), s.surveyTeams.back().id)}).ok,
                "Empty follower authorized");
        require(!sim.state().analysisPrograms.front().leasedTeamId, "No empty-input scientist hoarding");
        bool concurrent = false;
        for (int n = 0;
             n < 200 && sim.state().analysisPrograms.front().lifecycle != AnalysisLifecycle::Closed; ++n) {
            day(sim);
            const auto& state = sim.state();
            if (state.surveyPrograms.front().leasedTeamId && analysisWork(state.analysisPrograms.front()) > 0)
                concurrent = true;
            if (!separate && state.surveyPrograms.front().leasedTeamId)
                require(analysisWork(state.analysisPrograms.front()) == 0,
                        "Shared scientist teleported or borrowed");
        }
        const auto& end = sim.state();
        const auto& analysis = end.analysisPrograms.front();
        require(analysis.lifecycle == AnalysisLifecycle::Closed && analysisWork(analysis) == 6 &&
                    analysis.jobs.size() == 2,
                "Two real batches require exactly six lab days");
        require(end.observations.size() == 2 && end.assessments.size() == 2 &&
                    end.surveyPrograms.front().totalWorkDays == 10,
                "One batch/publication per pass and ten field days");
        require(end.ships.front().equipmentCondition.front().usedDuty == 10,
                "Analysis cannot add instrument wear");
        require(concurrent == separate, "Only independent physical scientist permits concurrency");
        require(analysis.receipts.front().day >= end.observations.front().availableDay,
                "Acquisition opening cannot perform analysis");
        if (separate)
            require(analysis.receipts.front().day == end.observations.front().availableDay,
                    "Independent analyst starts at first eligible opening");
        require(end.assessments.back().repeatedEvidence && !end.assessments.back().contentChanged,
                "Repeated negative pass adds no certainty");
        require(!analysis.leasedTeamId, "Completed job releases its scientist");
        // Fixed input can reuse another program's timed batches in explicitly
        // reversed order. This spends new work without altering the source.
        AnalysisCharter fixedSource{"Cross-program fixed inputs",
                                    end.colonies.back().id,
                                    FixedBatchInput{{end.observations[1].id, end.observations[0].id}},
                                    end.surveyTeams.front().id,
                                    end.people.front().id,
                                    std::nullopt};
        require(sim.execute(CreateAnalysisProgramCommand{fixedSource}).ok,
                "Timed batches accepted by independent fixed analysis");
        for (int i = 0; i < 6; ++i)
            day(sim);
        require(sim.state().analysisPrograms.back().jobs.size() == 2 &&
                    sim.state().analysisPrograms.back().jobs.front().batchId == ObservationBatchId{2} &&
                    sim.state().analysisPrograms.back().jobs.back().batchId == ObservationBatchId{1},
                "Fixed input order retained instead of sorting by acquisition or ID");
    }
}
void fixed_manual_and_lifecycle() {
    auto s = fixture();
    Simulation sim(s);
    require(sim.execute(ResourceSurveyCommand{s.fleets.front().id, s.fleets.front().currentBodyId}).ok,
            "Manual raw acquisition");
    require(sim.state().observations.size() == 1 && sim.state().assessments.empty(),
            "Manual action is not free analysis");
    AnalysisCharter c{"Fixed interpretation",
                      s.colonies.back().id,
                      FixedBatchInput{{sim.state().observations.front().id}},
                      s.surveyTeams.front().id,
                      s.people.front().id,
                      1.5};
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok, "Insufficient known allowance preserves intent");
    day(sim);
    day(sim);
    auto id = sim.state().analysisPrograms.front().id;
    require(analysisWork(sim.state().analysisPrograms.front()) == 1.5,
            "Finite allowance bounds actual fractional work");
    require(sim.execute(SuspendAnalysisProgramCommand{id}).ok, "Suspend partial analysis");
    require(!sim.state().analysisPrograms.front().leasedTeamId, "Suspend releases stationary scientist");
    require(sim.execute(ResumeAnalysisProgramCommand{id}).ok, "Unready resume accepted");
    day(sim);
    require(sim.state().assessments.empty(), "Exhausted allowance cannot publish");
    require(
        sim.execute(AmendAnalysisProgramCommand{id, {c.name, c.requestedTeamId, c.requestedLeaderId, 3.0}})
            .ok,
        "Expand authority");
    day(sim);
    day(sim);
    require(sim.state().assessments.size() == 1 && analysisWork(sim.state().analysisPrograms.front()) == 3,
            "Retained documented work completes once");
    require(sim.state().ships.front().equipmentCondition.front().usedDuty == 5, "Manual duty five only");
    auto bad = c;
    bad.source = FixedBatchInput{{ObservationBatchId{1}, ObservationBatchId{1}}};
    require(!sim.execute(CreateAnalysisProgramCommand{bad}).ok && sim.state().analysisPrograms.size() == 1,
            "Duplicate source rejected without partial program");
}
} // namespace
int main() {
    try {
        same_team_and_concurrency();
        fixed_manual_and_lifecycle();
        std::cout << "2 analysis execution groups passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
