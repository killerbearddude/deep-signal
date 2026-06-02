#pragma once

// Declares the application-facing service wrapper around the headless simulation.
// Future CLI, UI, and save/load workflows should depend on this layer instead of
// directly owning Simulation where application coordination is needed.

#include "sim/Commands.h"
#include "sim/Error.h"
#include "sim/Events.h"
#include "sim/GameState.h"
#include "sim/Simulation.h"

#include <filesystem>
#include <vector>

namespace deep {

// Owns the active Simulation instance for application-level use cases.
// The service coordinates repository operations so UI/CLI code does not need to
// know how GameState is persisted or how Simulation is rehydrated after loading.
class SimulationService {
public:
    // Creates a service with the default deterministic home-system scenario.
    SimulationService();

    // Creates a service from caller-provided state, usually for tests or a loaded
    // save file.
    explicit SimulationService(GameState initialState);

    // Returns a read-only view of active state. The reference remains valid until
    // the next mutating service call.
    [[nodiscard]] const GameState& state() const noexcept;

    // Executes one command through the simulation mutation boundary.
    CommandResult execute(const SimCommand& command);

    // Advances simulation time and returns events emitted during this call.
    std::vector<SimEvent> advanceDays(int days);

    // Replaces the active simulation with a fresh deterministic scenario.
    CommandResult newGame();

    // Saves the active state snapshot to a SQLite file. Returns a failed
    // CommandResult instead of throwing so UI callers can present the error.
    CommandResult saveGame(const std::filesystem::path& path) const;

    // Loads a SQLite save file and replaces the active simulation on success.
    // The existing state is preserved when loading fails.
    CommandResult loadGame(const std::filesystem::path& path);

private:
    Simulation simulation_;
};

} // namespace deep
