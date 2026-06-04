#include "save/SaveGameRepository.h"

// Implements schema v6 save/load mapping for the headless simulation state.
// The repository uses prepared statements and transactions throughout; raw SQL
// execution is limited to static schema/table maintenance statements with no data.

#include "save/Database.h"
#include "save/EventJson.h"
#include "save/Schema.h"
#include "sim/Minerals.h"
#include "sim/GameStateValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace deep::save {
namespace {

// Converts strongly typed IDs to their persisted integer representation.
template <typename IdT>
[[nodiscard]] std::int64_t idValue(const IdT id) noexcept {
    return id.value;
}

// Converts enum values to stable persisted ordinals. Schema migrations must
// account for any future enum reordering.
template <typename EnumT>
[[nodiscard]] std::int64_t enumValue(const EnumT value) noexcept {
    return static_cast<std::int64_t>(value);
}

// Returns true when a persisted enum ordinal is part of the current schema v6
// contract. Keep this explicit instead of raw-casting database values; SQLite
// files are inspectable and may be hand-edited or corrupted.
template <typename EnumT>
[[nodiscard]] bool isValidEnumValue(const std::int64_t value) noexcept {
    if constexpr (std::is_same_v<EnumT, Mineral>) {
        return value >= 0 && value < static_cast<std::int64_t>(mineralCount());
    } else if constexpr (std::is_same_v<EnumT, ProcessedMaterial>) {
        return value >= 0 && value < static_cast<std::int64_t>(processedMaterialCount());
    } else if constexpr (std::is_same_v<EnumT, InstitutionType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(InstitutionType::ContinuityOffice);
    } else if constexpr (std::is_same_v<EnumT, BodyType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(BodyType::Asteroid);
    } else if constexpr (std::is_same_v<EnumT, ShipRole>) {
        return value >= 0 && value <= static_cast<std::int64_t>(ShipRole::Escort);
    } else if constexpr (std::is_same_v<EnumT, ProcessingPolicy>) {
        return value >= 0 && value <= static_cast<std::int64_t>(ProcessingPolicy::Manual);
    } else if constexpr (std::is_same_v<EnumT, ShipyardOrderStatus>) {
        // Schema v1 persists only the lifecycle states the simulation can
        // produce from valid input. Temporary shortages remain Active.
        return value >= 0 && value <= static_cast<std::int64_t>(ShipyardOrderStatus::Completed);
    } else if constexpr (std::is_same_v<EnumT, FleetOrderType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(FleetOrderType::MoveToBody);
    } else if constexpr (std::is_same_v<EnumT, EventSeverity>) {
        return value >= 0 && value <= static_cast<std::int64_t>(EventSeverity::Critical);
    } else {
        static_assert(std::is_enum_v<EnumT>, "enumFromValue requires an enum type");
        return false;
    }
}

// Reconstructs an enum from its persisted ordinal. Validation is kept local to
// load paths so malformed save rows fail early instead of corrupting GameState.
template <typename EnumT>
[[nodiscard]] EnumT enumFromValue(const std::int64_t value) {
    if (!isValidEnumValue<EnumT>(value)) {
        throw std::runtime_error{"Save file contains an invalid enum value"};
    }
    return static_cast<EnumT>(value);
}

// Converts a database INTEGER to int after checking range. This prevents silent
// truncation when malformed saves contain values outside command/domain limits.
[[nodiscard]] int checkedIntFromSql(const std::int64_t value, const std::string_view fieldName) {
    if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error{std::string{"Save integer is out of range for field: "} + std::string{fieldName}};
    }
    return static_cast<int>(value);
}

// Parses persisted integer text strictly. SQLite CAST would silently convert
// values such as "abc" or "1junk" to integers, so metadata and counters are
// read as text and must match std::to_string(value) exactly.
[[nodiscard]] std::int64_t strictInt64FromText(const std::string_view text, const std::string_view fieldName) {
    if (text.empty()) {
        throw std::runtime_error{std::string{"Save integer is empty for field: "} + std::string{fieldName}};
    }

    std::int64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || std::to_string(value) != text) {
        throw std::runtime_error{std::string{"Save integer has invalid syntax for field: "} + std::string{fieldName}};
    }
    return value;
}

// Finds a mutable record by typed ID during load fix-up. Returned pointers are
// non-owning and must not be stored across vector mutations.
template <typename T, typename IdT>
[[nodiscard]] T* findById(std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// Binds an optional typed ID as either INTEGER or NULL. Optional body references
// are used by in-progress fleet movement orders.
template <typename IdT>
void bindOptionalId(Statement& stmt, const int index, const std::optional<IdT> id) {
    if (id.has_value()) {
        stmt.bindInt64(index, idValue(*id));
    } else {
        stmt.bindNull(index);
    }
}

// Converts a nullable INTEGER column to an optional typed ID.
template <typename IdT>
[[nodiscard]] std::optional<IdT> optionalIdFromColumn(const Statement& stmt, const int column) {
    if (stmt.columnIsNull(column)) {
        return std::nullopt;
    }
    return IdT{stmt.columnInt64(column)};
}

// Resets a reusable insert statement after one row. Keeping this helper avoids
// accidental reuse with stale bound values.
void reuse(Statement& stmt) {
    stmt.reset();
    stmt.clearBindings();
}

void clearExistingSave(Database& db) {
    // Delete child tables first because schema v6 intentionally uses explicit
    // foreign keys rather than ON DELETE CASCADE. This makes destructive save
    // behavior visible and easy to audit.
    db.execute(R"sql(
        DELETE FROM event_log;
        DELETE FROM ships;
        DELETE FROM fleet_order_queue;
        DELETE FROM fleets;
        DELETE FROM shipyard_orders;
        DELETE FROM ship_class_material_costs;
        DELETE FROM ship_classes;
        DELETE FROM colony_processing_allocations;
        DELETE FROM colony_materials;
        DELETE FROM colony_minerals;
        DELETE FROM mineral_deposits;
        DELETE FROM colonies;
        DELETE FROM people;
        DELETE FROM institutions;
        DELETE FROM bodies;
        DELETE FROM star_systems;
        DELETE FROM id_counters;
        DELETE FROM game_meta;
        DELETE FROM schema_version;
    )sql");
}

void saveSchemaVersion(Database& db) {
    // The fixed id makes schema_version a structural singleton in new saves.
    // Load still verifies row count because older/corrupted files may not have
    // been created with the current table constraint.
    Statement stmt{db, "INSERT INTO schema_version(id, version) VALUES (1, ?);"};
    stmt.bindInt64(1, kSchemaVersion);
    stmt.execute();
}

void saveMeta(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO game_meta(key, value) VALUES (?, ?);"};
    stmt.bindText(1, "current_day");
    stmt.bindText(2, std::to_string(state.date.day));
    stmt.execute();
}

void saveIdCounters(Database& db, const IdCounters& ids) {
    Statement stmt{db, "INSERT INTO id_counters(key, value) VALUES (?, ?);"};

    const auto insertCounter = [&stmt](const std::string_view key, const std::int64_t value) {
        stmt.bindText(1, key);
        stmt.bindInt64(2, value);
        stmt.execute();
        reuse(stmt);
    };

    insertCounter("next_star_system_id", ids.nextStarSystemId);
    insertCounter("next_body_id", ids.nextBodyId);
    insertCounter("next_colony_id", ids.nextColonyId);
    insertCounter("next_institution_id", ids.nextInstitutionId);
    insertCounter("next_person_id", ids.nextPersonId);
    insertCounter("next_ship_class_id", ids.nextShipClassId);
    insertCounter("next_shipyard_order_id", ids.nextShipyardOrderId);
    insertCounter("next_ship_id", ids.nextShipId);
    insertCounter("next_fleet_id", ids.nextFleetId);
    insertCounter("next_event_id", ids.nextEventId);
}

void saveStarSystems(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO star_systems(id, name) VALUES (?, ?);"};
    for (const StarSystem& system : state.starSystems) {
        stmt.bindInt64(1, idValue(system.id));
        stmt.bindText(2, system.name);
        stmt.execute();
        reuse(stmt);
    }
}

void saveInstitutions(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO institutions(id, name, institution_type) VALUES (?, ?, ?);"};
    for (const Institution& institution : state.institutions) {
        stmt.bindInt64(1, idValue(institution.id));
        stmt.bindText(2, institution.name);
        stmt.bindInt64(3, enumValue(institution.type));
        stmt.execute();
        reuse(stmt);
    }
}

void savePeople(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO people(
            id, name, institution_id, logistics, industry, survey, command,
            administration, engineering, intelligence, crisis_management,
            seniority_level, successful_assignments, failed_assignments,
            commendations, controversies
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (const Person& person : state.people) {
        stmt.bindInt64(1, idValue(person.id));
        stmt.bindText(2, person.name);
        stmt.bindInt64(3, idValue(person.institutionId));
        stmt.bindInt64(4, person.competencies.logistics);
        stmt.bindInt64(5, person.competencies.industry);
        stmt.bindInt64(6, person.competencies.survey);
        stmt.bindInt64(7, person.competencies.command);
        stmt.bindInt64(8, person.competencies.administration);
        stmt.bindInt64(9, person.competencies.engineering);
        stmt.bindInt64(10, person.competencies.intelligence);
        stmt.bindInt64(11, person.competencies.crisisManagement);
        stmt.bindInt64(12, person.seniorityLevel);
        stmt.bindInt64(13, person.serviceRecord.successfulAssignments);
        stmt.bindInt64(14, person.serviceRecord.failedAssignments);
        stmt.bindInt64(15, person.serviceRecord.commendations);
        stmt.bindInt64(16, person.serviceRecord.controversies);
        stmt.execute();
        reuse(stmt);
    }
}

void saveBodies(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO bodies(id, system_id, name, body_type, x, y) VALUES (?, ?, ?, ?, ?, ?);"};
    for (const Body& body : state.bodies) {
        stmt.bindInt64(1, idValue(body.id));
        stmt.bindInt64(2, idValue(body.systemId));
        stmt.bindText(3, body.name);
        stmt.bindInt64(4, enumValue(body.type));
        stmt.bindDouble(5, body.x);
        stmt.bindDouble(6, body.y);
        stmt.execute();
        reuse(stmt);
    }
}

void saveColonies(Database& db, const GameState& state) {
    Statement colonyStmt{db, R"sql(
        INSERT INTO colonies(
            id, body_id, name, owner_institution_id, mines, processor_capacity,
            shipyard_capacity, processing_policy
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement mineralStmt{db, "INSERT INTO colony_minerals(colony_id, mineral, amount) VALUES (?, ?, ?);"};
    Statement materialStmt{db, "INSERT INTO colony_materials(colony_id, material, amount) VALUES (?, ?, ?);"};
    Statement allocationStmt{db, R"sql(
        INSERT INTO colony_processing_allocations(colony_id, ordinal, material, weight)
        VALUES (?, ?, ?, ?);
    )sql"};

    for (const Colony& colony : state.colonies) {
        colonyStmt.bindInt64(1, idValue(colony.id));
        colonyStmt.bindInt64(2, idValue(colony.bodyId));
        colonyStmt.bindText(3, colony.name);
        bindOptionalId(colonyStmt, 4, colony.ownerInstitutionId);
        colonyStmt.bindDouble(5, colony.mines);
        colonyStmt.bindDouble(6, colony.processorCapacity);
        colonyStmt.bindDouble(7, colony.shipyardCapacity);
        colonyStmt.bindInt64(8, enumValue(colony.processingPolicy));
        colonyStmt.execute();
        reuse(colonyStmt);

        for (std::size_t mineral = 0; mineral < mineralCount(); ++mineral) {
            mineralStmt.bindInt64(1, idValue(colony.id));
            mineralStmt.bindInt64(2, static_cast<std::int64_t>(mineral));
            mineralStmt.bindDouble(3, colony.stockpile.amount.at(mineral));
            mineralStmt.execute();
            reuse(mineralStmt);
        }

        for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
            materialStmt.bindInt64(1, idValue(colony.id));
            materialStmt.bindInt64(2, static_cast<std::int64_t>(material));
            materialStmt.bindDouble(3, colony.processedStockpile.amount.at(material));
            materialStmt.execute();
            reuse(materialStmt);
        }

        for (std::size_t ordinal = 0; ordinal < colony.manualProcessingAllocations.size(); ++ordinal) {
            const ProcessingAllocation& allocation = colony.manualProcessingAllocations.at(ordinal);
            allocationStmt.bindInt64(1, idValue(colony.id));
            allocationStmt.bindInt64(2, static_cast<std::int64_t>(ordinal));
            allocationStmt.bindInt64(3, enumValue(allocation.material));
            allocationStmt.bindDouble(4, allocation.weight);
            allocationStmt.execute();
            reuse(allocationStmt);
        }
    }
}

void saveMineralDeposits(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO mineral_deposits(body_id, mineral, remaining, accessibility) VALUES (?, ?, ?, ?);"};
    for (const MineralDeposit& deposit : state.mineralDeposits) {
        stmt.bindInt64(1, idValue(deposit.bodyId));
        stmt.bindInt64(2, enumValue(deposit.mineral));
        stmt.bindDouble(3, deposit.remaining);
        stmt.bindDouble(4, deposit.accessibility);
        stmt.execute();
        reuse(stmt);
    }
}

void saveShipClasses(Database& db, const GameState& state) {
    Statement classStmt{db, R"sql(
        INSERT INTO ship_classes(id, name, role, build_points, speed_km_per_day, fuel_capacity)
        VALUES (?, ?, ?, ?, ?, ?);
    )sql"};
    Statement costStmt{db, "INSERT INTO ship_class_material_costs(ship_class_id, material, amount) VALUES (?, ?, ?);"};

    for (const ShipClass& shipClass : state.shipClasses) {
        classStmt.bindInt64(1, idValue(shipClass.id));
        classStmt.bindText(2, shipClass.name);
        classStmt.bindInt64(3, enumValue(shipClass.role));
        classStmt.bindDouble(4, shipClass.buildPoints);
        classStmt.bindDouble(5, shipClass.speedKmPerDay);
        classStmt.bindDouble(6, shipClass.fuelCapacity);
        classStmt.execute();
        reuse(classStmt);

        for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
            costStmt.bindInt64(1, idValue(shipClass.id));
            costStmt.bindInt64(2, static_cast<std::int64_t>(material));
            costStmt.bindDouble(3, shipClass.buildCost.amount.at(material));
            costStmt.execute();
            reuse(costStmt);
        }
    }
}

void saveShipyardOrders(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO shipyard_orders(
            id, colony_id, ship_class_id, quantity_requested, quantity_completed,
            accumulated_build_points, status
        ) VALUES (?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (const ShipyardOrder& order : state.shipyardOrders) {
        stmt.bindInt64(1, idValue(order.id));
        stmt.bindInt64(2, idValue(order.colonyId));
        stmt.bindInt64(3, idValue(order.shipClassId));
        stmt.bindInt64(4, order.quantityRequested);
        stmt.bindInt64(5, order.quantityCompleted);
        stmt.bindDouble(6, order.accumulatedBuildPoints);
        stmt.bindInt64(7, enumValue(order.status));
        stmt.execute();
        reuse(stmt);
    }
}

void saveFleets(Database& db, const GameState& state) {
    Statement fleetStmt{db, R"sql(
        INSERT INTO fleets(
            id, name, owner_institution_id, current_body_id, destination_body_id,
            order_type, order_target_body_id, order_days_remaining
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement queueStmt{db, R"sql(
        INSERT INTO fleet_order_queue(fleet_id, ordinal, order_type, target_body_id)
        VALUES (?, ?, ?, ?);
    )sql"};

    for (const Fleet& fleet : state.fleets) {
        fleetStmt.bindInt64(1, idValue(fleet.id));
        fleetStmt.bindText(2, fleet.name);
        bindOptionalId(fleetStmt, 3, fleet.ownerInstitutionId);
        fleetStmt.bindInt64(4, idValue(fleet.currentBodyId));
        bindOptionalId(fleetStmt, 5, fleet.destinationBodyId);
        fleetStmt.bindInt64(6, enumValue(fleet.activeOrder.type));
        bindOptionalId(fleetStmt, 7, fleet.activeOrder.targetBodyId);
        fleetStmt.bindInt64(8, fleet.activeOrder.daysRemaining);
        fleetStmt.execute();
        reuse(fleetStmt);

        for (std::size_t i = 0; i < fleet.queuedOrders.size(); ++i) {
            const QueuedFleetOrder& queuedOrder = fleet.queuedOrders.at(i);
            if (!queuedOrder.targetBodyId.has_value()) {
                throw std::runtime_error{"queued fleet order is missing a target body"};
            }

            queueStmt.bindInt64(1, idValue(fleet.id));
            queueStmt.bindInt64(2, static_cast<std::int64_t>(i));
            queueStmt.bindInt64(3, enumValue(queuedOrder.type));
            queueStmt.bindInt64(4, idValue(*queuedOrder.targetBodyId));
            queueStmt.execute();
            reuse(queueStmt);
        }
    }
}

void saveShips(Database& db, const GameState& state) {
    // Persist current per-ship propellant, not just class fuel capacity, so
    // loaded fleets keep the operational range they had at save time.
    Statement stmt{db, "INSERT INTO ships(id, ship_class_id, fleet_id, name, fuel) VALUES (?, ?, ?, ?, ?);"};
    for (const Ship& ship : state.ships) {
        stmt.bindInt64(1, idValue(ship.id));
        stmt.bindInt64(2, idValue(ship.shipClassId));
        stmt.bindInt64(3, idValue(ship.fleetId));
        stmt.bindText(4, ship.name);
        stmt.bindDouble(5, ship.fuel);
        stmt.execute();
        reuse(stmt);
    }
}

void saveEvents(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO event_log(id, day, severity, event_type, payload_json) VALUES (?, ?, ?, ?, ?);"};
    for (const SimEvent& event : state.eventLog) {
        stmt.bindInt64(1, idValue(event.id));
        stmt.bindInt64(2, event.day);
        stmt.bindInt64(3, enumValue(event.severity));
        stmt.bindText(4, eventTypeName(event.payload));
        stmt.bindText(5, eventPayloadToJson(event.payload));
        stmt.execute();
        reuse(stmt);
    }
}

[[nodiscard]] std::int64_t loadCounter(Database& db, const std::string_view key) {
    Statement stmt{db, "SELECT value FROM id_counters WHERE key = ?;"};
    stmt.bindText(1, key);
    if (!stmt.step()) {
        throw std::runtime_error{"Save file is missing an ID counter"};
    }

    const std::int64_t value = strictInt64FromText(stmt.columnText(0), key);
    if (value <= 0) {
        throw std::runtime_error{"Save file contains an invalid ID counter"};
    }
    return value;
}

[[nodiscard]] std::int64_t loadMetaInt64(Database& db, const std::string_view key) {
    Statement stmt{db, "SELECT value FROM game_meta WHERE key = ?;"};
    stmt.bindText(1, key);
    if (!stmt.step()) {
        throw std::runtime_error{"Save file is missing required metadata"};
    }

    const std::int64_t value = strictInt64FromText(stmt.columnText(0), key);
    if (key == "current_day" && value < 0) {
        throw std::runtime_error{"Save file contains a negative current day"};
    }
    return value;
}

void requireNoForeignKeyViolations(Database& db) {
    // Foreign keys can be disabled by external tools while editing SQLite files.
    // Re-run SQLite's integrity check during load so broken references fail
    // before rows are mapped into GameState vectors.
    Statement stmt{db, "PRAGMA foreign_key_check;"};
    if (stmt.step()) {
        throw std::runtime_error{"Save file contains foreign key violations"};
    }
}

void loadIdCounters(Database& db, IdCounters& ids) {
    ids.nextStarSystemId = loadCounter(db, "next_star_system_id");
    ids.nextBodyId = loadCounter(db, "next_body_id");
    ids.nextColonyId = loadCounter(db, "next_colony_id");
    ids.nextInstitutionId = loadCounter(db, "next_institution_id");
    ids.nextPersonId = loadCounter(db, "next_person_id");
    ids.nextShipClassId = loadCounter(db, "next_ship_class_id");
    ids.nextShipyardOrderId = loadCounter(db, "next_shipyard_order_id");
    ids.nextShipId = loadCounter(db, "next_ship_id");
    ids.nextFleetId = loadCounter(db, "next_fleet_id");
    ids.nextEventId = loadCounter(db, "next_event_id");
}

void loadStarSystems(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, name FROM star_systems ORDER BY id;"};
    while (stmt.step()) {
        state.starSystems.push_back(StarSystem{
            .id = StarSystemId{stmt.columnInt64(0)},
            .name = stmt.columnText(1)
        });
    }
}

void loadInstitutions(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, name, institution_type FROM institutions ORDER BY id;"};
    while (stmt.step()) {
        state.institutions.push_back(Institution{
            .id = InstitutionId{stmt.columnInt64(0)},
            .name = stmt.columnText(1),
            .type = enumFromValue<InstitutionType>(stmt.columnInt64(2))
        });
    }
}

void loadPeople(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, name, institution_id, logistics, industry, survey, command,
               administration, engineering, intelligence, crisis_management,
               seniority_level, successful_assignments, failed_assignments,
               commendations, controversies
        FROM people
        ORDER BY id;
    )sql"};

    while (stmt.step()) {
        state.people.push_back(Person{
            .id = PersonId{stmt.columnInt64(0)},
            .name = stmt.columnText(1),
            .institutionId = InstitutionId{stmt.columnInt64(2)},
            .competencies = PersonCompetencies{
                .logistics = checkedIntFromSql(stmt.columnInt64(3), "people.logistics"),
                .industry = checkedIntFromSql(stmt.columnInt64(4), "people.industry"),
                .survey = checkedIntFromSql(stmt.columnInt64(5), "people.survey"),
                .command = checkedIntFromSql(stmt.columnInt64(6), "people.command"),
                .administration = checkedIntFromSql(stmt.columnInt64(7), "people.administration"),
                .engineering = checkedIntFromSql(stmt.columnInt64(8), "people.engineering"),
                .intelligence = checkedIntFromSql(stmt.columnInt64(9), "people.intelligence"),
                .crisisManagement = checkedIntFromSql(stmt.columnInt64(10), "people.crisis_management")
            },
            .seniorityLevel = checkedIntFromSql(stmt.columnInt64(11), "people.seniority_level"),
            .serviceRecord = PersonServiceRecord{
                .successfulAssignments = checkedIntFromSql(stmt.columnInt64(12), "people.successful_assignments"),
                .failedAssignments = checkedIntFromSql(stmt.columnInt64(13), "people.failed_assignments"),
                .commendations = checkedIntFromSql(stmt.columnInt64(14), "people.commendations"),
                .controversies = checkedIntFromSql(stmt.columnInt64(15), "people.controversies")
            }
        });
    }
}

void loadBodies(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, system_id, name, body_type, x, y FROM bodies ORDER BY id;"};
    while (stmt.step()) {
        state.bodies.push_back(Body{
            .id = BodyId{stmt.columnInt64(0)},
            .systemId = StarSystemId{stmt.columnInt64(1)},
            .name = stmt.columnText(2),
            .type = enumFromValue<BodyType>(stmt.columnInt64(3)),
            .x = stmt.columnDouble(4),
            .y = stmt.columnDouble(5)
        });
    }
}

void loadColonies(Database& db, GameState& state) {
    Statement colonies{db, R"sql(
        SELECT id, body_id, name, owner_institution_id, mines, processor_capacity,
               shipyard_capacity, processing_policy
        FROM colonies
        ORDER BY id;
    )sql"};
    while (colonies.step()) {
        state.colonies.push_back(Colony{
            .id = ColonyId{colonies.columnInt64(0)},
            .bodyId = BodyId{colonies.columnInt64(1)},
            .name = colonies.columnText(2),
            .stockpile = MineralSet{},
            .processedStockpile = ProcessedMaterialSet{},
            .mines = colonies.columnDouble(4),
            .processorCapacity = colonies.columnDouble(5),
            .shipyardCapacity = colonies.columnDouble(6),
            .processingPolicy = enumFromValue<ProcessingPolicy>(colonies.columnInt64(7)),
            .manualProcessingAllocations = {},
            .ownerInstitutionId = optionalIdFromColumn<InstitutionId>(colonies, 3)
        });
    }

    Statement minerals{db, "SELECT colony_id, mineral, amount FROM colony_minerals ORDER BY colony_id, mineral;"};
    while (minerals.step()) {
        const ColonyId colonyId{minerals.columnInt64(0)};
        Colony* colony = findById(state.colonies, colonyId);
        if (colony == nullptr) {
            throw std::runtime_error{"colony_minerals references a missing colony"};
        }
        colony->stockpile.set(enumFromValue<Mineral>(minerals.columnInt64(1)), minerals.columnDouble(2));
    }

    Statement materials{db, "SELECT colony_id, material, amount FROM colony_materials ORDER BY colony_id, material;"};
    while (materials.step()) {
        const ColonyId colonyId{materials.columnInt64(0)};
        Colony* colony = findById(state.colonies, colonyId);
        if (colony == nullptr) {
            throw std::runtime_error{"colony_materials references a missing colony"};
        }
        colony->processedStockpile.set(enumFromValue<ProcessedMaterial>(materials.columnInt64(1)), materials.columnDouble(2));
    }

    Statement allocations{db, R"sql(
        SELECT colony_id, material, weight
        FROM colony_processing_allocations
        ORDER BY colony_id, ordinal;
    )sql"};
    while (allocations.step()) {
        const ColonyId colonyId{allocations.columnInt64(0)};
        Colony* colony = findById(state.colonies, colonyId);
        if (colony == nullptr) {
            throw std::runtime_error{"colony_processing_allocations references a missing colony"};
        }
        colony->manualProcessingAllocations.push_back(ProcessingAllocation{
            .material = enumFromValue<ProcessedMaterial>(allocations.columnInt64(1)),
            .weight = allocations.columnDouble(2)
        });
    }
}

void loadMineralDeposits(Database& db, GameState& state) {
    Statement stmt{db, "SELECT body_id, mineral, remaining, accessibility FROM mineral_deposits ORDER BY body_id, mineral;"};
    while (stmt.step()) {
        state.mineralDeposits.push_back(MineralDeposit{
            .bodyId = BodyId{stmt.columnInt64(0)},
            .mineral = enumFromValue<Mineral>(stmt.columnInt64(1)),
            .remaining = stmt.columnDouble(2),
            .accessibility = stmt.columnDouble(3)
        });
    }
}

void loadShipClasses(Database& db, GameState& state) {
    Statement classes{db, R"sql(
        SELECT id, name, role, build_points, speed_km_per_day, fuel_capacity
        FROM ship_classes
        ORDER BY id;
    )sql"};
    while (classes.step()) {
        state.shipClasses.push_back(ShipClass{
            .id = ShipClassId{classes.columnInt64(0)},
            .name = classes.columnText(1),
            .role = enumFromValue<ShipRole>(classes.columnInt64(2)),
            .buildCost = ProcessedMaterialSet{},
            .buildPoints = classes.columnDouble(3),
            .speedKmPerDay = classes.columnDouble(4),
            .fuelCapacity = classes.columnDouble(5)
        });
    }

    Statement costs{db, "SELECT ship_class_id, material, amount FROM ship_class_material_costs ORDER BY ship_class_id, material;"};
    while (costs.step()) {
        const ShipClassId shipClassId{costs.columnInt64(0)};
        ShipClass* shipClass = findById(state.shipClasses, shipClassId);
        if (shipClass == nullptr) {
            throw std::runtime_error{"ship_class_material_costs references a missing ship class"};
        }
        shipClass->buildCost.set(enumFromValue<ProcessedMaterial>(costs.columnInt64(1)), costs.columnDouble(2));
    }
}

void loadShipyardOrders(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, colony_id, ship_class_id, quantity_requested, quantity_completed,
               accumulated_build_points, status
        FROM shipyard_orders
        ORDER BY id;
    )sql"};
    while (stmt.step()) {
        state.shipyardOrders.push_back(ShipyardOrder{
            .id = ShipyardOrderId{stmt.columnInt64(0)},
            .colonyId = ColonyId{stmt.columnInt64(1)},
            .shipClassId = ShipClassId{stmt.columnInt64(2)},
            .quantityRequested = checkedIntFromSql(stmt.columnInt64(3), "shipyard_orders.quantity_requested"),
            .quantityCompleted = checkedIntFromSql(stmt.columnInt64(4), "shipyard_orders.quantity_completed"),
            .accumulatedBuildPoints = stmt.columnDouble(5),
            .status = enumFromValue<ShipyardOrderStatus>(stmt.columnInt64(6))
        });
    }
}

void loadFleets(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, name, owner_institution_id, current_body_id, destination_body_id,
               order_type, order_target_body_id, order_days_remaining
        FROM fleets
        ORDER BY id;
    )sql"};
    while (stmt.step()) {
        state.fleets.push_back(Fleet{
            .id = FleetId{stmt.columnInt64(0)},
            .name = stmt.columnText(1),
            .currentBodyId = BodyId{stmt.columnInt64(3)},
            .destinationBodyId = optionalIdFromColumn<BodyId>(stmt, 4),
            .shipIds = {},
            .activeOrder = FleetOrder{
                .type = enumFromValue<FleetOrderType>(stmt.columnInt64(5)),
                .targetBodyId = optionalIdFromColumn<BodyId>(stmt, 6),
                .daysRemaining = checkedIntFromSql(stmt.columnInt64(7), "fleets.order_days_remaining")
            },
            .queuedOrders = {},
            .ownerInstitutionId = optionalIdFromColumn<InstitutionId>(stmt, 2)
        });
    }

    Statement queue{db, R"sql(
        SELECT fleet_id, order_type, target_body_id
        FROM fleet_order_queue
        ORDER BY fleet_id, ordinal;
    )sql"};
    while (queue.step()) {
        const FleetId fleetId{queue.columnInt64(0)};
        Fleet* fleet = findById(state.fleets, fleetId);
        if (fleet == nullptr) {
            throw std::runtime_error{"fleet_order_queue references a missing fleet"};
        }
        fleet->queuedOrders.push_back(QueuedFleetOrder{
            .type = enumFromValue<FleetOrderType>(queue.columnInt64(1)),
            .targetBodyId = optionalIdFromColumn<BodyId>(queue, 2)
        });
    }
}

void loadShips(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, ship_class_id, fleet_id, name, fuel FROM ships ORDER BY id;"};
    while (stmt.step()) {
        Ship ship{
            .id = ShipId{stmt.columnInt64(0)},
            .shipClassId = ShipClassId{stmt.columnInt64(1)},
            .name = stmt.columnText(3),
            .fleetId = FleetId{stmt.columnInt64(2)},
            .fuel = stmt.columnDouble(4)
        };

        Fleet* fleet = findById(state.fleets, ship.fleetId);
        if (fleet == nullptr) {
            throw std::runtime_error{"ships references a missing fleet"};
        }
        fleet->shipIds.push_back(ship.id);
        state.ships.push_back(std::move(ship));
    }
}

void loadEvents(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, day, severity, event_type, payload_json FROM event_log ORDER BY id;"};
    while (stmt.step()) {
        const std::string eventType = stmt.columnText(3);
        const std::string payloadJson = stmt.columnText(4);
        state.eventLog.push_back(SimEvent{
            .id = EventId{stmt.columnInt64(0)},
            .day = stmt.columnInt64(1),
            .severity = enumFromValue<EventSeverity>(stmt.columnInt64(2)),
            .payload = eventPayloadFromJson(eventType, payloadJson)
        });
    }
}

} // namespace

void SaveGameRepository::save(const std::filesystem::path& path, const GameState& state) {
    // Save is also a trust boundary for tools/tests that may construct GameState
    // directly; do not persist a graph the simulation would later reject.
    validateGameState(state);

    Database db{path};
    initializeSchema(db);

    Transaction transaction{db, Transaction::Mode::Write};
    clearExistingSave(db);
    saveSchemaVersion(db);
    saveMeta(db, state);
    saveIdCounters(db, state.ids);
    saveStarSystems(db, state);
    saveInstitutions(db, state);
    savePeople(db, state);
    saveBodies(db, state);
    saveColonies(db, state);
    saveMineralDeposits(db, state);
    saveShipClasses(db, state);
    saveShipyardOrders(db, state);
    saveFleets(db, state);
    saveShips(db, state);
    saveEvents(db, state);
    // dailyEconomySnapshots is runtime-only telemetry for the active session.
    // Schema v1 deliberately omits it, so saves contain durable state and audit
    // events only; graphs can regenerate new samples after loading and advancing.
    transaction.commit();
}

GameState SaveGameRepository::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error{"Save file does not exist"};
    }

    Database db{path};

    Transaction transaction{db, Transaction::Mode::Read};
    requireSupportedSchema(db);
    requireNoForeignKeyViolations(db);

    GameState state;
    state.date.day = loadMetaInt64(db, "current_day");
    loadIdCounters(db, state.ids);
    loadStarSystems(db, state);
    loadInstitutions(db, state);
    loadPeople(db, state);
    loadBodies(db, state);
    loadColonies(db, state);
    loadMineralDeposits(db, state);
    loadShipClasses(db, state);
    loadShipyardOrders(db, state);
    loadFleets(db, state);
    loadShips(db, state);
    loadEvents(db, state);

    // There is intentionally no load step for dailyEconomySnapshots. Economy
    // telemetry is transient runtime data in schema v6 and remains empty until
    // the loaded simulation advances new days.

    // SQLite constraints are first-line protection only. The authoritative pass
    // validates cross-table semantics such as stale counters and fleet orders.
    validateGameState(state);

    transaction.commit();
    return state;
}

} // namespace deep::save
