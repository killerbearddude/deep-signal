#pragma once

// Responsibility: declare the current on-disk schema and inspect the structure
// of an existing destination. Row mapping belongs to SaveGameRepository, and
// graph semantics belong to sim/GameStateValidation. Pre-release files from
// older schemas are rejected without modifying them.

#include "save/Database.h"

#include <cstdint>

namespace deep::save {

inline constexpr std::int64_t kSchemaVersion = 14;

// Creates only the active v14 table/index structure in a schema-empty database.
// The repository owns the transaction and version-row insertion.
void createSchemaV14(Database& db);

// True only when no user schema objects exist. SQLite internal objects are
// ignored; unrelated tables/views/indexes make a destination nonempty.
[[nodiscard]] bool hasUserSchema(Database& db);

// Requires exactly one canonical schema_version value and returns it for
// version-specific repository dispatch. The caller supplies the transaction.
[[nodiscard]] std::int64_t readSchemaVersion(Database& db);

// Compare table columns, foreign keys, index/key shapes, and user object names
// against v14. Read-only Load may tolerate known-table triggers;
// Save rejects all user triggers because their write effects are not trusted.
// Does not repair or modify the destination.
void requireV14Structure(Database& db, bool allowKnownTableTriggers = false);

} // namespace deep::save
