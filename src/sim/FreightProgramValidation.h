#pragma once

// Rejects contradictory current-format cargo custody, ordered history and
// accounting. Legitimate readiness shortages and historical overfulfilment are
// valid; this boundary never repairs a save by deleting or creating material.

#include "sim/GameState.h"

namespace deep {
void validateFreightProgramState(const GameState&);
} // namespace deep
