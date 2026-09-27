#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/FleetPanel.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/StrategicMapPanel.h"

// These tests exercise the production panel renderers with public ImGui APIs.
// Native dock-tab focus and visual scrolling also need a running application.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace deep::ui_imgui {

struct InformationRevealTestAccess {
    static bool pending(const StrategicMapPanel& panel) { return panel.pendingReveal_.has_value(); }
    static bool pending(const ColonyPanel& panel) { return panel.pendingReveal_.has_value(); }
    static bool pending(const FleetPanel& panel) { return panel.pendingReveal_.has_value(); }
    static render::MapPoint mapCenter(const StrategicMapPanel& panel) { return panel.camera_.center(); }
    static double mapZoom(const StrategicMapPanel& panel) { return panel.camera_.zoom(); }
    static void centerMap(StrategicMapPanel& panel, const render::MapPoint point) { panel.camera_.centerOn(point); }
    static void zoomMap(StrategicMapPanel& panel, const double factor) {
        panel.camera_.zoomAt(panel.camera_.center(), factor);
    }
};

} // namespace deep::ui_imgui

namespace {

using namespace deep;
using deep::ui_imgui::InformationRevealTestAccess;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const double left, const double right) {
    return std::abs(left - right) < 0.000001;
}

[[nodiscard]] bool sameSelection(const SelectionState& left, const SelectionState& right) {
    return left.type() == right.type() && left.selectedId() == right.selectedId();
}

void requireCenter(const ui_imgui::StrategicMapPanel& panel, const double x, const double y) {
    const auto center = InformationRevealTestAccess::mapCenter(panel);
    require(near(center.x, x) && near(center.y, y), "map reveal centers on exact current DTO coordinates");
}

[[nodiscard]] GameState makeWorld() {
    GameState state = createHomeSystemScenario();
    const FleetId fleet{state.ids.nextFleetId++};
    const ShipId ship{state.ids.nextShipId++};
    const auto& shipClass = state.shipClasses.front();
    state.fleets.push_back(Fleet{
        .id = fleet, .name = "Reveal fleet", .currentBodyId = state.bodies.front().id,
        .destinationBodyId = std::nullopt, .shipIds = {ship}, .activeOrder = {},
        .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(Ship{
        .id = ship, .shipClassId = shipClass.id, .name = "Reveal ship",
        .fleetId = fleet, .fuel = shipClass.fuelCapacity
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
        ui_imgui::setOperationalWorkArea({0.0F, 24.0F, 900.0F, 696.0F});
    }

    ~Fixture() { ImGui::DestroyContext(); }

    template<class Draw>
    void frame(Draw&& draw) {
        ImGui::NewFrame();
        draw();
        ImGui::Render();
    }

    SimulationService service{makeWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter interactions{service};
    ui_imgui::StrategicMapPanel map;
    ui_imgui::ColonyPanel colonies;
    ui_imgui::FleetPanel fleets;
};

void map_reveal_is_world_stamped_live_and_one_shot() {
    Fixture fixture;
    bool visible = true;
    const auto marsIt = std::find_if(fixture.service.state().bodies.begin(), fixture.service.state().bodies.end(),
                                     [](const Body& body) { return body.name == "Mars"; });
    require(marsIt != fixture.service.state().bodies.end(), "fixture has Mars");
    const BodyId marsId = marsIt->id;
    const auto mars = fixture.queries.strategicBody(marsId);
    require(mars.has_value(), "Mars has a strategic DTO");
    const WorldGeneration world = fixture.interactions.world();
    const SelectionState selectionBefore = fixture.interactions.mainSelection();

    InformationRevealTestAccess::centerMap(fixture.map, {12345.0, -12345.0});
    InformationRevealTestAccess::zoomMap(fixture.map, 2.0);
    const double zoom = InformationRevealTestAccess::mapZoom(fixture.map);
    fixture.map.requestReveal({world, marsId});
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, mars->x, mars->y);
    require(near(InformationRevealTestAccess::mapZoom(fixture.map), zoom), "map reveal preserves zoom");
    require(!InformationRevealTestAccess::pending(fixture.map), "successful map request consumed");
    require(sameSelection(fixture.interactions.mainSelection(), selectionBefore), "map reveal does not select");

    InformationRevealTestAccess::centerMap(fixture.map, {321.0, 654.0});
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, 321.0, 654.0);

    fixture.map.requestReveal({world, BodyId{999999}});
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, 321.0, 654.0);
    require(!InformationRevealTestAccess::pending(fixture.map), "missing map target discarded");

    fixture.map.requestReveal({world, fixture.queries.colonies().front().id});
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, 321.0, 654.0);
    require(!InformationRevealTestAccess::pending(fixture.map), "unsupported map target discarded");

    const auto fleet = fixture.queries.strategicFleets().front();
    fixture.map.requestReveal({world, fleet.id});
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, fleet.x, fleet.y);
    require(near(InformationRevealTestAccess::mapZoom(fixture.map), zoom), "fleet reveal preserves zoom");
    require(!InformationRevealTestAccess::pending(fixture.map), "fleet request consumed");

    InformationRevealTestAccess::centerMap(fixture.map, {246.0, 135.0});
    fixture.map.requestReveal({world, marsId});
    require(fixture.interactions.newGame().ok, "replace world with reused body IDs");
    fixture.frame([&] { fixture.map.render(fixture.queries, fixture.interactions, visible); });
    requireCenter(fixture.map, 246.0, 135.0);
    require(!InformationRevealTestAccess::pending(fixture.map), "old-world map request discarded");
}

void table_reveals_consume_only_matching_live_rows() {
    Fixture fixture;
    bool colonyVisible = false;
    bool fleetVisible = true;
    const WorldGeneration world = fixture.interactions.world();
    const SelectionState selectionBefore = fixture.interactions.mainSelection();
    const ColonyId colonyId = fixture.queries.colonies().front().id;
    const FleetId fleetId = fixture.queries.fleets().front().id;

    fixture.colonies.requestReveal({world, colonyId});
    fixture.frame([&] { fixture.colonies.render(fixture.queries, fixture.interactions, colonyVisible); });
    require(InformationRevealTestAccess::pending(fixture.colonies), "hidden table retains valid request");
    colonyVisible = true;
    fixture.frame([&] { fixture.colonies.render(fixture.queries, fixture.interactions, colonyVisible); });
    require(!InformationRevealTestAccess::pending(fixture.colonies), "matching colony row consumes request");
    require(sameSelection(fixture.interactions.mainSelection(), selectionBefore), "colony reveal does not select");

    fixture.colonies.requestReveal({world, ColonyId{999999}});
    fixture.frame([&] { fixture.colonies.render(fixture.queries, fixture.interactions, colonyVisible); });
    require(!InformationRevealTestAccess::pending(fixture.colonies), "missing colony row discarded");

    fixture.colonies.requestReveal({world, fixture.service.state().bodies.front().id});
    fixture.frame([&] { fixture.colonies.render(fixture.queries, fixture.interactions, colonyVisible); });
    require(!InformationRevealTestAccess::pending(fixture.colonies), "non-colony row request discarded");

    fixture.fleets.requestReveal({world, fleetId});
    fixture.frame([&] { fixture.fleets.render(fixture.queries, fixture.interactions, fleetVisible); });
    require(!InformationRevealTestAccess::pending(fixture.fleets), "matching fleet row consumes request");
    require(sameSelection(fixture.interactions.mainSelection(), selectionBefore), "fleet reveal does not select");

    fixture.fleets.requestReveal({world, FleetId{999999}});
    fixture.frame([&] { fixture.fleets.render(fixture.queries, fixture.interactions, fleetVisible); });
    require(!InformationRevealTestAccess::pending(fixture.fleets), "missing fleet row discarded");

    fixture.fleets.requestReveal({world, colonyId});
    fixture.frame([&] { fixture.fleets.render(fixture.queries, fixture.interactions, fleetVisible); });
    require(!InformationRevealTestAccess::pending(fixture.fleets), "non-fleet row request discarded");

    fixture.colonies.requestReveal({world, colonyId});
    fixture.fleets.requestReveal({world, fleetId});
    require(fixture.interactions.newGame().ok, "replace world with reused colony ID");
    colonyVisible = false;
    fleetVisible = false;
    fixture.frame([&] {
        fixture.colonies.render(fixture.queries, fixture.interactions, colonyVisible);
        fixture.fleets.render(fixture.queries, fixture.interactions, fleetVisible);
    });
    require(!InformationRevealTestAccess::pending(fixture.colonies) &&
            !InformationRevealTestAccess::pending(fixture.fleets), "old-world table requests discarded while hidden");
    require(fixture.interactions.mainSelection().type() == SelectedObjectType::None,
            "world replacement leaves selection clear");
}

} // namespace

int main() {
    try {
        map_reveal_is_world_stamped_live_and_one_shot();
        table_reveals_consume_only_matching_live_rows();
        std::cout << "Information reveal checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
