#include "app/SimulationService.h"

// Implements the application service wrapper for the current headless prototype.
// This layer coordinates new/load/save workflows while keeping Simulation focused
// only on deterministic rules and command processing.

#include "save/SaveGameRepository.h"
#include "sim/ScenarioFactory.h"

#include <exception>
#include <utility>

namespace deep {

SimulationService::SimulationService()
    : simulation_{createHomeSystemScenario()} {}

SimulationService::SimulationService(GameState initialState)
    : simulation_{std::move(initialState)} {}

const GameState& SimulationService::state() const noexcept {
    return simulation_.state();
}

CommandResult SimulationService::execute(const SimCommand& command) {
    return simulation_.execute(command);
}

std::vector<SimEvent> SimulationService::advanceDays(const int days) {
    return simulation_.advanceDays(days);
}

CommandResult SimulationService::newGame() {
    simulation_ = Simulation{createHomeSystemScenario()};
    return CommandResult::success("New game created");
}

CommandResult SimulationService::saveGame(const std::filesystem::path& path) const {
    try {
        save::SaveGameRepository::save(path, simulation_.state());
        return CommandResult::success("Game saved");
    } catch (const std::exception& ex) {
        return CommandResult::failure(ex.what());
    }
}

CommandResult SimulationService::loadGame(const std::filesystem::path& path) {
    try {
        // Load into a temporary state first so a failed read cannot partially
        // replace the currently-running simulation.
        GameState loadedState = save::SaveGameRepository::load(path);
        simulation_ = Simulation{std::move(loadedState)};
        return CommandResult::success("Game loaded");
    } catch (const std::exception& ex) {
        return CommandResult::failure(ex.what());
    }
}

} // namespace deep
