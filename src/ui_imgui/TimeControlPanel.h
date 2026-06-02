#pragma once

// Declares the first interactive ImGui time-control panel.
// The panel mutates simulation time only through SimulationService commands and
// does not read or own raw GameState records.

#include "app/SimulationService.h"

#include <string>

namespace deep::ui_imgui {

// Renders simple deterministic time advancement controls for the prototype UI.
// The panel stores only the last command result so gameplay state remains owned
// by SimulationService.
class TimeControlPanel {
public:
    // Draws Advance 1/5/30 day buttons and submits accepted clicks through the
    // application service command boundary, updating visibility when closed.
    void render(SimulationService& service, bool& visible);

private:
    void advance(SimulationService& service, int days);

    std::string lastCommandMessage_ = "Ready";
    bool lastCommandSucceeded_ = true;
};

} // namespace deep::ui_imgui
