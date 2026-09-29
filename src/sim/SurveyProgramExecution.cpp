#include "sim/SurveyProgramExecution.h"

// Implements one bounded home-supported sortie at a time. The runner reads only
// known route, asset, charter, and receipt data while choosing a target. Hidden
// deposits are consulted solely by the shared survey-result helper after a
// physically completed visit.

#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"
#include "sim/SurveyProgramRules.h"
#include "sim/TransitPlanning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace deep {
namespace {

constexpr double kFuelEpsilon = 1.0e-8;

template <typename T, typename Id>
[[nodiscard]] T* byId(std::vector<T>& rows, const Id id) noexcept {
    const auto it = std::find_if(rows.begin(), rows.end(), [id](const T& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

template <typename T, typename Id>
[[nodiscard]] const T* byId(const std::vector<T>& rows, const Id id) noexcept {
    const auto it = std::find_if(rows.begin(), rows.end(), [id](const T& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

[[nodiscard]] bool stationary(const Fleet& fleet) noexcept {
    return fleet.activeOrder.type == FleetOrderType::None && !fleet.destinationBodyId.has_value();
}

[[nodiscard]] SurveyPlanningApproach approachFor(const GameState& state,
                                                  const SurveyProgram& program) noexcept {
    if (program.task != SurveyProgramTask::None && program.taskLeaderId.has_value()) {
        return program.taskApproach;
    }
    if (program.charter.requestedLeaderId.has_value()) {
        if (const Person* leader = byId(state.people, *program.charter.requestedLeaderId)) {
            return leader->surveyPlanningApproach;
        }
    }
    return SurveyPlanningApproach::CoverageFirst;
}

[[nodiscard]] double fleetFuel(const GameState& state, const Fleet& fleet) noexcept {
    double total = 0.0;
    for (const ShipId id : fleet.shipIds) {
        if (const Ship* ship = byId(state.ships, id)) total += ship->fuel;
    }
    return total;
}

[[nodiscard]] double missingTankCapacity(const GameState& state, const Fleet& fleet) {
    double total = 0.0;
    for (const ShipId id : fleet.shipIds) {
        const Ship* ship = byId(state.ships, id);
        if (ship == nullptr) continue;
        const ShipClass* shipClass = byId(state.shipClasses, ship->shipClassId);
        if (shipClass == nullptr) continue;
        const double cap = evaluateShipDesign(state.shipComponents, shipClass->components).propellantCapacity;
        total += std::max(0.0, cap - ship->fuel);
    }
    return total;
}

[[nodiscard]] double authorizationRemaining(const SurveyProgram& program) noexcept {
    if (!program.charter.policy.maxAdditionalPropellant.has_value()) {
        return std::numeric_limits<double>::infinity();
    }
    return std::max(0.0, *program.charter.policy.maxAdditionalPropellant - program.fuelLoaded);
}

[[nodiscard]] double homeStockAvailable(const Colony& home, const SurveyProgram& program) noexcept {
    return std::max(0.0, home.processedStockpile.get(ProcessedMaterial::Propellant) -
                             program.charter.policy.homeStockFloor);
}

struct SortieBudget {
    double requiredFuel = 0.0;
};

// A date-specific forecast is only an eligibility bound. The movement callback
// replans and pays the actual route when a departure is issued.
[[nodiscard]] std::optional<SortieBudget> sortieBudget(const GameState& state,
                                                       const SurveyProgram& program,
                                                       const Fleet& fleet,
                                                       const BodyId homeBodyId,
                                                       const BodyId targetBodyId,
                                                       const std::int64_t departureDay) noexcept {
    if (targetBodyId == homeBodyId) return SortieBudget{};
    if (departureDay < 0 || departureDay > std::numeric_limits<std::int64_t>::max() -
                                              kP3ASurveyWorkDaysPerPass - 1) return std::nullopt;
    const FleetOrder outbound = planFleetTransit(state, homeBodyId, targetBodyId, departureDay);
    if (outbound.type != FleetOrderType::MoveToBody || outbound.arrivalDay <= departureDay ||
        outbound.arrivalDay > std::numeric_limits<std::int64_t>::max() - kP3ASurveyWorkDaysPerPass - 1) {
        return std::nullopt;
    }
    const std::int64_t returnDay = outbound.arrivalDay + kP3ASurveyWorkDaysPerPass + 1;
    const double outward = adjustedFleetMoveFuelCost(state, fleet, homeBodyId, targetBodyId, departureDay);
    const double back = adjustedFleetMoveFuelCost(state, fleet, targetBodyId, homeBodyId, returnDay);
    const double required = outward + back * (1.0 + program.charter.policy.returnContingencyFraction);
    if (!std::isfinite(required) || required < 0.0) return std::nullopt;
    return SortieBudget{.requiredFuel = required};
}

struct CandidateBudget {
    SurveyPlanningCandidate row;
    std::optional<SortieBudget> today;
    std::optional<SortieBudget> afterRefill;
    bool availableNow = false;
    bool potentiallyAffordable = false;
    bool tankFeasible = false;
};

[[nodiscard]] std::vector<CandidateBudget> candidateBudgets(const GameState& state,
                                                              const SurveyProgram& program,
                                                              const Fleet& fleet,
                                                              const Colony& home,
                                                              const OpeningProgramContext* opening = nullptr) {
    std::vector<CandidateBudget> result;
    result.reserve(program.charter.targets.size());
    const double onBoard = fleetFuel(state, fleet);
    const double tankRoom = missingTankCapacity(state, fleet);
    const double authorized = authorizationRemaining(program);
    const double fromStock = opening == nullptr ? homeStockAvailable(home, program)
        : opening->available(state, home.id, ProcessedMaterial::Propellant, program.charter.policy.homeStockFloor);
    const bool canProjectTomorrow = state.date.day < std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < program.charter.targets.size(); ++i) {
        const SurveyProgramTarget& target = program.charter.targets[i];
        const int completed = completedSurveyPasses(program, target.bodyId);
        if (completed >= target.requestedPasses) continue;
        CandidateBudget candidate;
        candidate.row = SurveyPlanningCandidate{
            .bodyId = target.bodyId,
            .playerPriority = target.priority,
            .requestedPasses = target.requestedPasses,
            .completedPasses = completed,
            .charterOrdinal = i,
            .knownFeasible = false,
            .waitingReason = {}
        };
        candidate.today = sortieBudget(state, program, fleet, home.bodyId, target.bodyId, state.date.day);
        if (canProjectTomorrow) {
            candidate.afterRefill = sortieBudget(state, program, fleet, home.bodyId,
                                                 target.bodyId, state.date.day + 1);
        }
        const double availableTransfer = std::min({tankRoom, authorized, fromStock});
        const double eventualTransfer = std::min(tankRoom, authorized);
        candidate.availableNow = (candidate.today.has_value() &&
                                  onBoard + kFuelEpsilon >= candidate.today->requiredFuel) ||
                                 (candidate.afterRefill.has_value() &&
                                  onBoard + availableTransfer + kFuelEpsilon >=
                                      candidate.afterRefill->requiredFuel);
        candidate.potentiallyAffordable = candidate.afterRefill.has_value() &&
                                          onBoard + eventualTransfer + kFuelEpsilon >=
                                              candidate.afterRefill->requiredFuel;
        candidate.tankFeasible = candidate.afterRefill.has_value() &&
                                 onBoard + tankRoom + kFuelEpsilon >=
                                     candidate.afterRefill->requiredFuel;
        result.push_back(std::move(candidate));
    }
    return result;
}

enum class CandidateAvailability { Current, AuthorizedFuture, Tank };

[[nodiscard]] std::optional<SurveyTargetChoice> chooseFrom(const std::vector<CandidateBudget>& candidates,
                                                            const SurveyPlanningApproach approach,
                                                            const CandidateAvailability availability) {
    SurveyPlanningInputs input{.approach = approach, .candidates = {}};
    for (const CandidateBudget& candidate : candidates) {
        SurveyPlanningCandidate row = candidate.row;
        switch (availability) {
        case CandidateAvailability::Current: row.knownFeasible = candidate.availableNow; break;
        case CandidateAvailability::AuthorizedFuture: row.knownFeasible = candidate.potentiallyAffordable; break;
        case CandidateAvailability::Tank: row.knownFeasible = candidate.tankFeasible; break;
        }
        input.candidates.push_back(std::move(row));
    }
    return chooseSurveyTarget(input);
}

[[nodiscard]] const CandidateBudget* budgetForChoice(const std::vector<CandidateBudget>& candidates,
                                                     const SurveyTargetChoice& choice) noexcept {
    const auto it = std::find_if(candidates.begin(), candidates.end(), [&choice](const CandidateBudget& row) {
        return row.row.bodyId == choice.bodyId;
    });
    return it == candidates.end() ? nullptr : &*it;
}

void audit(const GameState& state, const SurveyProgram& program, const SurveyProgramAuditKind kind,
           const SurveyProgramExecutionHooks& hooks, std::string detail,
           const std::optional<BodyId> bodyId = std::nullopt,
           const int passNumber = 0, const double fuelAmount = 0.0) {
    hooks.emit(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program.id,
        .kind = kind,
        .fleetId = program.leasedFleetId,
        .bodyId = bodyId,
        .leaderId = program.taskLeaderId.has_value() ? program.taskLeaderId : program.charter.requestedLeaderId,
        .approach = approachFor(state, program),
        .charterRevision = program.charterRevision,
        .passNumber = passNumber,
        .fuelAmount = fuelAmount,
        .detail = std::move(detail)
    });
}

using Occupied = std::unordered_set<std::int64_t>;

// Lease acquisition is atomic for the requested fleet/team. The occupied sets
// are populated before the day's loop and never shrink during it, so a release
// becomes available only on the next program phase.
[[nodiscard]] bool acquireLease(GameState& state, SurveyProgram& program,
                                const Colony& home, Occupied& fleets, Occupied& teams) {
    if (program.leasedFleetId.has_value() && program.leasedTeamId.has_value()) return true;
    const std::optional<FleetId> desiredFleet = program.taskFleetId.has_value()
        ? program.taskFleetId : program.charter.requestedFleetId;
    const std::optional<SurveyTeamId> desiredTeam = program.taskTeamId.has_value()
        ? program.taskTeamId : program.charter.requestedTeamId;
    if (!desiredFleet.has_value() || !desiredTeam.has_value() ||
        (!program.taskLeaderId.has_value() && !program.charter.requestedLeaderId.has_value()) ||
        fleets.contains(desiredFleet->value) || teams.contains(desiredTeam->value)) {
        return false;
    }
    Fleet* fleet = byId(state.fleets, *desiredFleet);
    SurveyTeam* team = byId(state.surveyTeams, *desiredTeam);
    if (fleet == nullptr || team == nullptr || !stationary(*fleet) || !fleet->queuedOrders.empty()) return false;
    const bool teamAtHome = team->locationKind == SurveyTeamLocationKind::Colony &&
                            team->colonyId == home.id && fleet->currentBodyId == home.bodyId;
    const bool teamAboard = team->locationKind == SurveyTeamLocationKind::Fleet &&
                            team->fleetId == fleet->id;
    if (!teamAtHome && !teamAboard) return false;

    program.leasedFleetId = fleet->id;
    program.leasedTeamId = team->id;
    fleets.insert(fleet->id.value);
    teams.insert(team->id.value);
    return true;
}

void embarkAtHome(SurveyTeam& team, const Fleet& fleet, const Colony& home) {
    if (stationary(fleet) && fleet.currentBodyId == home.bodyId &&
        team.locationKind == SurveyTeamLocationKind::Colony && team.colonyId == home.id) {
        team.locationKind = SurveyTeamLocationKind::Fleet;
        team.colonyId.reset();
        team.fleetId = fleet.id;
    }
}

void clearTask(SurveyProgram& program) noexcept {
    program.task = SurveyProgramTask::None;
    program.taskBodyId.reset();
    program.taskFleetId.reset();
    program.taskTeamId.reset();
    program.taskLeaderId.reset();
    program.taskApproach = SurveyPlanningApproach::CoverageFirst;
    program.taskPassNumber = 0;
    program.workDaysCompleted = 0;
    program.firstWorkDay = 0;
}

// An amended home takes effect only after the old committed task has reached a
// stationary boundary. The fleet and embarked team keep their actual location;
// the next opening phase must pay for travel to the new support colony.
void applyPendingHome(SurveyProgram& program) noexcept {
    if (program.task != SurveyProgramTask::None || !program.pendingHomeColonyId.has_value()) return;
    program.charter.homeColonyId = *program.pendingHomeColonyId;
    program.pendingHomeColonyId.reset();
}

[[nodiscard]] bool requestedAssetsDifferFromLease(const SurveyProgram& program) noexcept {
    return program.leasedFleetId.has_value() && program.leasedTeamId.has_value() &&
           (program.charter.requestedFleetId != program.leasedFleetId ||
            program.charter.requestedTeamId != program.leasedTeamId);
}

void releaseLease(GameState& state, SurveyProgram& program, const SurveyProgramExecutionHooks& hooks) {
    const Fleet* fleet = program.leasedFleetId.has_value() ? byId(state.fleets, *program.leasedFleetId) : nullptr;
    if (fleet != nullptr && !stationary(*fleet)) return;
    if (program.lifecycle == SurveyProgramLifecycle::Closing) {
        program.lifecycle = SurveyProgramLifecycle::Closed;
        audit(state, program, SurveyProgramAuditKind::Closed, hooks,
              program.closure == SurveyProgramClosure::Completed ? "Survey charter completed at home"
                                                           : "Survey charter cancelled after safe stop");
    }
    static_cast<void>(releaseSurveyProgramLease(state, program));
}

// Fuel moves from one real colony stockpile into derived hull tankage in the
// persisted roster order. Counting only the actual credited amount keeps
// cumulative transfer accounting distinct from later departure burns.
[[nodiscard]] double transferHomeFuel(GameState& state, SurveyProgram& program,
                                      Fleet& fleet, Colony& home, const double requested,
                                      const SurveyProgramExecutionHooks& hooks,
                                      const BodyId targetBodyId) {
    if (!stationary(fleet) || fleet.currentBodyId != home.bodyId || requested <= kFuelEpsilon) return 0.0;
    double remaining = std::min({requested, homeStockAvailable(home, program),
                                 authorizationRemaining(program), missingTankCapacity(state, fleet)});
    if (hooks.opening != nullptr) remaining = std::min(remaining, hooks.opening->available(
        state, home.id, ProcessedMaterial::Propellant, program.charter.policy.homeStockFloor));
    if (remaining <= kFuelEpsilon) return 0.0;
    double transferred = 0.0;
    for (const ShipId id : fleet.shipIds) {
        Ship* ship = byId(state.ships, id);
        if (ship == nullptr) continue;
        const ShipClass* shipClass = byId(state.shipClasses, ship->shipClassId);
        if (shipClass == nullptr) continue;
        const double capacity = evaluateShipDesign(state.shipComponents, shipClass->components).propellantCapacity;
        const double added = std::min(remaining, std::max(0.0, capacity - ship->fuel));
        if (added <= 0.0) continue;
        ship->fuel += added;
        remaining -= added;
        transferred += added;
        if (remaining <= kFuelEpsilon) break;
    }
    if (transferred <= kFuelEpsilon) return 0.0;
    home.processedStockpile.set(ProcessedMaterial::Propellant,
                                home.processedStockpile.get(ProcessedMaterial::Propellant) - transferred);
    if (hooks.opening != nullptr) hooks.opening->debit(home.id, ProcessedMaterial::Propellant, transferred);
    program.fuelLoaded += transferred;
    std::ostringstream detail;
    detail << "Loaded " << transferred << " propellant at authorized home support";
    audit(state, program, SurveyProgramAuditKind::FuelTransferred, hooks,
          detail.str(), targetBodyId, program.taskPassNumber, transferred);
    return transferred;
}

[[nodiscard]] bool startLeg(SurveyProgram& program, const Fleet& fleet,
                            const BodyId destination, const SurveyProgramExecutionHooks& hooks) {
    double charged = 0.0;
    if (!hooks.startProgramMove(program.id, fleet.id, destination, charged)) return false;
    // The movement hook has already debited real hull fuel and stored the fixed
    // trajectory; this counter records the same program-attributed burn once.
    if (std::isfinite(charged) && charged >= 0.0) program.fuelBurned += charged;
    return true;
}

void selectTask(GameState& state, SurveyProgram& program, const Fleet& fleet,
                const SurveyTeam& team, const SurveyTargetChoice& choice,
                const SurveyProgramExecutionHooks& hooks, const bool alreadyAtTarget) {
    const SurveyPlanningApproach decisionApproach = approachFor(state, program);
    program.task = alreadyAtTarget ? SurveyProgramTask::Survey : SurveyProgramTask::Outbound;
    program.taskBodyId = choice.bodyId;
    program.taskFleetId = fleet.id;
    program.taskTeamId = team.id;
    program.taskLeaderId = program.charter.requestedLeaderId;
    program.taskApproach = decisionApproach;
    program.taskPassNumber = choice.passNumber;
    program.workDaysCompleted = 0;
    program.firstWorkDay = 0;
    program.lastSelectionReason = choice.reason;
    audit(state, program, SurveyProgramAuditKind::TaskSelected, hooks,
          choice.reason, choice.bodyId, choice.passNumber);
}

void performSurveyWork(GameState& state, SurveyProgram& program, const Fleet& fleet,
                       const SurveyTeam& team, const SurveyProgramExecutionHooks& hooks) {
    if (program.lifecycle != SurveyProgramLifecycle::Authorized ||
        program.task != SurveyProgramTask::Survey || !program.taskBodyId.has_value() ||
        !stationary(fleet) || fleet.currentBodyId != *program.taskBodyId ||
        team.locationKind != SurveyTeamLocationKind::Fleet || team.fleetId != fleet.id ||
        evaluateFleetSurvey(state, fleet).operationalCapability <= 0.0) return;

    if (program.workDaysCompleted == 0) program.firstWorkDay = state.date.day;
    ++program.workDaysCompleted;
    ++program.totalWorkDays;
    if (program.workDaysCompleted < kP3ASurveyWorkDaysPerPass) return;

    // This is the only hidden-geology access in the executor. It runs once at
    // the timed completion boundary, after physical team/equipment checks.
    const ResourceSurveyCompletedEvent result = applyResourceSurveyResult(
        state, fleet.id, *program.taskBodyId);
    program.receipts.push_back(SurveyVisitReceipt{
        .bodyId = *program.taskBodyId,
        .passNumber = program.taskPassNumber,
        .fleetId = fleet.id,
        .teamId = team.id,
        .leaderId = program.taskLeaderId,
        .approach = program.taskApproach,
        .firstWorkDay = program.firstWorkDay,
        .completedDay = state.date.day,
        .workDays = kP3ASurveyWorkDaysPerPass,
        .depositsImproved = result.depositsImproved,
        .averageConfidenceBefore = result.averageConfidenceBefore,
        .averageConfidenceAfter = result.averageConfidenceAfter
    });
    hooks.emit(EventSeverity::Info, result);
    audit(state, program, SurveyProgramAuditKind::VisitCompleted, hooks,
          result.depositsImproved == 0 ? "No new information from this pass" : "Timed survey pass completed",
          program.taskBodyId, program.taskPassNumber);
    program.task = SurveyProgramTask::Return;
    program.workDaysCompleted = 0;
    program.firstWorkDay = 0;
    if (surveyCharterFinished(program)) {
        program.lifecycle = SurveyProgramLifecycle::Closing;
        program.closure = SurveyProgramClosure::Completed;
    }
}

void dispatchTargetFromHome(GameState& state, SurveyProgram& program, Fleet& fleet,
                            SurveyTeam& team, Colony& home, const SurveyTargetChoice& choice,
                            const SurveyProgramExecutionHooks& hooks) {
    if (!stationary(fleet) || fleet.currentBodyId != home.bodyId ||
        team.locationKind != SurveyTeamLocationKind::Fleet || team.fleetId != fleet.id ||
        evaluateFleetSurvey(state, fleet).operationalCapability <= 0.0) return;

    if (choice.bodyId == home.bodyId) {
        if (program.task == SurveyProgramTask::None) {
            selectTask(state, program, fleet, team, choice, hooks, true);
        } else {
            program.task = SurveyProgramTask::Survey;
        }
        return;
    }

    const auto today = sortieBudget(state, program, fleet, home.bodyId, choice.bodyId, state.date.day);
    const double onBoard = fleetFuel(state, fleet);
    if (today.has_value() && onBoard + kFuelEpsilon >= today->requiredFuel) {
        if (startLeg(program, fleet, choice.bodyId, hooks)) {
            if (program.task == SurveyProgramTask::None) {
                selectTask(state, program, fleet, team, choice, hooks, false);
            } else {
                // A preserved partial visit keeps its original participants,
                // leader decision, and work interval across real repositioning.
                program.task = SurveyProgramTask::Outbound;
            }
        }
        return;
    }
    if (state.date.day == std::numeric_limits<std::int64_t>::max()) return;
    const auto tomorrow = sortieBudget(state, program, fleet, home.bodyId, choice.bodyId,
                                       state.date.day + 1);
    if (!tomorrow.has_value()) return;
    const double needed = std::max(0.0, tomorrow->requiredFuel - onBoard);
    static_cast<void>(transferHomeFuel(state, program, fleet, home, needed, hooks, choice.bodyId));
}

void returnToHome(GameState& state, SurveyProgram& program, Fleet& fleet,
                  const SurveyTeam& team, const Colony& home,
                  const SurveyProgramExecutionHooks& hooks) {
    if (!stationary(fleet) || fleet.currentBodyId == home.bodyId) return;
    const double cost = adjustedFleetMoveFuelCost(state, fleet, fleet.currentBodyId,
                                                  home.bodyId, state.date.day);
    if (!std::isfinite(cost) || fleetFuel(state, fleet) + kFuelEpsilon < cost) return;
    const BodyId departureBodyId = fleet.currentBodyId;
    const SurveyPlanningApproach decisionApproach = approachFor(state, program);
    if (!startLeg(program, fleet, home.bodyId, hooks)) return;
    if (program.task == SurveyProgramTask::None) {
        // A newly acquired remote fleet first returns to the authorized base.
        // Pass zero denotes repositioning before any target has been selected.
        program.task = SurveyProgramTask::Return;
        program.taskBodyId = departureBodyId;
        program.taskFleetId = fleet.id;
        program.taskTeamId = team.id;
        program.taskLeaderId = program.charter.requestedLeaderId;
        program.taskApproach = decisionApproach;
        program.taskPassNumber = 0;
    }
}

void runAuthorizedProgram(GameState& state, SurveyProgram& program,
                          const SurveyProgramExecutionHooks& hooks,
                          Occupied& occupiedFleets, Occupied& occupiedTeams) {
    Colony* home = byId(state.colonies, program.charter.homeColonyId);
    if (home == nullptr) return;
    if (!acquireLease(state, program, *home, occupiedFleets, occupiedTeams)) return;
    Fleet* fleet = byId(state.fleets, *program.leasedFleetId);
    SurveyTeam* team = byId(state.surveyTeams, *program.leasedTeamId);
    if (fleet == nullptr || team == nullptr || !stationary(*fleet) || !fleet->queuedOrders.empty()) return;
    embarkAtHome(*team, *fleet, *home);

    if (program.task == SurveyProgramTask::None && requestedAssetsDifferFromLease(program)) {
        // A quiescent amendment takes effect before selecting another sortie.
        // The old assets stay unavailable to other programs until tomorrow's
        // arbitration phase, but their physical locations are not rewritten.
        static_cast<void>(releaseSurveyProgramLease(state, program));
        applyPendingHome(program);
        return;
    }

    if (program.task == SurveyProgramTask::Return && fleet->currentBodyId == home->bodyId) {
        clearTask(program);
        if (program.lifecycle == SurveyProgramLifecycle::Authorized &&
            !surveyCharterFinished(program) && requestedAssetsDifferFromLease(program)) {
            static_cast<void>(releaseSurveyProgramLease(state, program));
            applyPendingHome(program);
            return;
        }
        applyPendingHome(program);
        home = byId(state.colonies, program.charter.homeColonyId);
        if (home == nullptr) return;
    }
    if (program.lifecycle == SurveyProgramLifecycle::Closing) {
        if (program.closure != SurveyProgramClosure::Completed) return;
        if (fleet->currentBodyId == home->bodyId) {
            clearTask(program);
            releaseLease(state, program, hooks);
        } else {
            returnToHome(state, program, *fleet, *team, *home, hooks);
        }
        return;
    }
    if (program.task == SurveyProgramTask::None && requestedAssetsDifferFromLease(program)) {
        static_cast<void>(releaseSurveyProgramLease(state, program));
        applyPendingHome(program);
        return;
    }

    if (program.task == SurveyProgramTask::Survey || program.task == SurveyProgramTask::Outbound) {
        if (!program.taskBodyId.has_value()) return;
        if (fleet->currentBodyId == *program.taskBodyId) {
            program.task = SurveyProgramTask::Survey;
            performSurveyWork(state, program, *fleet, *team, hooks);
        } else if (fleet->currentBodyId == home->bodyId) {
            const SurveyTargetChoice preserved{
                .bodyId = *program.taskBodyId,
                .passNumber = program.taskPassNumber,
                .reason = "Resuming the committed partial survey visit"
            };
            dispatchTargetFromHome(state, program, *fleet, *team, *home, preserved, hooks);
        } else {
            returnToHome(state, program, *fleet, *team, *home, hooks);
        }
        return;
    }
    if (program.task == SurveyProgramTask::Return) {
        returnToHome(state, program, *fleet, *team, *home, hooks);
        return;
    }
    if (fleet->currentBodyId != home->bodyId) {
        returnToHome(state, program, *fleet, *team, *home, hooks);
        return;
    }
    if (surveyCharterFinished(program)) {
        program.lifecycle = SurveyProgramLifecycle::Closing;
        program.closure = SurveyProgramClosure::Completed;
        releaseLease(state, program, hooks);
        return;
    }
    if (!program.charter.requestedLeaderId.has_value()) return;
    const auto candidates = candidateBudgets(state, program, *fleet, *home, hooks.opening);
    const SurveyPlanningApproach approach = approachFor(state, program);
    std::optional<SurveyTargetChoice> choice = chooseFrom(candidates, approach, CandidateAvailability::Current);
    if (!choice.has_value()) choice = chooseFrom(candidates, approach, CandidateAvailability::AuthorizedFuture);
    if (!choice.has_value()) choice = chooseFrom(candidates, approach, CandidateAvailability::Tank);
    if (!choice.has_value() || budgetForChoice(candidates, *choice) == nullptr) return;
    dispatchTargetFromHome(state, program, *fleet, *team, *home, *choice, hooks);
}

[[nodiscard]] std::string authorizationSignature(const SurveyProgram& program) {
    return "authorization:" + std::to_string(program.charterRevision);
}

[[nodiscard]] constexpr const char* authorizationMessage() noexcept {
    return "Additional propellant authorization is exhausted for the remaining survey work";
}

// Uses the same known route/tank forecasts as dispatch. A cap is an immediate
// known deficiency only when no unfinished tank-feasible target can be
// supported by current fuel plus the charter's remaining authorization.
[[nodiscard]] bool knownAuthorizationShortfall(const GameState& state,
                                               const SurveyProgram& program) {
    if (!program.charter.policy.maxAdditionalPropellant.has_value() || surveyCharterFinished(program)) {
        return false;
    }
    const std::optional<FleetId> fleetId = program.leasedFleetId.has_value()
        ? program.leasedFleetId
        : (program.taskFleetId.has_value() ? program.taskFleetId : program.charter.requestedFleetId);
    const Fleet* fleet = fleetId.has_value() ? byId(state.fleets, *fleetId) : nullptr;
    const Colony* home = byId(state.colonies, program.charter.homeColonyId);
    if (fleet == nullptr || home == nullptr) return false;
    bool tankFeasible = false;
    for (const CandidateBudget& candidate : candidateBudgets(state, program, *fleet, *home)) {
        if (candidate.availableNow || candidate.potentiallyAffordable) return false;
        tankFeasible = tankFeasible || candidate.tankFeasible;
    }
    return tankFeasible;
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>> consequentialIssue(
    const GameState& state, const SurveyProgram& program) {
    if (program.lifecycle != SurveyProgramLifecycle::Authorized &&
        !(program.lifecycle == SurveyProgramLifecycle::Closing &&
          program.closure == SurveyProgramClosure::Completed)) return std::nullopt;
    if (!program.leasedFleetId.has_value()) return std::nullopt;
    const Fleet* fleet = byId(state.fleets, *program.leasedFleetId);
    const Colony* home = byId(state.colonies, program.charter.homeColonyId);
    if (fleet == nullptr || home == nullptr || !stationary(*fleet)) return std::nullopt;
    if (fleet->currentBodyId != home->bodyId &&
        (program.task == SurveyProgramTask::Return || program.task == SurveyProgramTask::None ||
         (program.taskBodyId.has_value() && fleet->currentBodyId != *program.taskBodyId))) {
        const double back = adjustedFleetMoveFuelCost(state, *fleet, fleet->currentBodyId,
                                                      home->bodyId, state.date.day);
        if (!std::isfinite(back) || fleetFuel(state, *fleet) + kFuelEpsilon < back) {
            return std::pair{
                "return:" + std::to_string(program.charterRevision) + ":" +
                    std::to_string(fleet->currentBodyId.value),
                "Return leg is blocked by the fleet's actual fuel or route conditions"
            };
        }
    }
    if (fleet->currentBodyId != home->bodyId || program.task != SurveyProgramTask::None ||
        !program.charter.policy.maxAdditionalPropellant.has_value() || surveyCharterFinished(program)) {
        return std::nullopt;
    }
    // A cap that made the charter unready at authorization is already known to
    // the player. Interrupt only after actual activity consumes the commitment.
    const bool madePhysicalProgress = program.fuelBurned > 0.0 || program.fuelLoaded > 0.0 ||
                                      program.totalWorkDays > 0 || !program.receipts.empty();
    if (knownAuthorizationShortfall(state, program) && madePhysicalProgress) {
        return std::pair{
            authorizationSignature(program), authorizationMessage()
        };
    }
    return std::nullopt;
}

void updateIssue(GameState& state, SurveyProgram& program, const SurveyProgramExecutionHooks& hooks) {
    if (program.lifecycle == SurveyProgramLifecycle::Suspended && program.issue.acknowledged) {
        // Suspension itself is a player response to the known limitation. Keep
        // its signature so an unchanged return constraint does not re-prompt
        // immediately on resume; no physical work occurs while suspended.
        return;
    }
    const auto cause = consequentialIssue(state, program);
    if (!cause.has_value()) {
        // An accepted cap shortage may be latent during a committed transit.
        // Keep its acknowledged signature until its known route constraint
        // resolves or a new charter revision changes the decision context.
        if (!(program.issue.acknowledged &&
              program.issue.signature == authorizationSignature(program) &&
              knownAuthorizationShortfall(state, program))) {
            program.issue = SurveyProgramIssue{};
        }
        return;
    }
    if (program.issue.signature == cause->first) return;
    program.issue = SurveyProgramIssue{
        .signature = cause->first,
        .message = cause->second,
        .acknowledged = false
    };
    audit(state, program, SurveyProgramAuditKind::IssueRaised, hooks, cause->second, program.taskBodyId);
}

void writeDueReport(GameState& state, SurveyProgram& program,
                    const SurveyProgramExecutionHooks& hooks) {
    if (state.date.day < program.nextReportDay) return;
    const SurveyPlanningApproach approach = approachFor(state, program);
    std::optional<BodyId> fleetBody;
    if (program.leasedFleetId.has_value()) {
        if (const Fleet* fleet = byId(state.fleets, *program.leasedFleetId)) fleetBody = fleet->currentBodyId;
    }
    program.reports.push_back(SurveyProgramReport{
        .startDay = program.reportStartDay,
        .endDay = state.date.day,
        .isNinetyDayReview = state.date.day % 90 == 0,
        .charterRevision = program.charterRevision,
        .leaderId = program.taskLeaderId.has_value() ? program.taskLeaderId : program.charter.requestedLeaderId,
        .approach = approach,
        .visitsCompleted = static_cast<int>(program.receipts.size()) - program.reportedVisits,
        .workDays = program.totalWorkDays - program.reportedWorkDays,
        .fuelLoaded = program.fuelLoaded - program.reportedFuelLoaded,
        .fuelBurned = program.fuelBurned - program.reportedFuelBurned,
        .fleetId = program.leasedFleetId,
        .teamId = program.leasedTeamId,
        .fleetBodyId = fleetBody,
        .waitingReason = surveyProgramExecutionCondition(state, program)
    });
    program.reportedVisits = static_cast<int>(program.receipts.size());
    program.reportedWorkDays = program.totalWorkDays;
    program.reportedFuelLoaded = program.fuelLoaded;
    program.reportedFuelBurned = program.fuelBurned;
    program.reportStartDay = state.date.day + 1;
    program.nextReportDay = nextGlobalSurveyBoundary(state.date.day, 30);
    audit(state, program, SurveyProgramAuditKind::ReportPublished, hooks,
          program.reports.back().isNinetyDayReview ? "90-day program review published"
                                                    : "30-day program report published");
}

} // namespace

std::optional<double> projectedSurveySortieFuel(const GameState& state,
                                                const SurveyProgram& program,
                                                const Fleet& fleet,
                                                const BodyId homeBodyId,
                                                const BodyId targetBodyId,
                                                const std::int64_t departureDay) {
    const auto budget = sortieBudget(state, program, fleet, homeBodyId, targetBodyId, departureDay);
    return budget ? std::optional<double>{budget->requiredFuel} : std::nullopt;
}

bool releaseSurveyProgramLease(GameState& state, SurveyProgram& program) {
    if (!program.leasedFleetId.has_value() && !program.leasedTeamId.has_value()) return true;
    if (!program.leasedFleetId.has_value() || !program.leasedTeamId.has_value()) return false;
    Fleet* fleet = byId(state.fleets, *program.leasedFleetId);
    SurveyTeam* team = byId(state.surveyTeams, *program.leasedTeamId);
    const Colony* home = byId(state.colonies, program.charter.homeColonyId);
    if (fleet == nullptr || team == nullptr || !stationary(*fleet)) return false;
    if (home != nullptr && fleet->currentBodyId == home->bodyId &&
        team->locationKind == SurveyTeamLocationKind::Fleet && team->fleetId == fleet->id) {
        team->locationKind = SurveyTeamLocationKind::Colony;
        team->colonyId = home->id;
        team->fleetId.reset();
    }
    program.leasedFleetId.reset();
    program.leasedTeamId.reset();
    return true;
}

std::string surveyProgramExecutionCondition(const GameState& state, const SurveyProgram& program) {
    if (program.lifecycle == SurveyProgramLifecycle::Closed) {
        return program.closure == SurveyProgramClosure::Completed ? "Completed" : "Cancelled";
    }
    if (program.lifecycle == SurveyProgramLifecycle::Suspended) {
        return program.leasedFleetId.has_value() ? "Suspending after current transit" : "Suspended";
    }
    if (program.lifecycle == SurveyProgramLifecycle::Closing &&
        program.closure == SurveyProgramClosure::Cancelled) return "Cancelling after current transit";
    if (!program.issue.signature.empty() && !program.issue.acknowledged) {
        return "Decision needed: " + program.issue.message;
    }
    const Colony* home = byId(state.colonies, program.charter.homeColonyId);
    if (home == nullptr) return "Waiting: home support is unavailable";
    const std::optional<FleetId> desiredFleet = program.taskFleetId.has_value()
        ? program.taskFleetId : program.charter.requestedFleetId;
    const std::optional<SurveyTeamId> desiredTeam = program.taskTeamId.has_value()
        ? program.taskTeamId : program.charter.requestedTeamId;
    if (!desiredFleet.has_value()) return "Waiting: no fleet requested";
    if (!desiredTeam.has_value()) return "Waiting: no survey team requested";
    if (!program.taskLeaderId.has_value() && !program.charter.requestedLeaderId.has_value()) {
        return "Waiting: no leader requested";
    }
    const Fleet* fleet = byId(state.fleets, *desiredFleet);
    const SurveyTeam* team = byId(state.surveyTeams, *desiredTeam);
    if (fleet == nullptr || team == nullptr) return "Waiting: requested asset is unavailable";
    if (!program.leasedFleetId.has_value()) {
        for (const SurveyProgram& other : state.surveyPrograms) {
            if (other.id != program.id &&
                (other.leasedFleetId == desiredFleet || other.leasedTeamId == desiredTeam)) {
                return "Waiting: requested fleet or team is committed to another program";
            }
        }
        if (!stationary(*fleet) || !fleet->queuedOrders.empty()) {
            return "Waiting: requested fleet has manual movement or a queued order";
        }
        const bool coLocatedAtHome = fleet->currentBodyId == home->bodyId &&
            team->locationKind == SurveyTeamLocationKind::Colony && team->colonyId == home->id;
        const bool aboardSameFleet = team->locationKind == SurveyTeamLocationKind::Fleet &&
                                     team->fleetId == fleet->id;
        if (!coLocatedAtHome && !aboardSameFleet) {
            return "Waiting: requested fleet and survey team are not physically co-located";
        }
    }
    if (!stationary(*fleet)) return "Traveling on committed transit";
    if (program.task == SurveyProgramTask::Survey && program.taskBodyId == fleet->currentBodyId) {
        if (team->locationKind != SurveyTeamLocationKind::Fleet || team->fleetId != fleet->id) {
            return "Waiting: survey team is not aboard the fleet";
        }
        if (evaluateFleetSurvey(state, *fleet).operationalCapability <= 0.0) {
            return "Waiting: no operational powered survey equipment";
        }
        return "Surveying timed visit";
    }
    if (fleet->currentBodyId != home->bodyId) {
        if (program.task == SurveyProgramTask::Outbound && program.taskBodyId == fleet->currentBodyId) {
            return "At survey target; next workday is due";
        }
        const double back = adjustedFleetMoveFuelCost(state, *fleet, fleet->currentBodyId,
                                                      home->bodyId, state.date.day);
        if (!std::isfinite(back) || fleetFuel(state, *fleet) + kFuelEpsilon < back) {
            return "Waiting: return leg lacks real onboard propellant";
        }
        return "Returning to authorized home support";
    }
    if (program.lifecycle == SurveyProgramLifecycle::Closing &&
        program.closure == SurveyProgramClosure::Completed) return "Completion return reached home";
    if (evaluateFleetSurvey(state, *fleet).operationalCapability <= 0.0) {
        return "Waiting: no operational powered survey equipment";
    }
    if (program.task == SurveyProgramTask::None && surveyCharterFinished(program)) {
        return "Completing charter at home";
    }
    const auto candidates = candidateBudgets(state, program, *fleet, *home);
    if (std::any_of(candidates.begin(), candidates.end(),
                    [](const CandidateBudget& row) { return row.availableNow; })) {
        const bool readyOnBoard = std::any_of(candidates.begin(), candidates.end(),
            [&state, fleet](const CandidateBudget& row) {
                return row.today.has_value() &&
                       fleetFuel(state, *fleet) + kFuelEpsilon >= row.today->requiredFuel;
            });
        return readyOnBoard ? "Ready for next home-supported sortie" : "Refueling at home before sortie";
    }
    if (std::any_of(candidates.begin(), candidates.end(),
                    [](const CandidateBudget& row) { return row.potentiallyAffordable; })) {
        return "Waiting: authorized home propellant stock is insufficient above the floor";
    }
    if (program.charter.policy.maxAdditionalPropellant.has_value()) {
        const double onBoard = fleetFuel(state, *fleet);
        const double capacity = onBoard + missingTankCapacity(state, *fleet);
        for (const CandidateBudget& row : candidates) {
            if (row.afterRefill.has_value() && row.afterRefill->requiredFuel <= capacity + kFuelEpsilon) {
                return "Waiting: additional propellant authorization is insufficient";
            }
        }
    }
    return "Waiting: no remaining target fits this fleet's known route and tank capacity";
}

void acknowledgeKnownSurveyProgramLimitAtDecision(GameState& state, SurveyProgram& program) {
    if (const auto current = consequentialIssue(state, program)) {
        program.issue = SurveyProgramIssue{
            .signature = current->first,
            .message = current->second,
            .acknowledged = true
        };
    } else if (knownAuthorizationShortfall(state, program)) {
        program.issue = SurveyProgramIssue{
            .signature = authorizationSignature(program),
            .message = authorizationMessage(),
            .acknowledged = true
        };
    } else {
        program.issue = SurveyProgramIssue{};
    }
}

void runSurveyProgramsOpeningDay(GameState& state, const SurveyProgramExecutionHooks& hooks) {
    // Existing leases occupy their resources for this entire phase, even if a
    // program releases at its opening boundary. Program vector order grants
    // the first eligible claimant a newly available fleet and team.
    OpeningProgramContext opening(state);
    SurveyProgramExecutionHooks boundedHooks = hooks;
    boundedHooks.opening = &opening;
    for (SurveyProgram& program : state.surveyPrograms) {
        runSurveyProgramOpeningDay(state, program, opening, boundedHooks);
    }
}

void runSurveyProgramOpeningDay(GameState& state, SurveyProgram& program,
                                OpeningProgramContext& opening, const SurveyProgramExecutionHooks& hooks) {
    if (program.lifecycle == SurveyProgramLifecycle::Closed) return;
    if (program.lifecycle == SurveyProgramLifecycle::Suspended ||
        (program.lifecycle == SurveyProgramLifecycle::Closing &&
         program.closure == SurveyProgramClosure::Cancelled)) {
        if (program.leasedFleetId.has_value()) {
            releaseLease(state, program, hooks);
        } else if (program.lifecycle == SurveyProgramLifecycle::Closing) {
            program.lifecycle = SurveyProgramLifecycle::Closed;
            audit(state, program, SurveyProgramAuditKind::Closed, hooks,
                  "Survey charter cancelled after safe stop");
        }
        return;
    }
    runAuthorizedProgram(state, program, hooks, opening.occupiedFleets, opening.occupiedTeams);
}

void finishSurveyProgramsDay(GameState& state, const SurveyProgramExecutionHooks& hooks) {
    for (SurveyProgram& program : state.surveyPrograms) {
        if (program.leasedFleetId.has_value()) {
            const Fleet* fleet = byId(state.fleets, *program.leasedFleetId);
            const Colony* home = byId(state.colonies, program.charter.homeColonyId);
            if (fleet != nullptr && stationary(*fleet)) {
                if (program.lifecycle == SurveyProgramLifecycle::Suspended ||
                    (program.lifecycle == SurveyProgramLifecycle::Closing &&
                     program.closure == SurveyProgramClosure::Cancelled)) {
                    releaseLease(state, program, hooks);
                } else if (home != nullptr && fleet->currentBodyId == home->bodyId &&
                           program.task == SurveyProgramTask::Return) {
                    clearTask(program);
                    if (program.lifecycle == SurveyProgramLifecycle::Authorized &&
                        !surveyCharterFinished(program) && requestedAssetsDifferFromLease(program)) {
                        static_cast<void>(releaseSurveyProgramLease(state, program));
                    }
                    applyPendingHome(program);
                    home = byId(state.colonies, program.charter.homeColonyId);
                    if (program.leasedFleetId.has_value() && home != nullptr &&
                        fleet->currentBodyId == home->bodyId &&
                        (program.lifecycle == SurveyProgramLifecycle::Closing || surveyCharterFinished(program))) {
                        program.lifecycle = SurveyProgramLifecycle::Closing;
                        program.closure = SurveyProgramClosure::Completed;
                        releaseLease(state, program, hooks);
                    } else if (surveyCharterFinished(program)) {
                        program.lifecycle = SurveyProgramLifecycle::Closing;
                        program.closure = SurveyProgramClosure::Completed;
                    }
                } else if (home != nullptr && fleet->currentBodyId == home->bodyId &&
                           program.lifecycle == SurveyProgramLifecycle::Closing &&
                           program.closure == SurveyProgramClosure::Completed) {
                    releaseLease(state, program, hooks);
                }
            }
        } else if (program.lifecycle == SurveyProgramLifecycle::Closing &&
                   program.closure == SurveyProgramClosure::Cancelled) {
            program.lifecycle = SurveyProgramLifecycle::Closed;
            audit(state, program, SurveyProgramAuditKind::Closed, hooks,
                  "Survey charter cancelled after safe stop");
        }
        updateIssue(state, program, hooks);
        writeDueReport(state, program, hooks);
    }
}

} // namespace deep
