#pragma once
// Current-schema equipment and maintenance snapshot mapping. Repository owns
// transaction/preflight/validation; these helpers preserve explicit child order.
#include "save/Database.h"
#include "sim/GameState.h"
namespace deep::save {
// Called after base tables exist to create only the active-schema additions.
void createMaintenanceSchema(Database& db);
// Deletes service children before parents inside the repository's transaction.
void clearMaintenanceState(Database& db);
// Writes after base catalog/ships/program parents, with explicit child ordinals.
void saveMaintenanceState(Database& db, const GameState& state);
// Reconstructs detached data with strict numeric/ordinal/completeness checks.
void loadMaintenanceState(Database& db, GameState& state);
} // namespace deep::save
