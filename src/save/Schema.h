#pragma once

// Declares SQLite schema management for Deep Signal save files.
// Schema creation and version checks are separate from repository mapping so the
// persistence contract can evolve through explicit migrations later.

#include "save/Database.h"

#include <cstdint>

namespace deep::save {

// Current on-disk schema version supported by this prototype. Any future schema
// change that alters persisted rows must increment this value and add migration.
inline constexpr std::int64_t kSchemaVersion = 6;

// Creates schema v6 tables and indexes if they do not exist, then ensures the
// schema_version table contains the current version for new databases.
void initializeSchema(Database& db);

// Reads and validates the schema version inside an open transaction. Throws when
// the save file is missing version metadata or uses an unsupported version.
void requireSupportedSchema(Database& db);

} // namespace deep::save
