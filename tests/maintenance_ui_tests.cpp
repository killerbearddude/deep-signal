// Real ImGui rendering evidence without a native display. Rendering must not
// spend a workday, mutate condition, or retain old-world workflow after New/Load.
#include "app/InformationInteractionAdapter.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/MaintenanceProgramsPanel.h"
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
std::string render(ui_imgui::MaintenanceProgramsPanel& panel, SimulationQueries& queries,
                   SimulationService& service) {
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
    auto state = createTenderMaintenanceScenario();
    state.ships.front().equipmentCondition.front().usedDuty = 10;
    SimulationService service(state);
    SimulationQueries queries(service);
    ui_imgui::MaintenanceProgramsPanel panel;
    int resets = 0;
    InformationInteractionAdapter interaction(service, [&] {
        ++resets;
        panel.resetWorldState();
    });
    const auto empty = render(panel, queries, service);
    require(empty.find("Authorize support") != std::string::npos, "unready authoring is rendered");
    MaintenanceProgramCharter p;
    p.name = "Visible workshop support";
    p.serviceColonyId = state.colonies.back().id;
    p.requestedTenderId = state.fleets.back().id;
    p.requestedTeamId = state.maintenanceTeams.front().id;
    p.requestedLeaderId = state.people.front().id;
    p.clients = {state.fleets.front().id};
    require(service.execute(CreateMaintenanceProgramCommand{p}).ok, "support accepted");
    SurveyProgramCharter c;
    c.name = "Visible worn client";
    c.homeColonyId = p.serviceColonyId;
    c.requestedFleetId = state.fleets.front().id;
    c.requestedTeamId = state.surveyTeams.front().id;
    c.requestedLeaderId = state.people.front().id;
    c.targets = {{state.bodies.back().id, 0, 1}};
    c.policy.maintenanceProgramId = MaintenanceProgramId{1};
    require(service.execute(CreateSurveyProgramCommand{c}).ok, "client accepted");
    service.advanceDays(2);
    const auto day = service.state().date.day;
    const double duty = service.state().ships.front().equipmentCondition.front().usedDuty;
    const auto active = render(panel, queries, service);
    require(active.find("Active full-service job") != std::string::npos &&
                active.find("usable survey duty remaining") != std::string::npos &&
                active.find("not a second lease") != std::string::npos &&
                active.find("Suspend support") != std::string::npos,
            "physical hold, partial duty and safe actions rendered");
    require(service.state().date.day == day &&
                service.state().ships.front().equipmentCondition.front().usedDuty == duty,
            "rendering earns no service work");
    require(!interaction.loadGame({}).ok && resets == 0, "failed Load retains workflow");
    require(render(panel, queries, service).find(p.name) != std::string::npos,
            "failed Load retains old provider selection");
    require(interaction.newGame().ok && resets == 1, "successful world replacement resets panel once");
    require(render(panel, queries, service).find(p.name) == std::string::npos,
            "new world hides prior provider details");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        panel_and_world_replacement();
        std::cout << "Maintenance UI tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
