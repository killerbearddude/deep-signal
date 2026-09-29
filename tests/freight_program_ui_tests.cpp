#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/FreightProgramsPanel.h"
#include "ui_imgui/OperationalWindow.h"

// P3B-36: render the real panel via public ImGui text logging without a display.
// This verifies visible contracts and replacement callbacks, not native clicks.

#include <imgui.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string capturedText;
void require(const bool value, const std::string_view message) {
    if (!value) throw std::runtime_error{std::string{message}};
}

std::string render(deep::ui_imgui::FreightProgramsPanel& panel, const deep::SimulationQueries& queries,
                   deep::SimulationService& service) {
    capturedText.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    bool visible = true;
    deep::ui_imgui::setOperationalWorkArea({0.0F, 0.0F, 1400.0F, 1600.0F});
    panel.render(queries, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    require(visible, "passive rendering keeps the panel open");
    return capturedText;
}

void visibleIntentCustodyAndReplacement() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2{1400.0F, 1600.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { capturedText = text; };

    deep::SimulationService service{deep::createDelegatedFreightScenario()};
    deep::SimulationQueries queries{service};
    deep::ui_imgui::FreightProgramsPanel panel;
    int resets = 0;
    deep::InformationInteractionAdapter interactions{service, [&] { ++resets; panel.resetWorldState(); }};
    const auto empty = render(panel, queries, service);
    require(empty.find("No freight programs authorized yet") != std::string::npos &&
            empty.find("Authorize delivery") != std::string::npos,
            "empty authoring workflow is visible");
    deep::FreightProgramCharter charter;
    charter.name = "Visible waiting delivery";
    charter.source = service.state().colonies.at(service.state().colonies.size() - 2).id;
    charter.operatingBaseColonyId = std::get<deep::ColonyId>(charter.source);
    charter.destination = service.state().colonies.back().id;
    charter.commodity = deep::ProcessedMaterial::StructuralAlloys;
    charter.totalQuantity = 500.0;
    require(service.execute(deep::CreateFreightProgramCommand{charter}).ok, "unready charter accepted");
    const auto id = queries.freightPrograms().front().id;
    const auto waiting = render(panel, queries, service);
    require(waiting.find(charter.name) != std::string::npos && waiting.find("Waiting") != std::string::npos &&
            waiting.find("Edit delivery commitment") != std::string::npos,
            "accepted waiting intent remains visible and editable");
    require(waiting.find("Source, destination, operating base and commodity are fixed") != std::string::npos &&
            waiting.find("normalized units") != std::string::npos && waiting.find("ignores payload mass") != std::string::npos,
            "contract identity, units and retained transit limitation are explicit");
    auto amendment = deep::FreightProgramAmendment{
        .name = charter.name, .totalQuantity = 500.0,
        .requestedFleetId = service.state().fleets.back().id,
        .requestedLeaderId = service.state().people.front().id, .policy = {}
    };
    require(service.execute(deep::AmendFreightProgramCommand{id, amendment}).ok, "assign ready assets");
    for (int i = 0; i < 12 && queries.freightPrograms().front().cargoAboard == 0.0; ++i) {
        require(service.advanceDaysDetailed(1).advancedDays == 1, "fixture reaches physical loading");
    }
    require(queries.freightPrograms().front().cargoAboard > 0.0, "fixture has actual cargo");
    require(service.execute(deep::SuspendFreightProgramCommand{id}).ok, "suspend loaded program");
    const auto paused = render(panel, queries, service);
    require(paused.find("fleet retained while cargo is aboard") != std::string::npos &&
            paused.find("Actually aboard") != std::string::npos && paused.find("shipment 1") != std::string::npos &&
            paused.find("Resume") != std::string::npos && paused.find("Cancel future pickups") != std::string::npos,
            "cargo custody explains safe controls and identified per-hull manifest");
    require(!interactions.loadGame({}).ok && resets == 0, "failed load never resets panel workflow state");
    const auto failed = render(panel, queries, service);
    require(failed.find(charter.name) != std::string::npos && failed.find("fleet retained while cargo is aboard") != std::string::npos,
            "failed load preserves current world and visible custody");
    require(interactions.newGame().ok && resets == 1, "successful replacement invokes panel reset once");
    const auto replaced = render(panel, queries, service);
    require(replaced.find(charter.name) == std::string::npos &&
            replaced.find("No freight programs authorized yet") != std::string::npos,
            "successful replacement drops old program selection and detail");
    ImGui::DestroyContext();
}
} // namespace

int main() {
    try {
        visibleIntentCustodyAndReplacement();
        std::cout << "Freight program UI: 1 scenario passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Freight program UI failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
