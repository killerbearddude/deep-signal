// Real ImGui submission verifies public opportunity text, hidden-truth equality,
// waiting intent visibility, and world-replacement draft reset without a display.
#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/TechnicalDevelopmentPanel.h"

#include <imgui.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace deep;
std::string captured;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string render(ui_imgui::TechnicalDevelopmentPanel& panel, const SimulationQueries& queries,
                   SimulationService& service) {
    captured.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    bool visible = true;
    ui_imgui::setOperationalWorkArea({0, 0, 1400, 1800});
    panel.render(queries, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    require(visible, "technical panel stays open while rendered");
    return captured;
}
void workflow_and_hidden_truth() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1400, 1800};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        captured = text;
    };
    auto good = createHomeSystemScenario(), miss = good;
    miss.technologyCandidateTruths.front().achievedDetectionThreshold = 9.0;
    SimulationService left(good), right(miss);
    ui_imgui::TechnicalDevelopmentPanel leftPanel, rightPanel;
    const auto a = render(leftPanel, SimulationQueries{left}, left);
    const auto b = render(rightPanel, SimulationQueries{right}, right);
    require(a == b && a.find("Precision Characterization Array") != std::string::npos &&
                a.find("Achieved sensitivity: not established") != std::string::npos &&
                a.find("technology tree") == std::string::npos,
            "pre-test technical UI is identical and does not leak candidate truth");
    auto charter = TechnicalDevelopmentCharter{};
    charter.name = "Visible waiting development";
    charter.opportunityId = good.technologyOpportunities.front().id;
    charter.developmentColonyId = good.technicalFacilities.front().colonyId;
    require(left.execute(CreateTechnicalDevelopmentCommand{charter}).ok,
            "missing-resource technical intent accepted through service");
    const auto waiting = render(leftPanel, SimulationQueries{left}, left);
    require(waiting.find(charter.name) != std::string::npos &&
                waiting.find("Waiting for a responsible technical leader") != std::string::npos,
            "accepted waiting intent and honest reason remain visible");
    int resets = 0;
    InformationInteractionAdapter interactions(left, [&] {
        ++resets;
        leftPanel.resetWorldState();
    });
    require(!interactions.loadGame({}).ok && resets == 0, "failed Load preserves technical editor state");
    require(interactions.newGame().ok && resets == 1,
            "successful world replacement resets technical editor state");
    const auto replaced = render(leftPanel, SimulationQueries{left}, left);
    require(replaced.find(charter.name) == std::string::npos,
            "old-world technical selection is absent after successful replacement");
    SimulationService earned(earnTechnicalDevelopmentFixture());
    ui_imgui::TechnicalDevelopmentPanel earnedPanel;
    const auto detail = render(earnedPanel, SimulationQueries{earned}, earned);
    require(detail.find("Test 1") != std::string::npos &&
                detail.find("Demonstrated component") != std::string::npos &&
                detail.find("engineering test records") != std::string::npos &&
                detail.find("Local serial process: effective") != std::string::npos,
            "earned panel exposes technical evidence, provenance distinction, production, and support");
    ImGui::DestroyContext();
}
} // namespace

int main() {
    try {
        workflow_and_hidden_truth();
        std::cout << "Technical development UI: 1 scenario passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical development UI failure: " << error.what() << '\n';
        return 1;
    }
}
