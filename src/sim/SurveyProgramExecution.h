#pragma once

// Runs the bounded daily physical work of P3A home-supported survey programs.
// Simulation owns the world clock and event IDs; callbacks keep movement fuel
// payment and audit emission on the same authoritative paths as manual actions.

#include "sim/Events.h"
#include "sim/GameState.h"
#include "sim/ProgramControl.h"

#include <functional>
#include <optional>
#include <string>

namespace deep {

struct SurveyProgramExecutionHooks {
    // Starts one physically checked leg for a leased fleet. On success the
    // callback charges real ship fuel, emits the normal movement event, and
    // writes the charged amount. Failure leaves the program intention intact.
    std::function<bool(SurveyProgramId, FleetId, BodyId, double& chargedFuel)> startProgramMove;

    // Appends a dated audit event through Simulation, including the returned
    // event vector for the current advance. The runner does not allocate IDs.
    std::function<void(EventSeverity, SimEventPayload)> emit;

    // Non-owning phase budget supplied by Simulation; null only for read-only
    // helpers and legacy survey-only callers outside the mixed dispatcher.
    OpeningProgramContext* opening = nullptr;
};

// Releases a stationary program lease without moving its fleet or team. A
// co-located team aboard at home disembarks to the real colony; a remote team
// remains aboard. Returns false while transit is active and changes nothing.
[[nodiscard]] bool releaseSurveyProgramLease(GameState& state, SurveyProgram& program);

// Explains the next known execution limit from charter, lease, team location,
// operational equipment, fuel, and public route data. It never reads deposits
// and is safe for query DTOs and pre-interaction advice.
[[nodiscard]] std::string surveyProgramExecutionCondition(const GameState& state,
                                                           const SurveyProgram& program);

// Records a fuel-cap shortage already visible in a newly authorized or amended
// charter as acknowledged. The program still waits under its actual limits;
// a later materially different cause may raise a new interruption.
void acknowledgeKnownSurveyProgramLimitAtDecision(GameState& state, SurveyProgram& program);

// Called once just after advancing the integer day, before mining/processing.
// At most one transfer, departure, or qualifying survey workday is performed
// per controlled fleet; acquisition and bookkeeping do not add another action.
void runSurveyProgramsOpeningDay(GameState& state, const SurveyProgramExecutionHooks& hooks);

// Runs one stored survey head inside the shared mixed pass, retaining occupied
// assets for the whole phase even when this program releases its lease.
void runSurveyProgramOpeningDay(GameState& state, SurveyProgram& program,
                                OpeningProgramContext& opening,
                                const SurveyProgramExecutionHooks& hooks);

// Called after movement for arrival observations, safe lease release, issue
// detection, and due 30/90-day reports. It never dispatches physical work.
void finishSurveyProgramsDay(GameState& state, const SurveyProgramExecutionHooks& hooks);

// Read-only required fuel for a single outbound pass and projected return under
// current prototype transit rules. Empty means the dates or route are invalid.
[[nodiscard]] std::optional<double> projectedSurveySortieFuel(
    const GameState& state, const SurveyProgram& program, const Fleet& fleet,
    BodyId homeBodyId, BodyId targetBodyId, std::int64_t departureDay);

} // namespace deep
