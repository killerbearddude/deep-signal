#pragma once

// Presentation-only window entry point. The shell supplies one shared boundary
// each frame; ordinary panels retain their existing Begin/End and saved identity.

#include "ui_imgui/ShellLayout.h"

namespace deep::ui_imgui {

void setOperationalWorkArea(ShellRegion work);

// Same Begin/End contract as ImGui::Begin: callers always call ImGui::End().
// Docked windows use their dock node, while floating windows stay in workArea.
bool beginOperationalWindow(const char* name, bool* open);

} // namespace deep::ui_imgui
