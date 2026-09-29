#include "sim/SurveyProgramRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "sim/ShipDesignRules.h"

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
    if ((policy.maxAdditionalPropellant.has_value() &&
         (!std::isfinite(*policy.maxAdditionalPropellant) || *policy.maxAdditionalPropellant < 0.0)) ||
        !std::isfinite(policy.homeStockFloor) || policy.homeStockFloor < 0.0 ||
        !std::isfinite(policy.returnContingencyFraction) || policy.returnContingencyFraction < 0.0) {
        return "Survey program fuel policy must be finite and non-negative";
    }
    return std::nullopt;
}

std::string surveyProgramCondition(const GameState& state, const SurveyProgram& program) {
    if (program.lifecycle == SurveyProgramLifecycle::Closed) {
        return program.closure == SurveyProgramClosure::Completed ? "Completed" : "Cancelled";
    }
    if (program.lifecycle == SurveyProgramLifecycle::Closing) {
        return "Closing after current transit";
    }
    if (program.lifecycle == SurveyProgramLifecycle::Suspended) {
        return program.leasedFleetId.has_value() ? "Suspending after current transit" : "Suspended";
    }
    if (!program.issue.signature.empty() && !program.issue.acknowledged) {
        return "Decision needed: " + program.issue.message;
    }
    if (!program.charter.requestedFleetId.has_value()) return "Waiting: no fleet requested";
    if (!program.charter.requestedTeamId.has_value()) return "Waiting: no survey team requested";
    if (!program.charter.requestedLeaderId.has_value()) return "Waiting: no leader requested";
    if (!program.leasedFleetId.has_value()) {
        const auto requestedFleet = std::find_if(state.fleets.begin(), state.fleets.end(),
            [&program](const Fleet& row) { return row.id == *program.charter.requestedFleetId; });
        const auto requestedTeam = std::find_if(state.surveyTeams.begin(), state.surveyTeams.end(),
            [&program](const SurveyTeam& row) { return row.id == *program.charter.requestedTeamId; });
        const auto home = std::find_if(state.colonies.begin(), state.colonies.end(),
            [&program](const Colony& row) { return row.id == program.charter.homeColonyId; });
        if (requestedFleet == state.fleets.end() || requestedTeam == state.surveyTeams.end() ||
            home == state.colonies.end()) return "Waiting: requested assets are unavailable";
        const bool teamCoLocated = (requestedTeam->locationKind == SurveyTeamLocationKind::Fleet &&
                                    requestedTeam->fleetId == requestedFleet->id) ||
            (requestedTeam->locationKind == SurveyTeamLocationKind::Colony &&
             requestedTeam->colonyId == home->id && requestedFleet->currentBodyId == home->bodyId &&
             requestedFleet->activeOrder.type == FleetOrderType::None);
        const bool anotherLease = std::any_of(state.surveyPrograms.begin(), state.surveyPrograms.end(),
            [&program, &requestedFleet, &requestedTeam](const SurveyProgram& row) {
                return row.id != program.id &&
                    (row.leasedFleetId == requestedFleet->id || row.leasedTeamId == requestedTeam->id);
            });
        if (!teamCoLocated || anotherLease || requestedFleet->activeOrder.type != FleetOrderType::None ||
            !requestedFleet->queuedOrders.empty()) return "Waiting: requested assets are unavailable";
        if (evaluateFleetSurvey(state, *requestedFleet).operationalCapability <= 0.0) {
            return "Waiting: no operational survey capability";
        }
        return "Awaiting next program dispatch";
    }
    const auto fleet = std::find_if(state.fleets.begin(), state.fleets.end(),
        [&program](const Fleet& row) { return row.id == *program.leasedFleetId; });
    if (fleet == state.fleets.end()) return "Waiting: leased fleet is unavailable";
    if (fleet->activeOrder.type == FleetOrderType::MoveToBody) return "Traveling on committed transit";
    if (evaluateFleetSurvey(state, *fleet).operationalCapability <= 0.0) {
        return "Waiting: no operational survey capability";
    }
    if (program.task == SurveyProgramTask::Survey) return "Surveying timed visit";
    if (program.task == SurveyProgramTask::Return) return "Returning to home support";
    if (program.task == SurveyProgramTask::Outbound) return "At survey target";
    return "Planning next home-supported sortie";
}

} // namespace deep
