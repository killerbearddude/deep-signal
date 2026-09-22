#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/InformationPanel.h"
#include "ui_imgui/MainMenuBar.h"
#include "ui_imgui/ShellLayout.h"

// Exercise the production overview renderer with real app queries and public
// ImGui text logging. This needs no SDL display or GPU, and does not certify
// pixels, scrolling, shell geometry, or manual input behavior.

#include <imgui.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace {

using namespace deep;

std::string capturedText;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string normalized(const std::string_view text) {
    std::string result;
    bool pendingSpace = false;
    for (const char character : text) {
        // ImGui's public logger inserts pipe separators around table cells.
        // Compare content independently of those layout-only delimiters.
        if (character == '|' || std::isspace(static_cast<unsigned char>(character)) != 0) {
            pendingSpace = !result.empty();
        } else {
            if (pendingSpace) {
                result += ' ';
            }
            result += character;
            pendingSpace = false;
        }
    }
    return result;
}

void contains(const std::string& text, const std::string_view expected) {
    require(text.find(expected) != std::string::npos,
            "rendered text missing: " + std::string{expected} + "\nActual: " + text);
}

[[nodiscard]] std::string decimal(const double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

struct ImGuiFixture {
    ImGuiFixture() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2{1280.0F, 1800.0F};
        io.DeltaTime = 1.0F / 60.0F;
        // Enable the bundled ImGui's dynamic font sizes. CPU text logging never
        // submits draw data to a backend, so no GPU texture allocation is needed.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            capturedText = text;
        };
    }

    ~ImGuiFixture() { ImGui::DestroyContext(); }

    [[nodiscard]] std::string render(const SimulationQueries& queries, const SelectionState& selection) const {
        capturedText.clear();
        ImGui::NewFrame();
        ImGui::LogToClipboard();
        ui_imgui::InformationPanel{}.render(queries, selection, ImVec2{860.0F, 0.0F}, ImVec2{420.0F, 1800.0F});
        ImGui::LogFinish();
        ImGui::Render();
        return normalized(capturedText);
    }
};

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
        .id = ship, .shipClassId = shipClass.id, .name = "Overview test ship",
        .fleetId = fleet, .fuel = shipClass.fuelCapacity
    });
    return state;
}

struct Fixture {
    ImGuiFixture imgui;
    SimulationService service{makeWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter interactions{service};

    void select(const ObjectTarget target) {
        require(interactions.select({interactions.world(), target}), "select fixture target");
    }

    [[nodiscard]] std::string render() {
        const auto projection = interactions.mainSelection();
        const auto target = interactions.state().mainTarget();
        const auto previews = interactions.state().previewSnapshot();
        const auto generation = interactions.world();
        const auto& world = service.state();
        const auto worldBefore = std::tuple{world.date.day, world.eventLog.size(), world.ids.nextEventId,
            queries.colonies().front().totalRawStockpile, queries.colonies().front().totalProcessedStockpile};
        const auto text = imgui.render(queries, projection);
        require(projection.type() == interactions.mainSelection().type()
            && projection.selectedId() == interactions.mainSelection().selectedId(), "render preserves main selection");
        require(interactions.world() == generation && interactions.state().mainTarget() == target
            && interactions.state().previewSnapshot() == previews, "render preserves interaction identity and previews");
        require(worldBefore == std::tuple{world.date.day, world.eventLog.size(), world.ids.nextEventId,
            queries.colonies().front().totalRawStockpile, queries.colonies().front().totalProcessedStockpile},
            "render does not advance time, append events, or alter stockpiles");
        return text;
    }
};

void empty_and_unavailable() {
    Fixture fixture;
    const auto empty = fixture.render();
    contains(empty, "INFORMATION");
    contains(empty, "No selection");
    contains(empty, "Select a body, colony, or fleet from the map or an operational view to inspect its overview.");
    require(empty.find("Terra") == std::string::npos, "empty state does not select a fallback object");

    // An unresolved projection can occur at a query boundary; it must not be
    // represented as zero facts or a fallback entity. Ordinary shell reads
    // reconcile missing targets before reaching this renderer.
    for (const auto type : {SelectedObjectType::Body, SelectedObjectType::Colony, SelectedObjectType::Fleet}) {
        SelectionState missing;
        if (type == SelectedObjectType::Body) missing.selectBody(BodyId{999999});
        if (type == SelectedObjectType::Colony) missing.selectColony(ColonyId{999999});
        if (type == SelectedObjectType::Fleet) missing.selectFleet(FleetId{999999});
        const auto text = fixture.imgui.render(fixture.queries, missing);
        contains(text, "Selected object unavailable");
        contains(text, "The current selection could not be resolved.");
        require(text.find("Terra") == std::string::npos, "unresolved state does not display another object");
    }
}

void native_overviews_and_literal_names() {
    Fixture fixture;
    const auto body = fixture.queries.bodySystemOverview().front();
    const auto colony = fixture.queries.colonies().front();
    const auto fleet = fixture.queries.fleets().front();
    fixture.select(body.id);
    const auto bodyText = fixture.render();
    contains(bodyText, body.name);
    contains(bodyText, body.typeName);
    contains(bodyText, body.strategicZoneName);
    contains(bodyText, "Colonies " + std::to_string(body.colonyCount));
    contains(bodyText, "Deposits " + std::to_string(body.mineralDepositCount));
    contains(bodyText, "RESOURCE KNOWLEDGE");
    contains(bodyText, "Confirmed quantity " + decimal(body.confirmedDepositQuantity) + " units");

    fixture.select(colony.id);
    const auto colonyText = fixture.render();
    contains(colonyText, colony.name);
    contains(colonyText, colony.bodyName);
    contains(colonyText, colony.processingPolicyName);
    contains(colonyText, "PRODUCTION");
    contains(colonyText, "Raw " + decimal(colony.totalRawStockpile) + " units");
    contains(colonyText, "Processed " + decimal(colony.totalProcessedStockpile) + " units");

    fixture.select(fleet.id);
    const auto fleetText = fixture.render();
    contains(fleetText, fleet.name);
    contains(fleetText, "Responsible institution: Unassigned");
    contains(fleetText, "Ships 1");
    contains(fleetText, "Fuel 100.0 %");
    contains(fleetText, "Order Idle");
    contains(fleetText, "Destination -");
    contains(fleetText, "ETA -");
    contains(fleetText, "Queued orders 0");
    require(fleetText.find(colony.name) == std::string::npos, "new selection does not retain old overview");
    const std::string settings = ImGui::SaveIniSettingsToMemory();
    require(settings.find("InformationPanel") == std::string::npos
        && settings.find("literal") == std::string::npos, "overview identities do not write object-named window settings");
}

void live_values_and_lifecycle() {
    Fixture fixture;
    const auto colonyId = fixture.queries.colonies().front().id;
    const auto fleetId = fixture.queries.fleets().front().id;
    fixture.select(colonyId);
    const auto pin = fixture.interactions.inspect({fixture.interactions.world(), fleetId});
    require(pin.has_value() && fixture.interactions.pin(*pin), "seed independent pinned target");
    const auto before = fixture.queries.colonies().front();
    const auto beforeText = fixture.render();
    const auto generation = fixture.interactions.world();
    require(fixture.service.execute(AdvanceDaysCommand{.days = 1}).ok, "advance through real command boundary");
    const auto after = fixture.queries.colonies().front();
    require(before.totalRawStockpile != after.totalRawStockpile
        || before.totalProcessedStockpile != after.totalProcessedStockpile, "fixture economy changes during advance");
    const auto afterText = fixture.render();
    contains(afterText, "Raw " + decimal(after.totalRawStockpile) + " units");
    contains(afterText, "Processed " + decimal(after.totalProcessedStockpile) + " units");
    require(afterText != beforeText && fixture.interactions.world() == generation,
            "render uses live values without replacing the world");

    const auto previews = fixture.interactions.state().previewSnapshot();
    require(!fixture.interactions.loadGame({}).ok, "real empty-path load fails");
    require(fixture.render() == afterText && fixture.interactions.state().previewSnapshot() == previews,
            "failed load preserves displayed selection and independent pins");
    require(fixture.interactions.newGame().ok, "real new game succeeds");
    const auto replaced = fixture.render();
    contains(replaced, "No selection");
    require(fixture.interactions.world() != generation && fixture.interactions.state().previewSnapshot().empty(),
            "replacement lifecycle naturally produces empty overview");
    require(replaced.find("Colony ##literal") == std::string::npos, "replacement shows no old-world object text");
}

void workspace_presets_and_legacy_visibility() {
    using ui_imgui::PanelVisibility;
    using ui_imgui::Workspace;
    // Keep the expected operational composition explicit, including the legacy
    // Inspector. The persistent overview intentionally has no visibility flag.
    constexpr std::array<bool PanelVisibility::*, 9> operationalFields{
        &PanelVisibility::strategicMap, &PanelVisibility::bodies, &PanelVisibility::inspector,
        &PanelVisibility::colonies, &PanelVisibility::fleets, &PanelVisibility::fleetOrders,
        &PanelVisibility::shipyard, &PanelVisibility::economyForecast, &PanelVisibility::eventLog
    };
    struct Preset {
        Workspace workspace;
        const char* name;
        std::array<bool, 9> visible;
    };
    constexpr std::array<Preset, 6> presets{{
        {Workspace::System, "System", {true, true, false, false, false, false, false, false, false}},
        {Workspace::Economy, "Economy", {false, false, false, false, false, false, false, true, false}},
        {Workspace::Production, "Production", {false, false, false, true, false, false, true, true, false}},
        {Workspace::Fleets, "Fleets", {true, false, false, false, true, true, false, false, false}},
        {Workspace::Intelligence, "Intelligence", {true, true, false, false, false, true, false, false, false}},
        {Workspace::History, "History", {false, false, false, false, false, false, false, false, true}}
    }};
    const auto matches = [&](const PanelVisibility& visibility, const Preset& preset) {
        for (std::size_t index = 0; index < operationalFields.size(); ++index) {
            require(visibility.*operationalFields[index] == preset.visible[index],
                    std::string{preset.name} + " has exactly its approved operational panels");
        }
        require(!visibility.inspector, "Legacy Inspector is excluded from every normal workspace");
    };

    const PanelVisibility startup;
    matches(startup, presets.front());
    require(!startup.saveLoad && !startup.timeControl, "global fallback panels start hidden");

    for (const auto& preset : presets) {
        for (const bool saveLoad : {false, true}) {
            for (const bool timeControl : {false, true}) {
                PanelVisibility visibility;
                visibility.saveLoad = saveLoad;
                visibility.timeControl = timeControl;
                // Oppose every expected flag to exercise both reopening an
                // approved panel and clearing a manually enabled extra panel.
                for (std::size_t index = 0; index < operationalFields.size(); ++index) {
                    visibility.*operationalFields[index] = !preset.visible[index];
                }
                ui_imgui::applyWorkspace(preset.workspace, visibility);
                matches(visibility, preset);
                require(visibility.saveLoad == saveLoad && visibility.timeControl == timeControl,
                        "workspace preserves each manually chosen global-panel visibility");

                // Reselecting the active preset also resets manual changes.
                visibility.inspector = true;
                visibility.strategicMap = !visibility.strategicMap;
                ui_imgui::applyWorkspace(preset.workspace, visibility);
                matches(visibility, preset);
                require(visibility.saveLoad == saveLoad && visibility.timeControl == timeControl,
                        "preset reselection preserves global-panel visibility");
            }
        }
    }
}

void viewport_reservation_propagates_without_double_subtraction() {
    ImGuiFixture fixture;
    const auto near = [](const float actual, const float expected) {
        return std::abs(actual - expected) < 0.001F;
    };
    const auto frame = [&](const float displayWidth, const float expectedPublicWidth,
                           const float expectedInformationWidth, const float expectedDockWidth) {
        ImGui::GetIO().DisplaySize = ImVec2{displayWidth, 720.0F};
        ImGui::NewFrame();
        require(near(ImGui::GetMainViewport()->WorkSize.x, expectedPublicWidth),
                "next-frame public work area reflects the previous sidebar reservation");
        require(ImGui::BeginMainMenuBar(), "main menu opens for reservation test");
        const float menuBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
        const auto layout = ui_imgui::reserveInformationWorkArea(menuBottom);
        require(near(layout.information.width, expectedInformationWidth) &&
                    near(layout.dock.width, expectedDockWidth),
                "reservation uses available viewport width without subtracting the sidebar twice");
        require(near(layout.information.x, layout.dock.x + layout.dock.width) &&
                    near(layout.information.x + layout.information.width, displayWidth) &&
                    layout.dock.y >= menuBottom,
                "reserved shell regions meet below the menu and fill the viewport width");
        ImGui::Render();
    };

    frame(1280.0F, 1280.0F, 358.0F, 922.0F);
    frame(1280.0F, 922.0F, 358.0F, 922.0F);
    frame(1280.0F, 922.0F, 358.0F, 922.0F);
    // The first resized NewFrame still publishes the prior 358 px inset; the
    // production reservation adapts immediately, then publishes 300 px next time.
    frame(1000.0F, 642.0F, 300.0F, 700.0F);
    frame(1000.0F, 700.0F, 300.0F, 700.0F);
    frame(1000.0F, 700.0F, 300.0F, 700.0F);
    // This certifies work-area propagation only. Detached docking containment
    // and focus behavior still require actual running-UI verification.
}

} // namespace

int main() {
    static_assert(std::is_empty_v<deep::ui_imgui::InformationPanel>, "overview must not own selection or cached DTO state");
    try {
        empty_and_unavailable();
        std::cout << "PASS empty and unavailable states\n";
        native_overviews_and_literal_names();
        std::cout << "PASS native overviews and literal names\n";
        live_values_and_lifecycle();
        std::cout << "PASS live values and lifecycle\n";
        workspace_presets_and_legacy_visibility();
        std::cout << "PASS workspace presets and legacy visibility\n";
        viewport_reservation_propagates_without_double_subtraction();
        std::cout << "PASS viewport reservation propagation\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Information panel test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
