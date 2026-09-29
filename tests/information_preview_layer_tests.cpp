#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "ui_imgui/InformationPreviewLayer.h"

// Public ImGui text logging exercises the production preview renderer with
// real query DTOs. Geometry, native focus and pointer behavior need a running UI.
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using namespace deep;

std::string capturedText;

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void contains(const std::string& text, const std::string_view expected) {
    require(text.find(expected) != std::string::npos,
            "preview text missing: " + std::string{expected} + "\nActual: " + text);
}

[[nodiscard]] std::string normalized(const std::string_view text) {
    std::string result;
    bool space = false;
    for (const char character : text) {
        if (character == '|' || std::isspace(static_cast<unsigned char>(character)) != 0) {
            space = !result.empty();
        } else {
            if (space) result += ' ';
            result += character;
            space = false;
        }
    }
    return result;
}

[[nodiscard]] std::string decimal(const double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << value;
    return text.str();
}

[[nodiscard]] GameState makeWorld() {
    auto state = createHomeSystemScenario();
    state.bodies.front().name = "Terra ##literal ###stable";
    state.colonies.front().name = "Colony ##literal ###stable";
    const FleetId fleet{state.ids.nextFleetId++};
    const ShipId ship{state.ids.nextShipId++};
    const auto& shipClass = state.shipClasses.front();
    state.fleets.push_back(Fleet{
        .id = fleet, .name = "Fleet ##literal ###stable", .currentBodyId = state.bodies.front().id,
        .destinationBodyId = std::nullopt, .shipIds = {ship}, .activeOrder = {},
        .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(Ship{
        .id = ship, .shipClassId = shipClass.id, .name = "Preview test ship",
        .fleetId = fleet, .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity
    });
    return state;
}

struct Fixture {
    Fixture() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {1280.0F, 720.0F};
        io.DeltaTime = 1.0F / 60.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            capturedText = text;
        };
        const auto mars = std::find_if(service.state().bodies.begin(), service.state().bodies.end(),
                                       [](const Body& body) { return body.name == "Mars"; });
        require(mars != service.state().bodies.end(), "scenario contains Mars");
        mainBody = mars->id;
        require(interactions.select({interactions.world(), mainBody}), "select Mars as persistent main context");
    }

    ~Fixture() { ImGui::DestroyContext(); }

    [[nodiscard]] PreviewId inspect(const ObjectTarget target) {
        const auto id = interactions.inspect({interactions.world(), target});
        require(id.has_value(), "inspect live object");
        return *id;
    }

    [[nodiscard]] std::string render(const SimulationQueries* displayQueries = nullptr) {
        capturedText.clear();
        ImGui::NewFrame();
        ImGui::LogToClipboard();
        const auto intentions = layer.render(displayQueries ? *displayQueries : queries,
                                       interactions, {0.0F, 24.0F, 920.0F, 696.0F});
        require(!intentions.goTo && !intentions.configureProcessing,
                "render without activation emits no action intention");
        ImGui::LogFinish();
        ImGui::Render();
        require(interactions.mainSelection().isBodySelected(mainBody),
                "preview rendering leaves persistent main selection on Mars");
        return normalized(capturedText);
    }

    SimulationService service{makeWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter interactions{service};
    ui_imgui::InformationPreviewLayer layer;
    BodyId mainBody{};
};

void native_condensed_overviews_and_stable_identity() {
    Fixture fixture;
    const auto body = fixture.queries.bodySystemOverview().front();
    const auto colony = fixture.queries.colonies().front();
    const auto fleet = fixture.queries.fleets().front();

    const auto temporary = fixture.inspect(body.id);
    const std::string name = ui_imgui::informationPreviewWindowName(temporary);
    require(name == "Preview###InformationPreview_W1_P1", "window ID includes generation and PreviewId");
    require(name.find(body.name) == std::string::npos, "display name is never the native window ID");
    const auto bodyContentName = ui_imgui::informationPreviewContentName(
        {temporary.world, body.id});
    require(bodyContentName == ui_imgui::informationPreviewContentName({temporary.world, body.id})
            && bodyContentName != ui_imgui::informationPreviewContentName({temporary.world, colony.id})
            && bodyContentName != ui_imgui::informationPreviewContentName(
                {WorldGeneration{temporary.world.value + 1}, body.id}),
            "same-target refresh keeps inner scroll identity; retarget or world replacement resets it");
    const auto bodyText = fixture.render();
    contains(bodyText, body.name);
    contains(bodyText, body.typeName);
    contains(bodyText, body.strategicZoneName);
    contains(bodyText, "Colonies " + std::to_string(body.colonyCount));
    contains(bodyText, "Fleets referenced " + std::to_string(body.fleetCount));
    contains(bodyText, "Deposits " + std::to_string(body.mineralDepositCount));
    contains(bodyText, "RELATIONSHIPS");
    contains(bodyText, "Parent body");
    contains(bodyText, body.parentBodyName);
    contains(bodyText, "COLONIES");
    contains(bodyText, colony.name);
    contains(bodyText, "STATIONED FLEETS");
    contains(bodyText, fleet.name);
    contains(bodyText, "Inspect >");
    contains(bodyText, "Go To >");
    require(bodyText.find("Configure processing") == std::string::npos,
            "Body preview has no Colony processing control");

    require(fixture.inspect(colony.id) == temporary, "temporary retarget preserves PreviewId");
    require(ui_imgui::informationPreviewWindowName(temporary) == name, "retarget keeps native window name");
    const auto colonyText = fixture.render();
    contains(colonyText, colony.name);
    contains(colonyText, colony.bodyName);
    contains(colonyText, colony.processingPolicyName);
    contains(colonyText, "Raw stockpile " + decimal(colony.totalRawStockpile) + " units");
    contains(colonyText, "RELATIONSHIPS");
    contains(colonyText, "Body");
    contains(colonyText, body.name);
    contains(colonyText, "Inspect >");
    contains(colonyText, "Go To >");
    contains(colonyText, "Configure processing");
    require(colonyText.find("BODY ") == std::string::npos,
            "retarget does not retain the prior body overview");

    require(fixture.inspect(fleet.id) == temporary, "fleet reuses temporary identity");
    const auto fleetText = fixture.render();
    contains(fleetText, fleet.name);
    contains(fleetText, "Ships 1");
    contains(fleetText, "Order Idle");
    contains(fleetText, "Destination -");
    contains(fleetText, "ETA -");
    contains(fleetText, "RELATIONSHIPS");
    contains(fleetText, "Current body");
    contains(fleetText, body.name);
    contains(fleetText, "Inspect >");
    contains(fleetText, "Go To >");
    require(fleetText.find("Configure processing") == std::string::npos,
            "Fleet preview has no Colony processing control");
    require(fleetText.find(colony.name) == std::string::npos, "fleet does not retain colony text");
    const std::string settings = ImGui::SaveIniSettingsToMemory();
    require(settings.find("InformationPreview_") == std::string::npos
        && settings.find("literal") == std::string::npos, "session preview IDs create no saved settings");
}

void unresolved_display_omits_navigation_control() {
    Fixture fixture;
    const FleetId fleet = fixture.queries.fleets().front().id;
    const auto preview = fixture.inspect(fleet);

    // The interaction owner has a live fleet, but the display query cannot
    // resolve it. This exercises the between-snapshot-and-display fallback
    // without treating a query exception as an absent target.
    auto missingWorld = makeWorld();
    missingWorld.fleets.clear();
    missingWorld.ships.clear();
    SimulationService missingService{std::move(missingWorld)};
    SimulationQueries missingQueries{missingService};
    const auto text = fixture.render(&missingQueries);
    contains(text, "Selected object unavailable");
    require(text.find("Go To >") == std::string::npos
            && text.find("Configure processing") == std::string::npos,
            "unresolved preview offers no action controls");
    require(fixture.interactions.previewSnapshot().front().id == preview,
            "display-query mismatch does not remove the interaction record");
}

void independent_pins_and_live_values() {
    Fixture fixture;
    const auto colony = fixture.queries.colonies().front();
    const auto fleet = fixture.queries.fleets().front();
    const auto first = fixture.inspect(colony.id);
    require(fixture.interactions.pin(first), "pin first colony preview");
    const auto second = fixture.inspect(colony.id);
    require(second != first && fixture.interactions.pin(second), "explicit duplicate pin has distinct identity");
    const auto temporary = fixture.inspect(fleet.id);
    require(temporary != first && temporary != second, "new temporary follows pins");
    const auto beforeText = fixture.render();
    contains(beforeText, "PINNED");
    contains(beforeText, fleet.name);
    contains(beforeText, "Raw stockpile " + decimal(colony.totalRawStockpile) + " units");
    const auto before = fixture.interactions.previewSnapshot();
    require(before.size() == 3, "two pins and temporary render from distinct records");
    const auto generation = fixture.interactions.world();

    require(fixture.service.execute(AdvanceDaysCommand{.days = 1}).ok, "advance through real command");
    const auto updated = fixture.queries.colonies().front();
    require(colony.totalRawStockpile != updated.totalRawStockpile ||
            colony.totalProcessedStockpile != updated.totalProcessedStockpile,
            "fixture economy changes after a day");
    const auto afterText = fixture.render();
    contains(afterText, "Raw stockpile " + decimal(updated.totalRawStockpile) + " units");
    require(afterText != beforeText && fixture.interactions.world() == generation
        && fixture.interactions.previewSnapshot() == before,
        "live DTO values refresh while preview identity remains stable");

    require(!fixture.interactions.loadGame({}).ok && fixture.interactions.previewSnapshot() == before,
            "failed world replacement preserves previews");
    require(fixture.interactions.newGame().ok && fixture.interactions.previewSnapshot().empty(),
            "successful world replacement removes all previews");
}

} // namespace

int main() {
    static_assert(std::is_empty_v<deep::ui_imgui::InformationPreviewLayer>,
                  "preview renderer must not retain interaction or DTO state");
    try {
        native_condensed_overviews_and_stable_identity();
        std::cout << "PASS condensed native previews and stable window identity\n";
        unresolved_display_omits_navigation_control();
        std::cout << "PASS unresolved display omits Go To\n";
        independent_pins_and_live_values();
        std::cout << "PASS duplicate pins, live DTOs, and world lifecycle\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Information preview layer test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
