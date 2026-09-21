#include "save/Schema.h"

// Responsibility: define schema v10 tables and verify the version marker.
// Tables mirror durable GameState records; event payloads remain inspectable JSON
// text. Foreign keys and CHECK constraints provide a first line of validation,
// not complete type/graph validation. Repository reconstruction and the domain
// validator perform additional checks. Schema creation does not migrate saves.

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
            version INTEGER NOT NULL CHECK(version = 10)
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

        CREATE TABLE IF NOT EXISTS institutions (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            institution_type INTEGER NOT NULL CHECK(institution_type BETWEEN 0 AND 7)
        );

        CREATE TABLE IF NOT EXISTS people (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            institution_id INTEGER NOT NULL CHECK(institution_id > 0),
            logistics INTEGER NOT NULL CHECK(logistics >= 0),
            industry INTEGER NOT NULL CHECK(industry >= 0),
            survey INTEGER NOT NULL CHECK(survey >= 0),
            command INTEGER NOT NULL CHECK(command >= 0),
            administration INTEGER NOT NULL CHECK(administration >= 0),
            engineering INTEGER NOT NULL CHECK(engineering >= 0),
            intelligence INTEGER NOT NULL CHECK(intelligence >= 0),
            crisis_management INTEGER NOT NULL CHECK(crisis_management >= 0),
            seniority_level INTEGER NOT NULL CHECK(seniority_level >= 0),
            successful_assignments INTEGER NOT NULL CHECK(successful_assignments >= 0),
            failed_assignments INTEGER NOT NULL CHECK(failed_assignments >= 0),
            commendations INTEGER NOT NULL CHECK(commendations >= 0),
            controversies INTEGER NOT NULL CHECK(controversies >= 0),
            FOREIGN KEY(institution_id) REFERENCES institutions(id)
        );

        CREATE TABLE IF NOT EXISTS appointments (
            ordinal INTEGER PRIMARY KEY NOT NULL CHECK(ordinal >= 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 5),
            scope_type INTEGER NOT NULL CHECK(scope_type BETWEEN 0 AND 2),
            scope_id INTEGER NOT NULL CHECK(scope_id > 0),
            person_id INTEGER NOT NULL CHECK(person_id > 0),
            appointed_day INTEGER NOT NULL CHECK(appointed_day >= 0),
            UNIQUE(role, scope_type, scope_id),
            FOREIGN KEY(person_id) REFERENCES people(id)
        );

        CREATE TABLE IF NOT EXISTS bodies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            system_id INTEGER NOT NULL CHECK(system_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            body_type INTEGER NOT NULL CHECK(body_type BETWEEN 0 AND 4),
            strategic_zone INTEGER NOT NULL CHECK(strategic_zone BETWEEN 0 AND 4),
            parent_body_id INTEGER NULL CHECK(parent_body_id IS NULL OR parent_body_id > 0),
            orbital_radius_km REAL NOT NULL CHECK(orbital_radius_km >= 0.0),
            orbital_period_days REAL NOT NULL CHECK(orbital_period_days >= 0.0),
            phase_radians REAL NOT NULL,
            display_radius REAL NOT NULL CHECK(display_radius > 0.0),
            x REAL NOT NULL,
            y REAL NOT NULL,
            CHECK(parent_body_id IS NULL OR parent_body_id != id),
            FOREIGN KEY(system_id) REFERENCES star_systems(id)
        );

        CREATE TABLE IF NOT EXISTS colonies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            owner_institution_id INTEGER NULL CHECK(owner_institution_id IS NULL OR owner_institution_id > 0),
            mines REAL NOT NULL CHECK(mines >= 0.0),
            processor_capacity REAL NOT NULL CHECK(processor_capacity >= 0.0),
            shipyard_capacity REAL NOT NULL CHECK(shipyard_capacity >= 0.0),
            processing_policy INTEGER NOT NULL CHECK(processing_policy BETWEEN 0 AND 5),
            FOREIGN KEY(body_id) REFERENCES bodies(id),
            FOREIGN KEY(owner_institution_id) REFERENCES institutions(id)
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

        CREATE TABLE IF NOT EXISTS colony_processing_allocations (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            weight REAL NOT NULL CHECK(weight >= 0.0),
            PRIMARY KEY(colony_id, ordinal),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE IF NOT EXISTS mineral_deposits (
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            remaining REAL NOT NULL CHECK(remaining >= 0.0),
            accessibility REAL NOT NULL CHECK(accessibility >= 0.0),
            confidence REAL NOT NULL CHECK(confidence >= 0.0 AND confidence <= 1.0),
            PRIMARY KEY(body_id, mineral),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );

        -- Ship-class fuel_capacity defines the maximum propellant one hull contributes.
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
            owner_institution_id INTEGER NULL CHECK(owner_institution_id IS NULL OR owner_institution_id > 0),
            current_body_id INTEGER NOT NULL CHECK(current_body_id > 0),
            destination_body_id INTEGER NULL CHECK(destination_body_id IS NULL OR destination_body_id > 0),
            order_type INTEGER NOT NULL CHECK(order_type BETWEEN 0 AND 1),
            order_target_body_id INTEGER NULL CHECK(order_target_body_id IS NULL OR order_target_body_id > 0),
            order_days_remaining INTEGER NOT NULL CHECK(order_days_remaining >= 0),
            order_departure_body_id INTEGER NULL CHECK(order_departure_body_id IS NULL OR order_departure_body_id > 0),
            order_departure_day INTEGER NOT NULL CHECK(order_departure_day >= 0),
            order_arrival_day INTEGER NOT NULL CHECK(order_arrival_day >= 0),
            order_departure_x REAL NOT NULL,
            order_departure_y REAL NOT NULL,
            order_projected_arrival_x REAL NOT NULL,
            order_projected_arrival_y REAL NOT NULL,
            order_transit_distance_km REAL NOT NULL CHECK(order_transit_distance_km >= 0.0),
            order_burn_acceleration_g REAL NOT NULL CHECK(order_burn_acceleration_g >= 0.0),
            order_curve_control_x REAL NOT NULL,
            order_curve_control_y REAL NOT NULL,
            CHECK(
                (order_type = 0 AND destination_body_id IS NULL AND
                 order_target_body_id IS NULL AND order_days_remaining = 0 AND
                 order_departure_body_id IS NULL AND order_departure_day = 0 AND
                 order_arrival_day = 0 AND order_transit_distance_km = 0.0 AND
                 order_burn_acceleration_g = 0.0)
                OR
                (order_type = 1 AND destination_body_id IS NOT NULL AND
                 order_target_body_id IS NOT NULL AND
                 order_departure_body_id IS NOT NULL AND
                 destination_body_id = order_target_body_id AND
                 destination_body_id != current_body_id AND
                 order_days_remaining > 0 AND
                 order_arrival_day > order_departure_day AND
                 order_transit_distance_km > 0.0 AND
                 order_burn_acceleration_g > 0.0)
            ),
            FOREIGN KEY(owner_institution_id) REFERENCES institutions(id),
            FOREIGN KEY(current_body_id) REFERENCES bodies(id),
            FOREIGN KEY(destination_body_id) REFERENCES bodies(id),
            FOREIGN KEY(order_target_body_id) REFERENCES bodies(id)
        );

        -- Durable queued fleet intent. Active fleet orders are stored on fleets;
        -- this table preserves future player-authored moves in execution order.
        CREATE TABLE IF NOT EXISTS fleet_order_queue (
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            order_type INTEGER NOT NULL CHECK(order_type = 1),
            target_body_id INTEGER NOT NULL CHECK(target_body_id > 0),
            PRIMARY KEY(fleet_id, ordinal),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(target_body_id) REFERENCES bodies(id)
        );

        -- Ship fuel is the current propellant amount consumed by movement orders.
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

        CREATE INDEX IF NOT EXISTS idx_people_institution_id ON people(institution_id);
        CREATE INDEX IF NOT EXISTS idx_appointments_person_id ON appointments(person_id);
        CREATE INDEX IF NOT EXISTS idx_bodies_system_id ON bodies(system_id);
        CREATE INDEX IF NOT EXISTS idx_colonies_body_id ON colonies(body_id);
        CREATE INDEX IF NOT EXISTS idx_colonies_owner_institution_id ON colonies(owner_institution_id);
        CREATE INDEX IF NOT EXISTS idx_colony_processing_allocations_colony_id
            ON colony_processing_allocations(colony_id);
        CREATE INDEX IF NOT EXISTS idx_fleets_owner_institution_id ON fleets(owner_institution_id);
        CREATE INDEX IF NOT EXISTS idx_fleet_order_queue_fleet_id ON fleet_order_queue(fleet_id);
        CREATE INDEX IF NOT EXISTS idx_ships_fleet_id ON ships(fleet_id);
        CREATE INDEX IF NOT EXISTS idx_events_day ON event_log(day);
    )sql");

    // Seed empty metadata without replacing an existing version marker. This
    // deliberately does not establish that pre-existing tables match v10.
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
