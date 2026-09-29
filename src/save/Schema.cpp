#include "save/Schema.h"

// Responsibility: define the active v13 schema and inspect its structure.
// Tables mirror durable GameState records; event payloads remain inspectable JSON
// text. Foreign keys and CHECK constraints provide a first line of validation,
// not complete type/graph validation. Repository reconstruction and the domain
// validator perform additional checks. Schema creation does not migrate saves.

#include <algorithm>
#include <charconv>
#include <compare>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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

struct SchemaObject {
    std::string type;
    std::string name;
    std::string table;
    auto operator<=>(const SchemaObject&) const = default;
};

using PragmaCell = std::optional<std::string>;
using PragmaRows = std::vector<std::vector<PragmaCell>>;

[[nodiscard]] std::string quoteIdentifier(const std::string_view name) {
    std::string quoted{"\""};
    for (const char ch : name) {
        quoted += ch;
        if (ch == '"') {
            quoted += '"';
        }
    }
    quoted += '"';
    return quoted;
}

[[nodiscard]] PragmaRows readPragmaRows(Database& db, const std::string& sql, const int columnCount) {
    Statement stmt{db, sql};
    PragmaRows rows;
    while (stmt.step()) {
        std::vector<PragmaCell> row;
        row.reserve(static_cast<std::size_t>(columnCount));
        for (int column = 0; column < columnCount; ++column) {
            row.push_back(stmt.columnIsNull(column) ? std::nullopt : std::optional{stmt.columnText(column)});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

[[nodiscard]] std::vector<SchemaObject> readUserObjects(Database& db,
                                                         const std::vector<std::string>& knownTables = {},
                                                         const bool allowKnownTableTriggers = false) {
    Statement stmt{db, R"sql(
        SELECT type, name, tbl_name
        FROM sqlite_schema
        WHERE name NOT GLOB 'sqlite_*'
        ORDER BY type, name, tbl_name;
    )sql"};
    std::vector<SchemaObject> objects;
    while (stmt.step()) {
        SchemaObject object{stmt.columnText(0), stmt.columnText(1), stmt.columnText(2)};
        if (object.type == "trigger") {
            if (!allowKnownTableTriggers) {
                throw std::runtime_error{"Save destination contains a user trigger"};
            }
            if (std::find(knownTables.begin(), knownTables.end(), object.table) == knownTables.end()) {
                throw std::runtime_error{"Save file has a trigger on an unknown table"};
            }
            continue;
        }
        objects.push_back(std::move(object));
    }
    return objects;
}

struct IndexShape {
    std::string name;
    std::string unique;
    std::string origin;
    std::string partial;
    PragmaRows columns;
    auto operator<=>(const IndexShape&) const = default;
};

[[nodiscard]] std::vector<IndexShape> readIndexShapes(Database& db, const std::string_view tableName) {
    Statement stmt{db, "PRAGMA index_list(" + quoteIdentifier(tableName) + ");"};
    std::vector<IndexShape> indexes;
    while (stmt.step()) {
        const std::string actualName = stmt.columnText(1);
        const bool automaticName = actualName.starts_with("sqlite_autoindex_");
        indexes.push_back(IndexShape{
            .name = automaticName ? std::string{} : actualName,
            .unique = stmt.columnText(2),
            .origin = stmt.columnText(3),
            .partial = stmt.columnText(4),
            .columns = readPragmaRows(db, "PRAGMA index_xinfo(" + quoteIdentifier(actualName) + ");", 6)
        });
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

} // namespace

void createSchemaV13(Database& db) {
    db.execute(R"sql(
        CREATE TABLE schema_version (
            id INTEGER PRIMARY KEY CHECK(id = 1),
            version INTEGER NOT NULL CHECK(version = 13)
        );

        CREATE TABLE game_meta (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value TEXT NOT NULL
        );

        CREATE TABLE id_counters (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value INTEGER NOT NULL CHECK(value > 0)
        );

        CREATE TABLE star_systems (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0)
        );

        CREATE TABLE institutions (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            institution_type INTEGER NOT NULL CHECK(institution_type BETWEEN 0 AND 7)
        );

        CREATE TABLE people (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
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
            survey_planning_approach INTEGER NOT NULL CHECK(survey_planning_approach BETWEEN 0 AND 1),
            FOREIGN KEY(institution_id) REFERENCES institutions(id)
        );

        CREATE TABLE appointments (
            ordinal INTEGER PRIMARY KEY NOT NULL CHECK(ordinal >= 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 5),
            scope_type INTEGER NOT NULL CHECK(scope_type BETWEEN 0 AND 2),
            scope_id INTEGER NOT NULL CHECK(scope_id > 0),
            person_id INTEGER NOT NULL CHECK(person_id > 0),
            appointed_day INTEGER NOT NULL CHECK(appointed_day >= 0),
            UNIQUE(role, scope_type, scope_id),
            FOREIGN KEY(person_id) REFERENCES people(id)
        );

        CREATE TABLE bodies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
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

        CREATE TABLE colonies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
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

        CREATE TABLE colony_minerals (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, mineral),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE colony_materials (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, material),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE colony_processing_allocations (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            weight REAL NOT NULL CHECK(weight >= 0.0),
            PRIMARY KEY(colony_id, ordinal),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );

        CREATE TABLE mineral_deposits (
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            remaining REAL NOT NULL CHECK(remaining >= 0.0),
            accessibility REAL NOT NULL CHECK(accessibility >= 0.0),
            confidence REAL NOT NULL CHECK(confidence >= 0.0 AND confidence <= 1.0),
            PRIMARY KEY(body_id, mineral),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );

        CREATE TABLE ship_components (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 4),
            mass REAL NOT NULL CHECK(mass >= 0.0),
            volume REAL NOT NULL CHECK(volume >= 0.0),
            internal_volume_capacity REAL NOT NULL CHECK(internal_volume_capacity >= 0.0),
            power_generation REAL NOT NULL CHECK(power_generation >= 0.0),
            power_demand REAL NOT NULL CHECK(power_demand >= 0.0),
            propellant_capacity REAL NOT NULL CHECK(propellant_capacity >= 0.0),
            survey_capability REAL NOT NULL CHECK(survey_capability >= 0.0),
            build_points REAL NOT NULL CHECK(build_points >= 0.0)
        );

        CREATE TABLE ship_component_material_costs (
            component_id INTEGER NOT NULL CHECK(component_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(component_id, material),
            FOREIGN KEY(component_id) REFERENCES ship_components(id)
        );

        CREATE TABLE ship_classes (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 2),
            speed_km_per_day REAL NOT NULL CHECK(speed_km_per_day >= 0.0),
            revision INTEGER NOT NULL CHECK(revision > 0),
            based_on_class_id INTEGER NULL CHECK(based_on_class_id IS NULL OR based_on_class_id > 0),
            FOREIGN KEY(based_on_class_id) REFERENCES ship_classes(id) DEFERRABLE INITIALLY DEFERRED
        );

        CREATE TABLE ship_class_installs (
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            component_id INTEGER NOT NULL CHECK(component_id > 0),
            quantity INTEGER NOT NULL CHECK(quantity > 0),
            PRIMARY KEY(ship_class_id, ordinal),
            UNIQUE(ship_class_id, component_id),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id),
            FOREIGN KEY(component_id) REFERENCES ship_components(id)
        );

        CREATE TABLE shipyard_orders (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
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

        CREATE TABLE fleets (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
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
        CREATE TABLE fleet_order_queue (
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            order_type INTEGER NOT NULL CHECK(order_type = 1),
            target_body_id INTEGER NOT NULL CHECK(target_body_id > 0),
            PRIMARY KEY(fleet_id, ordinal),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(target_body_id) REFERENCES bodies(id)
        );

        -- Ship fuel is the current propellant amount consumed by movement orders.
        CREATE TABLE ships (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            fleet_ordinal INTEGER NOT NULL CHECK(typeof(fleet_ordinal) = 'integer' AND fleet_ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            fuel REAL NOT NULL CHECK(fuel >= 0.0),
            UNIQUE(fleet_id, fleet_ordinal),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id)
        );

        CREATE TABLE survey_teams (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            location_kind INTEGER NOT NULL CHECK(location_kind BETWEEN 0 AND 1),
            colony_id INTEGER NULL CHECK(colony_id IS NULL OR colony_id > 0),
            fleet_id INTEGER NULL CHECK(fleet_id IS NULL OR fleet_id > 0),
            CHECK((location_kind = 0 AND colony_id IS NOT NULL AND fleet_id IS NULL)
                OR (location_kind = 1 AND colony_id IS NULL AND fleet_id IS NOT NULL)),
            FOREIGN KEY(colony_id) REFERENCES colonies(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id)
        );

        -- Canonical physical leases are nullable unique IDs on the program.
        -- Requested IDs express intent and do not claim an unavailable asset.
        CREATE TABLE survey_programs (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            home_colony_id INTEGER NOT NULL CHECK(home_colony_id > 0),
            pending_home_colony_id INTEGER NULL CHECK(pending_home_colony_id IS NULL OR pending_home_colony_id > 0),
            requested_fleet_id INTEGER NULL CHECK(requested_fleet_id IS NULL OR requested_fleet_id > 0),
            requested_leader_id INTEGER NULL CHECK(requested_leader_id IS NULL OR requested_leader_id > 0),
            requested_team_id INTEGER NULL CHECK(requested_team_id IS NULL OR requested_team_id > 0),
            max_additional_propellant REAL NULL CHECK(max_additional_propellant IS NULL OR max_additional_propellant >= 0.0),
            home_stock_floor REAL NOT NULL CHECK(home_stock_floor >= 0.0),
            return_contingency_fraction REAL NOT NULL CHECK(return_contingency_fraction >= 0.0),
            created_day INTEGER NOT NULL CHECK(created_day >= 0),
            charter_revision INTEGER NOT NULL CHECK(charter_revision > 0),
            lifecycle INTEGER NOT NULL CHECK(lifecycle BETWEEN 0 AND 3),
            closure INTEGER NOT NULL CHECK(closure BETWEEN 0 AND 2),
            leased_fleet_id INTEGER NULL UNIQUE CHECK(leased_fleet_id IS NULL OR leased_fleet_id > 0),
            leased_team_id INTEGER NULL UNIQUE CHECK(leased_team_id IS NULL OR leased_team_id > 0),
            task INTEGER NOT NULL CHECK(task BETWEEN 0 AND 3),
            task_body_id INTEGER NULL CHECK(task_body_id IS NULL OR task_body_id > 0),
            task_fleet_id INTEGER NULL CHECK(task_fleet_id IS NULL OR task_fleet_id > 0),
            task_team_id INTEGER NULL CHECK(task_team_id IS NULL OR task_team_id > 0),
            task_leader_id INTEGER NULL CHECK(task_leader_id IS NULL OR task_leader_id > 0),
            task_approach INTEGER NOT NULL CHECK(task_approach BETWEEN 0 AND 1),
            task_pass_number INTEGER NOT NULL CHECK(task_pass_number >= 0),
            work_days_completed INTEGER NOT NULL CHECK(work_days_completed >= 0),
            first_work_day INTEGER NOT NULL CHECK(first_work_day >= 0),
            last_selection_reason TEXT NOT NULL,
            fuel_loaded REAL NOT NULL CHECK(fuel_loaded >= 0.0),
            fuel_burned REAL NOT NULL CHECK(fuel_burned >= 0.0),
            total_work_days INTEGER NOT NULL CHECK(total_work_days >= 0),
            next_report_day INTEGER NOT NULL CHECK(next_report_day > 0),
            report_start_day INTEGER NOT NULL CHECK(report_start_day >= 0),
            reported_fuel_loaded REAL NOT NULL CHECK(reported_fuel_loaded >= 0.0),
            reported_fuel_burned REAL NOT NULL CHECK(reported_fuel_burned >= 0.0),
            reported_work_days INTEGER NOT NULL CHECK(reported_work_days >= 0),
            reported_visits INTEGER NOT NULL CHECK(reported_visits >= 0),
            issue_signature TEXT NOT NULL,
            issue_message TEXT NOT NULL,
            issue_acknowledged INTEGER NOT NULL CHECK(issue_acknowledged IN (0, 1)),
            FOREIGN KEY(home_colony_id) REFERENCES colonies(id),
            FOREIGN KEY(pending_home_colony_id) REFERENCES colonies(id),
            FOREIGN KEY(requested_fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(requested_leader_id) REFERENCES people(id),
            FOREIGN KEY(requested_team_id) REFERENCES survey_teams(id),
            FOREIGN KEY(leased_fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(leased_team_id) REFERENCES survey_teams(id),
            FOREIGN KEY(task_body_id) REFERENCES bodies(id),
            FOREIGN KEY(task_fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(task_team_id) REFERENCES survey_teams(id),
            FOREIGN KEY(task_leader_id) REFERENCES people(id)
        );

        CREATE TABLE survey_program_targets (
            program_id INTEGER NOT NULL CHECK(program_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            priority INTEGER NOT NULL CHECK(priority >= 0),
            requested_passes INTEGER NOT NULL CHECK(requested_passes > 0),
            PRIMARY KEY(program_id, ordinal),
            UNIQUE(program_id, body_id),
            FOREIGN KEY(program_id) REFERENCES survey_programs(id),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );

        -- Receipts remain even when a later charter removes a target row.
        CREATE TABLE survey_program_receipts (
            program_id INTEGER NOT NULL CHECK(program_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            pass_number INTEGER NOT NULL CHECK(pass_number > 0),
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            team_id INTEGER NOT NULL CHECK(team_id > 0),
            leader_id INTEGER NULL CHECK(leader_id IS NULL OR leader_id > 0),
            approach INTEGER NOT NULL CHECK(approach BETWEEN 0 AND 1),
            first_work_day INTEGER NOT NULL CHECK(first_work_day >= 0),
            completed_day INTEGER NOT NULL CHECK(completed_day >= first_work_day),
            work_days INTEGER NOT NULL CHECK(work_days > 0),
            deposits_improved INTEGER NOT NULL CHECK(deposits_improved >= 0),
            average_confidence_before REAL NOT NULL CHECK(average_confidence_before BETWEEN 0.0 AND 1.0),
            average_confidence_after REAL NOT NULL CHECK(average_confidence_after BETWEEN 0.0 AND 1.0),
            PRIMARY KEY(program_id, ordinal),
            UNIQUE(program_id, body_id, pass_number),
            FOREIGN KEY(program_id) REFERENCES survey_programs(id),
            FOREIGN KEY(body_id) REFERENCES bodies(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(team_id) REFERENCES survey_teams(id),
            FOREIGN KEY(leader_id) REFERENCES people(id)
        );

        CREATE TABLE survey_program_reports (
            program_id INTEGER NOT NULL CHECK(program_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            start_day INTEGER NOT NULL CHECK(start_day >= 0),
            end_day INTEGER NOT NULL CHECK(end_day >= start_day),
            is_ninety_day_review INTEGER NOT NULL CHECK(is_ninety_day_review IN (0, 1)),
            charter_revision INTEGER NOT NULL CHECK(charter_revision > 0),
            leader_id INTEGER NULL CHECK(leader_id IS NULL OR leader_id > 0),
            approach INTEGER NOT NULL CHECK(approach BETWEEN 0 AND 1),
            visits_completed INTEGER NOT NULL CHECK(visits_completed >= 0),
            work_days INTEGER NOT NULL CHECK(work_days >= 0),
            fuel_loaded REAL NOT NULL CHECK(fuel_loaded >= 0.0),
            fuel_burned REAL NOT NULL CHECK(fuel_burned >= 0.0),
            fleet_id INTEGER NULL CHECK(fleet_id IS NULL OR fleet_id > 0),
            team_id INTEGER NULL CHECK(team_id IS NULL OR team_id > 0),
            fleet_body_id INTEGER NULL CHECK(fleet_body_id IS NULL OR fleet_body_id > 0),
            waiting_reason TEXT NOT NULL,
            PRIMARY KEY(program_id, ordinal),
            UNIQUE(program_id, end_day),
            FOREIGN KEY(program_id) REFERENCES survey_programs(id),
            FOREIGN KEY(leader_id) REFERENCES people(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(team_id) REFERENCES survey_teams(id),
            FOREIGN KEY(fleet_body_id) REFERENCES bodies(id)
        );

        CREATE TABLE event_log (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            day INTEGER NOT NULL CHECK(day >= 0),
            severity INTEGER NOT NULL CHECK(severity BETWEEN 0 AND 2),
            event_type TEXT NOT NULL CHECK(length(event_type) > 0),
            payload_json TEXT NOT NULL CHECK(length(payload_json) > 0)
        );

        CREATE INDEX idx_people_institution_id ON people(institution_id);
        CREATE INDEX idx_appointments_person_id ON appointments(person_id);
        CREATE INDEX idx_bodies_system_id ON bodies(system_id);
        CREATE INDEX idx_colonies_body_id ON colonies(body_id);
        CREATE INDEX idx_colonies_owner_institution_id ON colonies(owner_institution_id);
        CREATE INDEX idx_colony_processing_allocations_colony_id
            ON colony_processing_allocations(colony_id);
        CREATE INDEX idx_fleets_owner_institution_id ON fleets(owner_institution_id);
        CREATE INDEX idx_fleet_order_queue_fleet_id ON fleet_order_queue(fleet_id);
        CREATE INDEX idx_ships_fleet_id ON ships(fleet_id);
        CREATE INDEX idx_survey_teams_fleet_id ON survey_teams(fleet_id);
        CREATE INDEX idx_survey_program_targets_body_id ON survey_program_targets(body_id);
        CREATE INDEX idx_survey_program_receipts_body_id ON survey_program_receipts(body_id);
        CREATE INDEX idx_survey_program_reports_end_day ON survey_program_reports(end_day);
        CREATE INDEX idx_events_day ON event_log(day);
    )sql");
}


bool hasUserSchema(Database& db) {
    Statement stmt{db, "SELECT 1 FROM sqlite_schema WHERE name NOT GLOB 'sqlite_*' LIMIT 1;"};
    return stmt.step();
}

std::int64_t readSchemaVersion(Database& db) {
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
    return version;
}

namespace {

void requireStructure(Database& db, const bool allowKnownTableTriggers) {
    // Compare against a fresh v13 declaration. Load permits known-table
    // triggers only because it is read-only; Save rejects their write effects.
    Database reference{std::filesystem::path{":memory:"}};
    createSchemaV13(reference);
    const auto expectedObjects = readUserObjects(reference);
    std::vector<std::string> tables;
    for (const auto& object : expectedObjects) {
        if (object.type == "table") tables.push_back(object.name);
    }
    if (readUserObjects(db, tables, allowKnownTableTriggers) != expectedObjects) {
        throw std::runtime_error{"Save file has an incompatible schema object set"};
    }
    for (const auto& table : tables) {
        const std::string quoted = quoteIdentifier(table);
        if (readPragmaRows(db, "PRAGMA table_xinfo(" + quoted + ");", 7)
                != readPragmaRows(reference, "PRAGMA table_xinfo(" + quoted + ");", 7)
            || readPragmaRows(db, "PRAGMA foreign_key_list(" + quoted + ");", 8)
                != readPragmaRows(reference, "PRAGMA foreign_key_list(" + quoted + ");", 8)
            || readIndexShapes(db, table) != readIndexShapes(reference, table)) {
            throw std::runtime_error{"Save file has an incompatible table structure: " + table};
        }
    }
}

} // namespace

void requireV13Structure(Database& db, const bool allowKnownTableTriggers) {
    requireStructure(db, allowKnownTableTriggers);
}

} // namespace deep::save
