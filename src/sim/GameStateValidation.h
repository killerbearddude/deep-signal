#pragma once

// Declares invariant validation for complete GameState snapshots.
// This module belongs to the simulation layer because the rules define domain
// consistency, not persistence mechanics; SQLite loaders and public constructors
// both call it before accepting externally supplied state.

#include "sim/GameState.h"

namespace deep {

// Read-only trust-boundary check; throws std::runtime_error at the first failed
// invariant and never repairs the snapshot. Checks persistent records and audit
// history; runtime-only dailyEconomySnapshots are outside this validation pass.
// Call this at trust boundaries: scenario construction tests, save-file loading,
// and APIs that accept caller-provided GameState snapshots.
void validateGameState(const GameState& state);

} // namespace deep
