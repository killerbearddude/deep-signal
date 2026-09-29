#include "save/SaveGameRepository.h"

// Responsibility: map durable simulation records to active v14 rows.
// Older development schemas are rejected without modifying their files.
// Each operation owns its connection and reconstructed data; the input snapshot
// is borrowed unchanged during save. Parameter binding separates values from SQL.
// Destination recognition, schema creation, row replacement, and reads use
// scoped transactions. Gameplay rules and graph invariants remain in sim/.

#include "save/Database.h"
#include "save/EventJson.h"
#include "save/FreightPersistence.h"
#include "save/Schema.h"
#include "sim/Minerals.h"
#include "sim/GameStateValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
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
#include <unordered_map>
#include <utility>
#include <variant>

namespace deep::save {
namespace {

#ifdef DEEP_SIGNAL_TESTING
bool failAfterFirstInsertForTest = false;
#endif

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

// Returns true when a persisted enum ordinal is part of the supported schema
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
    } else if constexpr (std::is_same_v<EnumT, AppointmentRole>) {
        return value >= 0 && value <= static_cast<std::int64_t>(AppointmentRole::InstitutionHead);
    } else if constexpr (std::is_same_v<EnumT, AppointmentScopeType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(AppointmentScopeType::Institution);
    } else if constexpr (std::is_same_v<EnumT, BodyType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(BodyType::Asteroid);
    } else if constexpr (std::is_same_v<EnumT, StrategicZone>) {
        return value >= 0 && value <= static_cast<std::int64_t>(StrategicZone::DeepSurveyFrontier);
    } else if constexpr (std::is_same_v<EnumT, ShipRole>) {
        return value >= 0 && value <= static_cast<std::int64_t>(ShipRole::Escort);
    } else if constexpr (std::is_same_v<EnumT, ShipComponentKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(ShipComponentKind::CargoBay);
    } else if constexpr (std::is_same_v<EnumT, ProcessingPolicy>) {
        return value >= 0 && value <= static_cast<std::int64_t>(ProcessingPolicy::Manual);
    } else if constexpr (std::is_same_v<EnumT, ShipyardOrderStatus>) {
        // The schema persists only the lifecycle states the simulation can
        // produce from valid input. Temporary shortages remain Active.
        return value >= 0 && value <= static_cast<std::int64_t>(ShipyardOrderStatus::Completed);
    } else if constexpr (std::is_same_v<EnumT, FleetOrderType>) {
        return value >= 0 && value <= static_cast<std::int64_t>(FleetOrderType::MoveToBody);
    } else if constexpr (std::is_same_v<EnumT, EventSeverity>) {
        return value >= 0 && value <= static_cast<std::int64_t>(EventSeverity::Critical);
    } else if constexpr (std::is_same_v<EnumT, SurveyPlanningApproach>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyPlanningApproach::PriorityFirst);
    } else if constexpr (std::is_same_v<EnumT, SurveyTeamLocationKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyTeamLocationKind::Fleet);
    } else if constexpr (std::is_same_v<EnumT, SurveyProgramLifecycle>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyProgramLifecycle::Closed);
    } else if constexpr (std::is_same_v<EnumT, SurveyProgramClosure>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyProgramClosure::Cancelled);
    } else if constexpr (std::is_same_v<EnumT, SurveyProgramTask>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyProgramTask::Return);
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

// Checks narrowing from an already decoded int64 to the domain's int fields.
// This does not validate SQLite storage types or recover precision lost by its
// column conversion; metadata uses a separate strict text parser below.
[[nodiscard]] int checkedIntFromSql(const std::int64_t value, const std::string_view fieldName) {
    if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error{std::string{"Save integer is out of range for field: "} + std::string{fieldName}};
    }
    return static_cast<int>(value);
}

[[nodiscard]] bool checkedBoolFromSql(const Statement& stmt, const int column,
                                      const std::string_view fieldName) {
    const std::int64_t value = stmt.columnInt64Strict(column);
    if (value != 0 && value != 1) {
        throw std::runtime_error{std::string{"Save boolean is invalid for field: "} + std::string{fieldName}};
    }
    return value == 1;
}

[[nodiscard]] std::int64_t checkedOrdinal(const std::size_t index, const std::string_view scope) {
    if (index > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error{std::string{"Save ordinal exceeds SQLite integer range: "} + std::string{scope}};
    }
    return static_cast<std::int64_t>(index);
}

void requireNextOrdinal(const Statement& stmt, const int column, std::int64_t& expected,
                        const std::string_view scope) {
    const std::int64_t ordinal = stmt.columnInt64Strict(column);
    if (ordinal != expected || expected == std::numeric_limits<std::int64_t>::max()) {
        throw std::runtime_error{std::string{"Save contains a noncontiguous ordinal in "} + std::string{scope}};
    }
    ++expected;
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

void bindOptionalDouble(Statement& stmt, const int index, const std::optional<double> value) {
    if (value.has_value()) stmt.bindDouble(index, *value);
    else stmt.bindNull(index);
}

// Converts a nullable INTEGER column to an optional typed ID.
template <typename IdT>
[[nodiscard]] std::optional<IdT> optionalIdFromColumn(const Statement& stmt, const int column) {
    if (stmt.columnIsNull(column)) {
        return std::nullopt;
    }
    return IdT{stmt.columnInt64(column)};
}

template <typename IdT>
[[nodiscard]] std::optional<IdT> optionalStrictIdFromColumn(const Statement& stmt, const int column) {
    if (stmt.columnIsNull(column)) return std::nullopt;
    return IdT{stmt.columnInt64Strict(column)};
}

// Resets a reusable insert statement after one row. Keeping this helper avoids
// accidental reuse with stale bound values.
void reuse(Statement& stmt) {
    stmt.reset();
    stmt.clearBindings();
}

void clearExistingSave(Database& db) {
    // Delete child tables first because v14 retains explicit foreign keys
    // without ON DELETE CASCADE. This all runs inside the write transaction.
    db.execute(R"sql(
        DELETE FROM event_log;
        DELETE FROM ship_cargo;
        DELETE FROM freight_program_reports;
        DELETE FROM freight_transfer_receipts;
        DELETE FROM freight_shipment_manifest;
        DELETE FROM freight_shipments;
        DELETE FROM freight_programs;
        DELETE FROM survey_program_reports;
        DELETE FROM survey_program_receipts;
        DELETE FROM survey_program_targets;
        DELETE FROM survey_programs;
        DELETE FROM survey_teams;
        DELETE FROM appointments;
        DELETE FROM ships;
        DELETE FROM fleet_order_queue;
        DELETE FROM fleets;
        DELETE FROM shipyard_orders;
        DELETE FROM ship_class_installs;
        DELETE FROM ship_component_material_costs;
        DELETE FROM ship_classes;
        DELETE FROM ship_components;
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
    insertCounter("next_ship_component_id", ids.nextShipComponentId);
    insertCounter("next_shipyard_order_id", ids.nextShipyardOrderId);
    insertCounter("next_ship_id", ids.nextShipId);
    insertCounter("next_fleet_id", ids.nextFleetId);
    insertCounter("next_event_id", ids.nextEventId);
    insertCounter("next_survey_program_id", ids.nextSurveyProgramId);
    insertCounter("next_survey_team_id", ids.nextSurveyTeamId);
    insertCounter("next_freight_program_id", ids.nextFreightProgramId);
}

void saveStarSystems(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO star_systems(id, name, ordinal) VALUES (?, ?, ?);"};
    for (std::size_t ordinal = 0; ordinal < state.starSystems.size(); ++ordinal) {
        const StarSystem& system = state.starSystems.at(ordinal);
        stmt.bindInt64(1, idValue(system.id));
        stmt.bindText(2, system.name);
        stmt.bindInt64(3, checkedOrdinal(ordinal, "star_systems"));
        stmt.execute();
        reuse(stmt);
    }
}

void saveInstitutions(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO institutions(id, name, institution_type, ordinal) VALUES (?, ?, ?, ?);"};
    for (std::size_t ordinal = 0; ordinal < state.institutions.size(); ++ordinal) {
        const Institution& institution = state.institutions.at(ordinal);
        stmt.bindInt64(1, idValue(institution.id));
        stmt.bindText(2, institution.name);
        stmt.bindInt64(3, enumValue(institution.type));
        stmt.bindInt64(4, checkedOrdinal(ordinal, "institutions"));
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
            commendations, controversies, survey_planning_approach, ordinal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.people.size(); ++ordinal) {
        const Person& person = state.people.at(ordinal);
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
        stmt.bindInt64(17, enumValue(person.surveyPlanningApproach));
        stmt.bindInt64(18, checkedOrdinal(ordinal, "people"));
        stmt.execute();
        reuse(stmt);
    }
}

void saveAppointments(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO appointments(ordinal, role, scope_type, scope_id, person_id, appointed_day)
        VALUES (?, ?, ?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.appointments.size(); ++ordinal) {
        const Appointment& appointment = state.appointments.at(ordinal);
        stmt.bindInt64(1, checkedOrdinal(ordinal, "appointments"));
        stmt.bindInt64(2, enumValue(appointment.role));
        stmt.bindInt64(3, enumValue(appointment.scopeType));
        stmt.bindInt64(4, appointment.scopeId);
        stmt.bindInt64(5, idValue(appointment.personId));
        stmt.bindInt64(6, appointment.appointedDay);
        stmt.execute();
        reuse(stmt);
    }
}

void saveBodies(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO bodies(
            id, system_id, name, body_type, strategic_zone, parent_body_id,
            orbital_radius_km, orbital_period_days, phase_radians, display_radius, x, y, ordinal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    for (std::size_t ordinal = 0; ordinal < state.bodies.size(); ++ordinal) {
        const Body& body = state.bodies.at(ordinal);
        stmt.bindInt64(1, idValue(body.id));
        stmt.bindInt64(2, idValue(body.systemId));
        stmt.bindText(3, body.name);
        stmt.bindInt64(4, enumValue(body.type));
        stmt.bindInt64(5, enumValue(body.strategicZone));
        bindOptionalId(stmt, 6, body.parentBodyId);
        stmt.bindDouble(7, body.orbitalRadiusKm);
        stmt.bindDouble(8, body.orbitalPeriodDays);
        stmt.bindDouble(9, body.phaseRadians);
        stmt.bindDouble(10, body.displayRadius);
        stmt.bindDouble(11, body.x);
        stmt.bindDouble(12, body.y);
        stmt.bindInt64(13, checkedOrdinal(ordinal, "bodies"));
        stmt.execute();
        reuse(stmt);
    }
}

void saveColonies(Database& db, const GameState& state) {
    Statement colonyStmt{db, R"sql(
        INSERT INTO colonies(
            id, body_id, name, owner_institution_id, mines, processor_capacity,
            shipyard_capacity, processing_policy, ordinal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement mineralStmt{db, "INSERT INTO colony_minerals(colony_id, mineral, amount) VALUES (?, ?, ?);"};
    Statement materialStmt{db, "INSERT INTO colony_materials(colony_id, material, amount) VALUES (?, ?, ?);"};
    Statement allocationStmt{db, R"sql(
        INSERT INTO colony_processing_allocations(colony_id, ordinal, material, weight)
        VALUES (?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.colonies.size(); ++ordinal) {
        const Colony& colony = state.colonies.at(ordinal);
        colonyStmt.bindInt64(1, idValue(colony.id));
        colonyStmt.bindInt64(2, idValue(colony.bodyId));
        colonyStmt.bindText(3, colony.name);
        bindOptionalId(colonyStmt, 4, colony.ownerInstitutionId);
        colonyStmt.bindDouble(5, colony.mines);
        colonyStmt.bindDouble(6, colony.processorCapacity);
        colonyStmt.bindDouble(7, colony.shipyardCapacity);
        colonyStmt.bindInt64(8, enumValue(colony.processingPolicy));
        colonyStmt.bindInt64(9, checkedOrdinal(ordinal, "colonies"));
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
            allocationStmt.bindInt64(2, checkedOrdinal(ordinal, "colony_processing_allocations"));
            allocationStmt.bindInt64(3, enumValue(allocation.material));
            allocationStmt.bindDouble(4, allocation.weight);
            allocationStmt.execute();
            reuse(allocationStmt);
        }
    }
}

void saveMineralDeposits(Database& db, const GameState& state) {
    Statement stmt{db, "INSERT INTO mineral_deposits(body_id, mineral, remaining, accessibility, confidence, ordinal) VALUES (?, ?, ?, ?, ?, ?);"};
    for (std::size_t ordinal = 0; ordinal < state.mineralDeposits.size(); ++ordinal) {
        const MineralDeposit& deposit = state.mineralDeposits.at(ordinal);
        stmt.bindInt64(1, idValue(deposit.bodyId));
        stmt.bindInt64(2, enumValue(deposit.mineral));
        stmt.bindDouble(3, deposit.remaining);
        stmt.bindDouble(4, deposit.accessibility);
        stmt.bindDouble(5, deposit.confidence);
        stmt.bindInt64(6, checkedOrdinal(ordinal, "mineral_deposits"));
        stmt.execute();
        reuse(stmt);
    }
}

void saveShipComponents(Database& db, const GameState& state) {
    Statement component{db, R"sql(
        INSERT INTO ship_components(id, ordinal, name, kind, mass, volume,
            internal_volume_capacity, power_generation, power_demand,
            propellant_capacity, survey_capability, build_points,
            cargo_capacity, cargo_handling_per_day)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement cost{db, "INSERT INTO ship_component_material_costs(component_id, material, amount) VALUES (?, ?, ?);"};
    for (std::size_t ordinal = 0; ordinal < state.shipComponents.size(); ++ordinal) {
        const ShipComponentDefinition& row = state.shipComponents.at(ordinal);
        component.bindInt64(1, idValue(row.id));
        component.bindInt64(2, checkedOrdinal(ordinal, "ship_components"));
        component.bindText(3, row.name);
        component.bindInt64(4, enumValue(row.kind));
        component.bindDouble(5, row.mass);
        component.bindDouble(6, row.volume);
        component.bindDouble(7, row.internalVolumeCapacity);
        component.bindDouble(8, row.powerGeneration);
        component.bindDouble(9, row.powerDemand);
        component.bindDouble(10, row.propellantCapacity);
        component.bindDouble(11, row.surveyCapability);
        component.bindDouble(12, row.buildPoints);
        component.bindDouble(13, row.cargoCapacity);
        component.bindDouble(14, row.cargoHandlingPerDay);
        component.execute();
        reuse(component);
        for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
            cost.bindInt64(1, idValue(row.id));
            cost.bindInt64(2, static_cast<std::int64_t>(material));
            cost.bindDouble(3, row.buildCost.amount.at(material));
            cost.execute();
            reuse(cost);
        }
    }
}

void saveShipClasses(Database& db, const GameState& state) {
    Statement classStmt{db, R"sql(
        INSERT INTO ship_classes(id, name, role, speed_km_per_day, ordinal, revision, based_on_class_id)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement installStmt{db, R"sql(
        INSERT INTO ship_class_installs(ship_class_id, ordinal, component_id, quantity)
        VALUES (?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.shipClasses.size(); ++ordinal) {
        const ShipClass& shipClass = state.shipClasses.at(ordinal);
        classStmt.bindInt64(1, idValue(shipClass.id));
        classStmt.bindText(2, shipClass.name);
        classStmt.bindInt64(3, enumValue(shipClass.role));
        classStmt.bindDouble(4, shipClass.speedKmPerDay);
        classStmt.bindInt64(5, checkedOrdinal(ordinal, "ship_classes"));
        classStmt.bindInt64(6, shipClass.revision);
        bindOptionalId(classStmt, 7, shipClass.basedOnClassId);
        classStmt.execute();
        reuse(classStmt);

        for (std::size_t index = 0; index < shipClass.components.size(); ++index) {
            const ShipComponentInstall& row = shipClass.components.at(index);
            installStmt.bindInt64(1, idValue(shipClass.id));
            installStmt.bindInt64(2, checkedOrdinal(index, "ship_class_installs"));
            installStmt.bindInt64(3, idValue(row.componentId));
            installStmt.bindInt64(4, row.quantity);
            installStmt.execute();
            reuse(installStmt);
        }
    }
}

void saveShipyardOrders(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO shipyard_orders(
            id, colony_id, ship_class_id, quantity_requested, quantity_completed,
            accumulated_build_points, status, ordinal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.shipyardOrders.size(); ++ordinal) {
        const ShipyardOrder& order = state.shipyardOrders.at(ordinal);
        stmt.bindInt64(1, idValue(order.id));
        stmt.bindInt64(2, idValue(order.colonyId));
        stmt.bindInt64(3, idValue(order.shipClassId));
        stmt.bindInt64(4, order.quantityRequested);
        stmt.bindInt64(5, order.quantityCompleted);
        stmt.bindDouble(6, order.accumulatedBuildPoints);
        stmt.bindInt64(7, enumValue(order.status));
        stmt.bindInt64(8, checkedOrdinal(ordinal, "shipyard_orders"));
        stmt.execute();
        reuse(stmt);
    }
}

void saveFleets(Database& db, const GameState& state) {
    Statement fleetStmt{db, R"sql(
        INSERT INTO fleets(
            id, name, owner_institution_id, current_body_id, destination_body_id,
            order_type, order_target_body_id, order_days_remaining,
            order_departure_body_id, order_departure_day, order_arrival_day,
            order_departure_x, order_departure_y, order_projected_arrival_x,
            order_projected_arrival_y, order_transit_distance_km,
            order_burn_acceleration_g, order_curve_control_x, order_curve_control_y,
            ordinal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement queueStmt{db, R"sql(
        INSERT INTO fleet_order_queue(fleet_id, ordinal, order_type, target_body_id)
        VALUES (?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.fleets.size(); ++ordinal) {
        const Fleet& fleet = state.fleets.at(ordinal);
        fleetStmt.bindInt64(1, idValue(fleet.id));
        fleetStmt.bindText(2, fleet.name);
        bindOptionalId(fleetStmt, 3, fleet.ownerInstitutionId);
        fleetStmt.bindInt64(4, idValue(fleet.currentBodyId));
        bindOptionalId(fleetStmt, 5, fleet.destinationBodyId);
        fleetStmt.bindInt64(6, enumValue(fleet.activeOrder.type));
        bindOptionalId(fleetStmt, 7, fleet.activeOrder.targetBodyId);
        fleetStmt.bindInt64(8, fleet.activeOrder.daysRemaining);
        bindOptionalId(fleetStmt, 9, fleet.activeOrder.departureBodyId);
        fleetStmt.bindInt64(10, fleet.activeOrder.departureDay);
        fleetStmt.bindInt64(11, fleet.activeOrder.arrivalDay);
        fleetStmt.bindDouble(12, fleet.activeOrder.departurePosition.x);
        fleetStmt.bindDouble(13, fleet.activeOrder.departurePosition.y);
        fleetStmt.bindDouble(14, fleet.activeOrder.projectedArrivalPosition.x);
        fleetStmt.bindDouble(15, fleet.activeOrder.projectedArrivalPosition.y);
        fleetStmt.bindDouble(16, fleet.activeOrder.transitDistanceKm);
        fleetStmt.bindDouble(17, fleet.activeOrder.burnAccelerationG);
        fleetStmt.bindDouble(18, fleet.activeOrder.routeCurveControlPoint.x);
        fleetStmt.bindDouble(19, fleet.activeOrder.routeCurveControlPoint.y);
        fleetStmt.bindInt64(20, checkedOrdinal(ordinal, "fleets"));
        fleetStmt.execute();
        reuse(fleetStmt);

        for (std::size_t i = 0; i < fleet.queuedOrders.size(); ++i) {
            const QueuedFleetOrder& queuedOrder = fleet.queuedOrders.at(i);
            if (!queuedOrder.targetBodyId.has_value()) {
                throw std::runtime_error{"queued fleet order is missing a target body"};
            }

            queueStmt.bindInt64(1, idValue(fleet.id));
            queueStmt.bindInt64(2, checkedOrdinal(i, "fleet_order_queue"));
            queueStmt.bindInt64(3, enumValue(queuedOrder.type));
            queueStmt.bindInt64(4, idValue(*queuedOrder.targetBodyId));
            queueStmt.execute();
            reuse(queueStmt);
        }
    }
}

void saveShips(Database& db, const GameState& state) {
    // Global Ship order and each Fleet's payment roster are independent. Derive
    // both ordinals from the validated snapshot without changing either vector.
    std::unordered_map<std::int64_t, std::int64_t> fleetOrdinals;
    for (const Fleet& fleet : state.fleets) {
        for (std::size_t ordinal = 0; ordinal < fleet.shipIds.size(); ++ordinal) {
            if (!fleetOrdinals.emplace(idValue(fleet.shipIds.at(ordinal)),
                    checkedOrdinal(ordinal, "fleet.shipIds")).second) {
                throw std::runtime_error{"Ship occurs in more than one fleet roster"};
            }
        }
    }
    Statement stmt{db, R"sql(
        INSERT INTO ships(id, ship_class_id, fleet_id, name, fuel, ordinal, fleet_ordinal)
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )sql"};
    for (std::size_t ordinal = 0; ordinal < state.ships.size(); ++ordinal) {
        const Ship& ship = state.ships.at(ordinal);
        const auto roster = fleetOrdinals.find(idValue(ship.id));
        if (roster == fleetOrdinals.end()) throw std::runtime_error{"Ship is missing from its fleet roster"};
        stmt.bindInt64(1, idValue(ship.id));
        stmt.bindInt64(2, idValue(ship.shipClassId));
        stmt.bindInt64(3, idValue(ship.fleetId));
        stmt.bindText(4, ship.name);
        stmt.bindDouble(5, ship.fuel);
        stmt.bindInt64(6, checkedOrdinal(ordinal, "ships"));
        stmt.bindInt64(7, roster->second);
        stmt.execute();
        reuse(stmt);
    }
}

void saveSurveyTeams(Database& db, const GameState& state) {
    Statement stmt{db, R"sql(
        INSERT INTO survey_teams(id, ordinal, name, location_kind, colony_id, fleet_id)
        VALUES (?, ?, ?, ?, ?, ?);
    )sql"};
    for (std::size_t ordinal = 0; ordinal < state.surveyTeams.size(); ++ordinal) {
        const SurveyTeam& team = state.surveyTeams.at(ordinal);
        stmt.bindInt64(1, idValue(team.id));
        stmt.bindInt64(2, checkedOrdinal(ordinal, "survey_teams"));
        stmt.bindText(3, team.name);
        stmt.bindInt64(4, enumValue(team.locationKind));
        bindOptionalId(stmt, 5, team.colonyId);
        bindOptionalId(stmt, 6, team.fleetId);
        stmt.execute();
        reuse(stmt);
    }
}

void saveSurveyPrograms(Database& db, const GameState& state) {
    Statement program{db, R"sql(
        INSERT INTO survey_programs(
            id, ordinal, name, home_colony_id, requested_fleet_id, requested_leader_id,
            requested_team_id, max_additional_propellant, home_stock_floor,
            return_contingency_fraction, created_day, charter_revision, lifecycle, closure,
            leased_fleet_id, leased_team_id, task, task_body_id, task_fleet_id,
            task_team_id, task_leader_id, task_approach, task_pass_number,
            work_days_completed, first_work_day, last_selection_reason, fuel_loaded, fuel_burned, total_work_days,
            next_report_day, report_start_day, reported_fuel_loaded, reported_fuel_burned,
            reported_work_days, reported_visits, issue_signature, issue_message, issue_acknowledged,
            pending_home_colony_id
        ) VALUES (
            ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
        );
    )sql"};
    Statement target{db, R"sql(
        INSERT INTO survey_program_targets(program_id, ordinal, body_id, priority, requested_passes)
        VALUES (?, ?, ?, ?, ?);
    )sql"};
    Statement receipt{db, R"sql(
        INSERT INTO survey_program_receipts(
            program_id, ordinal, body_id, pass_number, fleet_id, team_id, leader_id, approach,
            first_work_day, completed_day, work_days, deposits_improved,
            average_confidence_before, average_confidence_after
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement report{db, R"sql(
        INSERT INTO survey_program_reports(
            program_id, ordinal, start_day, end_day, is_ninety_day_review, charter_revision,
            leader_id, approach, visits_completed, work_days, fuel_loaded, fuel_burned,
            fleet_id, team_id, fleet_body_id, waiting_reason
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (std::size_t ordinal = 0; ordinal < state.surveyPrograms.size(); ++ordinal) {
        const SurveyProgram& item = state.surveyPrograms.at(ordinal);
        int p = 1;
        program.bindInt64(p++, idValue(item.id));
        program.bindInt64(p++, checkedOrdinal(ordinal, "survey_programs"));
        program.bindText(p++, item.charter.name);
        program.bindInt64(p++, idValue(item.charter.homeColonyId));
        bindOptionalId(program, p++, item.charter.requestedFleetId);
        bindOptionalId(program, p++, item.charter.requestedLeaderId);
        bindOptionalId(program, p++, item.charter.requestedTeamId);
        bindOptionalDouble(program, p++, item.charter.policy.maxAdditionalPropellant);
        program.bindDouble(p++, item.charter.policy.homeStockFloor);
        program.bindDouble(p++, item.charter.policy.returnContingencyFraction);
        program.bindInt64(p++, item.createdDay);
        program.bindInt64(p++, item.charterRevision);
        program.bindInt64(p++, enumValue(item.lifecycle));
        program.bindInt64(p++, enumValue(item.closure));
        bindOptionalId(program, p++, item.leasedFleetId);
        bindOptionalId(program, p++, item.leasedTeamId);
        program.bindInt64(p++, enumValue(item.task));
        bindOptionalId(program, p++, item.taskBodyId);
        bindOptionalId(program, p++, item.taskFleetId);
        bindOptionalId(program, p++, item.taskTeamId);
        bindOptionalId(program, p++, item.taskLeaderId);
        program.bindInt64(p++, enumValue(item.taskApproach));
        program.bindInt64(p++, item.taskPassNumber);
        program.bindInt64(p++, item.workDaysCompleted);
        program.bindInt64(p++, item.firstWorkDay);
        program.bindText(p++, item.lastSelectionReason);
        program.bindDouble(p++, item.fuelLoaded);
        program.bindDouble(p++, item.fuelBurned);
        program.bindInt64(p++, item.totalWorkDays);
        program.bindInt64(p++, item.nextReportDay);
        program.bindInt64(p++, item.reportStartDay);
        program.bindDouble(p++, item.reportedFuelLoaded);
        program.bindDouble(p++, item.reportedFuelBurned);
        program.bindInt64(p++, item.reportedWorkDays);
        program.bindInt64(p++, item.reportedVisits);
        program.bindText(p++, item.issue.signature);
        program.bindText(p++, item.issue.message);
        program.bindInt64(p++, item.issue.acknowledged ? 1 : 0);
        bindOptionalId(program, p++, item.pendingHomeColonyId);
        program.execute();
        reuse(program);

        for (std::size_t child = 0; child < item.charter.targets.size(); ++child) {
            const SurveyProgramTarget& value = item.charter.targets.at(child);
            target.bindInt64(1, idValue(item.id));
            target.bindInt64(2, checkedOrdinal(child, "survey_program_targets"));
            target.bindInt64(3, idValue(value.bodyId));
            target.bindInt64(4, value.priority);
            target.bindInt64(5, value.requestedPasses);
            target.execute();
            reuse(target);
        }
        for (std::size_t child = 0; child < item.receipts.size(); ++child) {
            const SurveyVisitReceipt& value = item.receipts.at(child);
            int r = 1;
            receipt.bindInt64(r++, idValue(item.id));
            receipt.bindInt64(r++, checkedOrdinal(child, "survey_program_receipts"));
            receipt.bindInt64(r++, idValue(value.bodyId));
            receipt.bindInt64(r++, value.passNumber);
            receipt.bindInt64(r++, idValue(value.fleetId));
            receipt.bindInt64(r++, idValue(value.teamId));
            bindOptionalId(receipt, r++, value.leaderId);
            receipt.bindInt64(r++, enumValue(value.approach));
            receipt.bindInt64(r++, value.firstWorkDay);
            receipt.bindInt64(r++, value.completedDay);
            receipt.bindInt64(r++, value.workDays);
            receipt.bindInt64(r++, value.depositsImproved);
            receipt.bindDouble(r++, value.averageConfidenceBefore);
            receipt.bindDouble(r++, value.averageConfidenceAfter);
            receipt.execute();
            reuse(receipt);
        }
        for (std::size_t child = 0; child < item.reports.size(); ++child) {
            const SurveyProgramReport& value = item.reports.at(child);
            int r = 1;
            report.bindInt64(r++, idValue(item.id));
            report.bindInt64(r++, checkedOrdinal(child, "survey_program_reports"));
            report.bindInt64(r++, value.startDay);
            report.bindInt64(r++, value.endDay);
            report.bindInt64(r++, value.isNinetyDayReview ? 1 : 0);
            report.bindInt64(r++, value.charterRevision);
            bindOptionalId(report, r++, value.leaderId);
            report.bindInt64(r++, enumValue(value.approach));
            report.bindInt64(r++, value.visitsCompleted);
            report.bindInt64(r++, value.workDays);
            report.bindDouble(r++, value.fuelLoaded);
            report.bindDouble(r++, value.fuelBurned);
            bindOptionalId(report, r++, value.fleetId);
            bindOptionalId(report, r++, value.teamId);
            bindOptionalId(report, r++, value.fleetBodyId);
            report.bindText(r++, value.waitingReason);
            report.execute();
            reuse(report);
        }
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
    // Check declared foreign keys before mapping rows into GameState vectors.
    // This is not SQLite's full integrity check and does not cover references
    // represented only by application-level invariants.
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
    ids.nextShipComponentId = loadCounter(db, "next_ship_component_id");
    ids.nextShipyardOrderId = loadCounter(db, "next_shipyard_order_id");
    ids.nextShipId = loadCounter(db, "next_ship_id");
    ids.nextFleetId = loadCounter(db, "next_fleet_id");
    ids.nextEventId = loadCounter(db, "next_event_id");
    ids.nextSurveyProgramId = loadCounter(db, "next_survey_program_id");
    ids.nextSurveyTeamId = loadCounter(db, "next_survey_team_id");
    ids.nextFreightProgramId = loadCounter(db, "next_freight_program_id");
}

void loadStarSystems(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, name, ordinal FROM star_systems ORDER BY ordinal;"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 2, nextOrdinal, "star_systems");
        state.starSystems.push_back(StarSystem{
            .id = StarSystemId{stmt.columnInt64(0)},
            .name = stmt.columnText(1)
        });
    }
}

void loadInstitutions(Database& db, GameState& state) {
    Statement stmt{db, "SELECT id, name, institution_type, ordinal FROM institutions ORDER BY ordinal;"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 3, nextOrdinal, "institutions");
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
               commendations, controversies, survey_planning_approach, ordinal
        FROM people
        ORDER BY ordinal;
    )sql"};

    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 17, nextOrdinal, "people");
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
            },
            .surveyPlanningApproach = enumFromValue<SurveyPlanningApproach>(stmt.columnInt64(16))
        });
    }
}

void loadAppointments(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT role, scope_type, scope_id, person_id, appointed_day, ordinal
        FROM appointments
        ORDER BY ordinal;
    )sql"};

    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 5, nextOrdinal, "appointments");
        state.appointments.push_back(Appointment{
            .role = enumFromValue<AppointmentRole>(stmt.columnInt64(0)),
            .scopeType = enumFromValue<AppointmentScopeType>(stmt.columnInt64(1)),
            .scopeId = stmt.columnInt64(2),
            .personId = PersonId{stmt.columnInt64(3)},
            .appointedDay = stmt.columnInt64(4)
        });
    }
}

void loadBodies(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, system_id, name, body_type, strategic_zone, parent_body_id,
               orbital_radius_km, orbital_period_days, phase_radians, display_radius, x, y, ordinal
        FROM bodies
        ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 12, nextOrdinal, "bodies");
        state.bodies.push_back(Body{
            .id = BodyId{stmt.columnInt64(0)},
            .systemId = StarSystemId{stmt.columnInt64(1)},
            .name = stmt.columnText(2),
            .type = enumFromValue<BodyType>(stmt.columnInt64(3)),
            .strategicZone = enumFromValue<StrategicZone>(stmt.columnInt64(4)),
            .parentBodyId = optionalIdFromColumn<BodyId>(stmt, 5),
            .orbitalRadiusKm = stmt.columnDouble(6),
            .orbitalPeriodDays = stmt.columnDouble(7),
            .phaseRadians = stmt.columnDouble(8),
            .displayRadius = stmt.columnDouble(9),
            .x = stmt.columnDouble(10),
            .y = stmt.columnDouble(11)
        });
    }
}

void loadColonies(Database& db, GameState& state) {
    // Resource arrays begin at zero and child rows fill individual entries.
    // Missing entries currently remain zero; this reader does not enforce the
    // dense row set written by saveColonies. Domain validation sees only values.
    Statement colonies{db, R"sql(
        SELECT id, body_id, name, owner_institution_id, mines, processor_capacity,
               shipyard_capacity, processing_policy, ordinal
        FROM colonies
        ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (colonies.step()) {
        requireNextOrdinal(colonies, 8, nextOrdinal, "colonies");
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

    std::unordered_map<std::int64_t, std::size_t> mineralRows;
    Statement minerals{db, "SELECT colony_id, mineral, amount FROM colony_minerals ORDER BY colony_id, mineral;"};
    while (minerals.step()) {
        const ColonyId colonyId{minerals.columnInt64(0)};
        Colony* colony = findById(state.colonies, colonyId);
        if (colony == nullptr) {
            throw std::runtime_error{"colony_minerals references a missing colony"};
        }
        colony->stockpile.set(enumFromValue<Mineral>(minerals.columnInt64(1)), minerals.columnDouble(2));
        ++mineralRows[colonyId.value];
    }

    std::unordered_map<std::int64_t, std::size_t> materialRows;
    Statement materials{db, "SELECT colony_id, material, amount FROM colony_materials ORDER BY colony_id, material;"};
    while (materials.step()) {
        const ColonyId colonyId{materials.columnInt64(0)};
        Colony* colony = findById(state.colonies, colonyId);
        if (colony == nullptr) {
            throw std::runtime_error{"colony_materials references a missing colony"};
        }
        colony->processedStockpile.set(enumFromValue<ProcessedMaterial>(materials.columnInt64(1)), materials.columnDouble(2));
        ++materialRows[colonyId.value];
    }
    for (const Colony& colony : state.colonies) {
        if (mineralRows[colony.id.value] != mineralCount() ||
            materialRows[colony.id.value] != processedMaterialCount()) {
            throw std::runtime_error{"v14 colony resource rows must be complete"};
        }
    }

    Statement allocations{db, R"sql(
        SELECT colony_id, material, weight, ordinal
        FROM colony_processing_allocations
        ORDER BY colony_id, ordinal;
    )sql"};
    std::int64_t allocationParent = 0;
    std::int64_t nextAllocationOrdinal = 0;
    while (allocations.step()) {
        const ColonyId colonyId{allocations.columnInt64(0)};
        if (colonyId.value != allocationParent) {
            allocationParent = colonyId.value;
            nextAllocationOrdinal = 0;
        }
        requireNextOrdinal(allocations, 3, nextAllocationOrdinal, "colony_processing_allocations");
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
    Statement stmt{db, "SELECT body_id, mineral, remaining, accessibility, confidence, ordinal FROM mineral_deposits ORDER BY ordinal;"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 5, nextOrdinal, "mineral_deposits");
        state.mineralDeposits.push_back(MineralDeposit{
            .bodyId = BodyId{stmt.columnInt64(0)},
            .mineral = enumFromValue<Mineral>(stmt.columnInt64(1)),
            .remaining = stmt.columnDouble(2),
            .accessibility = stmt.columnDouble(3),
            .confidence = stmt.columnDouble(4)
        });
    }
}

void loadShipComponentsAndClasses(Database& db, GameState& state) {
    Statement components{db, R"sql(
        SELECT id, name, kind, mass, volume, internal_volume_capacity,
               power_generation, power_demand, propellant_capacity,
               survey_capability, build_points, ordinal, cargo_capacity, cargo_handling_per_day
        FROM ship_components ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (components.step()) {
        requireNextOrdinal(components, 11, nextOrdinal, "ship_components");
        state.shipComponents.push_back(ShipComponentDefinition{
            .id = ShipComponentId{components.columnInt64(0)}, .name = components.columnText(1),
            .kind = enumFromValue<ShipComponentKind>(components.columnInt64(2)),
            .mass = components.columnDouble(3), .volume = components.columnDouble(4),
            .internalVolumeCapacity = components.columnDouble(5),
            .powerGeneration = components.columnDouble(6), .powerDemand = components.columnDouble(7),
            .propellantCapacity = components.columnDouble(8), .surveyCapability = components.columnDouble(9),
            .cargoCapacity = components.columnDoubleStrict(12),
            .cargoHandlingPerDay = components.columnDoubleStrict(13),
            .buildCost = ProcessedMaterialSet{}, .buildPoints = components.columnDouble(10)
        });
    }
    std::unordered_map<std::int64_t, std::size_t> costRows;
    Statement costs{db, "SELECT component_id, material, amount FROM ship_component_material_costs ORDER BY component_id, material;"};
    while (costs.step()) {
        const ShipComponentId id{costs.columnInt64(0)};
        ShipComponentDefinition* row = findById(state.shipComponents, id);
        if (row == nullptr) throw std::runtime_error{"component cost references missing definition"};
        row->buildCost.set(enumFromValue<ProcessedMaterial>(costs.columnInt64(1)), costs.columnDouble(2));
        ++costRows[id.value];
    }
    for (const ShipComponentDefinition& row : state.shipComponents) {
        if (costRows[row.id.value] != processedMaterialCount()) {
            throw std::runtime_error{"v14 component cost rows must be complete"};
        }
    }
    Statement classes{db, R"sql(
        SELECT id, name, role, speed_km_per_day, revision, based_on_class_id, ordinal
        FROM ship_classes ORDER BY ordinal;
    )sql"};
    nextOrdinal = 0;
    while (classes.step()) {
        requireNextOrdinal(classes, 6, nextOrdinal, "ship_classes");
        state.shipClasses.push_back(ShipClass{
            .id = ShipClassId{classes.columnInt64(0)}, .name = classes.columnText(1),
            .revision = checkedIntFromSql(classes.columnInt64(4), "ship_classes.revision"),
            .role = enumFromValue<ShipRole>(classes.columnInt64(2)),
            .basedOnClassId = optionalIdFromColumn<ShipClassId>(classes, 5),
            .components = {},
            .speedKmPerDay = classes.columnDouble(3)
        });
    }
    Statement installs{db, R"sql(
        SELECT ship_class_id, ordinal, component_id, quantity
        FROM ship_class_installs ORDER BY ship_class_id, ordinal;
    )sql"};
    std::int64_t currentClass = 0;
    nextOrdinal = 0;
    while (installs.step()) {
        const ShipClassId id{installs.columnInt64(0)};
        if (id.value != currentClass) { currentClass = id.value; nextOrdinal = 0; }
        requireNextOrdinal(installs, 1, nextOrdinal, "ship_class_installs");
        ShipClass* row = findById(state.shipClasses, id);
        if (row == nullptr) throw std::runtime_error{"installation references missing class"};
        row->components.push_back(ShipComponentInstall{
            .componentId = ShipComponentId{installs.columnInt64(2)},
            .quantity = checkedIntFromSql(installs.columnInt64(3), "ship_class_installs.quantity")
        });
    }
}

void loadShipyardOrders(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, colony_id, ship_class_id, quantity_requested, quantity_completed,
               accumulated_build_points, status, ordinal
        FROM shipyard_orders
        ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 7, nextOrdinal, "shipyard_orders");
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
               order_type, order_target_body_id, order_days_remaining,
               order_departure_body_id, order_departure_day, order_arrival_day,
               order_departure_x, order_departure_y, order_projected_arrival_x,
               order_projected_arrival_y, order_transit_distance_km,
               order_burn_acceleration_g, order_curve_control_x, order_curve_control_y,
               ordinal
        FROM fleets
        ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 19, nextOrdinal, "fleets");
        state.fleets.push_back(Fleet{
            .id = FleetId{stmt.columnInt64(0)},
            .name = stmt.columnText(1),
            .currentBodyId = BodyId{stmt.columnInt64(3)},
            .destinationBodyId = optionalIdFromColumn<BodyId>(stmt, 4),
            .shipIds = {},
            .activeOrder = FleetOrder{
                .type = enumFromValue<FleetOrderType>(stmt.columnInt64(5)),
                .targetBodyId = optionalIdFromColumn<BodyId>(stmt, 6),
                .daysRemaining = checkedIntFromSql(stmt.columnInt64(7), "fleets.order_days_remaining"),
                .departureBodyId = optionalIdFromColumn<BodyId>(stmt, 8),
                .departureDay = stmt.columnInt64(9),
                .arrivalDay = stmt.columnInt64(10),
                .departurePosition = MapPosition{.x = stmt.columnDouble(11), .y = stmt.columnDouble(12)},
                .projectedArrivalPosition = MapPosition{.x = stmt.columnDouble(13), .y = stmt.columnDouble(14)},
                .transitDistanceKm = stmt.columnDouble(15),
                .burnAccelerationG = stmt.columnDouble(16),
                .routeCurveControlPoint = MapPosition{.x = stmt.columnDouble(17), .y = stmt.columnDouble(18)}
            },
            .queuedOrders = {},
            .ownerInstitutionId = optionalIdFromColumn<InstitutionId>(stmt, 2)
        });
    }

    Statement queue{db, R"sql(
        SELECT fleet_id, order_type, target_body_id, ordinal
        FROM fleet_order_queue
        ORDER BY fleet_id, ordinal;
    )sql"};
    std::int64_t queueParent = 0;
    std::int64_t nextQueueOrdinal = 0;
    while (queue.step()) {
        const FleetId fleetId{queue.columnInt64(0)};
        if (fleetId.value != queueParent) {
            queueParent = fleetId.value;
            nextQueueOrdinal = 0;
        }
        requireNextOrdinal(queue, 3, nextQueueOrdinal, "fleet_order_queue");
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
    Statement stmt{db, "SELECT id, ship_class_id, fleet_id, name, fuel, ordinal FROM ships ORDER BY ordinal;"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 5, nextOrdinal, "ships");
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
        state.ships.push_back(std::move(ship));
    }
    // Ship order in GameState is independent from fleet roster order used
    // for fuel payment. Reconstruct the latter from a second explicit sort.
    Statement roster{db, R"sql(
            SELECT fleet_id, id, fleet_ordinal
            FROM ships
            ORDER BY fleet_id, fleet_ordinal;
    )sql"};
    std::int64_t rosterParent = 0;
    std::int64_t nextRosterOrdinal = 0;
    while (roster.step()) {
        const FleetId fleetId{roster.columnInt64(0)};
        if (fleetId.value != rosterParent) {
            rosterParent = fleetId.value;
            nextRosterOrdinal = 0;
        }
        requireNextOrdinal(roster, 2, nextRosterOrdinal, "ships.fleet_ordinal");
        Fleet* fleet = findById(state.fleets, fleetId);
        if (fleet == nullptr) throw std::runtime_error{"ships references a missing fleet"};
        fleet->shipIds.push_back(ShipId{roster.columnInt64(1)});
    }
}

void loadSurveyTeams(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, name, location_kind, colony_id, fleet_id, ordinal
        FROM survey_teams ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 5, nextOrdinal, "survey_teams");
        state.surveyTeams.push_back(SurveyTeam{
            .id = SurveyTeamId{stmt.columnInt64Strict(0)},
            .name = stmt.columnText(1),
            .locationKind = enumFromValue<SurveyTeamLocationKind>(stmt.columnInt64Strict(2)),
            .colonyId = optionalStrictIdFromColumn<ColonyId>(stmt, 3),
            .fleetId = optionalStrictIdFromColumn<FleetId>(stmt, 4)
        });
    }
}

void loadSurveyPrograms(Database& db, GameState& state) {
    Statement stmt{db, R"sql(
        SELECT id, name, home_colony_id, requested_fleet_id, requested_leader_id,
               requested_team_id, max_additional_propellant, home_stock_floor,
               return_contingency_fraction, created_day, charter_revision, lifecycle, closure,
               leased_fleet_id, leased_team_id, task, task_body_id, task_fleet_id,
               task_team_id, task_leader_id, task_approach, task_pass_number,
               work_days_completed, first_work_day, last_selection_reason, fuel_loaded, fuel_burned, total_work_days,
               next_report_day, report_start_day, reported_fuel_loaded, reported_fuel_burned,
               reported_work_days, reported_visits, issue_signature, issue_message,
               issue_acknowledged, pending_home_colony_id, ordinal
        FROM survey_programs ORDER BY ordinal;
    )sql"};
    std::int64_t nextOrdinal = 0;
    while (stmt.step()) {
        requireNextOrdinal(stmt, 38, nextOrdinal, "survey_programs");
        state.surveyPrograms.push_back(SurveyProgram{
            .id = SurveyProgramId{stmt.columnInt64Strict(0)},
            .charter = SurveyProgramCharter{
                .name = stmt.columnText(1),
                .homeColonyId = ColonyId{stmt.columnInt64Strict(2)},
                .requestedFleetId = optionalStrictIdFromColumn<FleetId>(stmt, 3),
                .requestedLeaderId = optionalStrictIdFromColumn<PersonId>(stmt, 4),
                .requestedTeamId = optionalStrictIdFromColumn<SurveyTeamId>(stmt, 5),
                .targets = {},
                .policy = SurveyProgramPolicy{
                    .maxAdditionalPropellant = stmt.columnIsNull(6)
                        ? std::nullopt : std::optional<double>{stmt.columnDoubleStrict(6)},
                    .homeStockFloor = stmt.columnDoubleStrict(7),
                    .returnContingencyFraction = stmt.columnDoubleStrict(8)
                }
            },
            .pendingHomeColonyId = optionalStrictIdFromColumn<ColonyId>(stmt, 37),
            .createdDay = stmt.columnInt64Strict(9),
            .charterRevision = checkedIntFromSql(stmt.columnInt64Strict(10), "survey_programs.charter_revision"),
            .lifecycle = enumFromValue<SurveyProgramLifecycle>(stmt.columnInt64Strict(11)),
            .closure = enumFromValue<SurveyProgramClosure>(stmt.columnInt64Strict(12)),
            .leasedFleetId = optionalStrictIdFromColumn<FleetId>(stmt, 13),
            .leasedTeamId = optionalStrictIdFromColumn<SurveyTeamId>(stmt, 14),
            .task = enumFromValue<SurveyProgramTask>(stmt.columnInt64Strict(15)),
            .taskBodyId = optionalStrictIdFromColumn<BodyId>(stmt, 16),
            .taskFleetId = optionalStrictIdFromColumn<FleetId>(stmt, 17),
            .taskTeamId = optionalStrictIdFromColumn<SurveyTeamId>(stmt, 18),
            .taskLeaderId = optionalStrictIdFromColumn<PersonId>(stmt, 19),
            .taskApproach = enumFromValue<SurveyPlanningApproach>(stmt.columnInt64Strict(20)),
            .taskPassNumber = checkedIntFromSql(stmt.columnInt64Strict(21), "survey_programs.task_pass_number"),
            .workDaysCompleted = checkedIntFromSql(stmt.columnInt64Strict(22), "survey_programs.work_days_completed"),
            .firstWorkDay = stmt.columnInt64Strict(23),
            .lastSelectionReason = stmt.columnText(24),
            .receipts = {},
            .fuelLoaded = stmt.columnDoubleStrict(25),
            .fuelBurned = stmt.columnDoubleStrict(26),
            .totalWorkDays = stmt.columnInt64Strict(27),
            .nextReportDay = stmt.columnInt64Strict(28),
            .reportStartDay = stmt.columnInt64Strict(29),
            .reportedFuelLoaded = stmt.columnDoubleStrict(30),
            .reportedFuelBurned = stmt.columnDoubleStrict(31),
            .reportedWorkDays = stmt.columnInt64Strict(32),
            .reportedVisits = checkedIntFromSql(stmt.columnInt64Strict(33), "survey_programs.reported_visits"),
            .reports = {},
            .issue = SurveyProgramIssue{
                .signature = stmt.columnText(34),
                .message = stmt.columnText(35),
                .acknowledged = checkedBoolFromSql(stmt, 36, "survey_programs.issue_acknowledged")
            }
        });
    }

    Statement targets{db, R"sql(
        SELECT program_id, body_id, priority, requested_passes, ordinal
        FROM survey_program_targets ORDER BY program_id, ordinal;
    )sql"};
    std::unordered_map<std::int64_t, std::int64_t> nextTargetOrdinal;
    while (targets.step()) {
        const SurveyProgramId programId{targets.columnInt64Strict(0)};
        requireNextOrdinal(targets, 4, nextTargetOrdinal[idValue(programId)], "survey_program_targets");
        SurveyProgram* item = findById(state.surveyPrograms, programId);
        if (item == nullptr) throw std::runtime_error{"Save target references an unknown survey program"};
        item->charter.targets.push_back(SurveyProgramTarget{
            .bodyId = BodyId{targets.columnInt64Strict(1)},
            .priority = checkedIntFromSql(targets.columnInt64Strict(2), "survey_program_targets.priority"),
            .requestedPasses = checkedIntFromSql(targets.columnInt64Strict(3), "survey_program_targets.requested_passes")
        });
    }

    Statement receipts{db, R"sql(
        SELECT program_id, body_id, pass_number, fleet_id, team_id, leader_id, approach,
               first_work_day, completed_day, work_days, deposits_improved,
               average_confidence_before, average_confidence_after, ordinal
        FROM survey_program_receipts ORDER BY program_id, ordinal;
    )sql"};
    std::unordered_map<std::int64_t, std::int64_t> nextReceiptOrdinal;
    while (receipts.step()) {
        const SurveyProgramId programId{receipts.columnInt64Strict(0)};
        requireNextOrdinal(receipts, 13, nextReceiptOrdinal[idValue(programId)], "survey_program_receipts");
        SurveyProgram* item = findById(state.surveyPrograms, programId);
        if (item == nullptr) throw std::runtime_error{"Save receipt references an unknown survey program"};
        item->receipts.push_back(SurveyVisitReceipt{
            .bodyId = BodyId{receipts.columnInt64Strict(1)},
            .passNumber = checkedIntFromSql(receipts.columnInt64Strict(2), "survey_program_receipts.pass_number"),
            .fleetId = FleetId{receipts.columnInt64Strict(3)},
            .teamId = SurveyTeamId{receipts.columnInt64Strict(4)},
            .leaderId = optionalStrictIdFromColumn<PersonId>(receipts, 5),
            .approach = enumFromValue<SurveyPlanningApproach>(receipts.columnInt64Strict(6)),
            .firstWorkDay = receipts.columnInt64Strict(7),
            .completedDay = receipts.columnInt64Strict(8),
            .workDays = checkedIntFromSql(receipts.columnInt64Strict(9), "survey_program_receipts.work_days"),
            .depositsImproved = checkedIntFromSql(receipts.columnInt64Strict(10), "survey_program_receipts.deposits_improved"),
            .averageConfidenceBefore = receipts.columnDoubleStrict(11),
            .averageConfidenceAfter = receipts.columnDoubleStrict(12)
        });
    }

    Statement reports{db, R"sql(
        SELECT program_id, start_day, end_day, is_ninety_day_review, charter_revision,
               leader_id, approach, visits_completed, work_days, fuel_loaded, fuel_burned,
               fleet_id, team_id, fleet_body_id, waiting_reason, ordinal
        FROM survey_program_reports ORDER BY program_id, ordinal;
    )sql"};
    std::unordered_map<std::int64_t, std::int64_t> nextReportOrdinal;
    while (reports.step()) {
        const SurveyProgramId programId{reports.columnInt64Strict(0)};
        requireNextOrdinal(reports, 15, nextReportOrdinal[idValue(programId)], "survey_program_reports");
        SurveyProgram* item = findById(state.surveyPrograms, programId);
        if (item == nullptr) throw std::runtime_error{"Save report references an unknown survey program"};
        item->reports.push_back(SurveyProgramReport{
            .startDay = reports.columnInt64Strict(1),
            .endDay = reports.columnInt64Strict(2),
            .isNinetyDayReview = checkedBoolFromSql(reports, 3, "survey_program_reports.is_ninety_day_review"),
            .charterRevision = checkedIntFromSql(reports.columnInt64Strict(4), "survey_program_reports.charter_revision"),
            .leaderId = optionalStrictIdFromColumn<PersonId>(reports, 5),
            .approach = enumFromValue<SurveyPlanningApproach>(reports.columnInt64Strict(6)),
            .visitsCompleted = checkedIntFromSql(reports.columnInt64Strict(7), "survey_program_reports.visits_completed"),
            .workDays = reports.columnInt64Strict(8),
            .fuelLoaded = reports.columnDoubleStrict(9),
            .fuelBurned = reports.columnDoubleStrict(10),
            .fleetId = optionalStrictIdFromColumn<FleetId>(reports, 11),
            .teamId = optionalStrictIdFromColumn<SurveyTeamId>(reports, 12),
            .fleetBodyId = optionalStrictIdFromColumn<BodyId>(reports, 13),
            .waitingReason = reports.columnText(14)
        });
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

[[nodiscard]] GameState readSnapshot(Database& db) {
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
    loadShipComponentsAndClasses(db, state);
    loadShipyardOrders(db, state);
    loadFleets(db, state);
    loadShips(db, state);
    loadSurveyTeams(db, state);
    loadSurveyPrograms(db, state);
    loadFreightState(db, state);
    loadAppointments(db, state);
    loadEvents(db, state);

    // Telemetry is session-only. The reader reconstructs detached durable
    // state and pass through the same domain validator before publication.
    validateGameState(state);
    return state;
}

} // namespace

#ifdef DEEP_SIGNAL_TESTING
void setSaveFailureInjectionForTest(const bool enabled) noexcept {
    failAfterFirstInsertForTest = enabled;
}
#endif

void SaveGameRepository::save(const std::filesystem::path& path, const GameState& state) {
    // Save is also a trust boundary for tools/tests that may construct GameState
    // directly; do not persist a graph the simulation would later reject.
    validateGameState(state);

    Database db{path, Database::OpenMode::ReadWriteCreate};
    Transaction transaction{db, Transaction::Mode::Write};
    if (hasUserSchema(db)) {
        const std::int64_t version = readSchemaVersion(db);
        if (version != kSchemaVersion) throw std::runtime_error{"Unsupported save schema version"};
        requireV14Structure(db);
        (void)readSnapshot(db);
    } else {
        // DDL and rows share this transaction. A failed new-path save may
        // leave an empty file, but not a partially initialized schema.
        createSchemaV14(db);
    }
    clearExistingSave(db);
    saveSchemaVersion(db);
#ifdef DEEP_SIGNAL_TESTING
    if (failAfterFirstInsertForTest) {
        failAfterFirstInsertForTest = false;
        throw std::runtime_error{"Injected Save failure after deletion and first insertion"};
    }
#endif
    saveMeta(db, state);
    saveIdCounters(db, state.ids);
    saveStarSystems(db, state);
    saveInstitutions(db, state);
    savePeople(db, state);
    saveAppointments(db, state);
    saveBodies(db, state);
    saveColonies(db, state);
    saveMineralDeposits(db, state);
    saveShipComponents(db, state);
    saveShipClasses(db, state);
    saveShipyardOrders(db, state);
    saveFleets(db, state);
    saveShips(db, state);
    saveSurveyTeams(db, state);
    saveSurveyPrograms(db, state);
    saveFreightState(db, state);
    saveEvents(db, state);
    // Re-read on this connection before commit. This catches incomplete rows,
    // ordinal gaps, and foreign-key problems while rollback can still restore
    // the previous logical snapshot. Save rejects user triggers in preflight.
    (void)readSnapshot(db);
    // dailyEconomySnapshots is runtime-only telemetry for the active session.
    // The schema deliberately omits it, so saves contain durable state and audit
    // events only; graphs can regenerate new samples after loading and advancing.
    transaction.commit();
}

GameState SaveGameRepository::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error{"Save file does not exist"};
    }

    Database db{path, Database::OpenMode::ReadOnly};

    Transaction transaction{db, Transaction::Mode::Read};
    const std::int64_t version = readSchemaVersion(db);
    if (version != kSchemaVersion) throw std::runtime_error{"Unsupported save schema version"};
    requireV14Structure(db, true);
    GameState state = readSnapshot(db);

    transaction.commit();
    return state;
}

} // namespace deep::save
