#include "app/SimulationService.h"
#include "save/Database.h"
#include "save/EventJson.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// P3B-30..34: cargo custody, commitment order, reports and same-input continuation
// across the current-schema boundary. Every file lives in a unique RAII folder;
// no fixed save filename is shared with another CTest process or application.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace deep;
using namespace deep::save;

void require(const bool ok, const std::string_view message) {
    if (!ok) throw std::runtime_error{std::string{message}};
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("deep_signal_freight_save_" + std::to_string(stamp) + "_" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error{"Unable to create isolated freight save test directory"};
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

template <typename Id> std::int64_t value(const std::optional<Id> id) { return id ? id->value : 0; }
template <typename E> int number(const E enumeration) { return static_cast<int>(enumeration); }

// This independent, direct domain fingerprint enumerates every new durable
// field. It catches dropped mapping columns even if save->load->save would
// otherwise write the same incorrectly defaulted snapshot twice.
std::string freightFingerprint(const GameState& state) {
    std::ostringstream out;
    out << std::setprecision(17) << state.date.day << ':' << state.ids.nextFreightProgramId << '\n';
    for (const auto& component : state.shipComponents)
        out << "component " << component.id.value << ':' << component.cargoCapacity << ':' << component.cargoHandlingPerDay << '\n';
    for (const auto& ship : state.ships) {
        out << "ship " << ship.id.value << ':' << ship.fleetId.value << ':' << ship.fuel << ':' << ship.cargo.has_value();
        if (ship.cargo) out << ':' << ship.cargo->programId.value << ':' << ship.cargo->shipmentNumber
            << ':' << number(ship.cargo->material) << ':' << ship.cargo->quantity;
        out << '\n';
    }
    for (const auto& p : state.freightPrograms) {
        const auto& c = p.charter;
        out << "program " << p.id.value << ':' << std::quoted(c.name) << ':' << c.sourceColonyId.value
            << ':' << c.destinationColonyId.value << ':' << number(c.material) << ':' << c.totalQuantity
            << ':' << value(c.requestedFleetId) << ':' << value(c.requestedLeaderId)
            << ':' << c.policy.sourceCargoFloor << ':' << c.policy.sourcePropellantFloor
            << ':' << c.policy.maxAdditionalPropellant.has_value() << ':' << c.policy.maxAdditionalPropellant.value_or(-1.0)
            << ':' << c.policy.returnContingencyFraction << ':' << p.createdDay << ':' << p.charterRevision
            << ':' << number(p.lifecycle) << ':' << number(p.closure) << ':' << p.closedDay.value_or(-1)
            << ':' << value(p.leasedFleetId) << ':' << number(p.task) << ':' << value(p.taskFleetId)
            << ':' << p.nextShipmentNumber << ':' << p.cargoLoaded << ':' << p.cargoDelivered
            << ':' << p.cargoReturned << ':' << p.fuelLoaded << ':' << p.fuelBurned
            << ':' << p.nextReportDay << ':' << p.reportStartDay << ':' << p.reportedCargoLoaded
            << ':' << p.reportedCargoDelivered << ':' << p.reportedCargoReturned
            << ':' << p.reportedFuelLoaded << ':' << p.reportedFuelBurned << ':' << p.reportedShipments
            << ':' << std::quoted(p.issue.signature) << ':' << std::quoted(p.issue.message)
            << ':' << p.issue.acknowledged << ':' << p.shipment.has_value() << '\n';
        if (p.shipment) {
            const auto& s = *p.shipment;
            out << "shipment " << s.number << ':' << s.charterRevision << ':' << s.committedDay
                << ':' << s.fleetId.value << ':' << s.leaderId.value << ':' << s.sourceColonyId.value
                << ':' << s.destinationColonyId.value << ':' << number(s.material) << '\n';
            for (const auto& row : s.manifest) out << row.shipId.value << ':' << row.plannedQuantity << '\n';
        }
        for (const auto& r : p.receipts) out << "receipt " << r.sequence << ':' << r.shipmentNumber << ':' << r.day
            << ':' << r.fleetId.value << ':' << r.leaderId.value << ':' << r.colonyId.value
            << ':' << number(r.material) << ':' << number(r.kind) << ':' << r.amount << '\n';
        for (const auto& r : p.reports) out << "report " << r.startDay << ':' << r.endDay << ':' << r.isNinetyDayReview
            << ':' << r.charterRevision << ':' << r.cargoLoaded << ':' << r.cargoDelivered << ':' << r.cargoReturned
            << ':' << r.fuelLoaded << ':' << r.fuelBurned << ':' << r.cargoAboard << ':' << r.shipmentsStarted
            << ':' << r.targetQuantity << ':' << r.cumulativeDelivered << ':' << r.committedQuantity
            << ':' << value(r.fleetId) << ':' << value(r.fleetBodyId) << ':' << std::quoted(r.waitingReason)
            << ':' << r.policy.sourceCargoFloor << ':' << r.policy.sourcePropellantFloor
            << ':' << r.policy.maxAdditionalPropellant.has_value() << ':' << r.policy.maxAdditionalPropellant.value_or(-1.0)
            << ':' << r.policy.returnContingencyFraction << '\n';
    }
    for (const auto& event : state.eventLog) {
        if (const auto* e = std::get_if<FreightProgramAuditEvent>(&event.payload))
            out << "audit " << event.id.value << ':' << event.day << ':' << number(event.severity)
                << ':' << e->programId.value << ':' << number(e->kind) << ':' << value(e->fleetId)
                << ':' << value(e->colonyId) << ':' << value(e->leaderId) << ':' << e->charterRevision
                << ':' << e->shipmentNumber << ':' << e->amount << ':' << std::quoted(e->detail) << '\n';
    }
    return out.str();
}

std::string quoteIdentifier(const std::string& input) {
    std::string result{"\""};
    for (const char c : input) { result += c; if (c == '"') result += c; }
    return result + '"';
}

// Compare the complete durable world including predecessor records and audit.
// SQLite logical contents, not file bytes, are the snapshot round-trip contract.
std::vector<std::string> logicalRows(const std::filesystem::path& path) {
    Database db{path, Database::OpenMode::ReadOnly};
    std::vector<std::string> result;
    Statement tables{db, "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT GLOB 'sqlite_*' ORDER BY name;"};
    while (tables.step()) {
        const auto table = tables.columnText(0);
        Statement columns{db, "PRAGMA table_xinfo(" + quoteIdentifier(table) + ");"};
        std::vector<std::string> names;
        while (columns.step()) names.push_back(columns.columnText(1));
        std::string sql = "SELECT ";
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i) sql += ',';
            sql += "quote(" + quoteIdentifier(names[i]) + ')';
        }
        sql += " FROM " + quoteIdentifier(table) + " ORDER BY rowid;";
        Statement rows{db, sql};
        while (rows.step()) {
            std::string row = table;
            for (std::size_t i = 0; i < names.size(); ++i) {
                const auto cell = rows.columnText(static_cast<int>(i));
                row += ':' + std::to_string(cell.size()) + ':' + cell;
            }
            result.push_back(std::move(row));
        }
    }
    return result;
}

GameState fixture() {
    GameState state = createDelegatedFreightScenario();
    for (auto& colony : state.colonies) {
        colony.mines = 0.0;
        colony.processorCapacity = 0.0;
        colony.shipyardCapacity = 0.0;
    }
    // Six-day legs make both transit directions observable between daily steps.
    const BodyId destination = state.colonies.back().bodyId;
    for (auto& body : state.bodies) if (body.id == destination) body.x = 30.0;
    return state;
}

FreightProgramCharter charter(const GameState& state) {
    return FreightProgramCharter{
        .name = "Persistence cargo \"proof\"", .sourceColonyId = state.colonies.at(state.colonies.size()-2).id,
        .destinationColonyId = state.colonies.back().id, .material = ProcessedMaterial::StructuralAlloys,
        .totalQuantity = 500.0, .requestedFleetId = state.fleets.back().id,
        .requestedLeaderId = state.people.front().id,
        .policy = {.sourceCargoFloor = 0.0, .sourcePropellantFloor = 3.5,
                   .maxAdditionalPropellant = 1000.0, .returnContingencyFraction = 0.1}
    };
}

Simulation started() {
    Simulation sim{fixture()};
    require(sim.execute(CreateFreightProgramCommand{charter(sim.state())}).ok, "freight authorizes");
    return sim;
}

void roundTripAndContinue(const GameState& source, const int days, const TempDirectory& temp) {
    const auto path = temp.path / "checkpoint.sqlite";
    SaveGameRepository::save(path, source);
    GameState loaded = SaveGameRepository::load(path);
    require(freightFingerprint(source) == freightFingerprint(loaded), "every freight field round-trips");
    Simulation a{source};
    Simulation b{std::move(loaded)};
    const auto ra = a.advanceDaysDetailed(days);
    const auto rb = b.advanceDaysDetailed(days);
    require(ra.advancedDays == rb.advancedDays && ra.interrupted == rb.interrupted,
            "same-input continuation reaches same decision boundary");
    require(freightFingerprint(a.state()) == freightFingerprint(b.state()), "freight continuation stays equivalent");
    const auto left = temp.path / "left.sqlite";
    const auto right = temp.path / "right.sqlite";
    SaveGameRepository::save(left, a.state());
    SaveGameRepository::save(right, b.state());
    require(logicalRows(left) == logicalRows(right), "complete durable continuation is equivalent");
}

void test_every_physical_phase_round_trips() {
    // P3B-30: saves after real fuel transfer, partial handling, both trajectories,
    // and closure must resume without repeating the completed physical action.
    TempDirectory temp;
    Simulation sim = started();
    bool refueled = false, partialLoad = false, outbound = false, partialUnload = false, returning = false;
    for (int day = 0; day < 150 && sim.state().freightPrograms.front().lifecycle != FreightProgramLifecycle::Closed; ++day) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "ready freight advances without surprise issue");
        const auto& p = sim.state().freightPrograms.front();
        const auto& ship = sim.state().ships.back();
        const auto& fleet = sim.state().fleets.back();
        bool capture = false;
        if (!refueled && p.fuelLoaded > 0.0 && p.cargoLoaded == 0.0) refueled = capture = true;
        if (!partialLoad && p.task == FreightProgramTask::Loading && ship.cargo && ship.cargo->quantity < 200.0)
            partialLoad = capture = true;
        if (!outbound && p.task == FreightProgramTask::Outbound && fleet.activeOrder.type == FleetOrderType::MoveToBody)
            outbound = capture = true;
        if (!partialUnload && p.task == FreightProgramTask::Unloading && p.cargoDelivered > 0.0 && ship.cargo)
            partialUnload = capture = true;
        if (!returning && p.task == FreightProgramTask::Return && fleet.activeOrder.type == FleetOrderType::MoveToBody)
            returning = capture = true;
        if (capture) roundTripAndContinue(sim.state(), 150, temp);
    }
    require(refueled && partialLoad && outbound && partialUnload && returning, "all five physical checkpoint cases were exercised");
    const auto& p = sim.state().freightPrograms.front();
    require(p.lifecycle == FreightProgramLifecycle::Closed && p.cargoDelivered == 500.0 &&
            p.closedDay.has_value() && !p.reports.empty(), "completed fixture retains closure date, delivery and reports");
    roundTripAndContinue(sim.state(), 90, temp);
}

GameState partialLoading() {
    Simulation sim = started();
    for (int i = 0; i < 30; ++i) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "fixture reaches partial loading");
        const auto& ship = sim.state().ships.back();
        if (ship.cargo && ship.cargo->quantity > 0.0 && ship.cargo->quantity < 200.0) return sim.state();
    }
    throw std::runtime_error{"No partial load checkpoint"};
}

void test_suspended_and_cancelled_custody() {
    // P3B-31: suspension stores custody without doing transfers; cancellation
    // settlement survives a later suspension and cannot become new pickup work.
    TempDirectory temp;
    Simulation sim{partialLoading()};
    const auto id = sim.state().freightPrograms.front().id;
    Simulation amended{sim.state()};
    const auto originalCharter = amended.state().freightPrograms.front().charter;
    require(amended.execute(AmendFreightProgramCommand{id, FreightProgramAmendment{
        .name = originalCharter.name, .totalQuantity = 0.0,
        .requestedFleetId = originalCharter.requestedFleetId,
        .requestedLeaderId = originalCharter.requestedLeaderId, .policy = originalCharter.policy
    }}).ok, "zero amended target preserves committed cargo without requiring new pickups");
    roundTripAndContinue(amended.state(), 90, temp);
    require(sim.execute(SuspendFreightProgramCommand{id}).ok, "loaded freight suspends");
    roundTripAndContinue(sim.state(), 10, temp);
    const auto path = temp.path / "suspended.sqlite";
    SaveGameRepository::save(path, sim.state());
    Simulation loaded{SaveGameRepository::load(path)};
    require(sim.execute(ResumeFreightProgramCommand{id}).ok && loaded.execute(ResumeFreightProgramCommand{id}).ok,
            "same retained custody resumes");
    require(sim.execute(CancelFreightProgramCommand{id}).ok && loaded.execute(CancelFreightProgramCommand{id}).ok,
            "partly loaded shipment cancels into source settlement");
    require(sim.execute(SuspendFreightProgramCommand{id}).ok && loaded.execute(SuspendFreightProgramCommand{id}).ok,
            "cancellation settlement can be suspended");
    roundTripAndContinue(sim.state(), 10, temp);
    require(freightFingerprint(sim.state()) == freightFingerprint(loaded.state()), "command continuation preserves exact custody and identity");
    require(sim.execute(ResumeFreightProgramCommand{id}).ok, "cancelled settlement resumes");
    roundTripAndContinue(sim.state(), 20, temp);
}

void test_order_and_all_materials() {
    // P3B-32: deliberately non-ID vector order and non-global roster order must
    // reach the same physical transfer order after reconstruction.
    TempDirectory temp;
    GameState state = fixture();
    Ship second = state.ships.back();
    second.id = ShipId{state.ids.nextShipId++};
    second.name = "Second carrying hull";
    state.ships.push_back(second);
    state.fleets.back().shipIds.insert(state.fleets.back().shipIds.begin(), second.id);
    Simulation sim{state};
    auto c = charter(sim.state());
    require(sim.execute(CreateFreightProgramCommand{c}).ok, "first ordered charter creates");
    c.requestedFleetId.reset();
    c.material = ProcessedMaterial::OrdnanceMaterials;
    c.name = "Last processed material remains valid";
    require(sim.execute(CreateFreightProgramCommand{c}).ok, "all enum endpoints remain accepted");
    GameState reordered = sim.state();
    std::reverse(reordered.freightPrograms.begin(), reordered.freightPrograms.end());
    std::reverse(reordered.shipComponents.begin(), reordered.shipComponents.end());
    std::reverse(reordered.ships.begin(), reordered.ships.end());
    std::reverse(reordered.fleets.begin(), reordered.fleets.end());
    roundTripAndContinue(reordered, 80, temp);
    Simulation active{reordered};
    bool captured = false;
    for (int i = 0; i < 20; ++i) {
        require(active.advanceDaysDetailed(1).advancedDays == 1, "ordered fixture reaches real cargo handling");
        if (std::any_of(active.state().ships.begin(), active.state().ships.end(), [](const Ship& ship) { return ship.cargo.has_value(); })) {
            roundTripAndContinue(active.state(), 80, temp);
            captured = true;
            break;
        }
    }
    require(captured, "ordered manifest with multiple actual lots crosses the save boundary");
}

void test_completion_and_cancellation_issue_round_trips() {
    // P3B-31 and P3B-25: completion return is unresolved physical work. The
    // issue/custody must survive a save and remain actionable, not strand a
    // supposedly completed program behind a disabled lifecycle command.
    TempDirectory temp;
    Simulation progressing = started();
    std::optional<GameState> checkpoint;
    for (int i = 0; i < 150; ++i) {
        require(progressing.advanceDaysDetailed(1).advancedDays == 1, "ready fixture reaches final return");
        const auto& p = progressing.state().freightPrograms.front();
        if (p.cargoDelivered == 500.0 && p.task == FreightProgramTask::Return &&
            progressing.state().fleets.back().activeOrder.type == FleetOrderType::None) {
            checkpoint = progressing.state();
            break;
        }
    }
    require(checkpoint.has_value(), "final delivery exposes committed empty return");
    // Detached-fixture depletion represents a newly discovered physical return
    // limitation. It is not a production refueling or player inventory command.
    checkpoint->ships.back().fuel = 0.0;
    Simulation issue{*checkpoint};
    const auto stop = issue.advanceDaysDetailed(20);
    const auto& p = issue.state().freightPrograms.front();
    require(stop.interrupted && !p.issue.signature.empty() && !p.issue.acknowledged,
            "new blocked return produces a durable actionable issue");
    roundTripAndContinue(issue.state(), 30, temp);
    const auto path = temp.path / "return-issue.sqlite";
    SaveGameRepository::save(path, issue.state());
    Simulation loaded{SaveGameRepository::load(path)};
    const auto id = p.id;
    const auto signature = p.issue.signature;
    for (Simulation* sim : {&issue, &loaded}) {
        require(sim->execute(AcknowledgeFreightProgramIssueCommand{id, signature}).ok, "return issue can be acknowledged");
        require(sim->execute(SuspendFreightProgramCommand{id}).ok, "completion return remains suspendable");
        require(sim->execute(ResumeFreightProgramCommand{id}).ok, "completion return remains resumable");
        require(sim->execute(CancelFreightProgramCommand{id}).ok, "cancel can remove a newly unnecessary empty return");
    }
    roundTripAndContinue(issue.state(), 10, temp);
    require(freightFingerprint(issue.state()) == freightFingerprint(loaded.state()), "return issue command branch agrees after load");

    // Cancellation with cargo can itself wait on per-hull handling. Preserve
    // that custody and acknowledged decision across save/load as well.
    Simulation cancelled{partialLoading()};
    const auto cancelledId = cancelled.state().freightPrograms.front().id;
    require(cancelled.execute(CancelFreightProgramCommand{cancelledId}).ok, "loaded cancellation is accepted");
    GameState blocked = cancelled.state();
    for (auto& component : blocked.shipComponents)
        if (component.kind == ShipComponentKind::CargoBay) component.cargoHandlingPerDay = 0.0;
    Simulation disposition{std::move(blocked)};
    const auto dispositionStop = disposition.advanceDaysDetailed(10);
    require(dispositionStop.interrupted && !disposition.state().freightPrograms.front().issue.signature.empty(),
            "blocked cancellation disposition produces a freight issue");
    roundTripAndContinue(disposition.state(), 10, temp);
}

void executeSql(const std::filesystem::path& path, const std::string_view sql) { Database db{path}; db.execute(sql); }

void test_malformed_current_snapshot_and_failed_load() {
    // P3B-33: valid SQL-shaped corruption still fails the strict reader or graph
    // checks. Service Load must preserve the already-active world on failure.
    TempDirectory temp;
    const GameState state = partialLoading();
    const auto base = temp.path / "valid.sqlite";
    SaveGameRepository::save(base, state);
    const struct Case { const char* name; const char* sql; } cases[] = {
        {"cargo-capacity", "UPDATE ship_cargo SET quantity=99999;"},
        {"cargo-text", "UPDATE ship_cargo SET quantity='not a number';"},
        {"cargo-nonfinite", "UPDATE ship_cargo SET quantity=1e999;"},
        {"cargo-dangling", "PRAGMA foreign_keys=OFF; UPDATE ship_cargo SET program_id=999;"},
        {"cargo-shipment", "PRAGMA foreign_keys=OFF; UPDATE ship_cargo SET shipment_number=99;"},
        {"cargo-material", "UPDATE ship_cargo SET material=2;"},
        {"cargo-zero", "PRAGMA ignore_check_constraints=ON; UPDATE ship_cargo SET quantity=0;"},
        {"program-ordinal", "UPDATE freight_programs SET ordinal=7;"},
        {"program-type", "PRAGMA ignore_check_constraints=ON; UPDATE freight_programs SET task=1.5;"},
        {"program-counter", "UPDATE freight_programs SET cargo_loaded=cargo_loaded+1;"},
        {"return-with-cargo", "UPDATE freight_programs SET task=6;"},
        {"task-location", "UPDATE fleets SET current_body_id=(SELECT body_id FROM colonies WHERE id=(SELECT destination_colony_id FROM freight_programs)) WHERE id=(SELECT leased_fleet_id FROM freight_programs);"},
        {"program-fractional-id", "UPDATE id_counters SET value=1.5 WHERE key='next_freight_program_id';"},
        {"manifest-ordinal", "UPDATE freight_shipment_manifest SET ordinal=4;"},
        {"manifest-type", "UPDATE freight_shipment_manifest SET planned_quantity='bogus';"},
        {"receipt-ordinal", "UPDATE freight_transfer_receipts SET ordinal=ordinal+7;"},
        {"receipt-amount", "UPDATE freight_transfer_receipts SET amount=amount+1;"},
        {"receipt-type", "PRAGMA ignore_check_constraints=ON; UPDATE freight_transfer_receipts SET kind=0.5;"},
        {"receipt-dangling", "PRAGMA foreign_keys=OFF; UPDATE freight_transfer_receipts SET fleet_id=999;"},
        {"component-cargo-type", "UPDATE ship_components SET cargo_capacity='bogus' WHERE kind=5;"}
    };
    for (const auto& test : cases) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        executeSql(path, test.sql);
        const auto before = logicalRows(path);
        SimulationService service{state};
        const auto original = freightFingerprint(service.state());
        require(!service.loadGame(path).ok, std::string{"malformed save rejects: "} + test.name);
        require(freightFingerprint(service.state()) == original, "failed Load preserves active freight game");
        require(logicalRows(path) == before, "failed Load leaves corrupt source unchanged");
        bool rejected = false;
        try { SaveGameRepository::save(path, state); } catch (const std::exception&) { rejected = true; }
        require(rejected && logicalRows(path) == before, "malformed replacement rolls back without deleting goods");
    }
}

void test_report_history_corruption_rejects() {
    // Report summaries are checked against dated physical receipts. A save may
    // not erase a due report, invent in-transit inventory, or rewrite throughput
    // by keeping internally matching cursor caches.
    TempDirectory temp;
    Simulation sim = started();
    require(sim.advanceDaysDetailed(60).advancedDays == 60, "ready fixture crosses report boundaries");
    require(!sim.state().freightPrograms.front().reports.empty(), "freight report history exists");
    const auto base = temp.path / "reports.sqlite";
    SaveGameRepository::save(base, sim.state());
    const struct Case { const char* name; const char* sql; } cases[] = {
        {"report-aboard", "UPDATE freight_program_reports SET cargo_aboard=cargo_aboard+1 WHERE ordinal=0;"},
        {"report-delivered", "UPDATE freight_program_reports SET cumulative_delivered=cumulative_delivered+1 WHERE ordinal=0;"},
        {"report-gap", "UPDATE freight_program_reports SET ordinal=ordinal+3;"},
        {"report-type", "UPDATE freight_program_reports SET shipments_started=0.5 WHERE ordinal=0;"},
        {"report-policy-type", "UPDATE freight_program_reports SET source_cargo_floor='bogus' WHERE ordinal=0;"},
        {"report-policy-nonfinite", "UPDATE freight_program_reports SET return_contingency_fraction=1e999 WHERE ordinal=0;"},
        {"report-policy-negative", "PRAGMA ignore_check_constraints=ON; UPDATE freight_program_reports SET max_additional_propellant=-1 WHERE ordinal=0;"},
        {"report-missing", "DELETE FROM freight_program_reports; UPDATE freight_programs SET reported_cargo_loaded=0,reported_cargo_delivered=0,reported_cargo_returned=0,reported_fuel_loaded=0,reported_fuel_burned=0,reported_shipments=0,report_start_day=created_day,next_report_day=30;"}
    };
    for (const auto& test : cases) {
        const auto path = temp.path / (std::string{test.name} + ".sqlite");
        std::filesystem::copy_file(base, path);
        executeSql(path, test.sql);
        bool rejected = false;
        try { (void)SaveGameRepository::load(path); } catch (const std::exception&) { rejected = true; }
        require(rejected, std::string{"malformed freight report rejects: "} + test.name);
    }
}

void test_report_limits_survive_amendment_and_load() {
    // P3B-29/30: reports preserve the limits that actually governed that period.
    // A later policy amendment changes future authority without retroactively
    // rewriting history or making historical report policy authoritative again.
    TempDirectory temp;
    Simulation sim{fixture()};
    auto c = charter(sim.state());
    c.requestedFleetId.reset();
    c.policy = {.sourceCargoFloor = 7.0, .sourcePropellantFloor = 11.0,
                .maxAdditionalPropellant = 20.0, .returnContingencyFraction = 0.3};
    require(sim.execute(CreateFreightProgramCommand{c}).ok, "waiting charter with explicit limits authorizes");
    require(sim.advanceDaysDetailed(30).advancedDays == 30, "waiting charter publishes its first report");
    const auto id = sim.state().freightPrograms.front().id;
    auto amendment = FreightProgramAmendment{
        .name = c.name, .totalQuantity = 700.0, .requestedFleetId = std::nullopt,
        .requestedLeaderId = c.requestedLeaderId,
        .policy = {.sourceCargoFloor = 17.0, .sourcePropellantFloor = 31.0,
                   .maxAdditionalPropellant = std::nullopt, .returnContingencyFraction = 0.7}
    };
    require(sim.execute(AmendFreightProgramCommand{id, amendment}).ok, "later limits amendment is accepted");
    require(sim.advanceDaysDetailed(30).advancedDays == 30, "next report captures amended limits");
    const auto path = temp.path / "historical-limits.sqlite";
    SaveGameRepository::save(path, sim.state());
    const GameState loaded = SaveGameRepository::load(path);
    require(freightFingerprint(sim.state()) == freightFingerprint(loaded), "all historical and active limits round-trip");
    const auto& p = loaded.freightPrograms.front();
    require(p.reports.size() == 2, "both dated reports survive");
    const auto& oldPolicy = p.reports.front().policy;
    const auto& newPolicy = p.reports.back().policy;
    require(oldPolicy.sourceCargoFloor == 7.0 && oldPolicy.sourcePropellantFloor == 11.0 &&
            oldPolicy.maxAdditionalPropellant == 20.0 && oldPolicy.returnContingencyFraction == 0.3,
            "prior report retains its original finite lifetime allowance and floors");
    require(newPolicy.sourceCargoFloor == 17.0 && newPolicy.sourcePropellantFloor == 31.0 &&
            !newPolicy.maxAdditionalPropellant && newPolicy.returnContingencyFraction == 0.7 &&
            p.charter.policy.sourceCargoFloor == 17.0 && !p.charter.policy.maxAdditionalPropellant,
            "later report and active charter retain amended unlimited allowance independently");
    roundTripAndContinue(loaded, 30, temp);
}

} // namespace

int main() {
    const struct Test { const char* name; void (*run)(); } tests[] = {
        {"physical freight phases and continuation", test_every_physical_phase_round_trips},
        {"suspended and cancelled freight custody", test_suspended_and_cancelled_custody},
        {"freight vector and roster order", test_order_and_all_materials},
        {"completion and cancellation issues survive Load", test_completion_and_cancellation_issue_round_trips},
        {"malformed freight and failed application Load", test_malformed_current_snapshot_and_failed_load},
        {"freight report history rejects corruption", test_report_history_corruption_rejects},
        {"freight historical limits survive amendment and Load", test_report_limits_survive_amendment_and_load}
    };
    int failures = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "[PASS] " << test.name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
