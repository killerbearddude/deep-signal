#include "sim/SurveyProgramRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace deep {

std::optional<SurveyTargetChoice> chooseSurveyTarget(const SurveyPlanningInputs& inputs) {
    const SurveyPlanningCandidate* best = nullptr;
    for (const SurveyPlanningCandidate& row : inputs.candidates) {
        if (!row.knownFeasible || row.completedPasses >= row.requestedPasses) continue;
        if (best == nullptr) { best = &row; continue; }
        if (inputs.approach == SurveyPlanningApproach::CoverageFirst) {
            if (row.completedPasses < best->completedPasses ||
                (row.completedPasses == best->completedPasses && row.playerPriority > best->playerPriority) ||
                (row.completedPasses == best->completedPasses && row.playerPriority == best->playerPriority &&
                 row.charterOrdinal < best->charterOrdinal)) best = &row;
        } else {
            if (row.playerPriority > best->playerPriority ||
                (row.playerPriority == best->playerPriority && row.charterOrdinal < best->charterOrdinal)) best = &row;
        }
    }
    if (best == nullptr) return std::nullopt;
    std::ostringstream reason;
    reason << (inputs.approach == SurveyPlanningApproach::CoverageFirst ? "CoverageFirst" : "PriorityFirst")
           << " chose charter row " << best->charterOrdinal + 1
           << " at priority " << best->playerPriority
           << " after " << best->completedPasses << " completed pass(es)";
    return SurveyTargetChoice{
        .bodyId = best->bodyId,
        .passNumber = best->completedPasses + 1,
        .charterOrdinal = best->charterOrdinal,
        .reason = reason.str()
    };
}

std::int64_t nextGlobalSurveyBoundary(const std::int64_t day, const std::int64_t interval) {
    if (day < 0 || interval <= 0 || day > std::numeric_limits<std::int64_t>::max() - interval) {
        throw std::overflow_error{"Invalid survey reporting boundary"};
    }
    return (day / interval + 1) * interval;
}

int completedSurveyPasses(const SurveyProgram& program, const BodyId bodyId) noexcept {
    return static_cast<int>(std::count_if(program.receipts.begin(), program.receipts.end(),
        [bodyId](const SurveyVisitReceipt& row) { return row.bodyId == bodyId; }));
}

bool surveyCharterFinished(const SurveyProgram& program) noexcept {
    for (const SurveyProgramTarget& target : program.charter.targets) {
        if (completedSurveyPasses(program, target.bodyId) < target.requestedPasses) return false;
    }
    return true;
}

std::optional<std::string> validateSurveyProgramCharter(const GameState& state,
                                                        const SurveyProgramCharter& charter) {
    if (charter.name.empty()) return "Survey program name must be non-empty";
    const auto home = std::find_if(state.colonies.begin(), state.colonies.end(),
        [&charter](const Colony& row) { return row.id == charter.homeColonyId; });
    if (home == state.colonies.end()) return "Survey program home colony does not exist";
    if (charter.targets.empty()) return "Survey program needs at least one target";
    std::unordered_set<std::int64_t> seenTargets;
    for (const SurveyProgramTarget& target : charter.targets) {
        if (!target.bodyId || !seenTargets.insert(target.bodyId.value).second) {
            return "Survey program targets must have unique valid body IDs";
        }
        const bool exists = std::any_of(state.bodies.begin(), state.bodies.end(),
            [&target](const Body& body) { return body.id == target.bodyId; });
        if (!exists) return "Survey program target body does not exist";
        if (target.priority < 0 || target.requestedPasses <= 0) {
            return "Survey program priority and pass count must be non-negative and positive";
        }
    }
    const auto validRequested = [&](const auto& id, const auto& rows) {
        return !id.has_value() || std::any_of(rows.begin(), rows.end(),
            [&id](const auto& row) { return row.id == *id; });
    };
    if (!validRequested(charter.requestedFleetId, state.fleets)) return "Requested fleet does not exist";
    if (!validRequested(charter.requestedLeaderId, state.people)) return "Requested leader does not exist";
    if (!validRequested(charter.requestedTeamId, state.surveyTeams)) return "Requested survey team does not exist";
    const SurveyProgramPolicy& policy = charter.policy;
    if (!validRequested(policy.maintenanceProgramId, state.maintenancePrograms)) {
        return "Selected maintenance provider does not exist";
    }
    if (!std::isfinite(policy.remainingDutyTrigger) || policy.remainingDutyTrigger < 0.0 || policy.remainingDutyTrigger > 1.0) {
        return "Remaining survey duty trigger must be between zero and one";
    }
    if ((policy.maxAdditionalPropellant.has_value() &&
         (!std::isfinite(*policy.maxAdditionalPropellant) || *policy.maxAdditionalPropellant < 0.0)) ||
        !std::isfinite(policy.homeStockFloor) || policy.homeStockFloor < 0.0 ||
        !std::isfinite(policy.returnContingencyFraction) || policy.returnContingencyFraction < 0.0) {
        return "Survey program fuel policy must be finite and non-negative";
    }
    return std::nullopt;
}

} // namespace deep
