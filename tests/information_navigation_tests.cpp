#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/FleetPanel.h"
#include "ui_imgui/InformationNavigation.h"
#include "ui_imgui/StrategicMapPanel.h"

// Exercise the production navigation coordinator and real adapter. Presentation
// reveal delivery is inspected here; the panel test exercises its consumption.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace deep::ui_imgui {
struct InformationRevealTestAccess {
    static auto mapPending(const StrategicMapPanel& panel) { return panel.pendingReveal_; }
    static auto colonyPending(const ColonyPanel& panel) { return panel.pendingReveal_; }
    static auto fleetPending(const FleetPanel& panel) { return panel.pendingReveal_; }
};
} // namespace deep::ui_imgui

namespace {
using namespace deep;
using namespace deep::ui_imgui;
using Access = InformationRevealTestAccess;

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] BodyId bodyNamed(const GameState& world, const std::string_view name) {
    const auto row = std::find_if(world.bodies.begin(), world.bodies.end(), [name](const Body& body) {
        return body.name == name;
    });
    require(row != world.bodies.end(), "test body exists");
    return row->id;
}

[[nodiscard]] GameState makeWorld() {
    auto world = createHomeSystemScenario();
    const BodyId mars = bodyNamed(world, "Mars");
    for (const char* name : {"Navigation Mars B", "Navigation Mars C"}) {
        Colony colony = world.colonies.at(1);
        colony.id = ColonyId{world.ids.nextColonyId++};
        colony.name = name;
        world.colonies.push_back(std::move(colony));
    }
    const auto& shipClass = world.shipClasses.front();
    const FleetId fleet{world.ids.nextFleetId++};
    const ShipId ship{world.ids.nextShipId++};
    world.fleets.push_back(Fleet{
        .id = fleet, .name = "Navigation test fleet", .currentBodyId = mars,
        .destinationBodyId = std::nullopt, .shipIds = {ship}, .activeOrder = {},
        .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    world.ships.push_back(Ship{
        .id = ship, .shipClassId = shipClass.id, .name = "Navigation test ship",
        .fleetId = fleet, .fuel = deep::evaluateShipDesign(world.shipComponents, shipClass.components).propellantCapacity
    });
    return world;
}

[[nodiscard]] std::array<bool, 13> visibility(const PanelVisibility& value) {
    return {value.saveLoad, value.timeControl, value.strategicMap, value.bodies,
        value.inspector, value.colonies, value.fleets, value.fleetOrders,
        value.surveyPrograms, value.freightPrograms, value.shipyard, value.economyForecast, value.eventLog};
}

struct Fixture {
    SimulationService service{makeWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter interactions{service};
    InformationNavigation navigation;
    StrategicMapPanel map;
    ColonyPanel colonies;
    FleetPanel fleets;
    Workspace workspace = Workspace::System;
    PanelVisibility panels;

    [[nodiscard]] BodyId mars() const { return bodyNamed(service.state(), "Mars"); }
    [[nodiscard]] ColonyId colonyA() const { return service.state().colonies.at(1).id; }
    [[nodiscard]] ColonyId colonyB() const { return service.state().colonies.at(4).id; }
    [[nodiscard]] ColonyId colonyC() const { return service.state().colonies.at(5).id; }
    [[nodiscard]] FleetId fleetId() const { return service.state().fleets.front().id; }
    [[nodiscard]] ObjectReference ref(const ObjectTarget target) const {
        return {interactions.world(), target};
    }
    [[nodiscard]] PreviewId inspect(const ObjectTarget target) {
        const auto id = interactions.inspect(ref(target));
        require(id.has_value(), "inspect a fixture target");
        return *id;
    }
    [[nodiscard]] NavigationResult goTo(const PreviewId source, const ObjectReference target) {
        return navigation.goTo({source, target}, queries, interactions, workspace, panels,
                               map, colonies, fleets);
    }
    void selectMars() {
        require(interactions.select({interactions.world(), mars()}), "select Mars main context");
    }
    [[nodiscard]] auto gameFingerprint() const {
        const auto& world = service.state();
        return std::tuple{world.date.day, world.eventLog.size(), world.ids.nextEventId,
                          world.colonies.at(1).processingPolicy, world.fleets.front().shipIds.size()};
    }
};

void expectedPreset(const Fixture& fixture, const Workspace destination,
                    const PanelVisibility before) {
    auto expected = before;
    applyWorkspace(destination, expected);
    require(fixture.workspace == destination && visibility(fixture.panels) == visibility(expected),
            "navigation applies the exact existing workspace preset and preserves globals");
}

void temporary_colony_go_to_closes_only_its_source() {
    Fixture f;
    f.selectMars();
    const PreviewId pin = f.inspect(f.colonyA());
    require(f.interactions.pin(pin), "pin A explicitly");
    const PreviewId temporary = f.inspect(f.colonyB());
    require(f.inspect(f.colonyC()) == temporary, "B to C retains temporary ID");
    f.workspace = Workspace::History;
    applyWorkspace(f.workspace, f.panels);
    f.panels.saveLoad = true;
    f.panels.timeControl = true;
    const auto beforePanels = f.panels;
    const auto gameBefore = f.gameFingerprint();

    const auto result = f.goTo(temporary, f.ref(f.colonyC()));
    require(result.ok(), "valid temporary Go To succeeds");
    expectedPreset(f, Workspace::Production, beforePanels);
    require(f.interactions.mainSelection().isColonySelected(f.colonyC()),
            "Colony C becomes authoritative main selection");
    const auto previews = f.interactions.previewSnapshot();
    require(previews.size() == 1 && previews.front().id == pin
            && previews.front().pinned && previews.front().target == f.ref(f.colonyA()),
            "only originating temporary closes and pinned A survives");
    require(Access::colonyPending(f.colonies) == f.ref(f.colonyC())
            && !Access::mapPending(f.map) && !Access::fleetPending(f.fleets),
            "only Colony destination receives a world-stamped reveal");
    require(f.gameFingerprint() == gameBefore, "Go To issues no gameplay command");
}

void body_and_fleet_workspace_mapping_and_reveal() {
    {
        Fixture f;
        f.selectMars();
        const PreviewId temporary = f.inspect(f.mars());
        f.workspace = Workspace::Economy;
        applyWorkspace(f.workspace, f.panels);
        const auto beforePanels = f.panels;
        const auto gameBefore = f.gameFingerprint();
        require(f.goTo(temporary, f.ref(f.mars())).ok(), "Body Go To succeeds");
        expectedPreset(f, Workspace::System, beforePanels);
        require(f.interactions.mainSelection().isBodySelected(f.mars())
                && f.interactions.previewSnapshot().empty(), "Body main selection and temporary disposition");
        require(Access::mapPending(f.map) == f.ref(f.mars()), "Body reveal targets map");
        require(f.gameFingerprint() == gameBefore, "Body navigation has no gameplay effect");
    }
    {
        Fixture f;
        f.selectMars();
        const PreviewId temporary = f.inspect(f.fleetId());
        f.workspace = Workspace::History;
        applyWorkspace(f.workspace, f.panels);
        const auto beforePanels = f.panels;
        const auto gameBefore = f.gameFingerprint();
        require(f.goTo(temporary, f.ref(f.fleetId())).ok(), "Fleet Go To succeeds");
        expectedPreset(f, Workspace::Fleets, beforePanels);
        require(f.interactions.mainSelection().isFleetSelected(f.fleetId())
                && f.interactions.previewSnapshot().empty(), "Fleet main selection and temporary disposition");
        require(Access::mapPending(f.map) == f.ref(f.fleetId())
                && Access::fleetPending(f.fleets) == f.ref(f.fleetId()),
                "Fleet reveal targets both map and fleet table");
        require(f.gameFingerprint() == gameBefore, "Fleet navigation has no gameplay effect");
    }
}

void pinned_source_and_existing_temporary_follow_foundation_semantics() {
    Fixture f;
    f.selectMars();
    const PreviewId pin = f.inspect(f.colonyA());
    require(f.interactions.pin(pin), "pin A");
    const auto gameBefore = f.gameFingerprint();
    require(f.goTo(pin, f.ref(f.colonyA())).ok(), "Go To from pin succeeds");
    auto previews = f.interactions.previewSnapshot();
    require(previews.size() == 1 && previews.front().id == pin && previews.front().pinned
            && previews.front().target == f.ref(f.colonyA()),
            "pinned source retains ID, target and pinned state without empty temporary");
    require(f.interactions.mainSelection().isColonySelected(f.colonyA())
            && f.workspace == Workspace::Production, "pin Go To updates main context");

    const PreviewId temporary = f.inspect(f.colonyB());
    const auto before = f.interactions.previewSnapshot();
    require(before.size() == 2, "another temporary coexists with the pin");
    require(f.goTo(pin, f.ref(f.colonyA())).ok(), "pinned Go To with temporary succeeds");
    previews = f.interactions.previewSnapshot();
    require(previews.size() == 2 && previews.at(0).id == pin && previews.at(0).pinned
            && previews.at(0).target == f.ref(f.colonyA())
            && previews.at(1).id == temporary && !previews.at(1).pinned
            && previews.at(1).target == f.ref(f.colonyA()),
            "existing temporary is retargeted by selectMain, not duplicated or closed");
    require(f.gameFingerprint() == gameBefore, "pinned navigation has no gameplay effect");
}

void ordinary_validation_rejections_are_atomic() {
    Fixture f;
    f.selectMars();
    const PreviewId temporary = f.inspect(f.colonyB());
    const ObjectReference oldTarget = f.ref(f.colonyB());
    require(f.inspect(f.colonyC()) == temporary, "temporary retargets before delayed Go To");
    f.workspace = Workspace::Intelligence;
    applyWorkspace(f.workspace, f.panels);
    const auto beforePanels = f.panels;
    const auto beforeSelection = f.interactions.mainSelection();
    const auto beforePreviews = f.interactions.previewSnapshot();
    const auto gameBefore = f.gameFingerprint();
    const auto failed = f.goTo(temporary, oldTarget);
    require(failed.outcome == NavigationOutcome::ValidationRejected && !failed.message.empty(),
            "retargeted source rejects old displayed target with feedback");
    require(f.workspace == Workspace::Intelligence && visibility(f.panels) == visibility(beforePanels)
            && f.interactions.mainSelection().type() == beforeSelection.type()
            && f.interactions.mainSelection().selectedId() == beforeSelection.selectedId()
            && f.interactions.previewSnapshot() == beforePreviews
            && f.gameFingerprint() == gameBefore,
            "ordinary rejection preserves workspace, panels, selection, previews and gameplay");
    require(!Access::mapPending(f.map) && !Access::colonyPending(f.colonies)
            && !Access::fleetPending(f.fleets), "rejection issues no reveal");

    const PreviewId unknown{f.interactions.world(), temporary.value + 1000};
    require(f.goTo(unknown, f.ref(f.colonyC())).outcome == NavigationOutcome::ValidationRejected,
            "unknown source rejects");
    require(f.interactions.closePreview(temporary), "close source before delayed Go To");
    const auto closedState = f.interactions.previewSnapshot();
    require(f.goTo(temporary, f.ref(f.colonyC())).outcome == NavigationOutcome::ValidationRejected
            && f.interactions.previewSnapshot() == closedState,
            "closed source rejects without fabricating another preview");

    const auto fresh = f.inspect(f.colonyB());
    const auto staleTarget = f.ref(f.colonyB());
    require(f.interactions.newGame().ok, "replace world for stale request test");
    const auto afterReplacement = f.interactions.previewSnapshot();
    const auto replacedSelection = f.interactions.mainSelection();
    const auto replacedPanels = f.panels;
    require(f.goTo(fresh, staleTarget).outcome == NavigationOutcome::ValidationRejected
            && f.interactions.previewSnapshot() == afterReplacement
            && f.interactions.mainSelection().type() == replacedSelection.type()
            && visibility(f.panels) == visibility(replacedPanels),
            "old-world Go To cannot act on reused numeric IDs after replacement");
}

} // namespace

int main() {
    try {
        temporary_colony_go_to_closes_only_its_source();
        std::cout << "PASS temporary Colony C navigation and pin preservation\n";
        body_and_fleet_workspace_mapping_and_reveal();
        std::cout << "PASS Body and Fleet workspace/reveal mapping\n";
        pinned_source_and_existing_temporary_follow_foundation_semantics();
        std::cout << "PASS pinned source and existing temporary behavior\n";
        ordinary_validation_rejections_are_atomic();
        std::cout << "PASS ordinary navigation rejection atomicity\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Information navigation test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
