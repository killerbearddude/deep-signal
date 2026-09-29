#include "sim/ScenarioFactory.h"
#include "sim/SurveyProgramRules.h"

// Independent known-data checks for P3A target order and charter admission.
// Hidden mineral state is absent from SurveyPlanningInputs by construction.

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool okay, const std::string& message) {
    if (!okay) throw std::runtime_error{message};
}

void testApproachesProduceDistinctOrders() {
    for (const auto approach : {deep::SurveyPlanningApproach::CoverageFirst,
                                deep::SurveyPlanningApproach::PriorityFirst}) {
        deep::SurveyPlanningInputs inputs{.approach = approach, .candidates = {
            {deep::BodyId{1}, 3, 2, 0, 0, true, {}},
            {deep::BodyId{2}, 2, 2, 0, 1, true, {}},
            {deep::BodyId{3}, 1, 2, 0, 2, true, {}}
        }};
        std::vector<std::int64_t> actual;
        for (int pass = 0; pass < 6; ++pass) {
            const auto chosen = deep::chooseSurveyTarget(inputs);
            require(chosen.has_value() && !chosen->reason.empty(), "planner selects a reasoned target");
            actual.push_back(chosen->bodyId.value);
            ++inputs.candidates.at(chosen->charterOrdinal).completedPasses;
        }
        const std::vector<std::int64_t> expected = approach == deep::SurveyPlanningApproach::CoverageFirst
            ? std::vector<std::int64_t>{1, 2, 3, 1, 2, 3}
            : std::vector<std::int64_t>{1, 1, 2, 2, 3, 3};
        require(actual == expected, "leader approach changes deterministic target order");
        require(!deep::chooseSurveyTarget(inputs).has_value(), "completed quotas leave no further target");
    }
}

void testFeasibilitySkipsWithoutDeletingIntent() {
    deep::SurveyPlanningInputs inputs{.approach = deep::SurveyPlanningApproach::PriorityFirst,
        .candidates = {
            {deep::BodyId{1}, 10, 1, 0, 0, false, "known fuel envelope"},
            {deep::BodyId{2}, 1, 1, 0, 1, true, {}}
        }};
    const auto chosen = deep::chooseSurveyTarget(inputs);
    require(chosen.has_value() && chosen->bodyId == deep::BodyId{2},
            "known-infeasible target does not block executable lower priority work");
    require(inputs.candidates.front().completedPasses == 0,
            "skipped intent remains unfinished in the charter inputs");
}

void testAdmissionAndBoundaries() {
    deep::GameState state = deep::createHomeSystemScenario();
    deep::SurveyProgramCharter charter{
        .name = "Barren targets remain valid",
        .homeColonyId = state.colonies.front().id,
        .targets = {{state.bodies.back().id, 3, 2}}
    };
    require(!deep::validateSurveyProgramCharter(state, charter).has_value(),
            "valid charter accepts missing optional assignments without geology checks");
    charter.targets.push_back(charter.targets.front());
    require(deep::validateSurveyProgramCharter(state, charter).has_value(),
            "duplicate target body rejects");
    charter.targets.pop_back();
    charter.policy.homeStockFloor = std::numeric_limits<double>::quiet_NaN();
    require(deep::validateSurveyProgramCharter(state, charter).has_value(),
            "malformed fuel policy rejects");
    require(deep::nextGlobalSurveyBoundary(0, 30) == 30 &&
            deep::nextGlobalSurveyBoundary(17, 30) == 30 &&
            deep::nextGlobalSurveyBoundary(30, 30) == 60 &&
            deep::nextGlobalSurveyBoundary(90, 90) == 180,
            "report/review controls use strictly future global boundaries");
}

} // namespace

int main() {
    try {
        testApproachesProduceDistinctOrders();
        testFeasibilitySkipsWithoutDeletingIntent();
        testAdmissionAndBoundaries();
        std::cout << "Survey program rules tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Survey program rules test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
