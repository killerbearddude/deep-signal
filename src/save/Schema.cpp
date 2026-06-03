#include "save/Schema.h"

// Implements schema v2 for Prototype 0.1 saves.
// The schema mirrors GameState-owned records and keeps event payloads as typed
// JSON text for inspectable, forward-migratable audit history. CHECK constraints
// intentionally duplicate core invariants so hand-edited save files fail early.

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace deep::save {

namespace {

// Parses schema metadata without SQLite CAST so corrupted text such as "1junk"
// cannot masquerade as a supported schema version.
[[nodiscard]] std::int64_t strictSchemaInt64(const std::string_view text) {
    std::int64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (text.empty() || ec != std::errc{} || ptr != end || std::to_string(value) != text) {
        throw std::runtime_error{"Save file contains invalid schema_version metadata"};
    }
    return value;
}

} // namespace

void initializeSchema(Database& db) {
    db.execute(R"sql(
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS schema_version (
            id INTEGER PRIMARY KEY CHECK(id = 1),
            version INTEGER NOT NULL CHECK(version = 2)
        );

        CREATE TABLE IF NOT EXISTS game_meta (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS id_counters (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value INTEGER NOT NULL CHECK(value > 0)
        );

        CREATE TABLE IF NOT EXISTS star_systems (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0)
        );

        CREATE TABLE IF NOT EXISTS bodies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            system_id INTEGER NOT NULL CHECK(system_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            body_type INTEGER NOT NULL CHECK(body_type BETWEEN 0 AND 4),
            x REAL NOT NULL,
            y REAL NOT NULL,
            FOREIGN KEY(system_id) REFERENCES star_systems(id)
        );

        CREATE TABLE IF NOT EXISTS colonies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            mines REAL NOT NULL CHECK(mines >= 0.0),
            processor_capacity REAL NOT NULL CHECK(processor_capacity >= 0.0),
            shipyard_capacity REAL NOT NULL CHECK(shipyard_capacity >= 0.0),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );

        CREATE TABLE IF NOT EXISTS colony_minerals (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, mineral),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE IF NOT EXISTS colony_materials (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, material),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE IF NOT EXISTS mineral_deposits (
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            remaining REAL NOT NULL CHECK(remaining >= 0.0),
            accessibility REAL NOT NULL CHECK(accessibility >= 0.0),
            PRIMARY KEY(body_id, mineral),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );

        CREATE TABLE IF NOT EXISTS ship_classes (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 2),
            build_points REAL NOT NULL CHECK(build_points > 0.0),
            speed_km_per_day REAL NOT NULL CHECK(speed_km_per_day >= 0.0),
            fuel_capacity REAL NOT NULL CHECK(fuel_capacity >= 0.0)
        );

        CREATE TABLE IF NOT EXISTS ship_class_material_costs (
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(ship_class_id, material),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id)
        );

        CREATE TABLE IF NOT EXISTS shipyard_orders (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            quantity_requested INTEGER NOT NULL CHECK(quantity_requested > 0),
            quantity_completed INTEGER NOT NULL CHECK(quantity_completed >= 0),
            accumulated_build_points REAL NOT NULL CHECK(accumulated_build_points >= 0.0),
            status INTEGER NOT NULL CHECK(status BETWEEN 0 AND 1),
            CHECK(quantity_completed <= quantity_requested),
            CHECK(
                (status = 0 AND quantity_completed < quantity_requested)
                OR
                (status = 1 AND quantity_completed = quantity_requested AND accumulated_build_points = 0.0)
            ),
            FOREIGN KEY(colony_id) REFERENCES colonies(id),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id)
        );

        CREATE TABLE IF NOT EXISTS fleets (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            current_body_id INTEGER NOT NULL CHECK(current_body_id > 0),
            destination_body_id INTEGER NULL CHECK(destination_body_id IS NULL OR destination_body_id > 0),
            order_type INTEGER NOT NULL CHECK(order_type BETWEEN 0 AND 1),
            order_target_body_id INTEGER NULL CHECK(order_target_body_id IS NULL OR order_target_body_id > 0),
            order_days_remaining INTEGER NOT NULL CHECK(order_days_remaining >= 0),
            CHECK(
                (order_type = 0 AND destination_body_id IS NULL AND
                 order_target_body_id IS NULL AND order_days_remaining = 0)
                OR
                (order_type = 1 AND destination_body_id IS NOT NULL AND
                 order_target_body_id IS NOT NULL AND
                 destination_body_id = order_target_body_id AND
                 destination_body_id != current_body_id AND
                 order_days_remaining > 0)
            ),
            FOREIGN KEY(current_body_id) REFERENCES bodies(id),
            FOREIGN KEY(destination_body_id) REFERENCES bodies(id),
            FOREIGN KEY(order_target_body_id) REFERENCES bodies(id)
        );

        CREATE TABLE IF NOT EXISTS ships (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            fuel REAL NOT NULL CHECK(fuel >= 0.0),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id)
        );

        CREATE TABLE IF NOT EXISTS event_log (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            day INTEGER NOT NULL CHECK(day >= 0),
            severity INTEGER NOT NULL CHECK(severity BETWEEN 0 AND 2),
            event_type TEXT NOT NULL CHECK(length(event_type) > 0),
            payload_json TEXT NOT NULL CHECK(length(payload_json) > 0)
        );

        CREATE INDEX IF NOT EXISTS idx_bodies_system_id ON bodies(system_id);
        CREATE INDEX IF NOT EXISTS idx_colonies_body_id ON colonies(body_id);
        CREATE INDEX IF NOT EXISTS idx_ships_fleet_id ON ships(fleet_id);
        CREATE INDEX IF NOT EXISTS idx_events_day ON event_log(day);
    )sql");

    // Newly-created databases have an empty schema_version table. Existing save
    // files keep their current row until save() replaces contents atomically.
    Statement count{db, "SELECT COUNT(*) FROM schema_version;"};
    if (!count.step()) {
        throw std::runtime_error{"Failed to read schema_version count"};
    }
    const std::int64_t rowCount = count.columnInt64(0);
    if (rowCount == 0) {
        Statement insert{db, "INSERT INTO schema_version(id, version) VALUES (1, ?);"};
        insert.bindInt64(1, kSchemaVersion);
        insert.execute();
    }
}

void requireSupportedSchema(Database& db) {
    // Schema identity must be unambiguous. A malformed save with two version rows
    // must not load merely because SQLite happens to return the supported row
    // first for an unconstrained LIMIT query.
    Statement count{db, "SELECT COUNT(*) FROM schema_version;"};
    if (!count.step() || count.columnInt64(0) != 1) {
        throw std::runtime_error{"Save file must contain exactly one schema_version row"};
    }

    Statement stmt{db, "SELECT version FROM schema_version;"};
    if (!stmt.step()) {
        throw std::runtime_error{"Save file is missing schema_version metadata"};
    }

    const std::int64_t version = strictSchemaInt64(stmt.columnText(0));
    if (version != kSchemaVersion) {
        throw std::runtime_error{"Unsupported save schema version"};
    }
}

} // namespace deep::save
