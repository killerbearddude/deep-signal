#pragma once

// Current-schema mapping for P5 opportunities, development programs, evidence,
// prototypes, local processes/support, and frozen shipyard supply plans.
#include "save/Database.h"
#include "sim/GameState.h"

namespace deep::save {
void createTechnicalSchema(Database&);
void clearTechnicalState(Database&);
void saveTechnicalState(Database&, const GameState&);
void loadTechnicalState(Database&, GameState&);
} // namespace deep::save
