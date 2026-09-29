// Executes one physical freight action per opening while preserving per-hull
// custody, shared site budgets, immutable base routes and dated audit history.
#include "sim/FreightProgramExecution.h"

#include "sim/FreightProgramRules.h"
#include "sim/StockAccess.h"
#include "sim/ProgramControl.h"
#include "sim/ShipDesignRules.h"
#include "sim/SurveyProgramRules.h"
#include "sim/TransitPlanning.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace deep {
namespace {
template <class T, class Id> T* find(std::vector<T>& rows, Id id) {
    const auto i = std::find_if(rows.begin(), rows.end(), [id](const T& r) { return r.id == id; });
    return i == rows.end() ? nullptr : &*i;
}
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto i = std::find_if(rows.begin(), rows.end(), [id](const T& r) { return r.id == id; });
    return i == rows.end() ? nullptr : &*i;
}
bool stationary(const Fleet& f) {
    return f.activeOrder.type == FleetOrderType::None && !f.destinationBodyId;
}
bool targetMet(const FreightProgram& p) {
    return p.cargoDelivered >= p.charter.totalQuantity ||
           (p.cargoDelivered > 0.0 && freightNearlyEqual(p.cargoDelivered, p.charter.totalQuantity));
}
void audit(const FreightProgram& p, FreightProgramAuditKind kind, const FreightProgramExecutionHooks& h,
           std::string detail, double amount = 0.0, std::optional<StockLocation> location = std::nullopt,
           std::optional<Commodity> commodity = std::nullopt) {
    h.emit(EventSeverity::Info,
           FreightProgramAuditEvent{.programId = p.id,
                                    .kind = kind,
                                    .fleetId = p.leasedFleetId ? p.leasedFleetId : p.taskFleetId,
                                    .location = location,
                                    .leaderId = p.shipment ? std::optional<PersonId>{p.shipment->leaderId}
                                                           : p.charter.requestedLeaderId,
                                    .charterRevision = p.charterRevision,
                                    .shipmentNumber = p.shipment ? p.shipment->number : 0,
                                    .amount = amount,
                                    .detail = std::move(detail),
                                    .commodity = commodity});
}
void clearTask(FreightProgram& p) {
    p.task = FreightProgramTask::None;
    p.taskFleetId.reset();
    p.shipment.reset();
}
void close(GameState& state, FreightProgram& p, const FreightProgramExecutionHooks& hooks) {
    if (p.lifecycle == FreightProgramLifecycle::Closed || freightCargoAboard(state, p.id) > 0.0)
        return;
    const Fleet* fleet = p.leasedFleetId ? find(state.fleets, *p.leasedFleetId) : nullptr;
    if (fleet && !stationary(*fleet))
        return;
    p.lifecycle = FreightProgramLifecycle::Closed;
    p.closedDay = state.date.day;
    p.issue = {};
    std::ostringstream summary;
    summary << (p.closure == FreightProgramClosure::Completed
                    ? "Delivery target met; empty fleet settled at operating base"
                    : "Future pickups cancelled; all committed cargo settled")
            << "; loaded=" << p.cargoLoaded << "; delivered=" << p.cargoDelivered
            << "; returned=" << p.cargoReturned << "; operating fuel loaded=" << p.fuelLoaded
            << "; burned=" << p.fuelBurned
            << "; unmet target=" << std::max(0.0, p.charter.totalQuantity - p.cargoDelivered);
    if (fleet)
        summary << "; final body=" << fleet->currentBodyId.value;
    std::optional<StockLocation> disposition;
    if (fleet && p.task == FreightProgramTask::ReturningCargo)
        disposition = p.charter.source;
    else if (fleet && p.task == FreightProgramTask::Unloading)
        disposition = p.charter.destination;
    else if (fleet && fleet->currentBodyId == stockLocationBody(state, p.charter.operatingBaseColonyId))
        disposition = p.charter.operatingBaseColonyId;
    else if (fleet && fleet->currentBodyId == stockLocationBody(state, p.charter.source))
        disposition = p.charter.source;
    else if (fleet && fleet->currentBodyId == stockLocationBody(state, p.charter.destination))
        disposition = p.charter.destination;
    audit(p, FreightProgramAuditKind::Closed, hooks, summary.str(), 0.0, disposition);
    p.leasedFleetId.reset();
    clearTask(p);
}
bool acquire(GameState& state, FreightProgram& p, OpeningProgramContext& context,
             const FreightProgramExecutionHooks& hooks) {
    if (p.leasedFleetId)
        return true;
    const auto desired = p.taskFleetId ? p.taskFleetId : p.charter.requestedFleetId;
    if (!desired || (!p.shipment && !p.charter.requestedLeaderId) ||
        context.occupiedFleets.contains(desired->value))
        return false;
    Fleet* fleet = find(state.fleets, *desired);
    if (!fleet || !stationary(*fleet) || !fleet->queuedOrders.empty())
        return false;
    for (ShipId id : fleet->shipIds) {
        const Ship* ship = find(state.ships, id);
        if (ship && ship->cargo && ship->cargo->programId != p.id)
            return false;
    }
    p.leasedFleetId = *desired;
    context.occupiedFleets.insert(desired->value);
    audit(p, FreightProgramAuditKind::LeaseAcquired, hooks, "Exclusive freight fleet control acquired");
    return true;
}

// Receipts are allocated before debits. Following this call all numerical
// results and per-hull changes have already been checked and cannot throw.
void receipt(GameState& state, FreightProgram& p, FreightTransferKind kind, StockLocation location,
             Commodity commodity, double amount) {
    if (!p.shipment || p.receipts.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error{"Freight receipt identity is exhausted"};
    p.receipts.push_back({static_cast<int>(p.receipts.size()) + 1, p.shipment->number, state.date.day,
                          p.shipment->fleetId, p.shipment->leaderId, location, commodity, kind, amount});
}
struct HullTransfer {
    Ship* ship;
    double amount;
};
bool finiteAdd(double a, double b) {
    return std::isfinite(a + b) && a + b >= 0.0;
}
// A finite result can still round away a positive transfer at extreme scales.
// Verify both debited and credited representable differences before mutation;
// the shared tolerance permits floating error, never an entirely lost amount.
bool representsTransfer(double before, double after, double amount) {
    const double change = std::abs(after - before);
    return std::isfinite(after) && after >= 0.0 &&
           (amount == 0.0 || (change > 0.0 && freightNearlyEqual(change, amount)));
}

double refill(GameState& state, FreightProgram& p, Fleet& fleet, Colony& source, double required,
              OpeningProgramContext& context, const FreightProgramExecutionHooks& hooks) {
    double left =
        std::min({std::max(0.0, required - freightFleetFuel(state, fleet)), freightFuelAllowanceRemaining(p),
                  context.available(state, source.id, ProcessedMaterial::Propellant,
                                    freightEffectiveFloor(p, source.id, ProcessedMaterial::Propellant))});
    std::vector<HullTransfer> changes;
    changes.reserve(fleet.shipIds.size());
    double total = 0.0;
    for (ShipId id : fleet.shipIds) {
        Ship* ship = find(state.ships, id);
        const ShipClass* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (!ship || !cls)
            continue;
        const double cap = evaluateShipDesign(state.shipComponents, cls->components).propellantCapacity;
        const double q = std::min(left, std::max(0.0, cap - ship->fuel));
        if (!finiteAdd(ship->fuel, q) || !finiteAdd(total, q) ||
            !representsTransfer(ship->fuel, ship->fuel + q, q))
            return 0.0;
        if (q > 0.0)
            changes.push_back({ship, q});
        total += q;
        left -= q;
    }
    if (!freightPositive(total) || !finiteAdd(p.fuelLoaded, total))
        return 0.0;
    const double stock = source.processedStockpile.get(ProcessedMaterial::Propellant);
    if (stock < total || !representsTransfer(stock, stock - total, total) ||
        !representsTransfer(p.fuelLoaded, p.fuelLoaded + total, total))
        return 0.0;
    receipt(state, p, FreightTransferKind::OperatingFuel, source.id, ProcessedMaterial::Propellant, total);
    for (const auto& change : changes)
        change.ship->fuel += change.amount;
    source.processedStockpile.set(ProcessedMaterial::Propellant, stock - total);
    context.debit(source.id, ProcessedMaterial::Propellant, total);
    p.fuelLoaded += total;
    audit(p, FreightProgramAuditKind::Transfer, hooks,
          "Operating propellant transferred from base stock into engine tanks", total, source.id,
          Commodity{ProcessedMaterial::Propellant});
    return total;
}

double load(GameState& state, FreightProgram& p, Fleet& fleet, StockLocation source,
            OpeningProgramContext& context, const FreightProgramExecutionHooks& hooks) {
    if (!p.shipment)
        return 0.0;
    double remaining = context.available(state, source, p.shipment->commodity,
                                         freightEffectiveFloor(p, source, p.shipment->commodity));
    const auto* rawSite =
        std::holds_alternative<Mineral>(p.shipment->commodity) ? std::get_if<SiteId>(&source) : nullptr;
    if (rawSite)
        remaining = std::min(remaining, context.availableSiteRawHandling(*rawSite));
    const auto capabilities = freightHullCapabilities(state, fleet);
    std::vector<HullTransfer> changes;
    changes.reserve(capabilities.size());
    double total = 0.0;
    for (const auto& cap : capabilities) {
        Ship* ship = find(state.ships, cap.shipId);
        const auto row = std::find_if(p.shipment->manifest.begin(), p.shipment->manifest.end(),
                                      [&](const auto& r) { return r.shipId == cap.shipId; });
        if (!ship || row == p.shipment->manifest.end() || cap.operationalHandlingPerDay <= 0.0)
            continue;
        const double aboard = ship->cargo ? ship->cargo->quantity : 0.0;
        const double q =
            std::min({remaining, cap.operationalHandlingPerDay, std::max(0.0, cap.capacity - aboard),
                      std::max(0.0, row->plannedQuantity - aboard)});
        if (!finiteAdd(aboard, q) || !finiteAdd(total, q) || !representsTransfer(aboard, aboard + q, q))
            return 0.0;
        if (q > 0.0)
            changes.push_back({ship, q});
        total += q;
        remaining -= q;
    }
    if (!freightPositive(total) || !finiteAdd(p.cargoLoaded, total))
        return 0.0;
    const double stock = stockQuantity(state, source, p.shipment->commodity);
    if (stock < total || !representsTransfer(stock, stock - total, total) ||
        !representsTransfer(p.cargoLoaded, p.cargoLoaded + total, total))
        return 0.0;
    receipt(state, p, FreightTransferKind::Load, source, p.shipment->commodity, total);
    for (const auto& c : changes) {
        if (!c.ship->cargo)
            c.ship->cargo = ShipCargo{p.id, p.shipment->number, p.shipment->commodity, c.amount};
        else
            c.ship->cargo->quantity += c.amount;
    }
    stockQuantity(state, source, p.shipment->commodity) = stock - total;
    if (rawSite)
        context.debitSiteRawHandling(*rawSite, total);
    context.debit(source, p.shipment->commodity, total);
    p.cargoLoaded += total;
    audit(p, FreightProgramAuditKind::Transfer, hooks, "Cargo loaded from source into identified hulls",
          total, source, p.shipment->commodity);
    return total;
}

double unload(GameState& state, FreightProgram& p, Fleet& fleet, StockLocation location, bool toSource,
              OpeningProgramContext& context, const FreightProgramExecutionHooks& hooks) {
    if (!p.shipment)
        return 0.0;
    const auto* rawSite =
        std::holds_alternative<Mineral>(p.shipment->commodity) ? std::get_if<SiteId>(&location) : nullptr;
    double room =
        rawSite ? std::min(context.availableSiteRawHandling(*rawSite), context.availableSiteRawRoom(*rawSite))
                : std::numeric_limits<double>::infinity();
    std::vector<HullTransfer> changes;
    changes.reserve(fleet.shipIds.size());
    double total = 0.0;
    for (const auto& cap : freightHullCapabilities(state, fleet)) {
        Ship* ship = find(state.ships, cap.shipId);
        if (!ship || !ship->cargo || ship->cargo->programId != p.id)
            continue;
        const double q = std::min({ship->cargo->quantity, cap.operationalHandlingPerDay, room});
        if (!finiteAdd(total, q) || !representsTransfer(ship->cargo->quantity, ship->cargo->quantity - q, q))
            return 0.0;
        if (q > 0.0)
            changes.push_back({ship, q});
        total += q;
        room -= q;
    }
    double& counter = toSource ? p.cargoReturned : p.cargoDelivered;
    const double stock = stockQuantity(state, location, p.shipment->commodity);
    if (!freightPositive(total) || !finiteAdd(counter, total) || !finiteAdd(stock, total) ||
        !representsTransfer(stock, stock + total, total) ||
        !representsTransfer(counter, counter + total, total))
        return 0.0;
    receipt(state, p, toSource ? FreightTransferKind::SourceReturn : FreightTransferKind::Delivery, location,
            p.shipment->commodity, total);
    for (const auto& c : changes) {
        c.ship->cargo->quantity -= c.amount;
        // The debit uses the exact remaining lot on its final day; no positive
        // initial fractional shipment is dropped by an integer/tolerance gate.
        if (c.ship->cargo->quantity <= 0.0)
            c.ship->cargo.reset();
    }
    stockQuantity(state, location, p.shipment->commodity) = stock + total;
    if (rawSite) {
        context.debitSiteRawHandling(*rawSite, total);
        context.debitSiteRawRoom(*rawSite, total);
    }
    counter += total;
    audit(p, FreightProgramAuditKind::Transfer, hooks,
          toSource ? "Unshipped cargo returned to source stock" : "Cargo delivered into destination stock",
          total, location, p.shipment->commodity);
    return total;
}

bool startLeg(GameState& state, FreightProgram& p, Fleet& fleet, BodyId target,
              const FreightProgramExecutionHooks& hooks) {
    const double estimate =
        adjustedFleetMoveFuelCost(state, fleet, fleet.currentBodyId, target, state.date.day);
    if (!std::isfinite(estimate) || !finiteAdd(p.fuelBurned, estimate) ||
        !representsTransfer(p.fuelBurned, p.fuelBurned + estimate, estimate) ||
        !freightFuelDebitRepresentable(state, fleet, estimate) || freightFleetFuel(state, fleet) < estimate)
        return false;
    double paid = 0.0;
    if (!hooks.startProgramMove(p.id, fleet.id, target, paid))
        return false;
    p.fuelBurned += paid;
    audit(p, FreightProgramAuditKind::Departure, hooks,
          "Program leg started; engine fuel paid through common movement", paid);
    return true;
}
std::vector<FreightManifestRow> actualManifest(const GameState& state, const FreightProgram& p,
                                               const Fleet& fleet) {
    std::vector<FreightManifestRow> rows;
    rows.reserve(fleet.shipIds.size());
    for (ShipId id : fleet.shipIds) {
        const Ship* ship = find(state.ships, id);
        rows.push_back(
            {id, ship && ship->cargo && ship->cargo->programId == p.id ? ship->cargo->quantity : 0.0});
    }
    return rows;
}

// A fuel top-up is the day's only physical action. Reprice tomorrow, since the
// same fleet cannot both refill and depart today. Payload never supplies tanks.
bool depart(GameState& state, FreightProgram& p, Fleet& fleet, StockLocation source,
            StockLocation destination, OpeningProgramContext& context,
            const FreightProgramExecutionHooks& hooks) {
    if (freightCargoAboard(state, p.id) <= 0.0)
        return false;
    const auto manifest = actualManifest(state, p, fleet);
    const auto plan = evaluateFreightManifest(state, p, fleet, manifest, state.date.day);
    if (!plan.ready)
        return false;
    if (plan.additionalFuel > 0.0 && !freightNearlyEqual(plan.additionalFuel, 0.0)) {
        if (state.date.day == std::numeric_limits<std::int64_t>::max())
            return false;
        const auto tomorrow = evaluateFreightManifest(state, p, fleet, manifest, state.date.day + 1);
        if (tomorrow.ready && !freightIsCollection(p))
            static_cast<void>(refill(state, p, fleet, *find(state.colonies, p.charter.operatingBaseColonyId),
                                     tomorrow.requiredFuel, context, hooks));
        return false;
    }
    if (stockLocationBody(state, source) == stockLocationBody(state, destination)) {
        p.shipment->manifest = manifest;
        p.task = FreightProgramTask::Unloading;
        return true;
    }
    if (!startLeg(state, p, fleet, stockLocationBody(state, destination), hooks))
        return false;
    p.shipment->manifest = manifest;
    p.task = FreightProgramTask::Outbound;
    return true;
}

void commit(GameState& state, FreightProgram& p, const Fleet& fleet, FreightShipmentPlan plan,
            const FreightProgramExecutionHooks& hooks) {
    if (p.nextShipmentNumber == std::numeric_limits<int>::max())
        throw std::runtime_error{"Freight shipment identity exhausted"};
    p.shipment = FreightShipment{p.nextShipmentNumber++,
                                 p.charterRevision,
                                 state.date.day,
                                 fleet.id,
                                 *p.charter.requestedLeaderId,
                                 p.charter.source,
                                 p.charter.destination,
                                 p.charter.operatingBaseColonyId,
                                 p.charter.commodity,
                                 std::move(plan.manifest)};
    p.taskFleetId = fleet.id;
    p.task = FreightProgramTask::Preparing;
    audit(p, FreightProgramAuditKind::ShipmentPlanned, hooks,
          "Finite per-hull shipment committed under the current charter", plan.quantity);
}

void handleCancellation(GameState& state, FreightProgram& p, Fleet& fleet, OpeningProgramContext& context,
                        const FreightProgramExecutionHooks& hooks) {
    if (!stationary(fleet))
        return;
    if (freightCargoAboard(state, p.id) <= 0.0) {
        close(state, p, hooks);
        return;
    }
    const bool unshipped = p.task == FreightProgramTask::Preparing || p.task == FreightProgramTask::Loading ||
                           p.task == FreightProgramTask::ReturningCargo;
    p.task = unshipped ? FreightProgramTask::ReturningCargo : FreightProgramTask::Unloading;
    const auto location = unshipped ? p.charter.source : p.charter.destination;
    if (fleet.currentBodyId != stockLocationBody(state, location))
        return;
    static_cast<void>(unload(state, p, fleet, location, unshipped, context, hooks));
    if (freightCargoAboard(state, p.id) <= 0.0)
        close(state, p, hooks);
}

// Only changes in stable causal categories interrupt. Initial waiting (no real
// transfers/burn) is ordinary intent, and fluctuating estimated fuel numbers do
// not produce new issue identities every day.
std::optional<std::pair<std::string, std::string>> cause(const GameState& state, const FreightProgram& p) {
    if (p.lifecycle == FreightProgramLifecycle::Closed || p.lifecycle == FreightProgramLifecycle::Suspended)
        return std::nullopt;
    if (p.cargoLoaded == 0.0 && p.fuelLoaded == 0.0 && p.fuelBurned == 0.0)
        return std::nullopt;
    const auto id = p.leasedFleetId ? p.leasedFleetId : p.taskFleetId;
    const Fleet* fleet = id ? find(state.fleets, *id) : nullptr;
    if (!fleet || !stationary(*fleet))
        return std::nullopt;
    const auto readiness = freightProgramReadiness(state, p);
    // An uncommitted wait for another real cargo batch remains ordinary intent.
    // Existing delivery programs only interrupt here for exhausted authority.
    if (!p.shipment && readiness.cause != FreightReadinessCause::FuelAllowance)
        return std::nullopt;
    if (readiness.cause != FreightReadinessCause::None &&
        readiness.cause != FreightReadinessCause::Participants)
        return std::pair{readiness.cause == FreightReadinessCause::FuelAllowance ? std::string{"allowance"}
                         : readiness.cause == FreightReadinessCause::Precision
                             ? std::string{"fuel-precision"}
                             : "readiness-" + std::to_string(static_cast<int>(readiness.cause)),
                         readiness.message};

    return std::nullopt;
}
void updateIssue(GameState& state, FreightProgram& p, const FreightProgramExecutionHooks& hooks) {
    if (p.lifecycle == FreightProgramLifecycle::Suspended)
        return;
    const auto next = cause(state, p);
    if (!next) {
        // An acknowledged known cause stays latent during active transit; it
        // must not be resurrected merely by suspension/resumption bookkeeping.
        const Fleet* fleet = p.leasedFleetId ? find(state.fleets, *p.leasedFleetId) : nullptr;
        if (!p.issue.acknowledged || !fleet || stationary(*fleet))
            p.issue = {};
        return;
    }
    const std::string signature = next->first;
    if (p.issue.signature == signature)
        return;
    p.issue = {signature, next->second, false};
    audit(p, FreightProgramAuditKind::IssueRaised, hooks, next->second);
}
void writeReport(GameState& state, FreightProgram& p, const FreightProgramExecutionHooks& hooks) {
    if (state.date.day < p.nextReportDay || (p.closedDay && state.date.day > *p.closedDay))
        return;
    // Resolve the next date before appending history or changing cursors. The
    // shared clock rejects dates without a representable future boundary.
    const auto nextReportDay = nextGlobalSurveyBoundary(state.date.day, 30);
    if (state.date.day == std::numeric_limits<std::int64_t>::max())
        throw std::runtime_error{"Freight report date limit reached"};
    const auto fleetId = p.leasedFleetId ? p.leasedFleetId : p.taskFleetId;
    const Fleet* fleet = fleetId ? find(state.fleets, *fleetId) : nullptr;
    p.reports.push_back({p.reportStartDay, state.date.day, state.date.day % 90 == 0, p.charterRevision,
                         p.cargoLoaded - p.reportedCargoLoaded, p.cargoDelivered - p.reportedCargoDelivered,
                         p.cargoReturned - p.reportedCargoReturned, p.fuelLoaded - p.reportedFuelLoaded,
                         p.fuelBurned - p.reportedFuelBurned, freightCargoAboard(state, p.id),
                         p.nextShipmentNumber - 1 - p.reportedShipments, p.charter.totalQuantity,
                         p.cargoDelivered, p.shipment ? freightShipmentPlannedQuantity(*p.shipment) : 0.0,
                         fleetId, fleet ? std::optional<BodyId>{fleet->currentBodyId} : std::nullopt,
                         freightProgramExecutionCondition(state, p), p.charter.policy});
    p.reportedCargoLoaded = p.cargoLoaded;
    p.reportedCargoDelivered = p.cargoDelivered;
    p.reportedCargoReturned = p.cargoReturned;
    p.reportedFuelLoaded = p.fuelLoaded;
    p.reportedFuelBurned = p.fuelBurned;
    p.reportedShipments = p.nextShipmentNumber - 1;
    p.reportStartDay = state.date.day + 1;
    p.nextReportDay = nextReportDay;
    audit(p, FreightProgramAuditKind::ReportPublished, hooks,
          state.date.day % 90 == 0 ? "90-day freight review published" : "30-day freight report published");
}
} // namespace

bool releaseFreightProgramLease(GameState& state, FreightProgram& p) {
    if (freightCargoAboard(state, p.id) > 0.0)
        return false;
    const Fleet* fleet = p.leasedFleetId ? find(state.fleets, *p.leasedFleetId) : nullptr;
    if (fleet && !stationary(*fleet))
        return false;
    p.leasedFleetId.reset();
    return true;
}
void settleFreightCancellationAtDecision(GameState& state, FreightProgram& p,
                                         const FreightProgramExecutionHooks& hooks) {
    if (p.closure != FreightProgramClosure::Cancelled)
        return;
    const Fleet* fleet = p.leasedFleetId ? find(state.fleets, *p.leasedFleetId) : nullptr;
    if (freightCargoAboard(state, p.id) <= 0.0 && (!fleet || stationary(*fleet)))
        close(state, p, hooks);
    else if (freightCargoAboard(state, p.id) > 0.0 &&
             (p.task == FreightProgramTask::Preparing || p.task == FreightProgramTask::Loading))
        p.task = FreightProgramTask::ReturningCargo;
}
void acknowledgeKnownFreightLimitAtDecision(const GameState& state, FreightProgram& p) {
    const auto known = cause(state, p);
    if (known)
        p.issue = {known->first, known->second, true};
    else if (!p.issue.signature.empty())
        p.issue.acknowledged = true;
}

void runFreightProgramOpeningDay(GameState& state, FreightProgram& p, OpeningProgramContext& context,
                                 const FreightProgramExecutionHooks& hooks) {
    if (p.lifecycle == FreightProgramLifecycle::Closed)
        return;
    if (p.lifecycle == FreightProgramLifecycle::Suspended) {
        static_cast<void>(releaseFreightProgramLease(state, p));
        return;
    }
    if (!acquire(state, p, context, hooks))
        return;
    Fleet* fleet = find(state.fleets, *p.leasedFleetId);
    Colony* base = find(state.colonies, p.charter.operatingBaseColonyId);
    if (!fleet || !base || !stationary(*fleet))
        return;
    const BodyId source = stockLocationBody(state, p.charter.source),
                 destination = stockLocationBody(state, p.charter.destination);
    const bool collection = freightIsCollection(p);
    if (p.closure == FreightProgramClosure::Cancelled) {
        handleCancellation(state, p, *fleet, context, hooks);
        return;
    }
    if ((p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition) &&
        fleet->currentBodyId == base->bodyId)
        clearTask(p);
    if (p.task == FreightProgramTask::None && fleet->currentBodyId == base->bodyId) {
        if (targetMet(p)) {
            p.lifecycle = FreightProgramLifecycle::Closing;
            p.closure = FreightProgramClosure::Completed;
            close(state, p, hooks);
            return;
        }
        if (p.charter.requestedFleetId != p.leasedFleetId) {
            static_cast<void>(releaseFreightProgramLease(state, p));
            return;
        }
    }
    if (p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition ||
        (!p.shipment && fleet->currentBodyId != base->bodyId)) {
        if (startLeg(state, p, *fleet, base->bodyId, hooks)) {
            if (p.task == FreightProgramTask::None)
                p.task = FreightProgramTask::Reposition;
            p.taskFleetId = fleet->id;
        }
        return;
    }
    if (p.task == FreightProgramTask::Collecting) {
        if (fleet->currentBodyId != source) {
            // A released empty fleet may have moved manually while suspended.
            // Reacquisition retains the committed roster and pays its real leg.
            static_cast<void>(startLeg(state, p, *fleet, source, hooks));
            return;
        }
        p.task = FreightProgramTask::Loading;
    }
    if (p.task == FreightProgramTask::Outbound) {
        if (fleet->currentBodyId != destination)
            return;
        p.task = FreightProgramTask::Unloading;
    }
    if (p.task == FreightProgramTask::Unloading) {
        if (fleet->currentBodyId != destination)
            return;
        static_cast<void>(unload(state, p, *fleet, p.charter.destination, false, context, hooks));
        if (freightCargoAboard(state, p.id) <= 0.0) {
            p.task = FreightProgramTask::Return;
            if (targetMet(p)) {
                p.lifecycle = FreightProgramLifecycle::Closing;
                p.closure = FreightProgramClosure::Completed;
            }
            // A collection is already physically at its base after settlement.
            if (collection) {
                clearTask(p);
                if (targetMet(p))
                    close(state, p, hooks);
            }
        }
        return;
    }
    if (!p.shipment) {
        const auto plan = planFreightShipment(state, p, *fleet, &context);
        if (!plan.ready)
            return;
        commit(state, p, *fleet, plan, hooks);
        if (plan.additionalFuel > 0.0) {
            static_cast<void>(refill(state, p, *fleet, *base, plan.requiredFuel, context, hooks));
            if (!collection)
                p.task = FreightProgramTask::Loading;
            return;
        }
    }
    if (p.task == FreightProgramTask::Preparing && collection) {
        if (fleet->currentBodyId != base->bodyId) {
            static_cast<void>(startLeg(state, p, *fleet, base->bodyId, hooks));
            return;
        }
        const auto plan =
            evaluateFreightManifest(state, p, *fleet, p.shipment->manifest, state.date.day, true);
        if (!plan.ready)
            return;
        if (plan.additionalFuel > 0.0 && !freightNearlyEqual(plan.additionalFuel, 0.0)) {
            if (state.date.day == std::numeric_limits<std::int64_t>::max())
                return;
            const auto tomorrow =
                evaluateFreightManifest(state, p, *fleet, p.shipment->manifest, state.date.day + 1, true);
            if (tomorrow.ready)
                static_cast<void>(refill(state, p, *fleet, *base, tomorrow.requiredFuel, context, hooks));
            return;
        }
        if (base->bodyId == source) {
            p.task = FreightProgramTask::Loading;
        } else {
            if (startLeg(state, p, *fleet, source, hooks))
                p.task = FreightProgramTask::Collecting;
            return;
        }
    }
    if (p.task == FreightProgramTask::Preparing)
        p.task = FreightProgramTask::Loading;
    if (fleet->currentBodyId != source) {
        if (freightCargoAboard(state, p.id) <= 0.0 && p.task == FreightProgramTask::Loading)
            static_cast<void>(startLeg(state, p, *fleet, source, hooks));
        return;
    }
    if (p.task == FreightProgramTask::Loading) {
        const double aboard = freightCargoAboard(state, p.id),
                     planned = freightShipmentPlannedQuantity(*p.shipment);
        const double available =
            context.available(state, p.charter.source, p.charter.commodity,
                              freightEffectiveFloor(p, p.charter.source, p.charter.commodity));
        if (aboard >= planned || (aboard > 0.0 && freightNearlyEqual(aboard, planned)) ||
            (!collection && aboard > 0.0 && available <= 0.0)) {
            static_cast<void>(
                depart(state, p, *fleet, p.charter.source, p.charter.destination, context, hooks));
            return;
        }
        static_cast<void>(load(state, p, *fleet, p.charter.source, context, hooks));
    }
}

void finishFreightProgramsDay(GameState& state, const FreightProgramExecutionHooks& hooks) {
    for (auto& p : state.freightPrograms) {
        if (p.lifecycle != FreightProgramLifecycle::Closed) {
            Fleet* fleet = p.leasedFleetId ? find(state.fleets, *p.leasedFleetId) : nullptr;
            if (fleet && stationary(*fleet)) {
                if (p.lifecycle == FreightProgramLifecycle::Suspended)
                    static_cast<void>(releaseFreightProgramLease(state, p));
                else if (p.closure == FreightProgramClosure::Cancelled &&
                         freightCargoAboard(state, p.id) <= 0.0)
                    close(state, p, hooks);
                else if (p.task == FreightProgramTask::Outbound)
                    p.task = FreightProgramTask::Unloading;
                else if (p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition) {
                    const Colony* source = find(state.colonies, p.charter.operatingBaseColonyId);
                    if (source && fleet->currentBodyId == source->bodyId) {
                        clearTask(p);
                        if (targetMet(p)) {
                            p.closure = FreightProgramClosure::Completed;
                            close(state, p, hooks);
                        }
                    }
                }
            }
            updateIssue(state, p, hooks);
        }
        writeReport(state, p, hooks);
    }
}
} // namespace deep
