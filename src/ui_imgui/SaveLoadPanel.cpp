#include "ui_imgui/SaveLoadPanel.h"

// Implements manual save/load controls for the ImGui shell.
// All persistence requests go through SimulationService, preserving the app/save
// boundary and keeping SQLite details out of UI code.

#include "sim/Error.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace deep::ui_imgui {

void SaveLoadPanel::render(SimulationService& service, bool& visible) {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Save / Load", &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("SQLite save path");
    ImGui::InputText("##save_path", pathBuffer_.data(), pathBuffer_.size());

    if (ImGui::Button("New Game")) {
        newGame(service);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save(service);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load(service);
    }

    ImGui::Separator();
    ImGui::Text("Status: %s", lastActionSucceeded_ ? "OK" : "Error");
    ImGui::TextWrapped("%s", statusMessage_.c_str());

    ImGui::End();
}

void SaveLoadPanel::newGame(SimulationService& service) {
    applyResult("New Game", service.newGame());
}

void SaveLoadPanel::save(SimulationService& service) {
    const std::string path = currentPath();
    if (path.empty()) {
        applyResult("Save", CommandResult::failure("Save path is empty"));
        return;
    }

    applyResult("Save", service.saveGame(std::filesystem::path{path}));
}

void SaveLoadPanel::load(SimulationService& service) {
    const std::string path = currentPath();
    if (path.empty()) {
        applyResult("Load", CommandResult::failure("Load path is empty"));
        return;
    }

    applyResult("Load", service.loadGame(std::filesystem::path{path}));
}

std::string SaveLoadPanel::currentPath() const {
    // The fixed buffer is always null-terminated by ImGui::InputText. Construct
    // a string at action time so filesystem paths do not retain stale pointers.
    return std::string{pathBuffer_.data()};
}

void SaveLoadPanel::applyResult(const char* actionName, const CommandResult& result) {
    lastActionSucceeded_ = result.ok;
    statusMessage_ = std::string{actionName} + (result.ok ? " succeeded: " : " failed: ") + result.message;
}

} // namespace deep::ui_imgui
