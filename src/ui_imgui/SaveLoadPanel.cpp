#include "ui_imgui/SaveLoadPanel.h"

// Implements manual save/load controls for the ImGui shell.
// All persistence requests go through SimulationService, preserving the app/save
// boundary and keeping SQLite details out of UI code.

#include "sim/Error.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace deep::ui_imgui {

void SaveLoadPanel::render(SimulationService& service, InformationInteractionAdapter& interactions, bool& visible) {
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
        newGame(interactions);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save(service);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load(interactions);
    }

    ImGui::Separator();
    ImGui::Text("Status: %s", lastActionSucceeded_ ? "OK" : "Error");
    ImGui::TextWrapped("%s", statusMessage_.c_str());

    ImGui::End();
}

void SaveLoadPanel::newGame(InformationInteractionAdapter& interactions) {
    applyResult("New Game", interactions.newGame());
}

void SaveLoadPanel::save(SimulationService& service) {
    const std::string path = currentPath();
    if (path.empty()) {
        applyResult("Save", CommandResult::failure("Save path is empty"));
        return;
    }

    applyResult("Save", service.saveGame(std::filesystem::path{path}));
}

void SaveLoadPanel::load(InformationInteractionAdapter& interactions) {
    // Empty/failed paths are handled by the same actual-result boundary as a
    // successful load. Status continues to use the original operation result.
    applyResult("Load", interactions.loadGame(std::filesystem::path{currentPath()}));
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
