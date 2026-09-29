#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/SurveyProgramsPanel.h"

// Public ImGui text logging exercises the real panel without a display or GPU.
// This covers zero-program and accepted-but-unready render paths, not clicks.

#include <imgui.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string capturedText;

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

std::string render(deep::ui_imgui::SurveyProgramsPanel& panel,
                   const deep::SimulationQueries& queries,
                   deep::SimulationService& service) {
    capturedText.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    bool visible = true;
    deep::ui_imgui::setOperationalWorkArea({0.0F, 0.0F, 1200.0F, 900.0F});
    panel.render(queries, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    require(visible, "panel stays open during passive render");
    return capturedText;
}

void panelRendersUnreadyProgram() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2{1200.0F, 900.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        capturedText = text;
    };

    deep::SimulationService service;
    deep::SimulationQueries queries{service};
    deep::ui_imgui::SurveyProgramsPanel panel;
    const std::string empty = render(panel, queries, service);
    require(empty.find("No programs authorized yet") != std::string::npos,
            "empty program state renders clearly");

    deep::SurveyProgramCharter charter;
    charter.name = "Unready Mission";
    charter.homeColonyId = queries.colonies().front().id;
    charter.targets.push_back({.bodyId = queries.strategicBodies()[1].id, .priority = 2, .requestedPasses = 1});
    require(service.execute(deep::CreateSurveyProgramCommand{.charter = charter}).ok,
            "fixture authorizes an unready program");
    const std::string program = render(panel, queries, service);
    require(program.find("Unready Mission") != std::string::npos,
            "authorized program appears in overview and detail");
    require(program.find("Waiting") != std::string::npos,
            "unready condition is visible as accepted state");

    panel.resetWorldState();
    require(service.newGame().ok, "replace fixture world");
    const std::string replaced = render(panel, queries, service);
    require(replaced.find("Unready Mission") == std::string::npos &&
            replaced.find("No programs authorized yet") != std::string::npos,
            "world reset drops old program detail");
    ImGui::DestroyContext();
}

} // namespace

int main() {
    try {
        panelRendersUnreadyProgram();
        std::cout << "Survey program UI: 1 scenario passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Survey program UI failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
