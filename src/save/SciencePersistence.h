#pragma once
// Ordered v16 science mapping. Repository owns transactions and graph validation.
#include "save/Database.h"
#include "sim/GameState.h"
namespace deep::save {
// Creates only current scientific structures; never reads or migrates old files.
void createScienceSchema(Database&);
// Clears children before parents inside the snapshot replacement transaction.
void clearScienceState(Database&);
// Writes explicit ordered rows after operational parents exist.
void saveScienceState(Database&, const GameState&);
// Strictly reconstructs records; never samples or interprets physical geology.
void loadScienceState(Database&, GameState&);
} // namespace deep::save
