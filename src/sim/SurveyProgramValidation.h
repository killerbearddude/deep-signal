#pragma once

// Checks the durable P3A graph after general GameState IDs/references and before
// publishing a loaded snapshot. Waiting intent is valid; contradictory physical
// leases, locations, receipts, reports, and numeric accounting are not.

#include "sim/GameState.h"

namespace deep {

void validateSurveyProgramState(const GameState& state);

} // namespace deep
