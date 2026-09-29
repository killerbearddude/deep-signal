#pragma once

// Responsibility: write and read ordered snapshots in the active v13 schema.
// This boundary knows both simulation records and SQLite; sim/ remains database
// independent. App callers translate exceptions into user-facing results.

#include "sim/GameState.h"

#include <filesystem>

namespace deep::save {

// Stateless repository for full-snapshot operations. Each call owns its database
// connection and statements. Saves replace rows rather than applying incremental
// diffs. No old-schema migration or concurrent-save coordination is provided.
// Daily economy telemetry is session-only and is not persisted.
class SaveGameRepository {
public:
    // Borrows state, which the caller must keep unchanged for the entire call.
    // Validates it before opening path. One write transaction classifies the
    // destination, creates v13 schema only when empty, then replaces durable
    // rows. Existing valid v13 may be replaced; older and unrecognized databases
    // are rejected without replacement. A failed new-path save may leave an
    // empty file, while a failed replacement rolls back its previous contents.
    static void save(const std::filesystem::path& path, const GameState& state);

    // Reconstructs and returns an owned snapshot from one read transaction after
    // checking v13 structure and foreign keys. Validates the graph
    // before returning; failure throws without exposing a partial GameState.
    // No application state is replaced here. Runtime economy telemetry starts
    // empty. Older development schema versions are rejected without modifying
    // their source files.
    [[nodiscard]] static GameState load(const std::filesystem::path& path);
};

#ifdef DEEP_SIGNAL_TESTING
// Linked only into the test-specific save library. Production builds have no
// failure switch. The next Save throws after deleting old rows and inserting
// the v13 version marker, so tests can prove transaction rollback.
void setSaveFailureInjectionForTest(bool enabled) noexcept;
#endif

} // namespace deep::save
