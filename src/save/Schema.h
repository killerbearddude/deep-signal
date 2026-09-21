#pragma once

// Responsibility: declare the on-disk schema identity and its SQL structure.
// Row mapping belongs to SaveGameRepository, and graph semantics belong to
// sim/GameStateValidation. No migration path is implemented here.

#include "save/Database.h"

#include <cstdint>

namespace deep::save {

// The only on-disk version accepted by load. A persisted contract change requires
// a version decision and explicit compatibility handling; incrementing this
// constant alone does not migrate older saves.
inline constexpr std::int64_t kSchemaVersion = 10;

// Creates missing schema v10 tables/indexes and seeds an empty version table.
// Does not validate or upgrade existing tables or their version. This function
// does not start a transaction; the caller owns the atomicity boundary and must
// account for schema changes that precede a later failure.
void initializeSchema(Database& db);

// Requires exactly one canonical version value equal to kSchemaVersion. The
// caller supplies the read transaction. Throws for missing, ambiguous, malformed,
// or unsupported metadata; matching metadata is not a full schema validation.
void requireSupportedSchema(Database& db);

} // namespace deep::save
