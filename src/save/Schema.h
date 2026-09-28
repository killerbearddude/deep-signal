#pragma once

// Responsibility: declare the current on-disk schema and inspect the structure
// of an existing destination. Row mapping belongs to SaveGameRepository, and
// graph semantics belong to sim/GameStateValidation. Legacy loads convert in
// memory; no source file is migrated in place.

#include "save/Database.h"

#include <cstdint>

namespace deep::save {

inline constexpr std::int64_t kLegacySchemaVersion = 10;
inline constexpr std::int64_t kPreviousSchemaVersion = 11;
inline constexpr std::int64_t kSchemaVersion = 12;

// V11 remains the structural reference for supported legacy reads. V12 is the
// current writer shape. Neither seeds rows nor begins a transaction.
// The repository owns the write transaction and version-row insertion.
void createSchemaV11(Database& db);
void createSchemaV12(Database& db);

// True only when no user schema objects exist. SQLite internal objects are
// ignored; unrelated tables/views/indexes make a destination nonempty.
[[nodiscard]] bool hasUserSchema(Database& db);

// Requires exactly one canonical schema_version value and returns it for
// version-specific repository dispatch. The caller supplies the transaction.
[[nodiscard]] std::int64_t readSchemaVersion(Database& db);

// Compare table columns, foreign keys, index/key shapes, and user object names
// against the selected schema. Read-only Load may tolerate known-table triggers;
// Save rejects all user triggers because their write effects are not trusted.
// Does not repair or modify the destination.
void requireV11Structure(Database& db, bool allowKnownTableTriggers = false);
void requireV12Structure(Database& db, bool allowKnownTableTriggers = false);

// Read-only compatibility check for the legacy v10 table/column/key shape.
// Rejects a v11 structure merely relabeled as 10; no migration or repair.
void requireV10Structure(Database& db);

} // namespace deep::save
