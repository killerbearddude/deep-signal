#pragma once

// Bounded stationary work under Simulation's common daily phase. Hooks emit
// audits only; this module never starts movement or takes client control.
#include "sim/Events.h"
#include "sim/ProgramControl.h"
#include <functional>

namespace deep {
struct MaintenanceExecutionHooks {
    std::function<void(EventSeverity, SimEventPayload)> emit;
};
// Runs at most one group's work using the current opening stock/hold snapshot.
void runMaintenanceProgramOpeningDay(GameState&, MaintenanceProgram&, OpeningProgramContext&,
                                     const MaintenanceExecutionHooks&);
// Publishes dated reports and stable issues after all physical day phases.
void finishMaintenanceProgramsDay(GameState&, const MaintenanceExecutionHooks&);
// Serialized stop/assignment changes withdraw current claims before client or
// provider assets release. Restored duty and paid materials are never reversed.
void withdrawMaintenanceJob(GameState&, MaintenanceProgram&, const std::string&,
                            const MaintenanceExecutionHooks&);
void withdrawClientService(GameState&, SurveyProgramId, const std::string&, const MaintenanceExecutionHooks&);
// Releases a job-free stationary pair; disembarks only at the actual colony.
void releaseMaintenanceAssets(GameState&, MaintenanceProgram&);
// Command-time closure uses the same exactly-once due report path as the clock.
void publishMaintenanceReportIfDue(GameState&, MaintenanceProgram&, const MaintenanceExecutionHooks&);
// A player decision acknowledges its already-known limit without changing it.
void acknowledgeKnownMaintenanceLimit(const GameState&, MaintenanceProgram&);
} // namespace deep
