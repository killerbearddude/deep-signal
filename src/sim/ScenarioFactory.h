#pragma once

// Provides deterministic scenario construction for new games and tests.
// Scenario generation is isolated from Simulation so tests can start from known
// state without invoking gameplay commands.

#include "sim/GameState.h"

namespace deep {

// Creates the hand-authored Sol scenario: settled industrial bodies, frontier
// deposits, institutions, personnel/appointments, and the Survey Cutter class.
// Starts on day zero without built ships or production orders. Returns a detached
// snapshot; Simulation validates it when taking ownership.
[[nodiscard]] GameState createHomeSystemScenario();

// Dedicated P3A proof fixture: a fixed home base, one real survey fleet/team,
// two equally skilled leaders with different planning approaches, and three
// nearby public targets. No program is pre-authorized; tests submit commands.
[[nodiscard]] GameState createDelegatedSurveyScenario();

// Freight proof fixture: two fixed nearby colonies, a real two-bay freighter,
// source stock and a separate receiving survey fleet/team waiting for supply.
[[nodiscard]] GameState createDelegatedFreightScenario();

// Isolated ten-duty instrument, existing-colony tender and finite engineer.
// No programs are pre-authorized; source stock supports independent arithmetic.
[[nodiscard]] GameState createTenderMaintenanceScenario();
// Same service colony with empty parts bins and two real remote freighters,
// each able to deliver one recipe ingredient after explicit authorization.
[[nodiscard]] GameState createMaintenanceSupplyScenario();

// Fixed public body and finite home industry for the earned P4B inspection loop.
// No sites, programs, built hulls or knowledge are pre-created.
[[nodiscard]] GameState createSiteDevelopmentScenario(bool usefulIce = true);
} // namespace deep
