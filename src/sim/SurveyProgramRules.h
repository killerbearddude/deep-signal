#pragma once

// Pure, known-data decisions for the bounded P3A home-supported planner.
// This API deliberately cannot inspect mineral deposits or unacquired scientific knowledge.

#include "sim/GameState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep {

inline constexpr int kP3ASurveyWorkDaysPerPass = 5;

struct SurveyPlanningCandidate {
    BodyId bodyId;
    int playerPriority = 0;
    int requestedPasses = 1;
    int completedPasses = 0;
    std::size_t charterOrdinal = 0;
    bool knownFeasible = false;
    std::string waitingReason;
};

struct SurveyPlanningInputs {
    SurveyPlanningApproach approach = SurveyPlanningApproach::CoverageFirst;
    std::vector<SurveyPlanningCandidate> candidates;
};

struct SurveyTargetChoice {
    BodyId bodyId;
    int passNumber = 1;
    std::size_t charterOrdinal = 0;
    std::string reason;
};

// Chooses one currently feasible unfinished charter row without world/geology
// access. Returns empty when no supplied candidate can execute yet.
[[nodiscard]] std::optional<SurveyTargetChoice> chooseSurveyTarget(const SurveyPlanningInputs& inputs);
// Next strictly future multiple; throws if no representable boundary remains.
[[nodiscard]] std::int64_t nextGlobalSurveyBoundary(std::int64_t day, std::int64_t interval);
// Counts completed receipts for one stable body identity, including old charters.
[[nodiscard]] int completedSurveyPasses(const SurveyProgram& program, BodyId bodyId) noexcept;
// Requested passes are work objectives; this does not imply geological certainty.
[[nodiscard]] bool surveyCharterFinished(const SurveyProgram& program) noexcept;
// Checks structural input and supplied references without requiring readiness.
[[nodiscard]] std::optional<std::string> validateSurveyProgramCharter(
    const GameState& state, const SurveyProgramCharter& charter);
} // namespace deep
