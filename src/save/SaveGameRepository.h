#pragma once

// Responsibility: write ordered schema v11 snapshots and read validated v10/v11.
// This boundary knows both simulation records and SQLite; sim/ remains database
// independent. App callers translate exceptions into user-facing results.

#include "sim/GameState.h"

#include <filesystem>

namespace deep::save {

// Stateless repository for full-snapshot operations. Each call owns its database
// connection and statements. Saves replace rows rather than applying incremental
// diffs. No in-place v10 migration or concurrent-save coordination is provided.
// Daily economy telemetry is session-only and is not persisted.
class SaveGameRepository {
public:
    // Borrows state, which the caller must keep unchanged for the entire call.
    // Validates it before opening path. One write transaction classifies the
    // destination, creates v11 schema only when empty, then replaces durable
    // rows. Existing valid v11 may be replaced; v10 and unrecognized databases
    // are rejected without replacement. A failed new-path save may leave an
    // empty file, while a failed replacement rolls back its previous contents.
    static void save(const std::filesystem::path& path, const GameState& state);

    // Reconstructs and returns an owned snapshot from one read transaction after
    // checking version-specific structure and foreign keys. Validates the graph
    // before returning; failure throws without exposing a partial GameState.
    // No application state is replaced here. Runtime economy telemetry starts
    // empty. Valid v10 reads use their legacy ID ordering without upgrading the
    // file; v11 reads restore explicit global and child ordering.
    [[nodiscard]] static GameState load(const std::filesystem::path& path);
};

#ifdef DEEP_SIGNAL_TESTING
// Linked only into the test-specific save library. Production builds have no
// failure switch. The next Save throws after deleting old rows and inserting
// the v11 version marker, so tests can prove transaction rollback.
void setSaveFailureInjectionForTest(bool enabled) noexcept;
#endif

} // namespace deep::save
