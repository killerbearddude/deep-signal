#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/FleetOrdersPanel.h"
#include "ui_imgui/InspectorPanel.h"

// Exercise the production adapter, real New/Load operations, and actual inline
// workflow reset hooks without SDL/ImGui. UI entry wiring is additionally checked
// in the running application; no preview presentation is certified here.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
// Test-only fault injection into real query allocation. Disabled except around
// one call, and disarmed before throwing so error reporting can still allocate.
thread_local bool failNextAllocation = false;
}

void* operator new(const std::size_t size) {
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

namespace deep::ui_imgui {

// Narrow fixture access seeds/observes existing private drafts, rather than adding
// public mutation/debug controls or replacing the production reset implementation.
struct InformationLifecycleTestAccess {
    static void seed(InspectorPanel& inspector, FleetOrdersPanel& orders, ColonyPanel& colony,
                     FleetId fleet, BodyId body, ColonyId colonyId) {
        inspector.fleetMoveSource_ = fleet;
        inspector.lastMoveStatus_ = "old-world move";
        inspector.lastMoveSucceeded_ = false;
        inspector.lastCancelStatus_ = "old-world cancel";
        inspector.lastCancelSucceeded_ = false;
        orders.selectedFleetId_ = fleet;
        orders.destinationBodyId_ = body;
        orders.commandStatus_ = "old-world order";
        orders.commandSucceeded_ = false;
        colony.editingColony_ = colonyId;
        colony.selectedPolicy_ = ProcessingPolicy::Manual;
        colony.manualWeights_.fill(4.25);
        colony.statusMessage_ = "unapplied draft";
    }

    static auto snapshot(const InspectorPanel& inspector, const FleetOrdersPanel& orders, const ColonyPanel& colony) {
        return std::tuple{inspector.fleetMoveSource_, inspector.lastMoveStatus_, inspector.lastMoveSucceeded_,
                          inspector.lastCancelStatus_, inspector.lastCancelSucceeded_, orders.selectedFleetId_,
                          orders.destinationBodyId_, orders.commandStatus_, orders.commandSucceeded_,
                          colony.editingColony_, colony.selectedPolicy_, colony.manualWeights_, colony.statusMessage_};
    }

    static bool cleared(const InspectorPanel& inspector, const FleetOrdersPanel& orders, const ColonyPanel& colony) {
        return !inspector.fleetMoveSource_ && inspector.lastMoveStatus_ == "Ready" && inspector.lastMoveSucceeded_
            && inspector.lastCancelStatus_ == "No fleet order cancelled yet" && inspector.lastCancelSucceeded_
            && !orders.selectedFleetId_ && !orders.destinationBodyId_ && orders.commandStatus_ == "Ready"
            && orders.commandSucceeded_ && !colony.editingColony_ && colony.selectedPolicy_ == ProcessingPolicy::Balanced
            && std::all_of(colony.manualWeights_.begin(), colony.manualWeights_.end(), [](double value) { return value == 0.0; })
            && colony.statusMessage_.empty();
    }
};

} // namespace deep::ui_imgui

namespace {

using namespace deep;
using Access = ui_imgui::InformationLifecycleTestAccess;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

InformationPreview preview(const std::vector<InformationPreview>& records, const PreviewId id) {
    const auto it = std::find_if(records.begin(), records.end(), [id](const auto& record) { return record.id == id; });
    require(it != records.end(), "expected preview record exists");
    return *it;
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        static unsigned counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("deep-signal-3b-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        require(std::filesystem::create_directory(path), "create isolated test directory");
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

GameState makeWorld(std::string_view label) {
    auto state = createHomeSystemScenario();
    const auto& shipClass = state.shipClasses.front();
    const FleetId fleet{state.ids.nextFleetId++};
    const ShipId ship{state.ids.nextShipId++};
    state.fleets.push_back(Fleet{
        .id = fleet, .name = std::string{label} + " fleet", .currentBodyId = state.colonies.front().bodyId,
        .destinationBodyId = std::nullopt, .shipIds = {ship}, .activeOrder = FleetOrder{},
        .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(Ship{
        .id = ship, .shipClassId = shipClass.id, .name = std::string{label} + " ship",
        .fleetId = fleet, .fuel = shipClass.fuelCapacity
    });
    state.colonies.front().name = std::string{label} + " colony";
    return state;
}

struct Fixture {
    TempDirectory directory;
    SimulationService service{makeWorld("original")};
    ui_imgui::InspectorPanel inspector;
    ui_imgui::FleetOrdersPanel orders;
    ui_imgui::ColonyPanel colony;
    int resets = 0;
    InformationInteractionAdapter adapter{service, [this] {
        ++resets;
        inspector.resetWorldState();
        orders.resetWorldState();
        colony.resetWorldState();
    }};

    BodyId mars() const {
        const auto& bodies = service.state().bodies;
        const auto it = std::find_if(bodies.begin(), bodies.end(), [](const auto& body) { return body.name == "Mars"; });
        require(it != bodies.end(), "scenario has Mars");
        return it->id;
    }
    ColonyId colonyId() const { return service.state().colonies.front().id; }
    FleetId fleetId() const { return service.state().fleets.front().id; }
    ObjectReference ref(ObjectTarget object) const { return {adapter.world(), object}; }
    MainSelectionIntent selection(ObjectTarget object) const { return {adapter.world(), object}; }
    PreviewId inspect(ObjectTarget object) {
        const auto result = adapter.inspect(ref(object));
        require(result.has_value(), "inspect fixture object through production lookup");
        return *result;
    }
    void seed() {
        require(adapter.select(selection(mars())), "select main Mars");
        require(adapter.pin(inspect(colonyId())), "pin colony");
        inspect(fleetId());
        Access::seed(inspector, orders, colony, fleetId(), mars(), colonyId());
    }
    auto workflowSnapshot() const { return Access::snapshot(inspector, orders, colony); }
    auto interactionSnapshot() const {
        return std::tuple{adapter.world(), adapter.state().mainTarget(), adapter.state().previewSnapshot()};
    }
    bool workflowsCleared() const { return Access::cleared(inspector, orders, colony); }
};

void test_supported_selection_and_relationship_isolation() {
    Fixture f;
    for (const auto object : {ObjectTarget{f.mars()}, ObjectTarget{f.colonyId()}, ObjectTarget{f.fleetId()}}) {
        require(f.adapter.select(f.selection(object)), "select each supported typed object");
        require(f.adapter.state().mainTarget() == f.ref(object), "model is authoritative");
        require(f.adapter.state().previewSnapshot().empty(), "ordinary selection opens no preview");
    }
    require(f.adapter.select(f.selection(f.mars())), "select Mars");
    const auto related = f.inspect(f.colonyId());
    const auto before = f.interactionSnapshot();
    for (int frame = 0; frame < 10; ++frame) {
        require(f.adapter.mainSelection().isBodySelected(f.mars()), "fresh reader still sees Mars");
        f.adapter.reconcile();
    }
    require(f.interactionSnapshot() == before, "reads/reconciliation do not overwrite relationship preview");
    require(f.adapter.pin(related), "pin related object");
    const auto temporary = f.inspect(f.fleetId());
    // A real activation on the same already-selected row must not be lost.
    require(f.adapter.select(f.selection(f.mars())), "reselect same main object explicitly");
    const auto active = f.adapter.state().temporaryPreview();
    require(active && active->id == temporary && active->target == f.ref(f.mars()), "real activation retargets existing temporary");
    require(f.adapter.state().previewSnapshot().front().target == f.ref(f.colonyId()), "pin stays on colony");
    require(f.adapter.select({f.adapter.world(), std::nullopt}), "explicit empty-map clear");
    require(!f.adapter.state().mainTarget() && !f.adapter.state().temporaryPreview(), "clear removes main and temporary");
    require(f.adapter.state().previewSnapshot().size() == 1, "clear preserves pin");
}

void test_adapter_preview_actions_keep_window_and_main_identities() {
    Fixture f;
    require(f.adapter.select(f.selection(f.mars())), "select persistent main context");
    const auto main = f.adapter.state().mainTarget();
    const auto first = f.inspect(f.colonyId());
    const auto copied = f.adapter.previewSnapshot();
    require(copied.size() == 1 && preview(copied, first).target == f.ref(f.colonyId())
            && !preview(copied, first).pinned, "first adapter snapshot copies a temporary preview");

    require(f.adapter.pin(first), "pin first preview through adapter");
    const auto pinnedOnce = f.adapter.previewSnapshot();
    require(pinnedOnce.size() == 1 && preview(pinnedOnce, first).pinned,
            "pin preserves identity and creates no empty replacement");
    const auto second = f.inspect(f.colonyId());
    require(second != first && f.adapter.pin(second), "explicitly pin a duplicate target under another PreviewId");
    const auto temporary = f.inspect(f.colonyId());
    require(temporary != first && temporary != second && f.inspect(f.fleetId()) == temporary,
            "inspection after pinning creates a reusable temporary identity");
    const auto beforeUnpin = f.adapter.previewSnapshot();
    require(beforeUnpin.size() == 3 && preview(beforeUnpin, first).pinned
            && preview(beforeUnpin, second).pinned
            && preview(beforeUnpin, temporary).target == f.ref(f.fleetId()),
            "duplicate pins retain targets while temporary retargets");

    require(f.adapter.unpin(first), "unpin a displayed pin through adapter");
    const auto afterUnpin = f.adapter.previewSnapshot();
    require(afterUnpin.size() == 2 && !preview(afterUnpin, first).pinned
            && preview(afterUnpin, first).target == f.ref(f.colonyId())
            && preview(afterUnpin, second).pinned,
            "unpin keeps its ID/target, replaces the former temporary, and leaves other pins");
    require(!f.adapter.closePreview(temporary) && !f.adapter.unpin(temporary),
            "queued actions for replaced temporary cannot affect another window");
    require(f.adapter.closePreview(second) && f.adapter.previewSnapshot().size() == 1,
            "closing one pin preserves the temporary");
    require(f.adapter.closePreview(first) && f.adapter.previewSnapshot().empty(),
            "closing the remaining temporary removes only that record");
    require(!f.adapter.pin(first) && !f.adapter.unpin(first) && !f.adapter.closePreview(first),
            "closed PreviewId rejects delayed actions");
    require(f.adapter.state().mainTarget() == main && f.adapter.mainSelection().isBodySelected(f.mars()),
            "preview lifecycle actions never change main selection");
    require(copied.size() == 1 && !copied.front().pinned && copied.front().id == first,
            "earlier renderer snapshot stays independent of later actions");
}

void test_adapter_preview_snapshot_follows_replacement_outcomes() {
    Fixture f;
    f.seed();
    const auto before = f.adapter.previewSnapshot();
    require(before.size() == 2, "fixture has a pin and a temporary preview");
    require(!f.adapter.loadGame({}).ok && f.adapter.previewSnapshot() == before,
            "failed replacement preserves the rendered preview snapshot");
    require(f.adapter.newGame().ok && f.adapter.previewSnapshot().empty(),
            "successful replacement clears the rendered preview snapshot");
    for (const auto& record : before) {
        require(!f.adapter.pin(record.id) && !f.adapter.unpin(record.id)
                && !f.adapter.closePreview(record.id), "old-world preview actions are rejected");
    }
    const auto fresh = f.inspect(f.colonyId());
    require(fresh != before.front().id && fresh != before.back().id
            && f.adapter.previewSnapshot().size() == 1,
            "fresh world can create a new preview without recycling an old identity");
}

void test_adapter_preview_snapshot_reconciles_missing_targets() {
    Fixture f;
    const auto fleet = f.fleetId();
    require(f.adapter.select(f.selection(fleet)), "select fleet as main context");
    const auto first = f.inspect(fleet);
    require(f.adapter.pin(first), "pin fleet once");
    const auto second = f.inspect(fleet);
    require(f.adapter.pin(second), "pin fleet twice under a distinct ID");
    const auto survivor = f.inspect(f.colonyId());
    const auto copied = f.adapter.previewSnapshot();
    require(copied.size() == 3, "pre-removal snapshot includes both fleet pins and colony temporary");

    // No gameplay command deletes fleets. Replace the test-owned service value
    // without a New/Load notification to isolate live target reconciliation.
    auto changed = f.service.state();
    changed.fleets.clear();
    changed.ships.clear();
    f.service = SimulationService{std::move(changed)};
    const auto reconciled = f.adapter.previewSnapshot();
    require(reconciled.size() == 1 && reconciled.front().id == survivor
            && reconciled.front().target == f.ref(f.colonyId()),
            "previewSnapshot removes only records whose live query target vanished");
    require(!f.adapter.state().mainTarget() && copied.size() == 3,
            "same reconciliation clears missing main while prior copied snapshot remains valid");
    require(!f.adapter.closePreview(first) && !f.adapter.unpin(second),
            "missing-target preview actions are rejected after reconciliation");

    const auto beforeFailure = f.interactionSnapshot();
    bool threw = false;
    failNextAllocation = true;
    try { (void)f.adapter.previewSnapshot(); }
    catch (const std::bad_alloc&) { threw = true; }
    failNextAllocation = false;
    require(threw && f.interactionSnapshot() == beforeFailure,
            "previewSnapshot propagates a query failure without partially changing interaction state");
}

void test_new_game_success_clears_hidden_workflows_once() {
    Fixture f;
    f.seed();
    const auto origin = f.adapter.world();
    const auto* owner = &f.adapter.state();
    const auto oldBody = f.ref(f.mars());
    const auto result = f.adapter.newGame();
    require(result.ok && result.message == "New game created", "original real service result is preserved");
    require(f.adapter.world().value == origin.value + 1 && f.resets == 1, "one generation and callback per successful operation");
    require(&f.adapter.state() == owner, "interaction owner is not reconstructed");
    require(!f.adapter.state().mainTarget() && f.adapter.state().previewSnapshot().empty(), "New Game clears interaction records");
    require(f.workflowsCleared(), "actual reset hooks clear even never-rendered panel caches/drafts");
    require(SimulationQueries{f.service}.strategicBody(std::get<BodyId>(oldBody.object)).has_value(), "body ID recurs in new world");
    require(!f.adapter.inspect(oldBody), "reused current ID does not authorize old inspection");
    require(f.adapter.newGame().ok && f.adapter.world().value == origin.value + 2 && f.resets == 2,
            "a second actual New Game increments exactly once again");
}

void test_real_load_and_same_frame_stale_intentions() {
    Fixture f;
    SimulationService replacement{makeWorld("replacement")};
    require(replacement.execute(AdvanceDaysCommand{.days = 5}).ok, "advance replacement fixture");
    const auto save = f.directory.path / "replacement.sqlite";
    require(replacement.saveGame(save).ok, "write actual temporary replacement save");
    f.seed();
    const auto origin = f.adapter.world();
    const auto select = f.selection(f.colonyId());
    const auto inspect = f.ref(f.fleetId());
    const MainSelectionIntent clear{origin, std::nullopt};
    const auto result = f.adapter.loadGame(save);
    require(result.ok && result.message == "Game loaded", "actual Load result is preserved");
    require(f.service.state().date.day == 5 && f.service.state().fleets.front().name == "replacement fleet", "loaded a different real world");
    require(f.adapter.world().value == origin.value + 1 && f.resets == 1 && f.workflowsCleared(), "load notifies exactly once and resets real hooks");
    require(!f.adapter.state().mainTarget() && f.adapter.state().previewSnapshot().empty(), "load clears old interactions");
    require(f.adapter.select(f.selection(f.mars())), "new-world selection before delayed events arrive");
    f.inspect(f.colonyId());
    const auto before = f.interactionSnapshot();

    // Armed allocator proves stale intentions return before current-world query
    // allocation, not just that a reused ID happens to resolve to an equal value.
    failNextAllocation = true;
    const bool oldSelect = f.adapter.select(select);
    const auto oldInspect = f.adapter.inspect(inspect);
    const bool oldClear = f.adapter.select(clear);
    const bool noLookup = failNextAllocation;
    failNextAllocation = false;
    require(!oldSelect && !oldInspect && !oldClear && noLookup, "same-frame old intentions rejected before lookup");
    require(f.interactionSnapshot() == before, "stale events cannot clear or retarget new-world state");
    require(f.adapter.inspect(f.ref(f.fleetId())).has_value(), "fresh reference resolves reused fleet ID");
}

void test_failed_loads_preserve_state_and_actual_workflow_drafts() {
    Fixture f;
    f.seed();
    require(f.service.execute(AdvanceDaysCommand{.days = 3}).ok, "advance running world");
    const auto interactions = f.interactionSnapshot();
    const auto workflows = f.workflowSnapshot();
    const auto malformed = f.directory.path / "malformed.sqlite";
    { std::ofstream file{malformed}; file << "not a SQLite database"; }
    // Missing saves are rejected before opening SQLite. Exercise both a missing
    // file and a missing parent, confined to this test-owned directory.
    const std::filesystem::path paths[] = {{}, malformed, f.directory.path / "absent.sqlite",
                                          f.directory.path / "missing-directory" / "save.sqlite"};
    for (const auto& path : paths) {
        const auto result = f.adapter.loadGame(path);
        require(!result.ok && !result.message.empty(), "failure is not confused with lifecycle acknowledgment");
        require(f.interactionSnapshot() == interactions && f.workflowSnapshot() == workflows && f.resets == 0,
                "failed load preserves interaction records and real unapplied drafts");
        require(f.service.state().date.day == 3 && f.service.state().fleets.front().name == "original fleet",
                "failed load preserves running world");
    }
}

void test_save_and_time_are_not_replacement() {
    Fixture f;
    f.seed();
    const auto before = f.interactionSnapshot();
    const auto workflows = f.workflowSnapshot();
    const auto save = f.directory.path / "ordinary-save.sqlite";
    require(f.service.saveGame(save).ok, "ordinary save uses existing service path");
    require(f.service.execute(AdvanceDaysCommand{.days = 1}).ok, "advance 1");
    require(f.service.execute(AdvanceDaysCommand{.days = 5}).ok, "advance 5");
    require(f.service.execute(AdvanceDaysCommand{.days = 30}).ok, "advance 30");
    (void)f.adapter.mainSelection();
    require(f.service.state().date.day == 36, "all ordinary time commands execute");
    require(f.interactionSnapshot() == before && f.workflowSnapshot() == workflows && f.resets == 0,
            "data/date changes do not imply replacement or replay selection");
}

void test_current_lookup_and_missing_target_reconciliation() {
    Fixture f;
    const auto fleet = f.fleetId();
    require(f.adapter.select(f.selection(fleet)), "select existing fleet");
    require(f.adapter.pin(f.inspect(fleet)), "pin existing fleet");
    const auto unrelated = f.inspect(f.colonyId());
    const auto copied = f.adapter.state().previewSnapshot();
    // There is no gameplay deletion command. Inject a validated fixture with the
    // fleet/ship removed solely to exercise current-query disappearance; this is
    // NOT a New/Load test and issues no lifecycle notification. Every actual
    // New/Load scenario above goes through the production replacement wrapper.
    auto changed = f.service.state();
    changed.fleets.clear();
    changed.ships.clear();
    f.service = SimulationService{std::move(changed)};
    require(f.adapter.mainSelection().type() == SelectedObjectType::None, "consumer reconciles missing main before use");
    const auto temporary = f.adapter.state().temporaryPreview();
    require(temporary && temporary->id == unrelated && f.adapter.state().previewSnapshot().size() == 1,
            "unrelated relationship preview remains while missing fleet pin closes");
    require(!f.adapter.select(f.selection(fleet)) && !f.adapter.inspect(f.ref(fleet)), "resolver does not cache old DTO existence");
    require(copied.size() == 2, "previously copied records remain valid values");
}

void test_real_lookup_failure_and_unexpected_new_game_exception() {
    Fixture f;
    f.seed();
    const auto before = f.interactionSnapshot();
    const auto workflows = f.workflowSnapshot();
    bool lookupThrew = false;
    failNextAllocation = true;
    try { f.adapter.reconcile(); }
    catch (const std::bad_alloc&) { lookupThrew = true; }
    failNextAllocation = false;
    require(lookupThrew && f.interactionSnapshot() == before, "real query failure propagates without partial reconcile");
    bool newThrew = false;
    failNextAllocation = true;
    try { (void)f.adapter.newGame(); }
    catch (const std::bad_alloc&) { newThrew = true; }
    failNextAllocation = false;
    require(newThrew && f.interactionSnapshot() == before && f.workflowSnapshot() == workflows && f.resets == 0,
            "unexpected New Game exception is not reported as success or used to clear UI state");
}

void test_reset_failure_is_not_an_ordinary_failed_load() {
    SimulationService service{makeWorld("original")};
    InformationInteractionAdapter adapter{service, [] { throw std::runtime_error{"workflow reset failed"}; }};
    const auto origin = adapter.world();
    bool threw = false;
    try { (void)adapter.newGame(); }
    catch (const std::runtime_error& error) { threw = std::string_view{error.what()} == "workflow reset failed"; }
    require(threw && adapter.world().value == origin.value + 1 && service.state().fleets.empty(),
            "post-replacement integration failure surfaces instead of returning a recoverable failed result");
}

} // namespace

int main() {
    const std::pair<std::string_view, void (*)()> tests[] = {
        {"typed selection, actual reactivation, and inspection isolation", test_supported_selection_and_relationship_isolation},
        {"adapter preview actions retain identities and main selection", test_adapter_preview_actions_keep_window_and_main_identities},
        {"adapter preview snapshot follows replacement outcomes", test_adapter_preview_snapshot_follows_replacement_outcomes},
        {"adapter preview snapshot reconciles missing targets", test_adapter_preview_snapshot_reconciles_missing_targets},
        {"real New Game and hidden-workflow resets", test_new_game_success_clears_hidden_workflows_once},
        {"real Load, reused IDs, and same-frame stale intentions", test_real_load_and_same_frame_stale_intentions},
        {"failed/empty/malformed loads preserve world and drafts", test_failed_loads_preserve_state_and_actual_workflow_drafts},
        {"Save/time do not replace world", test_save_and_time_are_not_replacement},
        {"current query resolution and missing targets", test_current_lookup_and_missing_target_reconciliation},
        {"real lookup and New Game exception propagation", test_real_lookup_failure_and_unexpected_new_game_exception},
        {"post-replacement reset failure", test_reset_failure_is_not_an_ordinary_failed_load}
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) {
            failNextAllocation = false;
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
