#include "save/Database.h"
#include "save/SaveGameRepository.h"
#include "save/Schema.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// Destination and failure contracts for v11 full-snapshot persistence. Every
// database belongs to a unique temporary directory; no personal save is touched.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifndef DEEP_SIGNAL_TEST_SOURCE_DIR
#error DEEP_SIGNAL_TEST_SOURCE_DIR must identify the repository root
#endif
#ifndef DEEP_SIGNAL_TESTING
#error Save contract rollback tests require their isolated test-only repository build
#endif

namespace {

using namespace deep;
using namespace deep::save;

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path()
                / ("deep_signal_h1b_contract_" + std::to_string(stamp) + "_" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error{"Could not create isolated save-contract test directory"};
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct ScopedSaveFailureInjection {
    ScopedSaveFailureInjection() { setSaveFailureInjectionForTest(true); }
    ~ScopedSaveFailureInjection() { setSaveFailureInjectionForTest(false); }
    ScopedSaveFailureInjection(const ScopedSaveFailureInjection&) = delete;
    ScopedSaveFailureInjection& operator=(const ScopedSaveFailureInjection&) = delete;
};

[[nodiscard]] std::filesystem::path fixtureV10() {
    return std::filesystem::path{DEEP_SIGNAL_TEST_SOURCE_DIR} / "tests" / "fixtures" / "schema_v10_39fa628.sqlite";
}

[[nodiscard]] GameState makeOrderedState() {
    GameState state = createHomeSystemScenario();
    std::reverse(state.bodies.begin(), state.bodies.end());
    std::reverse(state.institutions.begin(), state.institutions.end());
    std::reverse(state.people.begin(), state.people.end());
    std::reverse(state.colonies.begin(), state.colonies.end());
    std::reverse(state.mineralDeposits.begin(), state.mineralDeposits.end());
    state.colonies.front().processingPolicy = ProcessingPolicy::Manual;
    state.colonies.front().manualProcessingAllocations = {
        ProcessingAllocation{ProcessedMaterial::StructuralAlloys, 1.0},
        ProcessingAllocation{ProcessedMaterial::Electronics, 2.0}
    };

    const ShipClass& shipClass = state.shipClasses.front();
    const FleetId firstFleet{state.ids.nextFleetId++};
    const FleetId secondFleet{state.ids.nextFleetId++};
    const ShipId firstShip{state.ids.nextShipId++};
    const ShipId secondShip{state.ids.nextShipId++};
    const ShipId thirdShip{state.ids.nextShipId++};
    const BodyId body = state.bodies.front().id;
    state.fleets.push_back(Fleet{
        .id = secondFleet, .name = "Second fleet", .currentBodyId = body,
        .destinationBodyId = std::nullopt, .shipIds = {thirdShip},
        .activeOrder = FleetOrder{}, .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    state.fleets.push_back(Fleet{
        .id = firstFleet, .name = "First fleet", .currentBodyId = body,
        .destinationBodyId = std::nullopt, .shipIds = {secondShip, firstShip},
        .activeOrder = FleetOrder{},
        .queuedOrders = {
            QueuedFleetOrder{FleetOrderType::MoveToBody, state.bodies.at(1).id},
            QueuedFleetOrder{FleetOrderType::MoveToBody, state.bodies.at(2).id}
        },
        .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(Ship{.id = firstShip, .shipClassId = shipClass.id, .name = "First ship",
                               .fleetId = firstFleet, .fuel = shipClass.fuelCapacity});
    state.ships.push_back(Ship{.id = thirdShip, .shipClassId = shipClass.id, .name = "Third ship",
                               .fleetId = secondFleet, .fuel = shipClass.fuelCapacity});
    state.ships.push_back(Ship{.id = secondShip, .shipClassId = shipClass.id, .name = "Second ship",
                               .fleetId = firstFleet, .fuel = shipClass.fuelCapacity});
    validateGameState(state);
    return state;
}

[[nodiscard]] std::string quoteIdentifier(const std::string_view name) {
    std::string quoted{"\""};
    for (const char ch : name) {
        quoted += ch;
        if (ch == '"') quoted += '"';
    }
    quoted += '"';
    return quoted;
}

void appendField(std::string& row, const std::string_view value) {
    row += std::to_string(value.size());
    row += ':';
    row += value;
}

// A logical SQLite snapshot, not a bytewise file comparison. It records user
// schema objects and every stored column value in a stable table/row order.
[[nodiscard]] std::vector<std::string> logicalSnapshot(const std::filesystem::path& path) {
    Database db{path, Database::OpenMode::ReadOnly};
    Transaction transaction{db, Transaction::Mode::Read};
    std::vector<std::string> snapshot;
    Statement objects{db, R"sql(
        SELECT type, name, tbl_name, COALESCE(sql, '')
        FROM sqlite_schema WHERE name NOT GLOB 'sqlite_*'
        ORDER BY type, name;
    )sql"};
    while (objects.step()) {
        const std::string type = objects.columnText(0);
        const std::string table = objects.columnText(1);
        std::string objectRecord{"schema:"};
        for (int column = 0; column < 4; ++column) appendField(objectRecord, objects.columnText(column));
        snapshot.push_back(std::move(objectRecord));
        if (type != "table") continue;

        Statement columns{db, "PRAGMA table_xinfo(" + quoteIdentifier(table) + ");"};
        std::vector<std::string> names;
        while (columns.step()) names.push_back(columns.columnText(1));
        require(!names.empty(), "snapshot table has no columns");
        std::string query{"SELECT "};
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i != 0) query += ", ";
            query += "quote(" + quoteIdentifier(names.at(i)) + ")";
        }
        query += " FROM " + quoteIdentifier(table) + " ORDER BY rowid;";
        Statement rows{db, query};
        while (rows.step()) {
            std::string record{"row:"};
            appendField(record, table);
            for (std::size_t i = 0; i < names.size(); ++i) appendField(record, rows.columnText(static_cast<int>(i)));
            snapshot.push_back(std::move(record));
        }
    }
    transaction.commit();
    return snapshot;
}

[[nodiscard]] std::int64_t versionAt(const std::filesystem::path& path) {
    Database db{path, Database::OpenMode::ReadOnly};
    Transaction transaction{db, Transaction::Mode::Read};
    const std::int64_t version = readSchemaVersion(db);
    transaction.commit();
    return version;
}

[[nodiscard]] std::string expectSaveReject(const std::filesystem::path& path, const GameState& state) {
    try { SaveGameRepository::save(path, state); }
    catch (const std::exception& error) { return error.what(); }
    throw std::runtime_error{"Save unexpectedly accepted an incompatible destination"};
}

[[nodiscard]] std::string expectLoadReject(const std::filesystem::path& path) {
    try { (void)SaveGameRepository::load(path); }
    catch (const std::exception& error) { return error.what(); }
    throw std::runtime_error{"Load unexpectedly accepted a malformed save"};
}

void executeSql(const std::filesystem::path& path, const std::string_view sql) {
    Database db{path};
    db.execute(sql);
}

void requireOrder(const GameState& expected, const GameState& actual) {
    require(expected.appointments.size() == actual.appointments.size(), "appointment count survives Load");
    for (std::size_t i = 0; i < expected.appointments.size(); ++i) {
        require(expected.appointments.at(i).role == actual.appointments.at(i).role
                && expected.appointments.at(i).scopeId == actual.appointments.at(i).scopeId,
                "appointment order survives Load");
    }
    require(expected.bodies.size() == actual.bodies.size(), "body count survives Load");
    for (std::size_t i = 0; i < expected.bodies.size(); ++i) {
        require(expected.bodies.at(i).id == actual.bodies.at(i).id, "body order survives Load");
    }
    require(expected.colonies.size() == actual.colonies.size(), "colony count survives Load");
    for (std::size_t i = 0; i < expected.colonies.size(); ++i) {
        const auto& left = expected.colonies.at(i);
        const auto& right = actual.colonies.at(i);
        require(left.id == right.id, "colony order survives Load");
        require(left.manualProcessingAllocations.size() == right.manualProcessingAllocations.size(),
                "manual allocation count survives Load");
        for (std::size_t row = 0; row < left.manualProcessingAllocations.size(); ++row) {
            require(left.manualProcessingAllocations.at(row).material == right.manualProcessingAllocations.at(row).material
                    && left.manualProcessingAllocations.at(row).weight == right.manualProcessingAllocations.at(row).weight,
                    "manual allocation order survives Load");
        }
    }
    require(expected.fleets.size() == actual.fleets.size(), "fleet count survives Load");
    for (std::size_t i = 0; i < expected.fleets.size(); ++i) {
        require(expected.fleets.at(i).id == actual.fleets.at(i).id, "fleet order survives Load");
        require(expected.fleets.at(i).shipIds == actual.fleets.at(i).shipIds, "fleet roster order survives Load");
        require(expected.fleets.at(i).queuedOrders.size() == actual.fleets.at(i).queuedOrders.size(),
                "queued order count survives Load");
        for (std::size_t row = 0; row < expected.fleets.at(i).queuedOrders.size(); ++row) {
            const auto& left = expected.fleets.at(i).queuedOrders.at(row);
            const auto& right = actual.fleets.at(i).queuedOrders.at(row);
            require(left.type == right.type && left.targetBodyId == right.targetBodyId,
                    "queued order sequence survives Load");
        }
    }
    require(expected.ships.size() == actual.ships.size(), "ship count survives Load");
    for (std::size_t i = 0; i < expected.ships.size(); ++i) {
        require(expected.ships.at(i).id == actual.ships.at(i).id, "global ship order survives Load");
    }
}

void test_new_and_schema_empty_destinations() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto fresh = temp.path / "fresh.sqlite";
    SaveGameRepository::save(fresh, state);
    require(versionAt(fresh) == kSchemaVersion, "new path writes v11");
    requireOrder(state, SaveGameRepository::load(fresh));

    const auto empty = temp.path / "empty.sqlite";
    { Database created{empty}; require(!hasUserSchema(created), "created database has no user schema"); }
    SaveGameRepository::save(empty, state);
    require(versionAt(empty) == kSchemaVersion, "schema-empty database writes v11");
    requireOrder(state, SaveGameRepository::load(empty));
}

void test_compatible_rewrite_and_invalid_source() {
    TempDirectory temp;
    const auto path = temp.path / "replace.sqlite";
    const GameState original = makeOrderedState();
    SaveGameRepository::save(path, original);
    const auto before = logicalSnapshot(path);

    GameState invalid = original;
    invalid.bodies.front().name.clear();
    require(!expectSaveReject(path, invalid).empty(), "invalid source is rejected");
    require(logicalSnapshot(path) == before, "invalid source leaves existing v11 logical contents intact");
    const auto absent = temp.path / "invalid-new.sqlite";
    (void)expectSaveReject(absent, invalid);
    require(!std::filesystem::exists(absent), "invalid source fails before creating a new destination");

    GameState replacement = original;
    replacement.date.day = 3;
    replacement.bodies.front().name = "Replacement body";
    validateGameState(replacement);
    SaveGameRepository::save(path, replacement);
    require(versionAt(path) == kSchemaVersion, "compatible rewrite stays v11");
    const GameState loaded = SaveGameRepository::load(path);
    require(loaded.date.day == 3 && loaded.bodies.front().name == "Replacement body",
            "compatible rewrite replaces durable values");
    requireOrder(replacement, loaded);
}

void test_equivalent_create_table_formatting_is_compatible() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto original = temp.path / "original.sqlite";
    const auto formatted = temp.path / "formatted.sqlite";
    SaveGameRepository::save(original, state);
    std::filesystem::copy_file(original, formatted);
    const auto beforeFormatting = logicalSnapshot(formatted);

    // SQLite's writable_schema is confined to this copied test save. One extra
    // space changes the stored DDL text without changing any table, key, index,
    // foreign-key, CHECK, or row semantics. A bytewise DDL comparison would fail.
    executeSql(formatted, R"sql(
        PRAGMA writable_schema = ON;
        UPDATE sqlite_schema
           SET sql = replace(sql, 'CREATE TABLE game_meta (', 'CREATE  TABLE game_meta (')
         WHERE type = 'table' AND name = 'game_meta';
        PRAGMA writable_schema = OFF;
    )sql");
    require(logicalSnapshot(formatted) != beforeFormatting,
            "test changed stored CREATE TABLE formatting in the copied save");
    {
        Database db{formatted, Database::OpenMode::ReadOnly};
        Transaction transaction{db, Transaction::Mode::Read};
        requireV11Structure(db);
        transaction.commit();
    }
    requireOrder(state, SaveGameRepository::load(formatted));

    GameState replacement = state;
    replacement.date.day = 2;
    replacement.bodies.front().name = "Formatted destination replacement";
    SaveGameRepository::save(formatted, replacement);
    const GameState loaded = SaveGameRepository::load(formatted);
    require(loaded.date.day == 2 && loaded.bodies.front().name == "Formatted destination replacement",
            "equivalent formatted schema accepts a successful v11 overwrite");
    requireOrder(replacement, loaded);
}

void test_legacy_and_incompatible_destinations() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto fixture = fixtureV10();
    require(std::filesystem::is_regular_file(fixture), "authentic v10 fixture is available");
    const auto legacy = temp.path / "legacy.sqlite";
    std::filesystem::copy_file(fixture, legacy);
    require(versionAt(legacy) == kLegacySchemaVersion, "fixture carries v10 metadata");
    const auto beforeLoad = logicalSnapshot(legacy);
    const GameState legacyLoaded = SaveGameRepository::load(legacy);
    require(legacyLoaded.shipyardOrders.size() == 2
            && legacyLoaded.shipyardOrders.at(0).id == ShipyardOrderId{1}
            && legacyLoaded.shipyardOrders.at(1).id == ShipyardOrderId{2},
            "v10 truthfully reconstructs the fixture's lost order as legacy ID order [1, 2]");
    require(logicalSnapshot(legacy) == beforeLoad, "v10 Load does not upgrade or rewrite the legacy file");
    const std::string guidance = expectSaveReject(legacy, state);
    require(guidance.find("v10") != std::string::npos && guidance.find("new path") != std::string::npos,
            "v10 overwrite explains the new-path requirement");
    require(versionAt(legacy) == kLegacySchemaVersion && logicalSnapshot(legacy) == beforeLoad,
            "v10 overwrite rejection leaves the legacy file unchanged");
    const auto migratedBySave = temp.path / "from-v10-new-path.sqlite";
    SaveGameRepository::save(migratedBySave, legacyLoaded);
    require(versionAt(migratedBySave) == kSchemaVersion, "loaded v10 state can be saved to a new v11 path");
    const GameState v11Loaded = SaveGameRepository::load(migratedBySave);
    require(v11Loaded.shipyardOrders.size() == 2
            && v11Loaded.shipyardOrders.at(0).id == ShipyardOrderId{1}
            && v11Loaded.shipyardOrders.at(1).id == ShipyardOrderId{2},
            "new v11 save preserves the loaded legacy order [1, 2]");
    Simulation legacyRun{legacyLoaded};
    Simulation v11Run{v11Loaded};
    const auto legacyEvents = legacyRun.advanceDays(1);
    const auto v11Events = v11Run.advanceDays(1);
    require(legacyEvents.size() == 1 && v11Events.size() == 1
            && legacyEvents.front().id == v11Events.front().id,
            "legacy and newly saved v11 continuations emit the same one-day event identity");
    const auto* legacyCompleted = std::get_if<ShipCompletedEvent>(&legacyEvents.front().payload);
    const auto* v11Completed = std::get_if<ShipCompletedEvent>(&v11Events.front().payload);
    require(legacyCompleted != nullptr && v11Completed != nullptr
            && legacyCompleted->orderId == ShipyardOrderId{1}
            && v11Completed->orderId == ShipyardOrderId{1},
            "both continuations complete expected first legacy order ID 1");
    const auto& legacyResult = legacyRun.state();
    const auto& v11Result = v11Run.state();
    require(legacyResult.date.day == 1 && v11Result.date.day == 1
            && legacyResult.shipyardOrders.size() == v11Result.shipyardOrders.size(),
            "both continuations reach the same one-day shipyard checkpoint");
    for (std::size_t i = 0; i < legacyResult.shipyardOrders.size(); ++i) {
        const auto& left = legacyResult.shipyardOrders.at(i);
        const auto& right = v11Result.shipyardOrders.at(i);
        require(left.id == right.id && left.quantityCompleted == right.quantityCompleted
                && left.status == right.status && left.accumulatedBuildPoints == right.accumulatedBuildPoints,
                "loaded v10 and its new v11 save have equivalent order progress after one day");
    }

    const auto unrelated = temp.path / "unrelated.sqlite";
    executeSql(unrelated, "CREATE TABLE notes(value TEXT); INSERT INTO notes VALUES ('keep');");
    const auto unrelatedBefore = logicalSnapshot(unrelated);
    (void)expectSaveReject(unrelated, state);
    (void)expectLoadReject(unrelated);
    require(logicalSnapshot(unrelated) == unrelatedBefore, "unrelated database is not rewritten");

    const auto base = temp.path / "v11-base.sqlite";
    SaveGameRepository::save(base, state);
    const struct Case { const char* name; const char* sql; } cases[] = {
        {"unsupported", "PRAGMA ignore_check_constraints=ON; UPDATE schema_version SET version=999;"},
        {"malformed", "PRAGMA ignore_check_constraints=ON; UPDATE schema_version SET version='11junk';"},
        {"ambiguous", "PRAGMA ignore_check_constraints=ON; INSERT INTO schema_version(id,version) VALUES (2,11);"},
        {"relabelled", "PRAGMA ignore_check_constraints=ON; UPDATE schema_version SET version=10;"},
        {"missing-index", "DROP INDEX idx_events_day;"},
        {"extra-table", "CREATE TABLE unrelated(value INTEGER);"}
    };
    for (const auto& test : cases) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        executeSql(path, test.sql);
        const auto malformedBefore = logicalSnapshot(path);
        (void)expectLoadReject(path);
        (void)expectSaveReject(path, state);
        require(logicalSnapshot(path) == malformedBefore, "malformed destination is unchanged after Save rejection");
    }

    const auto missing = temp.path / "missing.sqlite";
    (void)expectLoadReject(missing);
    require(!std::filesystem::exists(missing), "read-only Load does not create a missing file");
}

void test_v11_ordinal_failures() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto base = temp.path / "base.sqlite";
    SaveGameRepository::save(base, state);

    const struct Corruption { const char* name; const char* sql; const char* expectedError; } corruptions[] = {
        {"body-gap", "UPDATE bodies SET ordinal=100 WHERE ordinal=(SELECT MAX(ordinal) FROM bodies);", "bodies"},
        {"body-negative", "PRAGMA ignore_check_constraints=ON; UPDATE bodies SET ordinal=-1 WHERE ordinal=0;", "bodies"},
        {"body-fractional", "PRAGMA ignore_check_constraints=ON; UPDATE bodies SET ordinal=0.5 WHERE ordinal=0;", "non-integer ordinal"},
        {"roster-gap", "UPDATE ships SET fleet_ordinal=7 WHERE fleet_id=1 AND fleet_ordinal=1;", "ships.fleet_ordinal"},
        {"roster-negative", "PRAGMA ignore_check_constraints=ON; UPDATE ships SET fleet_ordinal=-1 WHERE fleet_id=1 AND fleet_ordinal=1;", "ships.fleet_ordinal"},
        {"roster-fractional", "PRAGMA ignore_check_constraints=ON; UPDATE ships SET fleet_ordinal=0.5 WHERE fleet_id=1 AND fleet_ordinal=1;", "non-integer ordinal"},
        {"appointment-gap", "UPDATE appointments SET ordinal=20 WHERE ordinal=(SELECT MAX(ordinal) FROM appointments);", "appointments"},
        {"allocation-gap", "UPDATE colony_processing_allocations SET ordinal=7 WHERE ordinal=1;", "colony_processing_allocations"},
        {"queue-gap", "UPDATE fleet_order_queue SET ordinal=7 WHERE fleet_id=1 AND ordinal=1;", "fleet_order_queue"}
    };
    for (const auto& test : corruptions) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        executeSql(path, test.sql);
        const auto malformedBefore = logicalSnapshot(path);
        const std::string failure = expectLoadReject(path);
        require(failure.find(test.expectedError) != std::string::npos,
                "the matching v11 ordinal reader rejected the malformed sequence");
        (void)expectSaveReject(path, state);
        require(logicalSnapshot(path) == malformedBefore,
                "malformed v11 ordinal destination is not replaced by Save");
    }

    const struct Constraint { const char* name; const char* sql; } constraints[] = {
        {"body-duplicate", "UPDATE bodies SET ordinal=0 WHERE ordinal=1;"},
        {"body-null", "UPDATE bodies SET ordinal=NULL WHERE ordinal=1;"},
        {"roster-duplicate", "UPDATE ships SET fleet_ordinal=0 WHERE fleet_id=1 AND fleet_ordinal=1;"},
        {"roster-null", "UPDATE ships SET fleet_ordinal=NULL WHERE fleet_id=1 AND fleet_ordinal=1;"}
    };
    for (const auto& test : constraints) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        const auto before = logicalSnapshot(path);
        bool rejected = false;
        try { executeSql(path, test.sql); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "v11 schema rejects duplicate or NULL ordinal at storage boundary");
        require(logicalSnapshot(path) == before, "failed ordinal corruption preserves valid save");
        requireOrder(state, SaveGameRepository::load(path));
    }
}

void test_contention_and_post_delete_rollback() {
    TempDirectory temp;
    const auto path = temp.path / "transaction.sqlite";
    const GameState state = makeOrderedState();
    SaveGameRepository::save(path, state);
    const auto beforeLock = logicalSnapshot(path);
    {
        Database blocker{path};
        Transaction lock{blocker, Transaction::Mode::Write};
        require(!expectSaveReject(path, state).empty(), "writer contention reports failure");
    }
    require(logicalSnapshot(path) == beforeLock, "contention leaves prior logical save intact");
    requireOrder(state, SaveGameRepository::load(path));

    // The test-only repository instance injects failure after the first v11
    // version-row INSERT, which follows clearExistingSave's row deletions. A
    // preflight-only rejection would not exercise rollback of actual mutation.
    const auto beforeFailure = logicalSnapshot(path);
    std::string failure;
    {
        ScopedSaveFailureInjection injection;
        failure = expectSaveReject(path, state);
    }
    require(failure.find("Injected Save failure after deletion and first insertion") != std::string::npos,
            "test-only failure injection reached version INSERT after row deletion");
    require(logicalSnapshot(path) == beforeFailure, "rollback restores prior schema and every stored row");
    requireOrder(state, SaveGameRepository::load(path));
}

void test_known_table_trigger_is_load_only() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto original = temp.path / "untouched.sqlite";
    const auto path = temp.path / "silent-trigger.sqlite";
    SaveGameRepository::save(original, state);
    std::filesystem::copy_file(original, path);

    // This trigger would silently rewrite a valid name after a colony INSERT.
    // Existing rows remain valid for read-only Load, but Save must reject the
    // destination before clearing any row or executing the trigger.
    executeSql(path, R"sql(
        CREATE TRIGGER silently_rewrite_colony AFTER INSERT ON colonies
        BEGIN
            UPDATE colonies SET name = 'Trigger-modified colony' WHERE id = NEW.id;
        END;
    )sql");
    const auto beforeFailure = logicalSnapshot(path);
    const GameState beforeLoad = SaveGameRepository::load(path);
    requireOrder(state, beforeLoad);
    const std::string failure = expectSaveReject(path, state);
    require(failure.find("trigger") != std::string::npos,
            "Save preflight rejects a user trigger on a known table");
    require(logicalSnapshot(path) == beforeFailure,
            "trigger rejection preserves the prior logical schema and every row");
    const GameState loaded = SaveGameRepository::load(path);
    requireOrder(state, loaded);
    require(loaded.colonies.front().name == state.colonies.front().name,
            "read-only Load still sees the original colony name despite the trigger");
}

} // namespace

int main() {
    const std::pair<std::string_view, void (*)()> tests[] = {
        {"new and schema-empty v11 destinations", test_new_and_schema_empty_destinations},
        {"compatible rewrite and invalid source", test_compatible_rewrite_and_invalid_source},
        {"equivalent CREATE TABLE formatting", test_equivalent_create_table_formatting_is_compatible},
        {"v10 and incompatible destination policy", test_legacy_and_incompatible_destinations},
        {"v11 ordinal corruption and constraints", test_v11_ordinal_failures},
        {"writer contention and post-delete rollback", test_contention_and_post_delete_rollback},
        {"known-table trigger is load only", test_known_table_trigger_is_load_only}
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try { run(); std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
