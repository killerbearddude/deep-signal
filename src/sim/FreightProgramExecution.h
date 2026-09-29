#pragma once

// One bounded opening action for one freight program. Simulation coordinates
// mixed ownership/budgets, checked physical departures, dates and event IDs.

#include "sim/Events.h"
#include "sim/GameState.h"

#include <functional>

namespace deep {

struct OpeningProgramContext;
struct FreightProgramExecutionHooks {
    std::function<bool(FreightProgramId, FleetId, BodyId, double&)> startProgramMove;
    std::function<void(EventSeverity, SimEventPayload)> emit;
};

void runFreightProgramOpeningDay(GameState&, FreightProgram&, OpeningProgramContext&,
                                 const FreightProgramExecutionHooks&);
void finishFreightProgramsDay(GameState&, const FreightProgramExecutionHooks&);
// Safe release is forbidden with real cargo or active transit. It never moves
// a ship/team or returns engine fuel; unresolved task identity stays durable.
[[nodiscard]] bool releaseFreightProgramLease(GameState&, FreightProgram&);
// Commands use this to settle an immediately quiescent cancellation and to
// acknowledge a current known readiness cause without manufacturing resources.
void settleFreightCancellationAtDecision(GameState&, FreightProgram&, const FreightProgramExecutionHooks&);
void acknowledgeKnownFreightLimitAtDecision(const GameState&, FreightProgram&);

} // namespace deep
