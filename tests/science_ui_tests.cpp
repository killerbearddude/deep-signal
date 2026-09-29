// Real ImGui rendering evidence without a native display. Rendering must not
// spend a workday, mutate condition, or retain old-world workflow after New/Load.
#include "app/InformationInteractionAdapter.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/SciencePanel.h"
#include "ui_imgui/OperationalWindow.h"
#include <imgui.h>
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
std::string captured;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string render(ui_imgui::SciencePanel& panel, SimulationQueries& queries, SimulationService& service) {
    captured.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    bool visible = true;
    ui_imgui::setOperationalWorkArea({0, 0, 1600, 1800});
    panel.render(queries, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    return captured;
}
void panel_and_world_replacement() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1600, 1800};
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        captured = text;
    };
    auto state = createDelegatedSurveyScenario();
    state.colonies.back().analysisCapacity = 1;
    SimulationService service(state);
    SimulationQueries queries(service);
    ui_imgui::SciencePanel panel;
    int resets = 0;
    InformationInteractionAdapter interaction(service, [&] {
        ++resets;
        panel.resetWorldState();
    });
    const auto empty = render(panel, queries, service);
    require(empty.find("Authorize analysis") != std::string::npos &&
                empty.find("Fixed acquired batches") != std::string::npos,
            "Unready intent and both source forms rendered");
    require(
        service.execute(ResourceSurveyCommand{state.fleets.front().id, state.fleets.front().currentBodyId})
            .ok,
        "Real raw acquisition");
    AnalysisCharter c{
        "Visible analysis",           state.colonies.back().id, FixedBatchInput{{ObservationBatchId{1}}},
        state.surveyTeams.front().id, state.people.front().id,  std::nullopt};
    require(service.execute(CreateAnalysisProgramCommand{c}).ok, "Analysis authorizes");
    const auto before = service.state().observations;
    const auto day = service.state().date.day;
    const auto raw = render(panel, queries, service);
    require(raw.find("Suspend") != std::string::npos && raw.find("Cancel") != std::string::npos &&
                raw.find("Raw batch #1") != std::string::npos &&
                raw.find("available day 1") != std::string::npos &&
                raw.find("absence not established") != std::string::npos,
            "Lifecycle, raw availability and negative limits displayed");
    require(service.state().date.day == day && service.state().observations == before &&
                service.state().assessments.empty(),
            "Rendering performed no analysis or acquisition");
    service.advanceDays(3);
    const auto assessed = render(panel, queries, service);
    require(assessed.find("Assessment #1") != std::string::npos &&
                assessed.find("Published day 3") != std::string::npos &&
                assessed.find("Scientific as-of day 0") != std::string::npos &&
                assessed.find("Observation age: 3 day(s)") != std::string::npos &&
                assessed.find("Unmeasured") != std::string::npos,
            "Formal publication date is distinct from scientific acquisition and unsupported claims");
    require(!interaction.loadGame({}).ok && resets == 0, "Failed Load preserves evidence draft and world");
    require(render(panel, queries, service).find("Visible analysis") != std::string::npos,
            "Failed Load retains existing evidence");
    require(interaction.newGame().ok && resets == 1, "New resets science workflow once");
    require(render(panel, queries, service).find("Visible analysis") == std::string::npos,
            "Old-world selection cannot leak into new game");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        panel_and_world_replacement();
        std::cout << "Science UI tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
