#pragma once

// Current-schema mapping for durable freight intent, commitment, history, and
// physical cargo lots. The repository owns transactions and validates the whole
// graph; this module only binds/reconstructs the freight rows in that snapshot.

#include "save/Database.h"
#include "sim/GameState.h"

namespace deep::save {

// Insert freight parents and ordered children, then cargo referencing the
// committed shipment. Colonies, people, fleets, and ships must already exist.
void saveFreightState(Database& db, const GameState& state);

// Reconstruct freight and per-ship cargo into detached state. Reject malformed
// storage types, enums, ordinals, or references; never repair physical goods.
void loadFreightState(Database& db, GameState& state);

} // namespace deep::save
