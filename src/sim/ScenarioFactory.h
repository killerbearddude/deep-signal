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

} // namespace deep
