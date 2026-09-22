#pragma once

// Declares the ImGui save/load control panel.
// The panel stores only UI-local path/status text and routes persistence actions
// through SimulationService so UI code never talks to the save repository.

#include "app/InformationInteractionAdapter.h"

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
    void render(SimulationService& service, InformationInteractionAdapter& interactions, bool& visible);

    // Both menu and panel buttons use this adapter path. Actual success clears
    // selection and world-bound workflows before another panel can consume them.
    void newGame(InformationInteractionAdapter& interactions);

    // Saves to the typed path, resolved relative to the process working directory
    // when not absolute. Empty paths and service failures become visible status.
    void save(SimulationService& service);

    // Loads from the typed path; service-reported failure leaves gameplay state
    // intact. Success replaces the world without reconstructing UI panels.
    void load(InformationInteractionAdapter& interactions);

private:
    static constexpr std::size_t kPathBufferSize = 512;

    [[nodiscard]] std::string currentPath() const;
    void applyResult(const char* actionName, const CommandResult& result);

    std::array<char, kPathBufferSize> pathBuffer_{"saves/deep_signal.sqlite"};
    std::string statusMessage_ = "Ready";
    bool lastActionSucceeded_ = true;
};

} // namespace deep::ui_imgui
