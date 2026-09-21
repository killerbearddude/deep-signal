#pragma once

// Responsibility: own the application's active simulation and coordinate
// new/save/load operations. Gameplay mutations remain in Simulation; persistence
// encoding remains in SaveGameRepository. This layer has no UI dependencies.

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
// Threading: callers must serialize reads, commands, and file operations. There
// is no locking, background saving, or immutable snapshot publication here.
class SimulationService {
public:
    // Creates a service with the default deterministic home-system scenario.
    SimulationService();

    // Takes ownership of caller-provided state. Simulation validation may throw
    // if the state violates domain invariants; construction does not catch it.
    explicit SimulationService(GameState initialState);

    // Borrows live state, not a frozen snapshot. Later commands change the viewed
    // values and may invalidate references into its vectors. Copy the GameState
    // to retain an independent snapshot; never retain entity pointers across a
    // mutating call or use the borrow after this service is destroyed.
    [[nodiscard]] const GameState& state() const noexcept;

    // Executes one command through the simulation mutation boundary. Validation
    // rejections return a failed result and may append a warning event; unexpected
    // simulation exceptions propagate. This call does not save automatically.
    CommandResult execute(const SimCommand& command);

    // Advances by a count of whole simulation days and returns emitted audit
    // events, which are also kept in state. Nonpositive input produces a warning
    // without advancing; simulation exceptions propagate to the caller.
    std::vector<SimEvent> advanceDays(int days);

    // Replaces the active simulation with a fresh deterministic scenario. This
    // does not save the old game or reset selection held by separate UI objects.
    CommandResult newGame();

    // Synchronously saves live state to SQLite without changing simulation state.
    // Repository std::exception failures become a failed CommandResult; file
    // replacement and transaction guarantees belong to SaveGameRepository.
    CommandResult saveGame(const std::filesystem::path& path) const;

    // Loads a SQLite save file and replaces the active simulation on success.
    // The existing state is preserved when loading or validation fails. Loading
    // does not reconcile app-owned selections or cached DTOs with the new world.
    CommandResult loadGame(const std::filesystem::path& path);

private:
    Simulation simulation_;
};

} // namespace deep
