#pragma once

// Responsibility: declare the current on-disk schema and inspect the structure
// of an existing destination. Row mapping belongs to SaveGameRepository, and
// graph semantics belong to sim/GameStateValidation. No migration is performed.

#include "save/Database.h"

#include <cstdint>

namespace deep::save {

inline constexpr std::int64_t kLegacySchemaVersion = 10;
inline constexpr std::int64_t kSchemaVersion = 11;

// Creates the v11 table/index structure in a schema-empty database. Does not
// seed schema_version, begin a transaction, repair a table, or migrate v10.
// The repository owns the write transaction and version-row insertion.
void createSchemaV11(Database& db);

// True only when no user schema objects exist. SQLite internal objects are
// ignored; unrelated tables/views/indexes make a destination nonempty.
[[nodiscard]] bool hasUserSchema(Database& db);

// Requires exactly one canonical schema_version value and returns it for
// version-specific repository dispatch. The caller supplies the transaction.
[[nodiscard]] std::int64_t readSchemaVersion(Database& db);

// Compare table columns, foreign keys, index/key shapes, and user object names
// against the v11 schema. Read-only Load may tolerate known-table triggers;
// Save rejects all user triggers because their write effects are not trusted.
// Does not repair or modify the destination.
void requireV11Structure(Database& db, bool allowKnownTableTriggers = false);

// Read-only compatibility check for the legacy v10 table/column/key shape.
// Rejects a v11 structure merely relabeled as 10; no migration or repair.
void requireV10Structure(Database& db);

} // namespace deep::save
