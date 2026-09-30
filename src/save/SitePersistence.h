#pragma once

// Current-schema mapping for sites, earned field construction, and operating
// history. Callers own the snapshot transaction and final domain validation.
#include "save/Database.h"
#include "sim/GameState.h"

namespace deep::save {
// Creates the current v18 site tables; no legacy reader or migration is provided.
void createSiteSchema(Database&);
// Delete site children before their construction/family/personnel parents.
void clearSiteState(Database&);
// Save after ordinary assets and engineering families/teams have been inserted.
void saveSiteState(Database&, const GameState&);
// Reconstruct ordered records into detached state; strict types and ordinals
// reject malformed snapshots before the repository publishes a new world.
void loadSiteState(Database&, GameState&);
} // namespace deep::save
