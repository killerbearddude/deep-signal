#include "save/FreightPersistence.h"

// Freight uses normalized cargo units and exact typed custody IDs. Manifest
// limits are stored separately from Ship::cargo because planned quantities are
// not inventory. Every ordered child read checks contiguous integer ordinals.

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace deep::save {
namespace {

void reuse(Statement& stmt) { stmt.reset(); stmt.clearBindings(); }

template <typename IdT>
void bindOptional(Statement& stmt, const int column, const std::optional<IdT> value) {
    if (value) stmt.bindInt64(column, value->value); else stmt.bindNull(column);
}

template <typename IdT>
std::optional<IdT> optionalId(const Statement& stmt, const int column) {
    return stmt.columnIsNull(column) ? std::nullopt : std::optional{IdT{stmt.columnInt64Strict(column)}};
}

int integer(const Statement& stmt, const int column) {
    const auto value = stmt.columnInt64Strict(column);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error{"Freight save integer exceeds representable range"};
    return static_cast<int>(value);
}

template <typename Enum>
Enum enumeration(const Statement& stmt, const int column, const Enum maximum) {
    const auto value = stmt.columnInt64Strict(column);
    if (value < 0 || value > static_cast<std::int64_t>(maximum))
        throw std::runtime_error{"Freight save contains an invalid enum"};
    return static_cast<Enum>(value);
}

bool boolean(const Statement& stmt, const int column) {
    const auto value = stmt.columnInt64Strict(column);
    if (value != 0 && value != 1) throw std::runtime_error{"Freight save contains a non-boolean flag"};
    return value != 0;
}

std::int64_t ordinal(const std::size_t index) {
    if (index > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error{"Freight save ordinal exceeds representable range"};
    return static_cast<std::int64_t>(index);
}

void requireOrdinal(const Statement& stmt, const int column, const std::size_t expected,
                    const std::string_view table) {
    if (stmt.columnInt64Strict(column) != ordinal(expected))
        throw std::runtime_error{"Save contains noncontiguous ordinal in " + std::string{table}};
}

FreightProgram& programById(GameState& state, const Statement& stmt) {
    const FreightProgramId id{stmt.columnInt64Strict(0)};
    for (auto& program : state.freightPrograms) if (program.id == id) return program;
    throw std::runtime_error{"Freight save child references an unknown program"};
}

} // namespace

void saveFreightState(Database& db, const GameState& state) {
    Statement program{db, R"sql(
        INSERT INTO freight_programs(
            id, ordinal, name, source_colony_id, destination_colony_id, material, total_quantity,
            requested_fleet_id, requested_leader_id, source_cargo_floor, source_propellant_floor,
            max_additional_propellant, return_contingency_fraction, created_day, charter_revision,
            lifecycle, closure, closed_day, leased_fleet_id, task, task_fleet_id,
            next_shipment_number, cargo_loaded, cargo_delivered, cargo_returned, fuel_loaded,
            fuel_burned, next_report_day, report_start_day, reported_cargo_loaded,
            reported_cargo_delivered, reported_cargo_returned, reported_fuel_loaded,
            reported_fuel_burned, reported_shipments, issue_signature, issue_message, issue_acknowledged
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement shipment{db, R"sql(
        INSERT INTO freight_shipments(program_id, number, charter_revision, committed_day,
            fleet_id, leader_id, source_colony_id, destination_colony_id, material)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement manifest{db, R"sql(
        INSERT INTO freight_shipment_manifest(program_id, ordinal, ship_id, planned_quantity)
        VALUES (?, ?, ?, ?);
    )sql"};
    Statement receipt{db, R"sql(
        INSERT INTO freight_transfer_receipts(program_id, ordinal, sequence, shipment_number,
            day, fleet_id, leader_id, colony_id, material, kind, amount)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};
    Statement report{db, R"sql(
        INSERT INTO freight_program_reports(program_id, ordinal, start_day, end_day,
            is_ninety_day_review, charter_revision, cargo_loaded, cargo_delivered,
            cargo_returned, fuel_loaded, fuel_burned, cargo_aboard, shipments_started,
            target_quantity, cumulative_delivered, committed_quantity, fleet_id, fleet_body_id, waiting_reason,
            source_cargo_floor, source_propellant_floor, max_additional_propellant, return_contingency_fraction)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql"};

    for (std::size_t index = 0; index < state.freightPrograms.size(); ++index) {
        const auto& item = state.freightPrograms.at(index);
        int p = 1;
        program.bindInt64(p++, item.id.value);
        program.bindInt64(p++, ordinal(index));
        program.bindText(p++, item.charter.name);
        program.bindInt64(p++, item.charter.sourceColonyId.value);
        program.bindInt64(p++, item.charter.destinationColonyId.value);
        program.bindInt64(p++, static_cast<std::int64_t>(item.charter.material));
        program.bindDouble(p++, item.charter.totalQuantity);
        bindOptional(program, p++, item.charter.requestedFleetId);
        bindOptional(program, p++, item.charter.requestedLeaderId);
        program.bindDouble(p++, item.charter.policy.sourceCargoFloor);
        program.bindDouble(p++, item.charter.policy.sourcePropellantFloor);
        if (item.charter.policy.maxAdditionalPropellant)
            program.bindDouble(p++, *item.charter.policy.maxAdditionalPropellant);
        else program.bindNull(p++);
        program.bindDouble(p++, item.charter.policy.returnContingencyFraction);
        program.bindInt64(p++, item.createdDay);
        program.bindInt64(p++, item.charterRevision);
        program.bindInt64(p++, static_cast<std::int64_t>(item.lifecycle));
        program.bindInt64(p++, static_cast<std::int64_t>(item.closure));
        if (item.closedDay) program.bindInt64(p++, *item.closedDay); else program.bindNull(p++);
        bindOptional(program, p++, item.leasedFleetId);
        program.bindInt64(p++, static_cast<std::int64_t>(item.task));
        bindOptional(program, p++, item.taskFleetId);
        program.bindInt64(p++, item.nextShipmentNumber);
        program.bindDouble(p++, item.cargoLoaded);
        program.bindDouble(p++, item.cargoDelivered);
        program.bindDouble(p++, item.cargoReturned);
        program.bindDouble(p++, item.fuelLoaded);
        program.bindDouble(p++, item.fuelBurned);
        program.bindInt64(p++, item.nextReportDay);
        program.bindInt64(p++, item.reportStartDay);
        program.bindDouble(p++, item.reportedCargoLoaded);
        program.bindDouble(p++, item.reportedCargoDelivered);
        program.bindDouble(p++, item.reportedCargoReturned);
        program.bindDouble(p++, item.reportedFuelLoaded);
        program.bindDouble(p++, item.reportedFuelBurned);
        program.bindInt64(p++, item.reportedShipments);
        program.bindText(p++, item.issue.signature);
        program.bindText(p++, item.issue.message);
        program.bindInt64(p++, item.issue.acknowledged ? 1 : 0);
        program.execute();
        reuse(program);

        if (item.shipment) {
            const auto& current = *item.shipment;
            shipment.bindInt64(1, item.id.value);
            shipment.bindInt64(2, current.number);
            shipment.bindInt64(3, current.charterRevision);
            shipment.bindInt64(4, current.committedDay);
            shipment.bindInt64(5, current.fleetId.value);
            shipment.bindInt64(6, current.leaderId.value);
            shipment.bindInt64(7, current.sourceColonyId.value);
            shipment.bindInt64(8, current.destinationColonyId.value);
            shipment.bindInt64(9, static_cast<std::int64_t>(current.material));
            shipment.execute();
            reuse(shipment);
            for (std::size_t child = 0; child < current.manifest.size(); ++child) {
                const auto& row = current.manifest.at(child);
                manifest.bindInt64(1, item.id.value);
                manifest.bindInt64(2, ordinal(child));
                manifest.bindInt64(3, row.shipId.value);
                manifest.bindDouble(4, row.plannedQuantity);
                manifest.execute();
                reuse(manifest);
            }
        }
        for (std::size_t child = 0; child < item.receipts.size(); ++child) {
            const auto& row = item.receipts.at(child);
            receipt.bindInt64(1, item.id.value);
            receipt.bindInt64(2, ordinal(child));
            receipt.bindInt64(3, row.sequence);
            receipt.bindInt64(4, row.shipmentNumber);
            receipt.bindInt64(5, row.day);
            receipt.bindInt64(6, row.fleetId.value);
            receipt.bindInt64(7, row.leaderId.value);
            receipt.bindInt64(8, row.colonyId.value);
            receipt.bindInt64(9, static_cast<std::int64_t>(row.material));
            receipt.bindInt64(10, static_cast<std::int64_t>(row.kind));
            receipt.bindDouble(11, row.amount);
            receipt.execute();
            reuse(receipt);
        }
        for (std::size_t child = 0; child < item.reports.size(); ++child) {
            const auto& row = item.reports.at(child);
            report.bindInt64(1, item.id.value);
            report.bindInt64(2, ordinal(child));
            report.bindInt64(3, row.startDay);
            report.bindInt64(4, row.endDay);
            report.bindInt64(5, row.isNinetyDayReview ? 1 : 0);
            report.bindInt64(6, row.charterRevision);
            report.bindDouble(7, row.cargoLoaded);
            report.bindDouble(8, row.cargoDelivered);
            report.bindDouble(9, row.cargoReturned);
            report.bindDouble(10, row.fuelLoaded);
            report.bindDouble(11, row.fuelBurned);
            report.bindDouble(12, row.cargoAboard);
            report.bindInt64(13, row.shipmentsStarted);
            report.bindDouble(14, row.targetQuantity);
            report.bindDouble(15, row.cumulativeDelivered);
            report.bindDouble(16, row.committedQuantity);
            bindOptional(report, 17, row.fleetId);
            bindOptional(report, 18, row.fleetBodyId);
            report.bindText(19, row.waitingReason);
            // Historical limits explain the report's decision context. They
            // never replace the active charter policy when loading a save.
            report.bindDouble(20, row.policy.sourceCargoFloor);
            report.bindDouble(21, row.policy.sourcePropellantFloor);
            if (row.policy.maxAdditionalPropellant) report.bindDouble(22, *row.policy.maxAdditionalPropellant);
            else report.bindNull(22);
            report.bindDouble(23, row.policy.returnContingencyFraction);
            report.execute();
            reuse(report);
        }
    }

    // FK parents exist before custody rows. There is deliberately no separate
    // cargo ordinal: each keyed row attaches to an already ordered Ship.
    Statement cargo{db, R"sql(
        INSERT INTO ship_cargo(ship_id, program_id, shipment_number, material, quantity)
        VALUES (?, ?, ?, ?, ?);
    )sql"};
    for (const auto& ship : state.ships) {
        if (!ship.cargo) continue;
        cargo.bindInt64(1, ship.id.value);
        cargo.bindInt64(2, ship.cargo->programId.value);
        cargo.bindInt64(3, ship.cargo->shipmentNumber);
        cargo.bindInt64(4, static_cast<std::int64_t>(ship.cargo->material));
        cargo.bindDouble(5, ship.cargo->quantity);
        cargo.execute();
        reuse(cargo);
    }
}

void loadFreightState(Database& db, GameState& state) {
    Statement program{db, R"sql(
        SELECT id, ordinal, name, source_colony_id, destination_colony_id, material, total_quantity,
            requested_fleet_id, requested_leader_id, source_cargo_floor, source_propellant_floor,
            max_additional_propellant, return_contingency_fraction, created_day, charter_revision,
            lifecycle, closure, closed_day, leased_fleet_id, task, task_fleet_id,
            next_shipment_number, cargo_loaded, cargo_delivered, cargo_returned, fuel_loaded,
            fuel_burned, next_report_day, report_start_day, reported_cargo_loaded,
            reported_cargo_delivered, reported_cargo_returned, reported_fuel_loaded,
            reported_fuel_burned, reported_shipments, issue_signature, issue_message, issue_acknowledged
        FROM freight_programs ORDER BY ordinal;
    )sql"};
    while (program.step()) {
        requireOrdinal(program, 1, state.freightPrograms.size(), "freight_programs");
        FreightProgram item;
        item.id = FreightProgramId{program.columnInt64Strict(0)};
        item.charter.name = program.columnText(2);
        item.charter.sourceColonyId = ColonyId{program.columnInt64Strict(3)};
        item.charter.destinationColonyId = ColonyId{program.columnInt64Strict(4)};
        item.charter.material = enumeration(program, 5, ProcessedMaterial::OrdnanceMaterials);
        item.charter.totalQuantity = program.columnDoubleStrict(6);
        item.charter.requestedFleetId = optionalId<FleetId>(program, 7);
        item.charter.requestedLeaderId = optionalId<PersonId>(program, 8);
        item.charter.policy.sourceCargoFloor = program.columnDoubleStrict(9);
        item.charter.policy.sourcePropellantFloor = program.columnDoubleStrict(10);
        if (!program.columnIsNull(11)) item.charter.policy.maxAdditionalPropellant = program.columnDoubleStrict(11);
        item.charter.policy.returnContingencyFraction = program.columnDoubleStrict(12);
        item.createdDay = program.columnInt64Strict(13);
        item.charterRevision = integer(program, 14);
        item.lifecycle = enumeration(program, 15, FreightProgramLifecycle::Closed);
        item.closure = enumeration(program, 16, FreightProgramClosure::Cancelled);
        if (!program.columnIsNull(17)) item.closedDay = program.columnInt64Strict(17);
        item.leasedFleetId = optionalId<FleetId>(program, 18);
        item.task = enumeration(program, 19, FreightProgramTask::ReturningCargo);
        item.taskFleetId = optionalId<FleetId>(program, 20);
        item.nextShipmentNumber = integer(program, 21);
        item.cargoLoaded = program.columnDoubleStrict(22);
        item.cargoDelivered = program.columnDoubleStrict(23);
        item.cargoReturned = program.columnDoubleStrict(24);
        item.fuelLoaded = program.columnDoubleStrict(25);
        item.fuelBurned = program.columnDoubleStrict(26);
        item.nextReportDay = program.columnInt64Strict(27);
        item.reportStartDay = program.columnInt64Strict(28);
        item.reportedCargoLoaded = program.columnDoubleStrict(29);
        item.reportedCargoDelivered = program.columnDoubleStrict(30);
        item.reportedCargoReturned = program.columnDoubleStrict(31);
        item.reportedFuelLoaded = program.columnDoubleStrict(32);
        item.reportedFuelBurned = program.columnDoubleStrict(33);
        item.reportedShipments = integer(program, 34);
        item.issue = {program.columnText(35), program.columnText(36), boolean(program, 37)};
        state.freightPrograms.push_back(std::move(item));
    }

    Statement shipment{db, R"sql(
        SELECT program_id, number, charter_revision, committed_day, fleet_id, leader_id,
            source_colony_id, destination_colony_id, material
        FROM freight_shipments ORDER BY program_id;
    )sql"};
    while (shipment.step()) {
        auto& item = programById(state, shipment);
        if (item.shipment) throw std::runtime_error{"Duplicate freight shipment"};
        item.shipment = FreightShipment{
            .number = integer(shipment, 1),
            .charterRevision = integer(shipment, 2),
            .committedDay = shipment.columnInt64Strict(3),
            .fleetId = FleetId{shipment.columnInt64Strict(4)},
            .leaderId = PersonId{shipment.columnInt64Strict(5)},
            .sourceColonyId = ColonyId{shipment.columnInt64Strict(6)},
            .destinationColonyId = ColonyId{shipment.columnInt64Strict(7)},
            .material = enumeration(shipment, 8, ProcessedMaterial::OrdnanceMaterials),
            .manifest = {}
        };
    }
    Statement manifest{db, R"sql(
        SELECT program_id, ordinal, ship_id, planned_quantity
        FROM freight_shipment_manifest ORDER BY program_id, ordinal;
    )sql"};
    while (manifest.step()) {
        auto& item = programById(state, manifest);
        if (!item.shipment) throw std::runtime_error{"Manifest references missing freight shipment"};
        requireOrdinal(manifest, 1, item.shipment->manifest.size(), "freight_shipment_manifest");
        item.shipment->manifest.push_back({ShipId{manifest.columnInt64Strict(2)}, manifest.columnDoubleStrict(3)});
    }

    Statement receipt{db, R"sql(
        SELECT program_id, ordinal, sequence, shipment_number, day, fleet_id, leader_id,
            colony_id, material, kind, amount
        FROM freight_transfer_receipts ORDER BY program_id, ordinal;
    )sql"};
    while (receipt.step()) {
        auto& item = programById(state, receipt);
        requireOrdinal(receipt, 1, item.receipts.size(), "freight_transfer_receipts");
        item.receipts.push_back(FreightTransferReceipt{
            .sequence = integer(receipt, 2), .shipmentNumber = integer(receipt, 3),
            .day = receipt.columnInt64Strict(4),
            .fleetId = FleetId{receipt.columnInt64Strict(5)},
            .leaderId = PersonId{receipt.columnInt64Strict(6)},
            .colonyId = ColonyId{receipt.columnInt64Strict(7)},
            .material = enumeration(receipt, 8, ProcessedMaterial::OrdnanceMaterials),
            .kind = enumeration(receipt, 9, FreightTransferKind::OperatingFuel),
            .amount = receipt.columnDoubleStrict(10)
        });
    }

    Statement report{db, R"sql(
        SELECT program_id, ordinal, start_day, end_day, is_ninety_day_review, charter_revision,
            cargo_loaded, cargo_delivered, cargo_returned, fuel_loaded, fuel_burned,
            cargo_aboard, shipments_started, target_quantity, cumulative_delivered,
            committed_quantity, fleet_id, fleet_body_id, waiting_reason,
            source_cargo_floor, source_propellant_floor, max_additional_propellant, return_contingency_fraction
        FROM freight_program_reports ORDER BY program_id, ordinal;
    )sql"};
    while (report.step()) {
        auto& item = programById(state, report);
        requireOrdinal(report, 1, item.reports.size(), "freight_program_reports");
        item.reports.push_back(FreightProgramReport{
            .startDay = report.columnInt64Strict(2), .endDay = report.columnInt64Strict(3),
            .isNinetyDayReview = boolean(report, 4), .charterRevision = integer(report, 5),
            .cargoLoaded = report.columnDoubleStrict(6), .cargoDelivered = report.columnDoubleStrict(7),
            .cargoReturned = report.columnDoubleStrict(8), .fuelLoaded = report.columnDoubleStrict(9),
            .fuelBurned = report.columnDoubleStrict(10), .cargoAboard = report.columnDoubleStrict(11),
            .shipmentsStarted = integer(report, 12), .targetQuantity = report.columnDoubleStrict(13),
            .cumulativeDelivered = report.columnDoubleStrict(14), .committedQuantity = report.columnDoubleStrict(15),
            .fleetId = optionalId<FleetId>(report, 16), .fleetBodyId = optionalId<BodyId>(report, 17),
            .waitingReason = report.columnText(18),
            .policy = FreightProgramPolicy{
                .sourceCargoFloor = report.columnDoubleStrict(19),
                .sourcePropellantFloor = report.columnDoubleStrict(20),
                .maxAdditionalPropellant = report.columnIsNull(21) ? std::nullopt
                    : std::optional<double>{report.columnDoubleStrict(21)},
                .returnContingencyFraction = report.columnDoubleStrict(22)
            }
        });
    }

    Statement cargo{db, R"sql(
        SELECT ship_id, program_id, shipment_number, material, quantity FROM ship_cargo ORDER BY ship_id;
    )sql"};
    while (cargo.step()) {
        const ShipId id{cargo.columnInt64Strict(0)};
        Ship* carrier = nullptr;
        for (auto& ship : state.ships) if (ship.id == id) { carrier = &ship; break; }
        if (!carrier || carrier->cargo) throw std::runtime_error{"Cargo references missing or duplicated ship"};
        carrier->cargo = ShipCargo{
            .programId = FreightProgramId{cargo.columnInt64Strict(1)},
            .shipmentNumber = integer(cargo, 2),
            .material = enumeration(cargo, 3, ProcessedMaterial::OrdnanceMaterials),
            .quantity = cargo.columnDoubleStrict(4)
        };
    }
}

} // namespace deep::save
