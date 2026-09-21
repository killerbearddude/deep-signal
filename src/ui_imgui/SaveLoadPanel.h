#pragma once

// Declares the ImGui save/load control panel.
// The panel stores only UI-local path/status text and routes persistence actions
// through SimulationService so UI code never talks to the save repository.

#include "app/SimulationService.h"

#include <array>
#include <cstddef>
#include <string>

namespace deep::ui_imgui {

// Renders simple manual save/load controls for the prototype shell.
// Native file dialogs and autosave are intentionally deferred so this remains a
// small app-layer workflow over the existing SQLite save/load service methods.
class SaveLoadPanel {
public:
    // Draws the path field, action buttons, latest status, and close state.
    void render(SimulationService& service, bool& visible);

    // Replaces the active scenario immediately; this panel has no unsaved-change
    // prompt and does not reset other panels' selection or draft state.
    void newGame(SimulationService& service);

    // Saves to the typed path, resolved relative to the process working directory
    // when not absolute. Empty paths and service failures become visible status.
    void save(SimulationService& service);

    // Loads from the typed path; service-reported failure leaves gameplay state
    // intact. Success replaces the world without reconstructing UI panels.
    void load(SimulationService& service);

private:
    static constexpr std::size_t kPathBufferSize = 512;

    [[nodiscard]] std::string currentPath() const;
    void applyResult(const char* actionName, const CommandResult& result);

    std::array<char, kPathBufferSize> pathBuffer_{"saves/deep_signal.sqlite"};
    std::string statusMessage_ = "Ready";
    bool lastActionSucceeded_ = true;
};

} // namespace deep::ui_imgui
