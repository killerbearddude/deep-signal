#pragma once

// Declares the SQLite repository that maps GameState to schema v2 save files.
// This boundary is the only layer that knows both simulation records and SQLite;
// sim/ remains database-independent and app/ calls this repository for save/load.

#include "sim/GameState.h"

#include <filesystem>

namespace deep::save {

// Stateless repository for full-file save/load operations. Prototype 0.1 uses a
// replace-all save strategy because the state graph is small and deterministic.
class SaveGameRepository {
public:
    // Saves a complete GameState snapshot to path. The operation uses a write
    // transaction and prepared statements for every value-bearing SQL command.
    static void save(const std::filesystem::path& path, const GameState& state);

    // Loads a complete GameState snapshot from path. The operation uses a read
    // transaction so all rows are read from one consistent schema v2 snapshot.
    [[nodiscard]] static GameState load(const std::filesystem::path& path);
};

} // namespace deep::save
