#include "sim/FreightProgramValidation.h"

#include "sim/FreightProgramRules.h"
#include "sim/ShipDesignRules.h"
#include "sim/SurveyProgramRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace deep {
namespace {
void check(bool condition, const char* why) {
    if (!condition)
        throw std::runtime_error{why};
}
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto i = std::find_if(rows.begin(), rows.end(), [id](const T& r) { return r.id == id; });
    return i == rows.end() ? nullptr : &*i;
}
bool nonnegative(double n) {
    return std::isfinite(n) && n >= 0.0;
}
bool atMost(double a, double b) {
    return a <= b || freightNearlyEqual(a, b);
}
struct ShipmentAccounting {
    FleetId fleetId;
    PersonId leaderId;
    bool participants = false;
    double loaded = 0.0;
    double delivered = 0.0;
    double returned = 0.0;
};
} // namespace

void validateFreightProgramState(const GameState& state) {
    std::unordered_set<std::int64_t> ids, owners;
    for (const auto& p : state.surveyPrograms)
        if (p.leasedFleetId)
            owners.insert(p.leasedFleetId->value);
    for (const FreightProgram& p : state.freightPrograms) {
        check(p.id.value > 0 && ids.insert(p.id.value).second,
              "freight program IDs must be unique and positive");
        check(p.id.value < state.ids.nextFreightProgramId,
              "freight program ID counter must exceed allocated IDs");
        check(!validateFreightProgramCharter(state, p.charter, true), "freight charter is malformed");
        check(p.createdDay >= 0 && p.createdDay <= state.date.day && p.charterRevision > 0,
              "freight creation date or revision is invalid");
        check(p.lifecycle >= FreightProgramLifecycle::Authorized &&
                  p.lifecycle <= FreightProgramLifecycle::Closed &&
                  p.closure >= FreightProgramClosure::None && p.closure <= FreightProgramClosure::Cancelled &&
                  p.task >= FreightProgramTask::None && p.task <= FreightProgramTask::ReturningCargo,
              "freight lifecycle/task is invalid");
        check(p.nextShipmentNumber > 0, "freight shipment counter must be positive");
        if (p.lifecycle == FreightProgramLifecycle::Authorized)
            check(p.closure == FreightProgramClosure::None, "authorized freight cannot have closure intent");
        if (p.lifecycle == FreightProgramLifecycle::Closing)
            check(p.closure != FreightProgramClosure::None, "closing freight requires closure intent");
        if (p.closure == FreightProgramClosure::Completed)
            check(atMost(p.charter.totalQuantity, p.cargoDelivered),
                  "freight completion return requires delivered target");
        const double aboard = freightCargoAboard(state, p.id);
        check(nonnegative(aboard), "freight onboard sum is not finite");
        if (p.lifecycle == FreightProgramLifecycle::Closed) {
            check(p.closedDay && *p.closedDay >= p.createdDay && *p.closedDay <= state.date.day &&
                      p.closure != FreightProgramClosure::None && !p.leasedFleetId && !p.taskFleetId &&
                      !p.shipment && p.task == FreightProgramTask::None && aboard == 0.0,
                  "closed freight retains custody, task, lease, or invalid closure date");
            if (p.closure == FreightProgramClosure::Completed)
                check(atMost(p.charter.totalQuantity, p.cargoDelivered), "completed freight target is unmet");
        } else
            check(!p.closedDay, "open freight cannot have a closed date");
        if (p.leasedFleetId) {
            const Fleet* fleet = find(state.fleets, *p.leasedFleetId);
            check(fleet && fleet->queuedOrders.empty() && owners.insert(p.leasedFleetId->value).second,
                  "freight fleet has manual queue, dangling reference, or duplicate survey/freight owner");
        }
        if (p.task == FreightProgramTask::None)
            check(!p.taskFleetId && !p.shipment, "idle freight retains committed task data");
        else {
            check(p.taskFleetId && find(state.fleets, *p.taskFleetId),
                  "freight task needs its original fleet");
            if (p.leasedFleetId)
                check(p.leasedFleetId == p.taskFleetId, "freight lease differs from original task fleet");
            check(p.shipment.has_value() == (p.task != FreightProgramTask::Reposition),
                  "freight shipment/task shape is inconsistent");
        }
        if (p.shipment) {
            const auto& s = *p.shipment;
            const Fleet* fleet = find(state.fleets, s.fleetId);
            check(s.number > 0 && s.number < p.nextShipmentNumber && s.charterRevision > 0 &&
                      s.charterRevision <= p.charterRevision && s.committedDay >= p.createdDay &&
                      s.committedDay <= state.date.day && fleet && find(state.people, s.leaderId) &&
                      p.taskFleetId == s.fleetId,
                  "freight shipment identity or participants are invalid");
            check(s.sourceColonyId == p.charter.sourceColonyId &&
                      s.destinationColonyId == p.charter.destinationColonyId &&
                      s.material == p.charter.material,
                  "freight shipment changed its fixed contract identity");
            check(s.manifest.size() == fleet->shipIds.size(), "freight committed roster size changed");
            double planned = 0.0;
            for (std::size_t i = 0; i < s.manifest.size(); ++i) {
                const auto& row = s.manifest[i];
                const Ship* ship = find(state.ships, row.shipId);
                const ShipClass* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
                check(row.shipId == fleet->shipIds[i] && ship && cls && nonnegative(row.plannedQuantity),
                      "freight committed roster/order or quantity is invalid");
                check(atMost(row.plannedQuantity,
                             evaluateShipDesign(state.shipComponents, cls->components).cargoCapacity),
                      "freight planned per-hull cargo exceeds capacity");
                planned += row.plannedQuantity;
            }
            check(freightPositive(planned), "freight shipment needs a finite positive planned maximum");
            check(atMost(freightShipmentLoadedQuantity(p), planned),
                  "freight shipment loaded beyond its committed maximum");
        }
        check(aboard == 0.0 || (p.shipment && p.leasedFleetId && p.taskFleetId == p.leasedFleetId),
              "freight cargo custody requires original shipment and actual fleet lease");
        if (p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition)
            check(aboard == 0.0, "freight return/reposition cannot carry an unsettled cargo lot");
        if (p.leasedFleetId) {
            const Fleet& fleet = *find(state.fleets, *p.leasedFleetId);
            const BodyId source = find(state.colonies, p.charter.sourceColonyId)->bodyId;
            const BodyId destination = find(state.colonies, p.charter.destinationColonyId)->bodyId;
            if (fleet.activeOrder.type == FleetOrderType::MoveToBody) {
                const bool outward = p.task == FreightProgramTask::Outbound;
                const bool returning =
                    p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition ||
                    ((p.task == FreightProgramTask::Loading || p.task == FreightProgramTask::Preparing ||
                      p.task == FreightProgramTask::ReturningCargo) &&
                     aboard == 0.0);
                check(outward || returning, "freight task cannot own this active movement leg");
                check(fleet.destinationBodyId == (outward ? destination : source),
                      "freight active trajectory targets the wrong contract endpoint");
            } else {
                if (p.task == FreightProgramTask::Unloading || p.task == FreightProgramTask::Outbound)
                    check(fleet.currentBodyId == destination,
                          "freight unloading/outbound arrival is away from destination");
                if (p.task == FreightProgramTask::ReturningCargo ||
                    ((p.task == FreightProgramTask::Loading || p.task == FreightProgramTask::Preparing) &&
                     aboard > 0.0))
                    check(fleet.currentBodyId == source, "unshipped freight cargo is away from source");
            }
        }

        std::array<double, 4> sums{};
        std::map<int, ShipmentAccounting> shipments;
        std::int64_t previousDay = p.createdDay;
        int previousShipment = 0;
        for (std::size_t i = 0; i < p.receipts.size(); ++i) {
            const auto& r = p.receipts[i];
            check(i < static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                      r.sequence == static_cast<int>(i) + 1 && r.shipmentNumber > 0 &&
                      r.shipmentNumber < p.nextShipmentNumber && r.shipmentNumber >= previousShipment &&
                      r.day >= previousDay && r.day <= state.date.day && freightPositive(r.amount),
                  "freight receipt identity, ordering, date or amount is invalid");
            check(find(state.fleets, r.fleetId) && find(state.people, r.leaderId) &&
                      find(state.colonies, r.colonyId),
                  "freight receipt participant/location is dangling");
            check(r.kind >= FreightTransferKind::Load && r.kind <= FreightTransferKind::OperatingFuel,
                  "freight receipt transfer kind is invalid");
            const ColonyId expected = r.kind == FreightTransferKind::Delivery ? p.charter.destinationColonyId
                                                                              : p.charter.sourceColonyId;
            check(r.colonyId == expected && r.material == (r.kind == FreightTransferKind::OperatingFuel
                                                               ? ProcessedMaterial::Propellant
                                                               : p.charter.material),
                  "freight receipt contradicts contract location/material");
            auto& a = shipments[r.shipmentNumber];
            if (!a.participants) {
                a.fleetId = r.fleetId;
                a.leaderId = r.leaderId;
                a.participants = true;
            }
            check(a.fleetId == r.fleetId && a.leaderId == r.leaderId,
                  "freight receipt changed committed shipment participants");
            if (r.kind == FreightTransferKind::Load)
                a.loaded += r.amount;
            else if (r.kind == FreightTransferKind::Delivery)
                a.delivered += r.amount;
            else if (r.kind == FreightTransferKind::SourceReturn)
                a.returned += r.amount;
            check(atMost(a.delivered + a.returned, a.loaded),
                  "freight receipt disposes of cargo before it was loaded");
            sums[static_cast<std::size_t>(r.kind)] += r.amount;
            previousDay = r.day;
            previousShipment = r.shipmentNumber;
        }
        for (const auto& [number, a] : shipments) {
            const bool current = p.shipment && p.shipment->number == number;
            check(freightNearlyEqual(a.loaded, a.delivered + a.returned + (current ? aboard : 0.0)),
                  "freight shipment receipts do not conserve physical cargo");
            if (current)
                check(a.fleetId == p.shipment->fleetId && a.leaderId == p.shipment->leaderId,
                      "freight current shipment disagrees with prior receipts");
        }
        check(nonnegative(p.cargoLoaded) && nonnegative(p.cargoDelivered) && nonnegative(p.cargoReturned) &&
                  nonnegative(p.fuelLoaded) && nonnegative(p.fuelBurned) &&
                  freightNearlyEqual(p.cargoLoaded, sums[0]) &&
                  freightNearlyEqual(p.cargoDelivered, sums[1]) &&
                  freightNearlyEqual(p.cargoReturned, sums[2]) && freightNearlyEqual(p.fuelLoaded, sums[3]) &&
                  freightNearlyEqual(p.cargoLoaded, p.cargoDelivered + p.cargoReturned + aboard),
              "freight counters disagree with receipts or physical cargo");

        double burned = 0.0;
        std::unordered_set<int> plannedNumbers;
        bool closedAudit = false;
        for (const SimEvent& event : state.eventLog)
            if (const auto* e = std::get_if<FreightProgramAuditEvent>(&event.payload);
                e && e->programId == p.id) {
                check(e->kind >= FreightProgramAuditKind::Authorized &&
                          e->kind <= FreightProgramAuditKind::IssueRaised && nonnegative(e->amount) &&
                          e->charterRevision > 0 && e->charterRevision <= p.charterRevision &&
                          e->shipmentNumber >= 0 && e->shipmentNumber < p.nextShipmentNumber,
                      "freight audit numeric/enum/identity is invalid");
                check(event.day >= p.createdDay && event.day <= state.date.day,
                      "freight audit date predates its program or exceeds the world date");
                if (e->fleetId)
                    check(find(state.fleets, *e->fleetId), "freight audit fleet is dangling");
                if (e->colonyId)
                    check(find(state.colonies, *e->colonyId), "freight audit colony is dangling");
                if (e->leaderId)
                    check(find(state.people, *e->leaderId), "freight audit leader is dangling");
                if (e->kind == FreightProgramAuditKind::Departure)
                    burned += e->amount;
                if (e->kind == FreightProgramAuditKind::ShipmentPlanned)
                    check(e->shipmentNumber > 0 && plannedNumbers.insert(e->shipmentNumber).second,
                          "freight shipment commitment audit is duplicated");
                if (e->kind == FreightProgramAuditKind::ShipmentPlanned && p.shipment &&
                    e->shipmentNumber == p.shipment->number)
                    check(event.day == p.shipment->committedDay && e->fleetId == p.shipment->fleetId &&
                              e->leaderId == p.shipment->leaderId &&
                              e->charterRevision == p.shipment->charterRevision &&
                              atMost(freightShipmentPlannedQuantity(*p.shipment), e->amount),
                          "freight active shipment disagrees with its original commitment audit");
                if (e->kind == FreightProgramAuditKind::Closed) {
                    check(!closedAudit && p.closedDay && event.day == *p.closedDay,
                          "freight closure audit is duplicated or disagrees with closed date");
                    closedAudit = true;
                    if (p.closure == FreightProgramClosure::Completed)
                        check(e->colonyId == p.charter.sourceColonyId,
                              "completed freight closure lacks physical source disposition");
                }
            }
        check(freightNearlyEqual(burned, p.fuelBurned),
              "freight fuel burn disagrees with common departure audit");
        check(plannedNumbers.size() == static_cast<std::size_t>(p.nextShipmentNumber - 1),
              "freight shipment identity history is incomplete");
        check(closedAudit == (p.lifecycle == FreightProgramLifecycle::Closed),
              "freight closure audit and lifecycle disagree");

        check(p.reportStartDay >= p.createdDay && p.nextReportDay > p.createdDay &&
                  p.nextReportDay % 30 == 0 && p.reportedShipments >= 0 &&
                  p.reportedShipments < p.nextShipmentNumber,
              "freight report cursor is invalid");
        std::array<double, 5> reported{};
        int reportedShipments = 0;
        std::int64_t start = p.createdDay;
        std::int64_t due = nextGlobalSurveyBoundary(p.createdDay, 30);
        const std::int64_t through = p.closedDay ? *p.closedDay : state.date.day;
        for (const auto& r : p.reports) {
            check(r.startDay == start && r.endDay == due && r.endDay >= r.startDay &&
                      r.endDay <= state.date.day && r.endDay % 30 == 0 &&
                      r.isNinetyDayReview == (r.endDay % 90 == 0) && r.charterRevision > 0 &&
                      r.charterRevision <= p.charterRevision && (!p.closedDay || r.endDay <= *p.closedDay),
                  "freight report dates/order/revision are invalid");
            std::array<double, 4> actual{};
            double actualBurn = 0.0;
            int started = 0;
            for (const auto& transfer : p.receipts)
                if (transfer.day >= r.startDay && transfer.day <= r.endDay)
                    actual[static_cast<std::size_t>(transfer.kind)] += transfer.amount;
            for (const auto& event : state.eventLog)
                if (event.day >= r.startDay && event.day <= r.endDay)
                    if (const auto* e = std::get_if<FreightProgramAuditEvent>(&event.payload);
                        e && e->programId == p.id) {
                        if (e->kind == FreightProgramAuditKind::Departure)
                            actualBurn += e->amount;
                        if (e->kind == FreightProgramAuditKind::ShipmentPlanned)
                            ++started;
                    }
            check(freightNearlyEqual(r.cargoLoaded, actual[0]) &&
                      freightNearlyEqual(r.cargoDelivered, actual[1]) &&
                      freightNearlyEqual(r.cargoReturned, actual[2]) &&
                      freightNearlyEqual(r.fuelLoaded, actual[3]) &&
                      freightNearlyEqual(r.fuelBurned, actualBurn) && r.shipmentsStarted == started,
                  "freight report totals disagree with dated receipts/audits");
            check(nonnegative(r.cargoAboard) && nonnegative(r.targetQuantity) &&
                      nonnegative(r.cumulativeDelivered) && nonnegative(r.committedQuantity),
                  "freight report snapshot is nonfinite or negative");
            check(nonnegative(r.policy.sourceCargoFloor) && nonnegative(r.policy.sourcePropellantFloor) &&
                      nonnegative(r.policy.returnContingencyFraction) &&
                      (!r.policy.maxAdditionalPropellant || nonnegative(*r.policy.maxAdditionalPropellant)),
                  "freight report policy snapshot is nonfinite or negative");
            double historicalLoaded = 0.0, historicalDelivered = 0.0, historicalReturned = 0.0;
            for (const auto& transfer : p.receipts)
                if (transfer.day <= r.endDay) {
                    if (transfer.kind == FreightTransferKind::Load)
                        historicalLoaded += transfer.amount;
                    else if (transfer.kind == FreightTransferKind::Delivery)
                        historicalDelivered += transfer.amount;
                    else if (transfer.kind == FreightTransferKind::SourceReturn)
                        historicalReturned += transfer.amount;
                }
            check(freightNearlyEqual(r.cumulativeDelivered, historicalDelivered) &&
                      freightNearlyEqual(r.cargoAboard,
                                         historicalLoaded - historicalDelivered - historicalReturned),
                  "freight report historical cargo snapshot disagrees with receipts");
            check(r.fleetId.has_value() == r.fleetBodyId.has_value(),
                  "freight report fleet/location snapshot is incomplete");
            if (r.fleetId)
                check(find(state.fleets, *r.fleetId), "freight report fleet is dangling");
            if (r.fleetBodyId)
                check(find(state.bodies, *r.fleetBodyId), "freight report body is dangling");
            reported[0] += r.cargoLoaded;
            reported[1] += r.cargoDelivered;
            reported[2] += r.cargoReturned;
            reported[3] += r.fuelLoaded;
            reported[4] += r.fuelBurned;
            reportedShipments += r.shipmentsStarted;
            start = r.endDay + 1;
            due = nextGlobalSurveyBoundary(r.endDay, 30);
        }
        check(due > through, "freight reports omit a required global reporting boundary");
        check(p.reportStartDay == start && freightNearlyEqual(p.reportedCargoLoaded, reported[0]) &&
                  freightNearlyEqual(p.reportedCargoDelivered, reported[1]) &&
                  freightNearlyEqual(p.reportedCargoReturned, reported[2]) &&
                  freightNearlyEqual(p.reportedFuelLoaded, reported[3]) &&
                  freightNearlyEqual(p.reportedFuelBurned, reported[4]) &&
                  p.reportedShipments == reportedShipments,
              "freight report cursors disagree with persisted reports");
        check(p.nextReportDay == (p.reports.empty() ? nextGlobalSurveyBoundary(p.createdDay, 30)
                                                    : nextGlobalSurveyBoundary(p.reports.back().endDay, 30)),
              "freight next report boundary is inconsistent");
        check((p.issue.signature.empty() && p.issue.message.empty() && p.issue.acknowledged) ||
                  (!p.issue.signature.empty() && !p.issue.message.empty()),
              "freight issue shape is invalid");
    }
    for (const Ship& ship : state.ships)
        if (ship.cargo) {
            const auto& lot = *ship.cargo;
            const FreightProgram* p = find(state.freightPrograms, lot.programId);
            const ShipClass* cls = find(state.shipClasses, ship.shipClassId);
            check(p && p->shipment && cls && lot.shipmentNumber == p->shipment->number &&
                      lot.material == p->shipment->material && freightPositive(lot.quantity),
                  "ship cargo has invalid program/shipment/material/quantity");
            check(ship.fleetId == p->shipment->fleetId && p->leasedFleetId == ship.fleetId,
                  "ship cargo left its custody fleet");
            check(
                atMost(lot.quantity, evaluateShipDesign(state.shipComponents, cls->components).cargoCapacity),
                "ship cargo exceeds derived per-hull capacity");
            const auto manifest = std::find_if(p->shipment->manifest.begin(), p->shipment->manifest.end(),
                                               [&](const auto& row) { return row.shipId == ship.id; });
            check(manifest != p->shipment->manifest.end() && atMost(lot.quantity, manifest->plannedQuantity),
                  "ship cargo exceeds its own committed manifest allocation");
        }
    for (const auto& event : state.eventLog)
        if (const auto* e = std::get_if<FreightProgramAuditEvent>(&event.payload))
            check(find(state.freightPrograms, e->programId),
                  "freight audit references a nonexistent program");
}
} // namespace deep
