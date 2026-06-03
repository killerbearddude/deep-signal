#include "app/SimulationService.h"
#include "save/Database.h"
#include "save/SaveGameRepository.h"
#include "sim/Commands.h"
#include "sim/Minerals.h"

// Regression tests for SQLite save/load round-tripping.
// These tests verify that schema v2 persists durable Prototype 0.1 state,
// including ID counters, economy rows, production, active fleet orders, and events.
// Runtime-only economy telemetry is tested separately as intentionally transient.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

bool almostEqual(const double lhs, const double rhs) noexcept {
    constexpr double kEpsilon = 1.0e-9;
    return std::fabs(lhs - rhs) <= kEpsilon;
}

template <typename IdT>
bool sameOptionalId(const std::optional<IdT> lhs, const std::optional<IdT> rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || lhs->value == rhs->value;
}

bool sameMineralSet(const deep::MineralSet& lhs, const deep::MineralSet& rhs) noexcept {
    for (std::size_t i = 0; i < deep::mineralCount(); ++i) {
        if (!almostEqual(lhs.amount.at(i), rhs.amount.at(i))) {
            return false;
        }
    }
    return true;
}

bool sameProcessedMaterialSet(const deep::ProcessedMaterialSet& lhs, const deep::ProcessedMaterialSet& rhs) noexcept {
    for (std::size_t i = 0; i < deep::processedMaterialCount(); ++i) {
        if (!almostEqual(lhs.amount.at(i), rhs.amount.at(i))) {
            return false;
        }
    }
    return true;
}

bool samePayload(const deep::SimEventPayload& lhs, const deep::SimEventPayload& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }

    return std::visit([](const auto& left, const auto& right) -> bool {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) {
            return false;
        } else if constexpr (std::is_same_v<Left, deep::MineralExtractedEvent>) {
            return left.colonyId == right.colonyId &&
                   left.bodyId == right.bodyId &&
                   left.mineral == right.mineral &&
                   almostEqual(left.amount, right.amount) &&
                   almostEqual(left.remainingDeposit, right.remainingDeposit);
        } else if constexpr (std::is_same_v<Left, deep::ShipyardOrderCreatedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipClassId == right.shipClassId &&
                   left.quantity == right.quantity;
        } else if constexpr (std::is_same_v<Left, deep::ShipCompletedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipId == right.shipId &&
                   left.fleetId == right.fleetId &&
                   left.shipClassId == right.shipClassId;
        } else if constexpr (std::is_same_v<Left, deep::FleetOrderAssignedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.originBodyId == right.originBodyId &&
                   left.destinationBodyId == right.destinationBodyId &&
                   left.daysRemaining == right.daysRemaining;
        } else if constexpr (std::is_same_v<Left, deep::FleetArrivedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.destinationBodyId == right.destinationBodyId;
        } else if constexpr (std::is_same_v<Left, deep::CommandRejectedEvent>) {
            return left.reason == right.reason;
        }
    }, lhs, rhs);
}

void requireSameState(const deep::GameState& expected, const deep::GameState& actual) {
    // Verifies table-by-table persistence fidelity. These checks intentionally
    // compare many primitive fields because schema regressions often drop only
    // one column while leaving row counts correct. dailyEconomySnapshots is not
    // compared here because it is explicitly runtime-only telemetry.
    require(expected.date.day == actual.date.day, "date day round-trips");
    require(expected.ids.nextStarSystemId == actual.ids.nextStarSystemId, "star-system counter round-trips");
    require(expected.ids.nextBodyId == actual.ids.nextBodyId, "body counter round-trips");
    require(expected.ids.nextColonyId == actual.ids.nextColonyId, "colony counter round-trips");
    require(expected.ids.nextShipClassId == actual.ids.nextShipClassId, "ship-class counter round-trips");
    require(expected.ids.nextShipyardOrderId == actual.ids.nextShipyardOrderId, "shipyard-order counter round-trips");
    require(expected.ids.nextShipId == actual.ids.nextShipId, "ship counter round-trips");
    require(expected.ids.nextFleetId == actual.ids.nextFleetId, "fleet counter round-trips");
    require(expected.ids.nextEventId == actual.ids.nextEventId, "event counter round-trips");

    require(expected.starSystems.size() == actual.starSystems.size(), "star-system row count round-trips");
    for (std::size_t i = 0; i < expected.starSystems.size(); ++i) {
        require(expected.starSystems.at(i).id == actual.starSystems.at(i).id, "star-system ID round-trips");
        require(expected.starSystems.at(i).name == actual.starSystems.at(i).name, "star-system name round-trips");
    }

    require(expected.bodies.size() == actual.bodies.size(), "body row count round-trips");
    for (std::size_t i = 0; i < expected.bodies.size(); ++i) {
        const deep::Body& left = expected.bodies.at(i);
        const deep::Body& right = actual.bodies.at(i);
        require(left.id == right.id, "body ID round-trips");
        require(left.systemId == right.systemId, "body system ID round-trips");
        require(left.name == right.name, "body name round-trips");
        require(left.type == right.type, "body type round-trips");
        require(almostEqual(left.x, right.x), "body x coordinate round-trips");
        require(almostEqual(left.y, right.y), "body y coordinate round-trips");
    }

    require(expected.colonies.size() == actual.colonies.size(), "colony row count round-trips");
    for (std::size_t i = 0; i < expected.colonies.size(); ++i) {
        const deep::Colony& left = expected.colonies.at(i);
        const deep::Colony& right = actual.colonies.at(i);
        require(left.id == right.id, "colony ID round-trips");
        require(left.bodyId == right.bodyId, "colony body ID round-trips");
        require(left.name == right.name, "colony name round-trips");
        require(sameMineralSet(left.stockpile, right.stockpile), "colony raw stockpile round-trips");
        require(sameProcessedMaterialSet(left.processedStockpile, right.processedStockpile),
                "colony processed stockpile round-trips");
        require(almostEqual(left.mines, right.mines), "colony mines round-trip");
        require(almostEqual(left.processorCapacity, right.processorCapacity), "processor capacity round-trips");
        require(almostEqual(left.shipyardCapacity, right.shipyardCapacity), "shipyard capacity round-trips");
    }

    require(expected.mineralDeposits.size() == actual.mineralDeposits.size(), "deposit row count round-trips");
    for (std::size_t i = 0; i < expected.mineralDeposits.size(); ++i) {
        const deep::MineralDeposit& left = expected.mineralDeposits.at(i);
        const deep::MineralDeposit& right = actual.mineralDeposits.at(i);
        require(left.bodyId == right.bodyId, "deposit body ID round-trips");
        require(left.mineral == right.mineral, "deposit mineral round-trips");
        require(almostEqual(left.remaining, right.remaining), "deposit remaining amount round-trips");
        require(almostEqual(left.accessibility, right.accessibility), "deposit accessibility round-trips");
    }

    require(expected.shipClasses.size() == actual.shipClasses.size(), "ship-class row count round-trips");
    for (std::size_t i = 0; i < expected.shipClasses.size(); ++i) {
        const deep::ShipClass& left = expected.shipClasses.at(i);
        const deep::ShipClass& right = actual.shipClasses.at(i);
        require(left.id == right.id, "ship-class ID round-trips");
        require(left.name == right.name, "ship-class name round-trips");
        require(left.role == right.role, "ship-class role round-trips");
        require(sameProcessedMaterialSet(left.buildCost, right.buildCost), "ship-class processed cost round-trips");
        require(almostEqual(left.buildPoints, right.buildPoints), "ship-class build points round-trip");
        require(almostEqual(left.speedKmPerDay, right.speedKmPerDay), "ship-class speed round-trips");
        require(almostEqual(left.fuelCapacity, right.fuelCapacity), "ship-class fuel capacity round-trips");
    }

    require(expected.shipyardOrders.size() == actual.shipyardOrders.size(), "shipyard-order row count round-trips");
    for (std::size_t i = 0; i < expected.shipyardOrders.size(); ++i) {
        const deep::ShipyardOrder& left = expected.shipyardOrders.at(i);
        const deep::ShipyardOrder& right = actual.shipyardOrders.at(i);
        require(left.id == right.id, "shipyard-order ID round-trips");
        require(left.colonyId == right.colonyId, "shipyard-order colony ID round-trips");
        require(left.shipClassId == right.shipClassId, "shipyard-order ship-class ID round-trips");
        require(left.quantityRequested == right.quantityRequested, "shipyard-order requested quantity round-trips");
        require(left.quantityCompleted == right.quantityCompleted, "shipyard-order completed quantity round-trips");
        require(almostEqual(left.accumulatedBuildPoints, right.accumulatedBuildPoints), "shipyard-order progress round-trips");
        require(left.status == right.status, "shipyard-order status round-trips");
    }

    require(expected.fleets.size() == actual.fleets.size(), "fleet row count round-trips");
    for (std::size_t i = 0; i < expected.fleets.size(); ++i) {
        const deep::Fleet& left = expected.fleets.at(i);
        const deep::Fleet& right = actual.fleets.at(i);
        require(left.id == right.id, "fleet ID round-trips");
        require(left.name == right.name, "fleet name round-trips");
        require(left.currentBodyId == right.currentBodyId, "fleet current body round-trips");
        require(sameOptionalId(left.destinationBodyId, right.destinationBodyId), "fleet destination body round-trips");
        require(left.shipIds == right.shipIds, "fleet ship ID list is rebuilt from ships");
        require(left.activeOrder.type == right.activeOrder.type, "fleet order type round-trips");
        require(sameOptionalId(left.activeOrder.targetBodyId, right.activeOrder.targetBodyId), "fleet order target round-trips");
        require(left.activeOrder.daysRemaining == right.activeOrder.daysRemaining, "fleet order days remaining round-trips");
    }

    require(expected.ships.size() == actual.ships.size(), "ship row count round-trips");
    for (std::size_t i = 0; i < expected.ships.size(); ++i) {
        const deep::Ship& left = expected.ships.at(i);
        const deep::Ship& right = actual.ships.at(i);
        require(left.id == right.id, "ship ID round-trips");
        require(left.shipClassId == right.shipClassId, "ship class reference round-trips");
        require(left.name == right.name, "ship name round-trips");
        require(left.fleetId == right.fleetId, "ship fleet reference round-trips");
        require(almostEqual(left.fuel, right.fuel), "ship fuel round-trips");
    }

    require(expected.eventLog.size() == actual.eventLog.size(), "event log row count round-trips");
    for (std::size_t i = 0; i < expected.eventLog.size(); ++i) {
        const deep::SimEvent& left = expected.eventLog.at(i);
        const deep::SimEvent& right = actual.eventLog.at(i);
        require(left.id == right.id, "event ID round-trips");
        require(left.day == right.day, "event day round-trips");
        require(left.severity == right.severity, "event severity round-trips");
        require(samePayload(left.payload, right.payload), "typed event payload round-trips");
    }
}

std::filesystem::path testSavePath() {
    // Use the temp directory so repeated local test runs do not dirty the source
    // tree. The filename is fixed because the test is single-process.
    return std::filesystem::temp_directory_path() / "deep_signal_save_roundtrip.sqlite";
}

std::filesystem::path malformedSavePath(const std::string_view suffix) {
    // Each malformed-save test gets its own file so one failed corruption step
    // cannot influence a later validation case.
    return std::filesystem::temp_directory_path() / ("deep_signal_malformed_" + std::string{suffix} + ".sqlite");
}

void createPopulatedSave(const std::filesystem::path& path) {
    // Builds a save with ships, fleets, active movement, and events so malformed
    // tests can damage specific rows without relying on empty scenario state.
    std::filesystem::remove(path);

    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted while preparing malformed save");
    service.advanceDays(5);
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = service.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "move order accepted while preparing malformed save");
    service.advanceDays(1);

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "populated malformed-test fixture saves successfully");
}

void corruptSave(const std::filesystem::path& path, const std::string_view sql,
                 const bool ignoreCheckConstraints = false, const bool disableForeignKeys = false) {
    // Corruption helpers deliberately use direct SQL because the public repository
    // API should never be able to create malformed state. Prepared statements are
    // unnecessary here because test SQL is static and contains no user data.
    deep::save::Database db{path};
    if (ignoreCheckConstraints) {
        db.execute("PRAGMA ignore_check_constraints = ON;");
    }
    if (disableForeignKeys) {
        db.execute("PRAGMA foreign_keys = OFF;");
    }
    db.execute(sql);
}

void requireRepositoryLoadFails(const std::filesystem::path& path, const std::string_view message) {
    try {
        (void)deep::save::SaveGameRepository::load(path);
    } catch (const std::exception&) {
        return;
    }
    throw TestFailure{message};
}

void requireServiceLoadFailsWithoutStateReplacement(const std::filesystem::path& path) {
    // SimulationService must load into a temporary GameState first. This protects
    // a running game from being partially replaced by a malformed save file.
    deep::SimulationService service;
    const std::int64_t originalDay = service.state().date.day;
    const std::size_t originalBodyCount = service.state().bodies.size();

    const auto result = service.loadGame(path);
    require(!result.ok, "service reports malformed save load failure");
    require(service.state().date.day == originalDay, "failed load preserves current day");
    require(service.state().bodies.size() == originalBodyCount, "failed load preserves current state contents");
}

void expectMalformedSaveRejected(const std::string_view suffix, const std::string_view corruptionSql,
                                 const bool ignoreCheckConstraints = false, const bool disableForeignKeys = false) {
    const std::filesystem::path path = malformedSavePath(suffix);
    createPopulatedSave(path);
    corruptSave(path, corruptionSql, ignoreCheckConstraints, disableForeignKeys);

    requireRepositoryLoadFails(path, "malformed save is rejected by repository load");
    requireServiceLoadFailsWithoutStateReplacement(path);

    std::filesystem::remove(path);
}

void test_sqlite_save_load_round_trip() {
    // Saves a non-trivial mid-operation state and reloads it. This catches schema
    // omissions such as active fleet orders, ID counters, event payloads, and
    // raw and processed stockpile/cost rows.
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);

    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before save");

    service.advanceDays(5);
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = service.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "movement order accepted before save");

    service.advanceDays(2);
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = deep::ColonyId{999},
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok == false, "invalid command creates warning event before save");

    const deep::GameState expected = service.state();
    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves SQLite file");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(expected, loaded);

    deep::SimulationService loadedService;
    const auto loadResult = loadedService.loadGame(path);
    require(loadResult.ok, "service loads SQLite file");
    requireSameState(expected, loadedService.state());

    // Continue the loaded simulation to prove that rehydrated active movement
    // state is not merely present but still valid for rule execution.
    loadedService.advanceDays(3);
    require(loadedService.state().fleets.front().currentBodyId == marsId, "loaded fleet arrives after remaining movement days");
    require(loadedService.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "loaded fleet clears movement order after arrival");

    std::filesystem::remove(path);
}

void test_daily_economy_snapshots_are_runtime_only() {
    // Confirms the schema v2 contract for high-volume economy telemetry. The
    // stockpile/deposit state is durable, but per-day mining samples are a
    // current-session UI/forecast/debug aid and intentionally reload empty.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "deep_signal_transient_telemetry.sqlite";
    std::filesystem::remove(path);

    deep::SimulationService service;
    service.advanceDays(1);
    require(!service.state().dailyEconomySnapshots.empty(), "advancing simulation creates economy telemetry");

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves state with runtime telemetry present");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    require(loaded.dailyEconomySnapshots.empty(), "repository load does not restore runtime telemetry");

    deep::SimulationService loadedService;
    const auto loadResult = loadedService.loadGame(path);
    require(loadResult.ok, "service loads state with transient telemetry omitted");
    require(loadedService.state().dailyEconomySnapshots.empty(), "loaded service starts with no runtime telemetry");

    loadedService.advanceDays(1);
    require(!loadedService.state().dailyEconomySnapshots.empty(), "loaded simulation creates new telemetry normally");

    std::filesystem::remove(path);
}

void test_malformed_save_missing_schema_version_is_rejected() {
    // Deletes required schema metadata. This protects the loader from treating an
    // arbitrary SQLite file as a compatible save.
    expectMalformedSaveRejected("missing_schema", "DELETE FROM schema_version;");
}

void test_malformed_save_unsupported_schema_version_is_rejected() {
    // Simulates a future or corrupted schema version row. CHECK constraints are
    // disabled only for the injection step so load-time validation is exercised.
    expectMalformedSaveRejected("bad_schema", "UPDATE schema_version SET version = 999;", true);
}

void test_malformed_save_multiple_schema_versions_are_rejected() {
    // Schema version metadata is a singleton identity record. If multiple rows
    // exist, the loader must reject the save rather than accepting whichever row
    // SQLite returns first.
    expectMalformedSaveRejected("duplicate_schema_rows",
                                "INSERT INTO schema_version(id, version) VALUES (2, 999);",
                                true);
}

void test_malformed_save_missing_id_counter_is_rejected() {
    // ID counters are part of the deterministic allocation contract. Missing one
    // would risk duplicate IDs after load.
    expectMalformedSaveRejected("missing_counter", "DELETE FROM id_counters WHERE key = 'next_ship_id';");
}

void test_malformed_save_stale_id_counter_is_rejected() {
    // A positive but stale counter is dangerous because the next simulation
    // allocation would duplicate an existing row ID in memory.
    expectMalformedSaveRejected("stale_ship_counter",
                                "UPDATE id_counters SET value = '1' WHERE key = 'next_ship_id';");
}

void test_malformed_save_non_numeric_current_day_is_rejected() {
    // Metadata must be parsed as canonical integer text. SQLite CAST would turn
    // this into day zero, silently corrupting the campaign timeline.
    expectMalformedSaveRejected("bad_current_day",
                                "UPDATE game_meta SET value = 'abc' WHERE key = 'current_day';");
}

void test_malformed_save_current_day_before_event_history_is_rejected() {
    // The event log is persisted campaign history. Loading a save whose current
    // date predates its own event history would corrupt the audit timeline.
    expectMalformedSaveRejected("current_day_before_events",
                                "UPDATE game_meta SET value = '0' WHERE key = 'current_day';");
}

void test_malformed_save_future_event_day_is_rejected() {
    // Future-dated events imply history that has not happened yet in the loaded
    // simulation. The validator must reject them even though the row is otherwise
    // well-formed.
    expectMalformedSaveRejected("future_event_day",
                                "UPDATE event_log SET day = 999 WHERE id = (SELECT MAX(id) FROM event_log);");
}

void test_malformed_save_decreasing_event_days_are_rejected() {
    // Event IDs define append order. Event days may repeat within a day, but they
    // must not move backwards as IDs increase.
    expectMalformedSaveRejected("event_day_decreases",
                                "UPDATE event_log SET day = 0 WHERE id = (SELECT MAX(id) FROM event_log);");
}

void test_malformed_save_invalid_enum_is_rejected() {
    // Invalid enum ordinals must not be raw-cast into domain state because later
    // switch/visitor code assumes only known alternatives.
    expectMalformedSaveRejected("invalid_enum", "UPDATE bodies SET body_type = 99 WHERE id = 1;", true);
}

void test_malformed_save_negative_stockpile_is_rejected() {
    // Stockpiles represent physical quantities. Negative amounts should fail
    // through both schema checks and MineralSet validation on load.
    expectMalformedSaveRejected("negative_stockpile",
                                "UPDATE colony_minerals SET amount = -1.0 WHERE colony_id = 1 AND mineral = 0;",
                                true);
}

void test_malformed_save_broken_ship_fleet_reference_is_rejected() {
    // Foreign-key corruption can happen if users edit saves with constraints off.
    // The loader runs PRAGMA foreign_key_check to reject this before fix-up.
    expectMalformedSaveRejected("broken_ship_fk", "UPDATE ships SET fleet_id = 999 WHERE id = 1;", false, true);
}

void test_malformed_save_broken_fleet_target_reference_is_rejected() {
    // Active movement targets must reference existing bodies; otherwise movement
    // completion would write an impossible body ID into fleet state.
    expectMalformedSaveRejected("broken_fleet_target",
                                "UPDATE fleets SET destination_body_id = 999, order_target_body_id = 999 WHERE id = 1;",
                                false, true);
}

void test_malformed_save_impossible_idle_fleet_order_is_rejected() {
    // Idle fleets must not retain stale destination/target fields. Without this
    // check a UI could display movement state the simulation will never resolve.
    expectMalformedSaveRejected("idle_fleet_with_destination",
                                "UPDATE fleets SET order_type = 0, destination_body_id = 2, "
                                "order_target_body_id = 2, order_days_remaining = 3 WHERE id = 1;",
                                true);
}

void test_malformed_save_completed_order_with_build_progress_is_rejected() {
    // Completed production orders are terminal snapshots. Keeping build progress
    // on a completed order would make a future production tick ambiguous.
    expectMalformedSaveRejected("completed_order_with_progress",
                                "UPDATE shipyard_orders SET accumulated_build_points = 1.0 WHERE id = 1;",
                                true);
}

void test_malformed_save_active_order_already_complete_is_rejected() {
    // Active orders must still have work remaining. This prevents corrupted saves
    // from loading an order that should already be in the Completed state.
    expectMalformedSaveRejected("active_order_already_complete",
                                "UPDATE shipyard_orders SET status = 0 WHERE id = 1;",
                                true);
}

void test_malformed_save_unknown_order_status_is_rejected() {
    // Status ordinal 2 was used by an earlier prototype-only production state.
    // That state was removed, so v1 loading treats the old ordinal as malformed.
    expectMalformedSaveRejected("unknown_order_status",
                                "UPDATE shipyard_orders SET status = 2 WHERE id = 1;",
                                true);
}

void test_malformed_save_negative_colony_mines_is_rejected() {
    // SQLite CHECK constraints are not authoritative because external tools can
    // disable them. The loaded GameState graph must reject negative production.
    expectMalformedSaveRejected("negative_mines",
                                "UPDATE colonies SET mines = -5.0 WHERE id = 1;",
                                true);
}

void test_malformed_save_non_finite_numeric_value_is_rejected() {
    // Non-finite doubles are invalid domain quantities even when SQLite accepts
    // the textual/REAL representation. Forecasting and simulation math require
    // finite values.
    expectMalformedSaveRejected("infinite_build_points",
                                "UPDATE ship_classes SET build_points = 1e999 WHERE id = 1;");
}

void test_malformed_save_event_payload_missing_field_is_rejected() {
    // Event payload JSON is typed audit data. Missing fields should fail instead
    // of producing default IDs or zero amounts.
    expectMalformedSaveRejected("bad_event_payload", "UPDATE event_log SET payload_json = '{}' WHERE id = 1;");
}

} // namespace

int main() {
    try {
        test_sqlite_save_load_round_trip();
        test_daily_economy_snapshots_are_runtime_only();
        test_malformed_save_missing_schema_version_is_rejected();
        test_malformed_save_unsupported_schema_version_is_rejected();
        test_malformed_save_multiple_schema_versions_are_rejected();
        test_malformed_save_missing_id_counter_is_rejected();
        test_malformed_save_stale_id_counter_is_rejected();
        test_malformed_save_non_numeric_current_day_is_rejected();
        test_malformed_save_current_day_before_event_history_is_rejected();
        test_malformed_save_future_event_day_is_rejected();
        test_malformed_save_decreasing_event_days_are_rejected();
        test_malformed_save_invalid_enum_is_rejected();
        test_malformed_save_negative_stockpile_is_rejected();
        test_malformed_save_broken_ship_fleet_reference_is_rejected();
        test_malformed_save_broken_fleet_target_reference_is_rejected();
        test_malformed_save_impossible_idle_fleet_order_is_rejected();
        test_malformed_save_completed_order_with_build_progress_is_rejected();
        test_malformed_save_active_order_already_complete_is_rejected();
        test_malformed_save_unknown_order_status_is_rejected();
        test_malformed_save_negative_colony_mines_is_rejected();
        test_malformed_save_non_finite_numeric_value_is_rejected();
        test_malformed_save_event_payload_missing_field_is_rejected();
    } catch (const std::exception& ex) {
        std::cerr << "Save/load test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal save/load tests passed.\n";
    return EXIT_SUCCESS;
}
