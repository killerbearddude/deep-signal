#pragma once

// One physical opening action per development; Simulation alone starts and pays
// fleet movement. This executor owns engineering/material receipts and closeout.
#include "sim/Events.h"
#include "sim/ProgramControl.h"
#include <functional>
namespace deep {
struct SiteDevelopmentExecutionHooks {
    std::function<bool(SiteDevelopmentProgramId, FleetId, BodyId, double&)> startProgramMove;
    std::function<void(EventSeverity, SimEventPayload)> emit;
    std::function<void(std::size_t)> prepareEvents;
};
// Uses only opening stock and finite unleased participants; no incoming credits.
void runSiteDevelopmentOpeningDay(GameState&, SiteDevelopmentProgram&, OpeningProgramContext&,
                                  const SiteDevelopmentExecutionHooks&);
// Publishes stable interruption episodes and periodic owned reports after work.
void finishSiteDevelopmentsDay(GameState&, const SiteDevelopmentExecutionHooks&);
} // namespace deep
