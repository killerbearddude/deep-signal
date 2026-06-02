#pragma once

// Provides deterministic scenario construction for tests and the CLI smoke run.
// Scenario generation is isolated from Simulation so tests can start from known
// state without invoking gameplay commands.

#include "sim/GameState.h"

namespace deep {

// Creates the Prototype 0.1 Sol/Terra/Mars scenario with one colony, two mineral
// deposits, and the Survey Cutter ship class. The returned state is ready to pass
// into Simulation.
[[nodiscard]] GameState createHomeSystemScenario();

} // namespace deep
