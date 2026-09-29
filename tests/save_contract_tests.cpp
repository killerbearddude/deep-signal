#include "sim/EquipmentServiceRules.h"
#include "save/Database.h"
#include "save/SaveGameRepository.h"
#include "save/Schema.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

// Destination and failure contracts for v16 full-snapshot persistence. Every
// database belongs to a unique temporary directory; no personal save is touched.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
                               .fleetId = firstFleet, .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity});
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.ships.push_back(Ship{.id = thirdShip, .shipClassId = shipClass.id, .name = "Third ship",
                               .fleetId = secondFleet, .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity});
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.ships.push_back(Ship{.id = secondShip, .shipClassId = shipClass.id, .name = "Second ship",
                               .fleetId = firstFleet, .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity});
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    const SurveyTeamId firstTeam{state.ids.nextSurveyTeamId++};
    const SurveyTeamId secondTeam{state.ids.nextSurveyTeamId++};
    state.surveyTeams.push_back(SurveyTeam{
        .id = secondTeam, .name = "Second team", .locationKind = SurveyTeamLocationKind::Colony,
        .colonyId = state.colonies.front().id, .fleetId = std::nullopt
    });
    state.surveyTeams.push_back(SurveyTeam{
        .id = firstTeam, .name = "First team", .locationKind = SurveyTeamLocationKind::Colony,
        .colonyId = state.colonies.front().id, .fleetId = std::nullopt
    });
    SurveyProgram firstProgram;
    firstProgram.id = SurveyProgramId{state.ids.nextSurveyProgramId++};
    firstProgram.charter.name = "First program";
    firstProgram.charter.homeColonyId = state.colonies.front().id;
    firstProgram.charter.targets = {
        SurveyProgramTarget{.bodyId = state.bodies.at(3).id, .priority = 0, .requestedPasses = 1},
        SurveyProgramTarget{.bodyId = state.bodies.at(4).id, .priority = 1, .requestedPasses = 2}
    };
    SurveyProgram secondProgram = firstProgram;
    secondProgram.id = SurveyProgramId{state.ids.nextSurveyProgramId++};
    secondProgram.charter.name = "Second program";
    secondProgram.charter.targets.front().bodyId = state.bodies.at(5).id;
    state.surveyPrograms.push_back(std::move(secondProgram));
    state.surveyPrograms.push_back(std::move(firstProgram));
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
    require(expected.surveyTeams.size() == actual.surveyTeams.size(), "survey team count survives Load");
    for (std::size_t i = 0; i < expected.surveyTeams.size(); ++i) {
        require(expected.surveyTeams.at(i).id == actual.surveyTeams.at(i).id,
                "survey team global order survives Load");
    }
    require(expected.surveyPrograms.size() == actual.surveyPrograms.size(), "survey program count survives Load");
    for (std::size_t i = 0; i < expected.surveyPrograms.size(); ++i) {
        const auto& left = expected.surveyPrograms.at(i);
        const auto& right = actual.surveyPrograms.at(i);
        require(left.id == right.id, "survey program global order survives Load");
        require(left.charter.targets.size() == right.charter.targets.size(), "survey target count survives Load");
        for (std::size_t j = 0; j < left.charter.targets.size(); ++j) {
            require(left.charter.targets.at(j).bodyId == right.charter.targets.at(j).bodyId,
                    "survey target charter order survives Load");
        }
    }
}

void test_new_and_schema_empty_destinations() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto fresh = temp.path / "fresh.sqlite";
    SaveGameRepository::save(fresh, state);
    require(versionAt(fresh) == kSchemaVersion, "new path writes v16");
    requireOrder(state, SaveGameRepository::load(fresh));

    const auto empty = temp.path / "empty.sqlite";
    { Database created{empty}; require(!hasUserSchema(created), "created database has no user schema"); }
    SaveGameRepository::save(empty, state);
    require(versionAt(empty) == kSchemaVersion, "schema-empty database writes v16");
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
    require(logicalSnapshot(path) == before, "invalid source leaves existing v16 logical contents intact");
    const auto absent = temp.path / "invalid-new.sqlite";
    (void)expectSaveReject(absent, invalid);
    require(!std::filesystem::exists(absent), "invalid source fails before creating a new destination");

    GameState replacement = original;
    replacement.date.day = 3;
    replacement.bodies.front().name = "Replacement body";
    validateGameState(replacement);
    SaveGameRepository::save(path, replacement);
    require(versionAt(path) == kSchemaVersion, "compatible rewrite stays v16");
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
        requireV16Structure(db);
        transaction.commit();
    }
    requireOrder(state, SaveGameRepository::load(formatted));

    GameState replacement = state;
    replacement.date.day = 2;
    replacement.bodies.front().name = "Formatted destination replacement";
    SaveGameRepository::save(formatted, replacement);
    const GameState loaded = SaveGameRepository::load(formatted);
    require(loaded.date.day == 2 && loaded.bodies.front().name == "Formatted destination replacement",
            "equivalent formatted schema accepts a successful v16 overwrite");
    requireOrder(replacement, loaded);
}

void test_old_schema_rejected_without_mutation() {
    TempDirectory temp;
    const auto v10 = temp.path / "old-v10.sqlite";
    require(std::filesystem::is_regular_file(fixtureV10()), "historical v10 fixture exists");
    std::filesystem::copy_file(fixtureV10(), v10);

    const auto reconstructSqlFixture = [&temp](const std::string_view fileName,
                                               const std::string_view outputName) {
        const auto fixture = std::filesystem::path{DEEP_SIGNAL_TEST_SOURCE_DIR} /
            "tests" / "fixtures" / std::string{fileName};
        std::ifstream source{fixture};
        require(source.good(), "historical SQL fixture exists");
        const std::string script{std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
        const auto output = temp.path / std::string{outputName};
        executeSql(output, script);
        return output;
    };
    const auto v11 = reconstructSqlFixture("schema_v11_p1_reference.sql", "old-v11.sqlite");
    const auto v12 = reconstructSqlFixture("schema_v12_p2_reference.sql", "old-v12.sqlite");
    // Version rejection happens before gameplay reconstruction. A minimal v13
    // metadata probe exercises that boundary without adding another old reader
    // or a large historical fixture maintained by the current implementation.
    const auto v13 = temp.path / "old-v13.sqlite";
    executeSql(v13, "CREATE TABLE schema_version(id INTEGER PRIMARY KEY, version INTEGER);"
                   "INSERT INTO schema_version VALUES(1,13);"
                   "CREATE TABLE preserve_me(payload BLOB);"
                   "INSERT INTO preserve_me VALUES(X'000102FF');");
    const auto v14 = temp.path / "old-v14.sqlite";
    executeSql(v14, "CREATE TABLE schema_version(id INTEGER PRIMARY KEY, version INTEGER);"
                   "INSERT INTO schema_version VALUES(1,14);"
                   "CREATE TABLE preserve_me(payload BLOB);"
                   "INSERT INTO preserve_me VALUES(X'000102FF');");

    const auto bytes = [](const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    };
    const GameState replacement = makeOrderedState();
    for (const auto& [path, expectedVersion] : {
             std::pair{v10, std::int64_t{10}}, std::pair{v11, std::int64_t{11}},
             std::pair{v12, std::int64_t{12}}, std::pair{v13, std::int64_t{13}}, std::pair{v14, std::int64_t{14}}
         }) {
        require(versionAt(path) == expectedVersion, "historical fixture has expected old schema marker");
        const std::string beforeBytes = bytes(path);
        const auto beforeLogical = logicalSnapshot(path);
        const std::string loadError = expectLoadReject(path);
        require(loadError.find("Unsupported save schema version") != std::string::npos,
                "old development save fails cleanly on Load");
        require(bytes(path) == beforeBytes && logicalSnapshot(path) == beforeLogical,
                "rejected old-schema Load does not modify source file");
        const std::string saveError = expectSaveReject(path, replacement);
        require(saveError.find("Unsupported save schema version") != std::string::npos &&
                logicalSnapshot(path) == beforeLogical,
                "Save does not overwrite an unsupported development schema");
    }
}

void test_incompatible_destinations() {
    TempDirectory temp;
    const GameState state = makeOrderedState();
    const auto unrelated = temp.path / "unrelated.sqlite";
    executeSql(unrelated, "CREATE TABLE notes(value TEXT); INSERT INTO notes VALUES ('keep');");
    const auto unrelatedBefore = logicalSnapshot(unrelated);
    (void)expectSaveReject(unrelated, state);
    (void)expectLoadReject(unrelated);
    require(logicalSnapshot(unrelated) == unrelatedBefore, "unrelated database is not rewritten");

    const auto base = temp.path / "v16-base.sqlite";
    SaveGameRepository::save(base, state);
    const struct Case { const char* name; const char* sql; } cases[] = {
        {"unsupported", "PRAGMA ignore_check_constraints=ON; UPDATE schema_version SET version=999;"},
        {"malformed", "PRAGMA ignore_check_constraints=ON; UPDATE schema_version SET version='11junk';"},
        {"ambiguous", "PRAGMA ignore_check_constraints=ON; INSERT INTO schema_version(id,version) VALUES (2,14);"},
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

void test_v16_revision_and_catalog_order_survives_save() {
    TempDirectory temp;
    Simulation sim{createHomeSystemScenario()};
    auto draft = sim.state().shipClasses.front().components;
    draft.at(2).quantity = 2;
    require(sim.execute(CreateShipClassRevisionCommand{
        .name = "Reordered revision", .role = ShipRole::Escort,
        .basedOnClassId = sim.state().shipClasses.front().id,
        .components = draft
    }).ok, "revision exists before ordering test");
    GameState source = sim.state();
    std::reverse(source.shipClasses.begin(), source.shipClasses.end());
    std::reverse(source.shipComponents.begin(), source.shipComponents.end());
    std::reverse(source.shipClasses.front().components.begin(), source.shipClasses.front().components.end());
    validateGameState(source);
    const auto path = temp.path / "reordered-v16.sqlite";
    SaveGameRepository::save(path, source);
    const GameState loaded = SaveGameRepository::load(path);
    require(loaded.shipClasses.front().id == source.shipClasses.front().id &&
            loaded.shipClasses.front().basedOnClassId == source.shipClasses.front().basedOnClassId &&
            loaded.shipClasses.front().components == source.shipClasses.front().components &&
            loaded.shipComponents.front().id == source.shipComponents.front().id,
            "v16 preserves class/catalog/installation order even when lineage ID order differs");
}

void test_v16_ordinal_failures() {
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
        {"queue-gap", "UPDATE fleet_order_queue SET ordinal=7 WHERE fleet_id=1 AND ordinal=1;", "fleet_order_queue"},
        {"survey-team-gap", "UPDATE survey_teams SET ordinal=7 WHERE ordinal=1;", "survey_teams"},
        {"survey-program-gap", "UPDATE survey_programs SET ordinal=7 WHERE ordinal=1;", "survey_programs"},
        {"survey-target-gap", "UPDATE survey_program_targets SET ordinal=7 WHERE program_id=1 AND ordinal=1;", "survey_program_targets"},
        {"survey-target-fractional", "PRAGMA ignore_check_constraints=ON; UPDATE survey_program_targets SET ordinal=0.5 WHERE program_id=1 AND ordinal=0;", "non-integer ordinal"}
    };
    for (const auto& test : corruptions) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        executeSql(path, test.sql);
        const auto malformedBefore = logicalSnapshot(path);
        const std::string failure = expectLoadReject(path);
        require(failure.find(test.expectedError) != std::string::npos,
                "the matching v16 ordinal reader rejected the malformed sequence");
        (void)expectSaveReject(path, state);
        require(logicalSnapshot(path) == malformedBefore,
                "malformed v16 ordinal destination is not replaced by Save");
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
        require(rejected, "v16 schema rejects duplicate or NULL ordinal at storage boundary");
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

    // The test-only repository instance injects failure after the first v16
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
        {"new and schema-empty v16 destinations", test_new_and_schema_empty_destinations},
        {"compatible rewrite and invalid source", test_compatible_rewrite_and_invalid_source},
        {"equivalent CREATE TABLE formatting", test_equivalent_create_table_formatting_is_compatible},
        {"old schema rejected without mutation", test_old_schema_rejected_without_mutation},
        {"incompatible destinations", test_incompatible_destinations},
        {"v16 revision and catalog ordering", test_v16_revision_and_catalog_order_survives_save},
        {"v16 ordinal corruption and constraints", test_v16_ordinal_failures},
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
