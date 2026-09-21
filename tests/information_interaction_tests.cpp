#include "app/InformationInteractionState.h"

// Headless interaction-contract tests. The lookup fixture stands in for copied
// query DTOs; this executable deliberately cannot call the simulation service,
// persistence, SDL, or ImGui. Window focus/geometry and actual New/Load wiring
// require the later UI integration and are not certified by these model tests.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace deep;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

struct QueryRow {
    ObjectTarget id;
    int currentValue = 0;
    bool operator==(const QueryRow&) const = default;
};

struct Fixture {
    InformationInteractionState state;
    std::vector<QueryRow> rows{
        {BodyId{1}, 10}, {BodyId{2}, 20}, {ColonyId{1}, 30},
        {ColonyId{2}, 40}, {ColonyId{3}, 50}, {FleetId{1}, 60}
    };

    ObjectReference ref(ObjectTarget id) const { return {state.world(), id}; }

    InformationInteractionState::TargetExists lookup() const {
        return [this](const ObjectTarget& id) {
            return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
        };
    }

    PreviewId inspect(ObjectTarget id) {
        const auto preview = state.inspect(ref(id), lookup());
        require(preview.has_value(), "fixture object must be inspectable");
        return *preview;
    }

    PreviewId pin(ObjectTarget id) {
        const auto preview = inspect(id);
        require(state.pin(preview), "fixture preview must pin");
        return preview;
    }

    void remove(ObjectTarget id) {
        std::erase_if(rows, [&](const auto& row) { return row.id == id; });
    }
};

struct Snapshot {
    WorldGeneration world;
    std::optional<ObjectReference> main;
    std::vector<InformationPreview> previews;
    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const InformationInteractionState& state) {
    return {state.world(), state.mainTarget(), state.previewSnapshot()};
}

InformationPreview preview(const InformationInteractionState& state, const PreviewId id) {
    for (const auto& record : state.previewSnapshot()) {
        if (record.id == id) {
            return record;
        }
    }
    throw std::runtime_error{"expected preview is missing"};
}

void requireInvariants(const InformationInteractionState& state) {
    const auto records = state.previewSnapshot();
    require(std::count_if(records.begin(), records.end(), [](const auto& record) { return !record.pinned; }) <= 1,
            "at most one temporary preview");
    for (const auto& record : records) {
        require(record.id.world == state.world() && record.target.world == state.world(), "records belong to active world");
        require(record.id.value > 0, "window IDs are assigned");
        require(std::count_if(records.begin(), records.end(), [&](const auto& other) { return other.id == record.id; }) == 1,
                "window IDs are unique even for duplicate targets");
    }
}

void test_initial_state_and_typed_main_selection() {
    Fixture f;
    require(!f.state.mainTarget() && f.state.previewSnapshot().empty(), "initial interaction state is empty");
    require(f.state.mainSelection().type() == SelectedObjectType::None, "initial selection is None");
    require(f.ref(BodyId{1}) != f.ref(ColonyId{1}) && f.ref(ColonyId{1}) != f.ref(FleetId{1}),
            "object kind distinguishes the same numeric ID");
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select body");
    require(f.state.mainSelection().isBodySelected(BodyId{1}), "existing typed body accessor remains usable");
    require(f.state.selectMain(f.ref(ColonyId{1}), f.lookup()), "select colony");
    require(f.state.mainSelection().isColonySelected(ColonyId{1}), "typed colony selection");
    require(f.state.selectMain(f.ref(FleetId{1}), f.lookup()), "select fleet");
    require(f.state.mainSelection().isFleetSelected(FleetId{1}), "typed fleet selection");
    require(f.state.previewSnapshot().empty(), "selection primitive alone never spawns previews");
}

void test_relationship_inspection_reuses_window_without_selection_feedback() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    const auto id = f.inspect(ColonyId{1});
    for (int i = 0; i < 30; ++i) {
        require(f.inspect(ColonyId{2}) == id && f.inspect(ColonyId{1}) == id, "inspection reuses window identity");
        (void)f.state.mainSelection();
        (void)f.state.previewSnapshot();
        require(f.state.reconcile(f.state.world(), f.lookup()), "validate still-existing targets");
        require(f.state.temporaryPreview()->target == f.ref(ColonyId{1}), "unchanged main must not overwrite relationship");
    }
    require(f.state.mainTarget() == f.ref(BodyId{1}), "relationship never changes main selection");
    require(f.state.previewSnapshot().size() == 1, "ordinary inspection cannot accumulate windows");
    requireInvariants(f.state);
}

void test_selection_retargets_only_existing_temporary() {
    Fixture f;
    const auto pinned = f.pin(ColonyId{1});
    const auto temporary = f.inspect(ColonyId{2});
    require(f.state.selectMain(f.ref(BodyId{2}), f.lookup()), "new actual selection");
    require(f.state.temporaryPreview()->id == temporary, "selection preserves temporary window identity");
    require(f.state.temporaryPreview()->target == f.ref(BodyId{2}), "temporary follows selection event");
    require(preview(f.state, pinned).target == f.ref(ColonyId{1}), "pinned target is independent");
    require(f.state.pin(temporary), "pin selected object");
    require(f.state.selectMain(f.ref(FleetId{1}), f.lookup()), "select while only pins remain");
    require(!f.state.temporaryPreview(), "selection does not create a replacement after pinning");
    requireInvariants(f.state);
}

void test_pin_a_inspect_b_then_c_and_follow_pinned_relationship() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    const auto a = f.inspect(ColonyId{1});
    require(f.state.pin(a) && !f.state.temporaryPreview(), "pin keeps record without empty replacement");
    const auto b = f.inspect(ColonyId{2});
    require(b != a && f.inspect(ColonyId{3}) == b, "subsequent inspections create then reuse temporary");
    require(f.state.previewSnapshot().size() == 2, "one pin and one temporary remain");
    require(preview(f.state, a).target == f.ref(ColonyId{1}), "A is still pinned to A");
    require(f.state.temporaryPreview()->target == f.ref(ColonyId{3}), "temporary now targets C");
    // A relationship clicked in pinned A uses inspection, not a pin-target setter.
    require(f.inspect(BodyId{2}) == b, "pinned relationship follows the shared temporary path");
    require(preview(f.state, a).target == f.ref(ColonyId{1}), "relationship cannot retarget its originating pin");
    require(f.state.mainTarget() == f.ref(BodyId{1}), "main context selection stays independent");
    requireInvariants(f.state);
}

void test_unpin_replaces_temporary_and_survives_erase_order() {
    Fixture f;
    const auto a = f.pin(ColonyId{1});
    const auto other = f.pin(FleetId{1});
    const auto b = f.inspect(ColonyId{2});
    require(f.state.unpin(a), "unpin A while B is temporary");
    require(f.state.temporaryPreview()->id == a && f.state.temporaryPreview()->target == f.ref(ColonyId{1}),
            "A preserves identity and target as new temporary");
    require(!f.state.close(b), "old temporary B has been replaced");
    require(preview(f.state, other).pinned, "other pin survives");
    // The temporary A precedes 'other' in storage: erase must not reuse an iterator.
    require(f.state.unpin(other), "unpin with temporary earlier in storage");
    require(f.state.temporaryPreview()->id == other && f.state.previewSnapshot().size() == 1,
            "correct record survives vector compaction");
    require(f.state.unpin(other), "unpinning the current temporary is harmless");
    requireInvariants(f.state);
}

void test_close_and_delayed_record_requests() {
    Fixture f;
    const auto a = f.pin(ColonyId{1});
    const auto b = f.inspect(ColonyId{2});
    const auto renderRecords = f.state.previewSnapshot();
    require(f.state.close(a), "close pin");
    require(f.state.temporaryPreview()->id == b, "closing a pin leaves temporary intact");
    require(f.state.close(b), "close temporary");
    const auto c = f.inspect(ColonyId{3});
    require(c != b, "closed window identity is not recycled");
    for (const auto& record : renderRecords) {
        require(!f.state.pin(record.id) && !f.state.unpin(record.id) && !f.state.close(record.id),
                "queued requests for closed windows cannot affect a later preview");
    }
    require(f.state.temporaryPreview()->id == c, "new temporary survives stale requests");
    requireInvariants(f.state);
}

void test_explicit_duplicate_pins() {
    Fixture f;
    const auto a = f.pin(ColonyId{1});
    const auto b = f.inspect(ColonyId{1});
    require(a != b && preview(f.state, a).pinned, "inspecting pinned target does not steal its window");
    require(f.inspect(ColonyId{1}) == b, "repeated inspection alone creates no duplicates");
    require(f.state.pin(b), "explicitly pin duplicate target");
    require(f.state.previewSnapshot().size() == 2 && !f.state.temporaryPreview(), "both explicit pins retained");
    const auto before = snapshot(f.state);
    require(f.state.pin(a) && snapshot(f.state) == before, "repeated pin is idempotent");
    requireInvariants(f.state);
}

void test_invalid_requests_preserve_state() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    f.pin(ColonyId{1});
    f.inspect(ColonyId{2});
    const auto before = snapshot(f.state);
    const ObjectReference invalid[] = {
        f.ref(BodyId{0}), f.ref(ColonyId{-1}), f.ref(FleetId{999}),
        {WorldGeneration{0}, BodyId{1}}, {WorldGeneration{2}, BodyId{1}}
    };
    for (const auto& target : invalid) {
        require(!f.state.selectMain(target, f.lookup()), "invalid selection rejected");
        require(!f.state.inspect(target, f.lookup()), "invalid inspection rejected");
        require(!f.state.requestGoTo(target, f.lookup()), "invalid navigation intention rejected");
        require(snapshot(f.state) == before, "rejection has no interaction side effects");
    }
    require(!f.state.inspect(f.ref(BodyId{1}), {}), "absent lookup rejects inspection");
    require(!f.state.reconcile(f.state.world(), {}), "absent lookup cannot remove existing records");
    require(!f.state.pin(PreviewId{f.state.world(), 999}), "unknown window rejected");
    require(snapshot(f.state) == before, "all rejected operations leave state intact");
}

void test_clear_selection_keeps_pins() {
    Fixture f;
    const auto pinned = f.pin(ColonyId{1});
    f.inspect(ColonyId{2});
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select body");
    require(f.state.clearMain(f.state.world()), "clear main selection explicitly");
    require(!f.state.mainTarget() && !f.state.temporaryPreview(), "clear closes temporary and empties main");
    require(f.state.previewSnapshot().size() == 1 && preview(f.state, pinned).pinned, "valid pins survive clear");
}

void test_missing_targets_remove_only_matching_records() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select body");
    f.pin(BodyId{1});
    f.pin(BodyId{1});
    const auto other = f.pin(FleetId{1});
    const auto temporary = f.inspect(ColonyId{2});
    f.remove(BodyId{1});
    require(f.state.reconcile(f.state.world(), f.lookup()), "reconcile removed main/pinned object");
    require(!f.state.mainTarget(), "missing main object cleared");
    require(f.state.previewSnapshot().size() == 2 && preview(f.state, other).pinned, "only affected duplicate pins closed");
    require(f.state.temporaryPreview()->id == temporary, "unrelated relationship preview survives missing main");
    f.remove(ColonyId{2});
    require(f.state.reconcile(f.state.world(), f.lookup()), "reconcile removed temporary object");
    require(!f.state.temporaryPreview() && f.state.previewSnapshot().size() == 1, "only missing temporary closed");
    requireInvariants(f.state);
}

void test_lookup_failure_does_not_partially_reconcile() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    f.pin(ColonyId{1});
    f.inspect(ColonyId{2});
    const auto before = snapshot(f.state);
    int calls = 0;
    bool threw = false;
    try {
        (void)f.state.reconcile(f.state.world(), [&](const ObjectTarget&) {
            if (++calls == 3) {
                throw std::runtime_error{"lookup failed"};
            }
            return false;
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && calls == 3, "lookup fails after earlier targets were marked missing");
    require(snapshot(f.state) == before, "failed lookup cannot partially clear selection or compact records");
    requireInvariants(f.state);
}

void test_world_replacement_and_reused_numeric_ids() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    const auto pin = f.pin(ColonyId{1});
    const auto temporary = f.inspect(ColonyId{2});
    const auto oldTarget = f.ref(ColonyId{1});
    const auto oldWorld = f.state.world();
    const auto navigation = f.state.requestGoTo(oldTarget, f.lookup());
    const auto before = snapshot(f.state);
    require(f.state.worldReplacementFinished(oldWorld, false), "accept failed-load notification");
    require(snapshot(f.state) == before, "failed replacement preserves all interactions");
    require(f.state.worldReplacementFinished(oldWorld, true), "accept successful New/Load notification");
    require(f.state.world() != oldWorld && !f.state.mainTarget() && f.state.previewSnapshot().empty(),
            "successful replacement changes world identity and clears interactions");
    // Same numeric IDs still resolve in the fixture. Old requests must fail first.
    int lookups = 0;
    const auto currentLookup = [&](const ObjectTarget&) { ++lookups; return true; };
    require(!f.state.inspect(oldTarget, currentLookup) && !f.state.selectMain(oldTarget, currentLookup), "old object requests rejected");
    require(!f.state.requestGoTo(navigation->target, currentLookup), "queued navigation is stale after replacement");
    require(!f.state.reconcile(oldWorld, currentLookup) && lookups == 0, "old requests never query reused current IDs");
    require(!f.state.clearMain(oldWorld) && !f.state.worldReplacementFinished(oldWorld, true), "stale lifecycle notifications rejected");
    require(!f.state.pin(pin) && !f.state.unpin(pin) && !f.state.close(temporary), "old window requests rejected");
    const auto fresh = f.inspect(ColonyId{1});
    require(fresh != pin && fresh.value > temporary.value, "new world does not recycle preview serials");
    require(f.state.temporaryPreview()->target == f.ref(ColonyId{1}), "fresh reference can inspect reused numeric ID");
    requireInvariants(f.state);
}

void test_navigation_is_inert_and_query_values_stay_live() {
    Fixture f;
    const auto queryBefore = f.rows;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select main body");
    const auto pin = f.pin(ColonyId{1});
    f.inspect(ColonyId{2});
    const auto before = snapshot(f.state);
    const auto request = f.state.requestGoTo(preview(f.state, pin).target, f.lookup());
    require(request && request->target == f.ref(ColonyId{1}), "navigation intention captures explicit target");
    require(snapshot(f.state) == before, "navigation request alone cannot select, close, or retarget");
    require(f.rows == queryBefore, "inspection/pinning/navigation never mutate caller-owned query data");

    // Simulate fresh query results without handing values to the identity model.
    auto& liveRow = f.rows[2];
    liveRow.currentValue += 7;
    const auto pinnedTarget = preview(f.state, pin).target;
    const auto resolved = std::find_if(f.rows.begin(), f.rows.end(), [&](const auto& row) { return row.id == pinnedTarget.object; });
    require(resolved != f.rows.end() && resolved->currentValue == 37, "pinned identity resolves current external values");
    require(f.state.reconcile(f.state.world(), f.lookup()) && snapshot(f.state) == before, "changing values does not change pinned identity");
}

void test_render_snapshots_are_independent_values() {
    Fixture f;
    require(f.state.selectMain(f.ref(BodyId{1}), f.lookup()), "select body");
    const auto a = f.inspect(ColonyId{1});
    auto records = f.state.previewSnapshot();
    auto selected = f.state.mainSelection();
    selected.selectFleet(FleetId{1});
    records.front().pinned = true;
    require(!f.state.temporaryPreview()->pinned && f.state.mainTarget() == f.ref(BodyId{1}),
            "editing returned snapshots cannot bypass interaction methods");
    require(f.inspect(ColonyId{2}) == a, "retarget actual model");
    require(records.front().target == f.ref(ColonyId{1}), "retarget does not alter a render snapshot already in use");
    require(f.state.close(a) && records.front().id == a, "closing a record does not invalidate copied render data");
}

} // namespace

int main() {
    const std::pair<std::string_view, void (*)()> tests[] = {
        {"initial state and typed main selection", test_initial_state_and_typed_main_selection},
        {"relationship isolation and temporary reuse", test_relationship_inspection_reuses_window_without_selection_feedback},
        {"main selection follows only into existing temporary", test_selection_retargets_only_existing_temporary},
        {"pin A, inspect B/C, follow pinned relationship", test_pin_a_inspect_b_then_c_and_follow_pinned_relationship},
        {"unpin replacement and erase order", test_unpin_replaces_temporary_and_survives_erase_order},
        {"close and delayed record requests", test_close_and_delayed_record_requests},
        {"explicit duplicate pins", test_explicit_duplicate_pins},
        {"invalid request preservation", test_invalid_requests_preserve_state},
        {"empty selection policy", test_clear_selection_keeps_pins},
        {"missing target policy", test_missing_targets_remove_only_matching_records},
        {"lookup failure atomicity", test_lookup_failure_does_not_partially_reconcile},
        {"world replacement and reused numeric IDs", test_world_replacement_and_reused_numeric_ids},
        {"inert navigation and live external values", test_navigation_is_inert_and_query_values_stay_live},
        {"render snapshot ownership", test_render_snapshots_are_independent_values}
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
