// Real ImGui submission proof for owned site workflows. No renderer/display is
// required; this proves text/actions and reset wiring, not native click-through.
#include "app/InformationInteractionAdapter.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/SiteDevelopmentPanel.h"
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
std::string render(ui_imgui::SiteDevelopmentPanel& panel, SimulationQueries& q, SimulationService& service) {
    captured.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    bool visible = true;
    ui_imgui::setOperationalWorkArea({0, 0, 1800, 2400});
    panel.render(q, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    return captured;
}
void site_workflow() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1800, 2400};
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        captured = text;
    };
    SimulationService service(createSiteDevelopmentScenario(true));
    SimulationQueries q(service);
    ui_imgui::SiteDevelopmentPanel panel;
    int resets = 0;
    InformationInteractionAdapter interaction(service, [&] {
        ++resets;
        panel.resetWorldState();
    });
    const auto empty = render(panel, q, service);
    require(empty.find("Authorize site development") != std::string::npos &&
                empty.find("not established") != std::string::npos,
            "cold authoring and unknown yield are explicit");
    // Publicly identical worlds must expose identical complete panel text before
    // an actual interaction, including the valid-intent/readiness distinction.
    SimulationService barren(createSiteDevelopmentScenario(false));
    SimulationQueries barrenQueries(barren);
    ui_imgui::SiteDevelopmentPanel barrenPanel;
    require(render(barrenPanel, barrenQueries, barren) == empty,
            "hidden deposit changes cannot alter site authoring panel");
    CreateSiteDevelopmentCommand create;
    create.charter.assignments.name = "Visible waiting construction";
    create.charter.supportColonyId = service.state().colonies.back().id;
    create.charter.package = referenceSitePackage();
    NewResourceSite site;
    site.bodyId = service.state().bodies.back().id;
    site.name = "Visible uninvestigated site";
    site.operatingPolicy.enabled = false;
    create.newSite = site;
    require(q.previewSiteDevelopment(create).structurallyValid,
            "missing builder/leader/engineer/stock/evidence remains admissible");
    require(service.execute(create).ok, "coherent unready site authorizes");
    const auto day = service.state().date.day;
    const auto events = service.state().eventLog.size();
    const auto active = render(panel, q, service);
    for (const char* text : {"Visible waiting construction", "Actual leased builder", "Amend development",
                             "Suspend development", "Cancel development", "Edit operating policy",
                             "Resume site operation", "Related freight commitments"})
        require(active.find(text) != std::string::npos,
                "site/detail physical and independent operating actions rendered");
    require(service.state().date.day == day && service.state().eventLog.size() == events &&
                service.state().resourceSites.back().installed.empty(),
            "rendering cannot earn work or mutate world");
    require(!interaction.loadGame({}).ok && resets == 0, "failed Load retains current workflow");
    require(render(panel, q, service).find("Visible waiting construction") != std::string::npos,
            "failed Load retains selected project");
    require(interaction.newGame().ok && resets == 1, "successful New resets site draft exactly once");
    require(render(panel, q, service).find("Visible waiting construction") == std::string::npos,
            "world replacement discards old site/project selection");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        site_workflow();
        std::cout << "Development UI checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
