#include "sim/FreightProgramRules.h"

#include "sim/ProgramControl.h"
#include "sim/ShipDesignRules.h"
#include "sim/TransitPlanning.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace deep {
namespace {
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto i = std::find_if(rows.begin(), rows.end(), [id](const T& r) { return r.id == id; });
    return i == rows.end() ? nullptr : &*i;
}
bool stationary(const Fleet& fleet) {
    return fleet.activeOrder.type == FleetOrderType::None && !fleet.destinationBodyId;
}
bool nonnegative(double n) {
    return std::isfinite(n) && n >= 0.0;
}
double stockAvailable(const GameState& state, const FreightProgram& program, ProcessedMaterial material,
                      const OpeningProgramContext* context) {
    const Colony* source = find(state.colonies, program.charter.sourceColonyId);
    if (!source)
        return 0.0;
    const double floor = freightEffectiveFloor(program, material);
    return context ? context->available(state, source->id, material, floor)
                   : std::max(0.0, source->processedStockpile.get(material) - floor);
}
std::optional<std::int64_t> workDays(double quantity, double rate) {
    if (quantity == 0.0)
        return 0;
    if (!freightPositive(quantity) || !freightPositive(rate))
        return std::nullopt;
    const double days = std::ceil(quantity / rate);
    if (!std::isfinite(days) || days >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(days));
}
std::optional<std::int64_t> addDays(std::int64_t day, std::int64_t count) {
    if (day < 0 || count < 0 || day > std::numeric_limits<std::int64_t>::max() - count)
        return std::nullopt;
    return day + count;
}
double tankCapacity(const GameState& state, const Fleet& fleet) {
    double total = 0.0;
    for (ShipId id : fleet.shipIds) {
        const Ship* ship = find(state.ships, id);
        const ShipClass* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (cls)
            total += evaluateShipDesign(state.shipComponents, cls->components).propellantCapacity;
    }
    return total;
}
std::vector<FreightManifestRow> allocate(const std::vector<FreightHullCapability>& hulls, double quantity) {
    std::vector<FreightManifestRow> rows;
    rows.reserve(hulls.size());
    for (const auto& hull : hulls) {
        const double amount = hull.operationalHandlingPerDay > 0.0 ? std::min(quantity, hull.capacity) : 0.0;
        rows.push_back({hull.shipId, amount});
        quantity -= amount;
    }
    return rows;
}
} // namespace

bool freightNearlyEqual(double a, double b) noexcept {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <=
               kFreightAbsoluteTolerance + kFreightRelativeTolerance * std::max(std::abs(a), std::abs(b));
}
bool freightPositive(double amount) noexcept {
    return std::isfinite(amount) && amount > 0.0;
}

std::vector<FreightHullCapability> freightHullCapabilities(const GameState& state, const Fleet& fleet) {
    std::vector<FreightHullCapability> out;
    out.reserve(fleet.shipIds.size());
    for (ShipId id : fleet.shipIds) {
        const Ship* ship = find(state.ships, id);
        const ShipClass* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (!ship || !cls)
            continue;
        const auto design = evaluateShipDesign(state.shipComponents, cls->components);
        out.push_back({id, design.cargoCapacity,
                       design.powerGeneration >= design.powerDemand ? design.cargoHandlingPerDay : 0.0,
                       ship->cargo ? ship->cargo->quantity : 0.0});
    }
    return out;
}
double freightCargoAboard(const GameState& state, FreightProgramId id) noexcept {
    double sum = 0.0;
    for (const Ship& ship : state.ships)
        if (ship.cargo && ship.cargo->programId == id)
            sum += ship.cargo->quantity;
    return sum;
}
double freightShipmentPlannedQuantity(const FreightShipment& shipment) noexcept {
    double sum = 0.0;
    for (const auto& row : shipment.manifest)
        sum += row.plannedQuantity;
    return sum;
}
double freightShipmentLoadedQuantity(const FreightProgram& program) noexcept {
    if (!program.shipment)
        return 0.0;
    double sum = 0.0;
    for (const auto& row : program.receipts)
        if (row.shipmentNumber == program.shipment->number && row.kind == FreightTransferKind::Load)
            sum += row.amount;
    return sum;
}
double freightUnpickedQuantity(const GameState& state, const FreightProgram& program) noexcept {
    return std::max(0.0, program.charter.totalQuantity - program.cargoDelivered -
                             freightCargoAboard(state, program.id));
}
double freightFleetFuel(const GameState& state, const Fleet& fleet) noexcept {
    double sum = 0.0;
    for (ShipId id : fleet.shipIds)
        if (const Ship* ship = find(state.ships, id))
            sum += ship->fuel;
    return sum;
}
double freightFuelAllowanceRemaining(const FreightProgram& program) noexcept {
    return program.charter.policy.maxAdditionalPropellant
               ? std::max(0.0, *program.charter.policy.maxAdditionalPropellant - program.fuelLoaded)
               : std::numeric_limits<double>::infinity();
}
bool freightFuelDebitRepresentable(const GameState& state, const Fleet& fleet, double cost) noexcept {
    if (!std::isfinite(cost) || cost < 0.0)
        return false;
    if (cost <= kFuelComparisonEpsilon)
        return cost == 0.0;
    double remaining = cost;
    double actualDebit = 0.0;
    for (ShipId id : fleet.shipIds) {
        const Ship* ship = find(state.ships, id);
        if (!ship || ship->fuel <= 0.0)
            continue;
        const double consumed = std::min(ship->fuel, remaining);
        double after = ship->fuel - consumed;
        if (after < kFuelComparisonEpsilon)
            after = 0.0;
        const double debit = ship->fuel - after;
        if (consumed > 0.0 && (!(debit > 0.0) || !freightNearlyEqual(debit, consumed)))
            return false;
        actualDebit += debit;
        if (!std::isfinite(actualDebit))
            return false;
        remaining -= consumed;
        if (remaining <= kFuelComparisonEpsilon)
            break;
    }
    return remaining <= kFuelComparisonEpsilon && freightNearlyEqual(actualDebit, cost);
}
double freightEffectiveFloor(const FreightProgram& program, ProcessedMaterial material) noexcept {
    if (material == ProcessedMaterial::Propellant) {
        return program.charter.material == ProcessedMaterial::Propellant
                   ? std::max(program.charter.policy.sourceCargoFloor,
                              program.charter.policy.sourcePropellantFloor)
                   : program.charter.policy.sourcePropellantFloor;
    }
    return program.charter.policy.sourceCargoFloor;
}
std::optional<std::string> validateFreightProgramCharter(const GameState& state,
                                                         const FreightProgramCharter& c,
                                                         bool allowZeroQuantity) {
    if (c.name.empty() || c.name.find_first_not_of(" \t\n\r") == std::string::npos)
        return "Freight program name must be nonempty";
    if (!find(state.colonies, c.sourceColonyId) || !find(state.colonies, c.destinationColonyId) ||
        c.sourceColonyId == c.destinationColonyId)
        return "Freight source and destination must be distinct existing colonies";
    if (static_cast<std::size_t>(c.material) >= processedMaterialCount())
        return "Freight material is invalid";
    if (!nonnegative(c.totalQuantity) || (!allowZeroQuantity && c.totalQuantity == 0.0))
        return "Delivery quantity must be finite and positive";
    if (c.requestedFleetId && !find(state.fleets, *c.requestedFleetId))
        return "Requested freight fleet does not exist";
    if (c.requestedLeaderId && !find(state.people, *c.requestedLeaderId))
        return "Requested freight leader does not exist";
    if (!nonnegative(c.policy.sourceCargoFloor) || !nonnegative(c.policy.sourcePropellantFloor) ||
        !nonnegative(c.policy.returnContingencyFraction) ||
        (c.policy.maxAdditionalPropellant && !nonnegative(*c.policy.maxAdditionalPropellant)))
        return "Freight policy values must be finite and nonnegative";
    return std::nullopt;
}
FreightProgramAmendment freightAmendmentFromCharter(const FreightProgramCharter& c) {
    return {c.name, c.totalQuantity, c.requestedFleetId, c.requestedLeaderId, c.policy};
}
void applyFreightAmendment(FreightProgramCharter& c, const FreightProgramAmendment& a) {
    c.name = a.name;
    c.totalQuantity = a.totalQuantity;
    c.requestedFleetId = a.requestedFleetId;
    c.requestedLeaderId = a.requestedLeaderId;
    c.policy = a.policy;
}

FreightShipmentPlan evaluateFreightManifest(const GameState& state, const FreightProgram& program,
                                            const Fleet& fleet,
                                            const std::vector<FreightManifestRow>& manifest,
                                            std::int64_t departureDay) {
    FreightShipmentPlan result;
    result.manifest = manifest;
    result.departureDay = departureDay;
    const Colony* source = find(state.colonies, program.charter.sourceColonyId);
    const Colony* destination = find(state.colonies, program.charter.destinationColonyId);
    if (!source || !destination) {
        result.waitingReason = "Route endpoints are unavailable";
        return result;
    }
    const auto hulls = freightHullCapabilities(state, fleet);
    for (const auto& row : manifest) {
        const auto hull =
            std::find_if(hulls.begin(), hulls.end(), [&](const auto& h) { return h.shipId == row.shipId; });
        if (hull == hulls.end() || !nonnegative(row.plannedQuantity) ||
            (row.plannedQuantity > hull->capacity &&
             !freightNearlyEqual(row.plannedQuantity, hull->capacity))) {
            result.waitingReason = "Manifest exceeds the carrying hull's cargo capacity";
            return result;
        }
        const auto days = workDays(row.plannedQuantity, hull->operationalHandlingPerDay);
        if (!days) {
            result.waitingReason = "Cargo handling unavailable on a carrying hull (power or handling rate)";
            return result;
        }
        result.quantity += row.plannedQuantity;
        result.unloadingDays = std::max(result.unloadingDays, *days);
    }
    if (!freightPositive(result.quantity)) {
        result.waitingReason = "No positive cargo manifest";
        return result;
    }
    if (source->bodyId == destination->bodyId) {
        result.returnDepartureDay = departureDay;
        result.ready = true;
        return result;
    }
    const auto outward = planFleetTransit(state, source->bodyId, destination->bodyId, departureDay);
    if (outward.type != FleetOrderType::MoveToBody || outward.arrivalDay <= departureDay) {
        result.waitingReason = "Outbound route has no representable timetable";
        return result;
    }
    auto backDay = addDays(outward.arrivalDay, result.unloadingDays);
    if (backDay)
        backDay = addDays(*backDay, 1);
    if (!backDay) {
        result.waitingReason = "Return timetable exceeds date limits";
        return result;
    }
    result.returnDepartureDay = *backDay;
    const double outwardCost =
        adjustedFleetMoveFuelCost(state, fleet, source->bodyId, destination->bodyId, departureDay);
    const double returnCost =
        adjustedFleetMoveFuelCost(state, fleet, destination->bodyId, source->bodyId, *backDay);
    result.requiredFuel = outwardCost + returnCost * (1.0 + program.charter.policy.returnContingencyFraction);
    if (!nonnegative(result.requiredFuel)) {
        result.waitingReason = "Route fuel budget is not representable";
        return result;
    }
    result.additionalFuel = std::max(0.0, result.requiredFuel - freightFleetFuel(state, fleet));
    if (result.requiredFuel > tankCapacity(state, fleet) &&
        !freightNearlyEqual(result.requiredFuel, tankCapacity(state, fleet))) {
        result.waitingReason = "Tank capacity cannot support the outbound and dated return envelope";
        return result;
    }
    result.ready = true;
    return result;
}

FreightShipmentPlan planFreightShipment(const GameState& state, const FreightProgram& program,
                                        const Fleet& fleet, const OpeningProgramContext* context,
                                        std::int64_t openingDay) {
    if (openingDay < 0)
        openingDay = state.date.day;
    FreightShipmentPlan failure;
    if (!program.charter.requestedLeaderId) {
        failure.waitingReason = "Waiting for a named freight leader";
        return failure;
    }
    const auto hulls = freightHullCapabilities(state, fleet);
    double capacity = 0.0;
    for (const auto& hull : hulls)
        if (hull.operationalHandlingPerDay > 0.0)
            capacity += hull.capacity;
    if (!freightPositive(capacity)) {
        failure.waitingReason = "Waiting for powered cargo capacity and handling equipment";
        return failure;
    }
    const double cargoAvailable = stockAvailable(state, program, program.charter.material, context);
    double candidate = std::min({capacity, cargoAvailable, freightUnpickedQuantity(state, program)});
    if (!freightPositive(candidate)) {
        failure.waitingReason = "Waiting for source cargo stock above the protected floor";
        return failure;
    }
    const double fuelAvailable = stockAvailable(state, program, ProcessedMaterial::Propellant, context);
    // A fixed evaluation bound prevents a daily tick from becoming an optimizer.
    // The fuel-adjusted candidate solves the common shared-Propellant scarcity
    // case directly; the remaining halves cover shorter handling timetables.
    for (int attempt = 0; attempt < 14 && freightPositive(candidate); ++attempt) {
        const auto manifest = allocate(hulls, candidate);
        std::int64_t loadingDays = 0;
        for (std::size_t i = 0; i < manifest.size(); ++i) {
            const auto days = workDays(manifest[i].plannedQuantity, hulls[i].operationalHandlingPerDay);
            if (!days) {
                failure.waitingReason = "Loading timetable exceeds numeric limits";
                return failure;
            }
            loadingDays = std::max(loadingDays, *days);
        }
        auto departure = addDays(openingDay, loadingDays);
        if (!departure) {
            failure.waitingReason = "Loading timetable exceeds date limits";
            return failure;
        }
        auto plan = evaluateFreightManifest(state, program, fleet, manifest, *departure);
        if (plan.additionalFuel > 0.0) {
            departure = addDays(*departure, 1);
            if (!departure) {
                failure.waitingReason = "Refueling timetable exceeds date limits";
                return failure;
            }
            plan = evaluateFreightManifest(state, program, fleet, manifest, *departure);
        }
        plan.loadingDays = loadingDays;
        bool supported = plan.ready;
        if (supported && plan.additionalFuel > freightFuelAllowanceRemaining(program) &&
            !freightNearlyEqual(plan.additionalFuel, freightFuelAllowanceRemaining(program))) {
            supported = false;
            plan.waitingReason = "Operating-fuel allowance cannot support the next shipment";
        }
        if (supported && plan.additionalFuel > fuelAvailable &&
            !freightNearlyEqual(plan.additionalFuel, fuelAvailable)) {
            supported = false;
            plan.waitingReason = "Waiting for source operating Propellant above the protected floor";
        }
        if (supported && program.charter.material == ProcessedMaterial::Propellant &&
            plan.quantity + plan.additionalFuel > cargoAvailable &&
            !freightNearlyEqual(plan.quantity + plan.additionalFuel, cargoAvailable)) {
            supported = false;
            plan.waitingReason = "Shared Propellant stock must cover operating fuel before payload";
        }
        if (supported) {
            const Colony* source = find(state.colonies, program.charter.sourceColonyId);
            const double cargoStock = source->processedStockpile.get(program.charter.material);
            const double fuelStock = source->processedStockpile.get(ProcessedMaterial::Propellant);
            if (cargoStock - plan.quantity == cargoStock ||
                (plan.additionalFuel > 0.0 && fuelStock - plan.additionalFuel == fuelStock)) {
                supported = false;
                plan.waitingReason = "Transfer amount is below the source stock's representable precision";
            }
        }
        if (supported)
            return plan;
        failure = plan;
        failure.ready = false;
        const double reduced = std::max(0.0, cargoAvailable - plan.additionalFuel);
        candidate = attempt == 0 && program.charter.material == ProcessedMaterial::Propellant &&
                            reduced < candidate && reduced > 0.0
                        ? reduced
                        : candidate * 0.5;
    }
    failure.waitingReason =
        failure.waitingReason.empty()
            ? "Bounded freight planner could not establish a supported positive shipment"
            : "Bounded freight planner did not establish readiness: " + failure.waitingReason;
    return failure;
}

std::string freightProgramExecutionCondition(const GameState& state, const FreightProgram& p) {
    if (p.lifecycle == FreightProgramLifecycle::Closed)
        return p.closure == FreightProgramClosure::Completed ? "Completed; delivered and returned to source"
                                                             : "Cancelled; cargo custody settled";
    const double aboard = freightCargoAboard(state, p.id);
    if (p.lifecycle == FreightProgramLifecycle::Suspended)
        return aboard > 0.0 ? "Suspended; fleet retained while cargo is aboard. Resume or cancel future "
                              "pickups to settle the load."
                            : "Suspended; empty stationary assets may be released";
    const auto fleetId =
        p.leasedFleetId ? p.leasedFleetId : (p.taskFleetId ? p.taskFleetId : p.charter.requestedFleetId);
    if (!fleetId)
        return "Waiting for a requested freight fleet";
    const Fleet* fleet = find(state.fleets, *fleetId);
    if (!fleet)
        return "Waiting for an existing freight fleet";
    if (!p.leasedFleetId && controllingProgram(state, *fleetId))
        return "Waiting for fleet controlled by another program";
    if (!p.leasedFleetId && (!stationary(*fleet) || !fleet->queuedOrders.empty()))
        return "Waiting for busy fleet to finish its existing orders";
    if (!stationary(*fleet))
        return "In transit on the committed physical leg";
    const Colony* source = find(state.colonies, p.charter.sourceColonyId);
    if (!source)
        return "Waiting for source colony";
    if (p.task == FreightProgramTask::Unloading || p.task == FreightProgramTask::ReturningCargo) {
        for (const auto& hull : freightHullCapabilities(state, *fleet))
            if (hull.onboardQuantity > 0.0 && hull.operationalHandlingPerDay <= 0.0)
                return "Cargo disposition blocked by an unpowered or zero-rate carrying hull";
        return p.task == FreightProgramTask::ReturningCargo
                   ? "Returning unshipped cargo to source stock over handling days"
                   : "Unloading committed cargo to destination stock over handling days";
    }
    if (p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition ||
        fleet->currentBodyId != source->bodyId) {
        const double cost =
            adjustedFleetMoveFuelCost(state, *fleet, fleet->currentBodyId, source->bodyId, state.date.day);
        if (!std::isfinite(cost) || freightFleetFuel(state, *fleet) < cost)
            return "Return or reposition leg blocked by actual engine fuel or route conditions";
        if (!freightFuelDebitRepresentable(state, *fleet, cost))
            return "Engine fuel debit is below representable precision or would lose a residual";
        return "Ready to return empty to the source";
    }
    if (p.shipment) {
        std::vector<FreightManifestRow> actual;
        bool remainingHandling = false;
        for (const auto& hull : freightHullCapabilities(state, *fleet)) {
            actual.push_back({hull.shipId, hull.onboardQuantity});
            const auto row =
                std::find_if(p.shipment->manifest.begin(), p.shipment->manifest.end(),
                             [&](const auto& candidate) { return candidate.shipId == hull.shipId; });
            if (row != p.shipment->manifest.end() && row->plannedQuantity > hull.onboardQuantity &&
                hull.operationalHandlingPerDay > 0.0)
                remainingHandling = true;
            if (hull.onboardQuantity > 0.0 && hull.operationalHandlingPerDay <= 0.0)
                return "Cargo handling unavailable on a carrying hull (power or handling rate)";
        }
        const auto& manifest = aboard > 0.0 ? actual : p.shipment->manifest;
        const auto plan = evaluateFreightManifest(state, p, *fleet, manifest, state.date.day);
        if (!plan.ready)
            return plan.waitingReason;
        if (plan.additionalFuel > freightFuelAllowanceRemaining(p) &&
            !freightNearlyEqual(plan.additionalFuel, freightFuelAllowanceRemaining(p)))
            return "Operating-fuel allowance cannot support the committed shipment";
        const double fuelAvailable =
            std::max(0.0, source->processedStockpile.get(ProcessedMaterial::Propellant) -
                              freightEffectiveFloor(p, ProcessedMaterial::Propellant));
        if (plan.additionalFuel > fuelAvailable && !freightNearlyEqual(plan.additionalFuel, fuelAvailable))
            return "Waiting for source operating Propellant above the protected floor";
        if (plan.additionalFuel > 0.0 && !freightNearlyEqual(plan.additionalFuel, 0.0))
            return "Preparing additional operating fuel before the next cargo action";
        const double cargoAvailable = std::max(0.0, source->processedStockpile.get(p.charter.material) -
                                                        freightEffectiveFloor(p, p.charter.material));
        if (aboard <= 0.0 && cargoAvailable <= 0.0)
            return "Waiting for source cargo stock above the protected floor";
        if (aboard <= 0.0 && !remainingHandling)
            return "Waiting for powered handling on the committed cargo hulls";
        if (aboard >= freightShipmentPlannedQuantity(*p.shipment) || cargoAvailable <= 0.0) {
            const Colony* destination = find(state.colonies, p.charter.destinationColonyId);
            const double cost =
                adjustedFleetMoveFuelCost(state, *fleet, source->bodyId, destination->bodyId, state.date.day);
            if (!freightFuelDebitRepresentable(state, *fleet, cost))
                return "Engine fuel debit is below representable precision or would lose a residual";
            return "Ready to dispatch the actual loaded manifest";
        }
        return "Loading committed cargo over per-hull handling days";
    }
    const auto plan = planFreightShipment(state, p, *fleet);
    return plan.ready ? "Ready for source-supported freight shipment" : plan.waitingReason;
}
} // namespace deep
