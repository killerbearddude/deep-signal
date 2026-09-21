#pragma once

// Responsibility: map durable GameState records to and from SQLite schema v10.
// This boundary knows both simulation records and SQLite; sim/ remains database
// independent. App callers translate exceptions into user-facing results.

#include "sim/GameState.h"

#include <filesystem>

namespace deep::save {

// Stateless repository for full-snapshot operations. Each call owns its database
// connection and statements. The current small state graph uses replacement rows
// instead of incremental diffs; no migration or concurrent-save coordination is
// provided. Daily economy telemetry is session-only and is not persisted.
class SaveGameRepository {
public:
    // Borrows state, which the caller must keep unchanged for the entire call.
    // Validates it before opening path, then initializes schema and replaces all
    // durable rows in a write transaction. Failed replacement attempts rollback;
    // file creation/schema setup precede that transaction and
    // may remain after failure. Existing paths are assumed schema-compatible;
    // save does not run load's version check or migrate incompatible databases.
    static void save(const std::filesystem::path& path, const GameState& state);

    // Reconstructs and returns an owned snapshot from one read transaction after
    // checking schema identity and foreign keys. Validates the assembled graph
    // before returning; failure throws without exposing a partial GameState.
    // No application state is replaced here. Runtime economy telemetry starts
    // empty, and unsupported schema versions are rejected rather than migrated.
    [[nodiscard]] static GameState load(const std::filesystem::path& path);
};

} // namespace deep::save
