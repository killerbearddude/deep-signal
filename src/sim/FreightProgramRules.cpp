// Pure public-state freight planning: typed inventory floors, dated operating-base
// cycles, bounded shared handling estimates and structured waiting causes.
#include "sim/FreightProgramRules.h"

#include "sim/ProgramControl.h"
#include "sim/StockAccess.h"
#include "sim/SiteOperationRules.h"
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
double stockAvailable(const GameState& state, const FreightProgram& program, const StockLocation& location,
                      const Commodity& commodity, const OpeningProgramContext* context) {
    if (!stockLocationExists(state, location))
        return 0.0;
    const double floor = freightEffectiveFloor(program, location, commodity);
    return context ? context->available(state, location, commodity, floor)
                   : std::max(0.0, stockQuantity(state, location, commodity) - floor);
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
// Roster-greedy shared handling is not max(hull days, total/site rate).
// Jump across unchanged full days; a literal boundary day redistributes freed
// throughput to later hulls. Every iteration finishes at least one hull, so the
// cost is bounded by roster size rather than the number of simulated days.
std::optional<std::int64_t> handlingDays(const std::vector<FreightHullCapability>& hulls,
                                         const std::vector<FreightManifestRow>& manifest, double shared) {
    std::vector<double> left, rates;
    for (const auto& row : manifest) {
        auto h =
            std::find_if(hulls.begin(), hulls.end(), [&](const auto& x) { return x.shipId == row.shipId; });
        if (h == hulls.end() || !nonnegative(row.plannedQuantity))
            return std::nullopt;
        left.push_back(row.plannedQuantity);
        rates.push_back(h->operationalHandlingPerDay);
    }
    std::int64_t elapsed = 0;
    for (std::size_t boundary = 0; boundary <= left.size(); ++boundary) {
        bool any = false;
        double pool = shared;
        std::vector<double> allocation(left.size());
        double jump = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < left.size(); ++i)
            if (left[i] > 0.0) {
                any = true;
                allocation[i] = std::min(rates[i], pool);
                pool -= allocation[i];
                if (allocation[i] > 0.0)
                    jump = std::min(jump, std::max(0.0, std::ceil(left[i] / allocation[i]) - 1.0));
            }
        if (!any)
            return elapsed;
        if (!std::isfinite(jump) || jump >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return std::nullopt;
        const auto next = addDays(elapsed, static_cast<std::int64_t>(jump));
        if (!next || *next == std::numeric_limits<std::int64_t>::max())
            return std::nullopt;
        elapsed = *next + 1;
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] = std::max(0.0, left[i] - allocation[i] * jump);
        pool = shared;
        for (std::size_t i = 0; i < left.size(); ++i) {
            const double q = std::min({left[i], rates[i], pool});
            left[i] -= q;
            pool -= q;
        }
    }
    return std::nullopt;
}
double locationHandling(const GameState& state, const StockLocation& location, const Commodity& commodity,
                        std::int64_t day, const OpeningProgramContext* context = nullptr) {
    const auto* site = std::get_if<SiteId>(&location);
    if (!site || !std::holds_alternative<Mineral>(commodity))
        return std::numeric_limits<double>::infinity();
    return context ? context->availableSiteRawHandling(*site) : siteRawHandlingPreview(state, *site, day);
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
bool freightIsCollection(const FreightProgram& p) noexcept {
    return p.charter.destination == StockLocation{p.charter.operatingBaseColonyId};
}
double freightEffectiveFloor(const FreightProgram& program, const StockLocation& location,
                             const Commodity& commodity) noexcept {
    double floor = location == program.charter.source && commodity == program.charter.commodity
                       ? program.charter.policy.sourceCargoFloor
                       : 0.0;
    if (location == StockLocation{program.charter.operatingBaseColonyId} &&
        commodity == Commodity{ProcessedMaterial::Propellant})
        floor = std::max(floor, program.charter.policy.basePropellantFloor);
    return floor;
}
std::optional<std::string> validateFreightProgramCharter(const GameState& state,
                                                         const FreightProgramCharter& c,
                                                         bool allowZeroQuantity) {
    if (c.name.empty() || c.name.find_first_not_of(" \t\n\r") == std::string::npos)
        return "Freight program name must be nonempty";
    if (!stockLocationExists(state, c.source) || !stockLocationExists(state, c.destination) ||
        c.source == c.destination)
        return "Freight endpoints must be distinct existing stock locations";
    if (std::holds_alternative<SiteId>(c.source) && std::holds_alternative<SiteId>(c.destination))
        return "Site to site freight is not supported";
    if (!find(state.colonies, c.operatingBaseColonyId) ||
        (c.source != StockLocation{c.operatingBaseColonyId} &&
         c.destination != StockLocation{c.operatingBaseColonyId}))
        return "Freight operating base must be a colony endpoint";
    if (!validCommodity(c.commodity))
        return "Freight commodity is invalid";
    if (!nonnegative(c.totalQuantity) || (!allowZeroQuantity && c.totalQuantity == 0.0))
        return "Delivery quantity must be finite and positive";
    if (c.requestedFleetId && !find(state.fleets, *c.requestedFleetId))
        return "Requested freight fleet does not exist";
    if (c.requestedLeaderId && !find(state.people, *c.requestedLeaderId))
        return "Requested freight leader does not exist";
    if (!nonnegative(c.policy.sourceCargoFloor) || !nonnegative(c.policy.basePropellantFloor) ||
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
                                            std::int64_t departureDay, bool collectionFromBase) {
    FreightShipmentPlan result;
    result.manifest = manifest;
    result.departureDay = departureDay;
    if (!stockLocationExists(state, program.charter.source) ||
        !stockLocationExists(state, program.charter.destination)) {
        result.cause = FreightReadinessCause::Route;
        result.waitingReason = "Route endpoints are unavailable";
        return result;
    }
    const BodyId source = stockLocationBody(state, program.charter.source);
    const BodyId destination = stockLocationBody(state, program.charter.destination);
    const auto hulls = freightHullCapabilities(state, fleet);
    for (const auto& row : manifest) {
        const auto hull =
            std::find_if(hulls.begin(), hulls.end(), [&](const auto& h) { return h.shipId == row.shipId; });
        if (hull == hulls.end() || !nonnegative(row.plannedQuantity) ||
            (row.plannedQuantity > hull->capacity &&
             !freightNearlyEqual(row.plannedQuantity, hull->capacity))) {
            result.cause = FreightReadinessCause::Capacity;
            result.waitingReason = "Manifest exceeds the carrying hull's cargo capacity";
            return result;
        }
        const auto days = workDays(row.plannedQuantity, hull->operationalHandlingPerDay);
        if (!days) {
            result.cause = FreightReadinessCause::Handling;
            result.waitingReason = "Cargo handling unavailable on a carrying hull (power or handling rate)";
            return result;
        }
        result.quantity += row.plannedQuantity;
        result.unloadingDays = std::max(result.unloadingDays, *days);
    }
    if (!freightPositive(result.quantity)) {
        result.cause = FreightReadinessCause::CargoStock;
        result.waitingReason = "No positive cargo manifest";
        return result;
    }
    const auto unloadDays = handlingDays(
        hulls, manifest,
        locationHandling(state, program.charter.destination, program.charter.commodity, departureDay));
    if (!unloadDays) {
        result.cause = FreightReadinessCause::Handling;
        result.waitingReason = "Destination raw or hull handling is unavailable";
        return result;
    }
    result.unloadingDays = *unloadDays;
    if (source == destination) {
        result.returnDepartureDay = departureDay;
        result.ready = true;
        return result;
    }
    const bool collection = freightIsCollection(program);
    const BodyId firstFrom = collection && collectionFromBase ? destination : source;
    const BodyId firstTo = collection && collectionFromBase ? source : destination;
    const auto outward = planFleetTransit(state, firstFrom, firstTo, departureDay);
    if (outward.type != FleetOrderType::MoveToBody || outward.arrivalDay <= departureDay) {
        result.cause = FreightReadinessCause::Route;
        result.waitingReason = "Outbound route has no representable timetable";
        return result;
    }
    const double outwardCost = adjustedFleetMoveFuelCost(state, fleet, firstFrom, firstTo, departureDay);
    if (collection && !collectionFromBase) {
        // Once remote, only the actual loaded home leg remains. No remote fuel
        // transfer or invented final empty return is included in this envelope.
        result.returnDepartureDay = departureDay;
        result.requiredFuel = outwardCost;
    } else {
        std::int64_t work = result.unloadingDays;
        if (collection) {
            const auto days = handlingDays(
                hulls, manifest,
                locationHandling(state, program.charter.source, program.charter.commodity, departureDay));
            if (!days) {
                result.cause = FreightReadinessCause::Handling;
                result.waitingReason = "Source raw or hull handling is unavailable";
                return result;
            }
            work = *days;
            result.loadingDays = work;
        }
        auto backDay = addDays(outward.arrivalDay, work);
        if (backDay)
            backDay = addDays(*backDay, 1);
        if (!backDay) {
            result.cause = FreightReadinessCause::Route;
            result.waitingReason = "Return timetable exceeds date limits";
            return result;
        }
        result.returnDepartureDay = *backDay;
        const double returnCost = adjustedFleetMoveFuelCost(state, fleet, firstTo, firstFrom, *backDay);
        result.requiredFuel =
            outwardCost + returnCost * (1.0 + program.charter.policy.returnContingencyFraction);
    }
    if (!nonnegative(result.requiredFuel)) {
        result.cause = FreightReadinessCause::Route;
        result.waitingReason = "Route fuel budget is not representable";
        return result;
    }
    result.additionalFuel = std::max(0.0, result.requiredFuel - freightFleetFuel(state, fleet));
    if (result.requiredFuel > tankCapacity(state, fleet) &&
        !freightNearlyEqual(result.requiredFuel, tankCapacity(state, fleet))) {
        result.cause = FreightReadinessCause::FuelCapacity;
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
        failure.cause = FreightReadinessCause::Participants;
        failure.waitingReason = "Waiting for a named freight leader";
        return failure;
    }
    const auto hulls = freightHullCapabilities(state, fleet);
    double capacity = 0.0;
    for (const auto& hull : hulls)
        if (hull.operationalHandlingPerDay > 0.0)
            capacity += hull.capacity;
    if (!freightPositive(capacity)) {
        failure.cause = FreightReadinessCause::Capacity;
        failure.waitingReason = "Waiting for powered cargo capacity and handling equipment";
        return failure;
    }
    const double cargoAvailable =
        stockAvailable(state, program, program.charter.source, program.charter.commodity, context);
    double candidate = std::min({capacity, cargoAvailable, freightUnpickedQuantity(state, program)});
    if (!freightPositive(candidate)) {
        failure.cause = FreightReadinessCause::CargoStock;
        failure.waitingReason = "Waiting for source cargo stock above the protected floor";
        return failure;
    }
    const double fuelAvailable = stockAvailable(state, program, program.charter.operatingBaseColonyId,
                                                ProcessedMaterial::Propellant, context);
    // A fixed evaluation bound prevents a daily tick from becoming an optimizer.
    // The fuel-adjusted candidate solves the common shared-Propellant scarcity
    // case directly; the remaining halves cover shorter handling timetables.
    for (int attempt = 0; attempt < 14 && freightPositive(candidate); ++attempt) {
        const auto manifest = allocate(hulls, candidate);
        const auto loading = handlingDays(
            hulls, manifest,
            locationHandling(state, program.charter.source, program.charter.commodity, openingDay, context));
        if (!loading) {
            failure.cause = FreightReadinessCause::Handling;
            failure.waitingReason = "Source raw or hull handling is unavailable";
            return failure;
        }
        const std::int64_t loadingDays = *loading;
        auto departure = addDays(openingDay, freightIsCollection(program) ? 0 : loadingDays);
        if (!departure) {
            failure.cause = FreightReadinessCause::Route;
            failure.waitingReason = "Loading timetable exceeds date limits";
            return failure;
        }
        auto plan = evaluateFreightManifest(state, program, fleet, manifest, *departure,
                                            freightIsCollection(program));
        if (plan.additionalFuel > 0.0) {
            departure = addDays(*departure, 1);
            if (!departure) {
                failure.cause = FreightReadinessCause::Route;
                failure.waitingReason = "Refueling timetable exceeds date limits";
                return failure;
            }
            plan = evaluateFreightManifest(state, program, fleet, manifest, *departure,
                                           freightIsCollection(program));
        }
        plan.loadingDays = loadingDays;
        bool supported = plan.ready;
        if (supported && plan.additionalFuel > freightFuelAllowanceRemaining(program) &&
            !freightNearlyEqual(plan.additionalFuel, freightFuelAllowanceRemaining(program))) {
            supported = false;
            plan.cause = FreightReadinessCause::FuelAllowance;
            plan.waitingReason = "Operating-fuel allowance cannot support the next shipment";
        }
        if (supported && plan.additionalFuel > fuelAvailable &&
            !freightNearlyEqual(plan.additionalFuel, fuelAvailable)) {
            supported = false;
            plan.cause = FreightReadinessCause::FuelStock;
            plan.waitingReason = "Waiting for base operating Propellant above the protected floor";
        }
        if (supported && program.charter.commodity == Commodity{ProcessedMaterial::Propellant} &&
            !freightIsCollection(program) && plan.quantity + plan.additionalFuel > cargoAvailable &&
            !freightNearlyEqual(plan.quantity + plan.additionalFuel, cargoAvailable)) {
            supported = false;
            plan.cause = FreightReadinessCause::FuelStock;
            plan.waitingReason = "Shared Propellant stock must cover operating fuel before payload";
        }
        if (supported) {
            const double cargoStock = stockQuantity(state, program.charter.source, program.charter.commodity);
            const double fuelStock =
                stockQuantity(state, program.charter.operatingBaseColonyId, ProcessedMaterial::Propellant);
            if (cargoStock - plan.quantity == cargoStock ||
                (plan.additionalFuel > 0.0 && fuelStock - plan.additionalFuel == fuelStock)) {
                supported = false;
                plan.cause = FreightReadinessCause::Precision;
                plan.waitingReason = "Transfer amount is below the source stock's representable precision";
            }
        }
        if (supported)
            return plan;
        failure = plan;
        failure.ready = false;
        const double reduced = std::max(0.0, cargoAvailable - plan.additionalFuel);
        candidate = attempt == 0 && program.charter.commodity == Commodity{ProcessedMaterial::Propellant} &&
                            !freightIsCollection(program) && reduced < candidate && reduced > 0.0
                        ? reduced
                        : candidate * 0.5;
    }
    failure.waitingReason =
        failure.waitingReason.empty()
            ? "Bounded freight planner could not establish a supported positive shipment"
            : "Bounded freight planner did not establish readiness: " + failure.waitingReason;
    return failure;
}

FreightReadiness freightProgramReadiness(const GameState& state, const FreightProgram& p) {
    using C = FreightReadinessCause;
    const auto previewDay =
        state.date.day == std::numeric_limits<std::int64_t>::max() ? state.date.day : state.date.day + 1;
    if (p.lifecycle == FreightProgramLifecycle::Closed)
        return {C::None, p.closure == FreightProgramClosure::Completed
                             ? "Completed; cargo delivered and fleet settled at operating base"
                             : "Cancelled; cargo custody settled"};
    if (p.lifecycle == FreightProgramLifecycle::Suspended)
        return {C::None, "Suspended; physical cargo and paid transit retained"};
    const auto id =
        p.leasedFleetId ? p.leasedFleetId : (p.taskFleetId ? p.taskFleetId : p.charter.requestedFleetId);
    const Fleet* fleet = id ? find(state.fleets, *id) : nullptr;
    if (!fleet)
        return {C::Participants, "Waiting for requested freight fleet"};
    if (!p.leasedFleetId && controllingProgram(state, *id))
        return {C::Participants, "Waiting for fleet controlled by another program"};
    if (!stationary(*fleet))
        return {C::None, "In transit on the committed physical leg"};
    if (!p.leasedFleetId && !fleet->queuedOrders.empty())
        return {C::Participants, "Waiting for existing fleet orders"};
    const auto base = StockLocation{p.charter.operatingBaseColonyId};
    const BodyId baseBody = stockLocationBody(state, base),
                 source = stockLocationBody(state, p.charter.source);
    const double aboard = freightCargoAboard(state, p.id);
    if (p.task == FreightProgramTask::Unloading || p.task == FreightProgramTask::ReturningCargo) {
        const auto location =
            p.task == FreightProgramTask::ReturningCargo ? p.charter.source : p.charter.destination;
        for (const auto& hull : freightHullCapabilities(state, *fleet))
            if (hull.onboardQuantity > 0.0 && hull.operationalHandlingPerDay <= 0.0)
                return {C::Handling, "Cargo disposition blocked by carrying hull power or handling"};
        if (locationHandling(state, location, p.charter.commodity, previewDay) <= 0.0)
            return {C::Handling, "Cargo retained; site raw handling is unsupported"};
        if (const auto* site = std::get_if<SiteId>(&location);
            site && std::holds_alternative<Mineral>(p.charter.commodity) &&
            siteRawRoom(state, *site, previewDay) <= 0.0)
            return {C::SiteRoom, "Cargo retained; site raw storage is full"};
        return {C::None, p.task == FreightProgramTask::ReturningCargo
                             ? "Returning undispatched cargo to original source"
                             : "Unloading committed cargo at destination"};
    }
    if (p.task == FreightProgramTask::Return || p.task == FreightProgramTask::Reposition ||
        (!p.shipment && fleet->currentBodyId != baseBody)) {
        const double cost =
            adjustedFleetMoveFuelCost(state, *fleet, fleet->currentBodyId, baseBody, state.date.day);
        if (!std::isfinite(cost) || freightFleetFuel(state, *fleet) < cost)
            return {C::FuelStock, "Return or reposition to operating base needs actual engine fuel"};
        if (cost > 0.0 && !freightFuelDebitRepresentable(state, *fleet, cost))
            return {C::Precision, "Engine fuel debit cannot preserve representable inventory"};
        return {C::None, "Ready to return or reposition to operating base"};
    }
    if (p.shipment && p.task == FreightProgramTask::Loading && fleet->currentBodyId == source) {
        for(const auto& hull:freightHullCapabilities(state,*fleet)) {
            const auto row=std::find_if(p.shipment->manifest.begin(),p.shipment->manifest.end(),[&](const auto& r){return r.shipId==hull.shipId;});
            if(row!=p.shipment->manifest.end() && row->plannedQuantity>0.0 && hull.operationalHandlingPerDay<=0.0)
                return {C::Handling,"Committed carrying hull is unpowered or has no handling rate"};
        }
        if (locationHandling(state, p.charter.source, p.charter.commodity, previewDay) <= 0.0)
            return {C::Handling, "Waiting for supported site raw handling"};
        if (aboard < freightShipmentPlannedQuantity(*p.shipment) &&
            stockAvailable(state, p, p.charter.source, p.charter.commodity, nullptr) <= 0.0 &&
            (freightIsCollection(p) || aboard <= 0.0))
            return {C::CargoStock, "Waiting for source stock; committed manifest and partial cargo retained"};
        if (aboard < freightShipmentPlannedQuantity(*p.shipment))
            return {C::None, "Loading committed cargo over per-hull handling days"};
        const auto plan = evaluateFreightManifest(state, p, *fleet, p.shipment->manifest, state.date.day);
        if (!plan.ready)
            return {plan.cause, plan.waitingReason};
        if (plan.additionalFuel > freightFuelAllowanceRemaining(p) &&
            !freightNearlyEqual(plan.additionalFuel, freightFuelAllowanceRemaining(p)))
            return {C::FuelAllowance, "Operating-fuel allowance cannot support committed shipment"};
        if (plan.additionalFuel > stockAvailable(state, p, base, ProcessedMaterial::Propellant, nullptr) &&
            !freightNearlyEqual(plan.additionalFuel,
                                stockAvailable(state, p, base, ProcessedMaterial::Propellant, nullptr)))
            return {C::FuelStock, "Base operating Propellant no longer supports committed shipment"};
        if (plan.additionalFuel > 0.0 && freightIsCollection(p))
            return {C::FuelStock,
                    "Loaded return waits for actual engine fuel; remote stocks cannot refill engines"};
        const double cost = adjustedFleetMoveFuelCost(
            state, *fleet, source, stockLocationBody(state, p.charter.destination), state.date.day);
        if (plan.additionalFuel == 0.0 && cost > 0.0 && !freightFuelDebitRepresentable(state, *fleet, cost))
            return {C::Precision, "Engine fuel debit cannot conserve representable tank inventory"};
        return {C::None, "Ready to dispatch committed cargo"};
    }
    const auto plan = planFreightShipment(state, p, *fleet);
    return {plan.cause, plan.ready ? "Ready for base-supported freight cycle" : plan.waitingReason};
}
std::string freightProgramExecutionCondition(const GameState& state, const FreightProgram& p) {
    return freightProgramReadiness(state, p).message;
}
} // namespace deep
