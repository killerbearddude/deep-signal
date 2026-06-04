#include "sim/GameStateValidation.h"

// Implements zero-trust validation for fully assembled simulation snapshots.
// The checks intentionally duplicate some SQLite CHECK/FOREIGN KEY constraints
// because database files can be hand-edited with constraints disabled and because
// GameState can also enter the system without going through SQLite.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace deep {
namespace {

template <typename IdT>
[[nodiscard]] std::int64_t idValue(const IdT id) noexcept {
    return id.value;
}

[[nodiscard]] bool isFinite(const double value) noexcept {
    return std::isfinite(value);
}

void requireState(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{"Invalid GameState: "} + std::string{message}};
    }
}

[[nodiscard]] bool isValidInstitutionType(const InstitutionType value) noexcept {
    switch (value) {
    case InstitutionType::InnerAuthority:
    case InstitutionType::NavalConstruction:
    case InstitutionType::ExtractionCombine:
    case InstitutionType::FuelTrust:
    case InstitutionType::SurveyOffice:
    case InstitutionType::PrivateHauler:
    case InstitutionType::DevelopmentBureau:
    case InstitutionType::ContinuityOffice:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidAppointmentRole(const AppointmentRole value) noexcept {
    switch (value) {
    case AppointmentRole::FleetCommander:
    case AppointmentRole::ColonyAdministrator:
    case AppointmentRole::ShipyardDirector:
    case AppointmentRole::SurveyChief:
    case AppointmentRole::LogisticsCoordinator:
    case AppointmentRole::InstitutionHead:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidAppointmentScopeType(const AppointmentScopeType value) noexcept {
    switch (value) {
    case AppointmentScopeType::Fleet:
    case AppointmentScopeType::Colony:
    case AppointmentScopeType::Institution:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidBodyType(const BodyType value) noexcept {
    switch (value) {
    case BodyType::Star:
    case BodyType::Terrestrial:
    case BodyType::GasGiant:
    case BodyType::Moon:
    case BodyType::Asteroid:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidStrategicZone(const StrategicZone value) noexcept {
    switch (value) {
    case StrategicZone::InnerCore:
    case StrategicZone::MilitaryIndustrial:
    case StrategicZone::BeltIndustrial:
    case StrategicZone::OuterLogistics:
    case StrategicZone::DeepSurveyFrontier:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidShipRole(const ShipRole value) noexcept {
    switch (value) {
    case ShipRole::Survey:
    case ShipRole::Freighter:
    case ShipRole::Escort:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidProcessingPolicy(const ProcessingPolicy value) noexcept {
    switch (value) {
    case ProcessingPolicy::Balanced:
    case ProcessingPolicy::ShipbuildingFocus:
    case ProcessingPolicy::FuelFocus:
    case ProcessingPolicy::ElectronicsFocus:
    case ProcessingPolicy::StockpileRecovery:
    case ProcessingPolicy::Manual:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidShipyardOrderStatus(const ShipyardOrderStatus value) noexcept {
    switch (value) {
    case ShipyardOrderStatus::Active:
    case ShipyardOrderStatus::Completed:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidFleetOrderType(const FleetOrderType value) noexcept {
    switch (value) {
    case FleetOrderType::None:
    case FleetOrderType::MoveToBody:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidEventSeverity(const EventSeverity value) noexcept {
    switch (value) {
    case EventSeverity::Info:
    case EventSeverity::Warning:
    case EventSeverity::Critical:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidMineral(const Mineral value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < mineralCount();
}

[[nodiscard]] bool isValidProcessedMaterial(const ProcessedMaterial value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < processedMaterialCount();
}

template <typename T, typename IdT>
[[nodiscard]] bool containsId(const std::vector<T>& items, const IdT id) noexcept {
    return std::any_of(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
}

// Finds a read-only record by typed ID for cross-record invariants that need
// more than existence, such as validating per-ship fuel against class capacity.
template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}


// Validates that IDs are positive, unique, and below their monotonic allocator.
// This prevents stale counters from producing duplicate IDs after a loaded game
// continues simulating and allocating new records.
template <typename T, typename IdT>
void validateIdsAndCounter(const std::vector<T>& items,
                           const std::int64_t nextId,
                           const std::string_view label) {
    requireState(nextId > 0, std::string{label} + " counter must be positive");

    std::unordered_set<std::int64_t> seen;
    std::int64_t maxId = 0;
    for (const T& item : items) {
        const std::int64_t value = idValue(item.id);
        requireState(value > 0, std::string{label} + " ID must be positive");
        requireState(seen.insert(value).second, std::string{label} + " IDs must be unique");
        maxId = std::max(maxId, value);
    }

    requireState(nextId > maxId, std::string{label} + " counter must be greater than max loaded ID");
}

template <typename IdT>
void requireValidReference(const bool exists, const IdT id, const std::string_view label) {
    requireState(static_cast<bool>(id), std::string{label} + " ID must be positive");
    requireState(exists, std::string{label} + " reference is missing");
}

void validateMineralSet(const MineralSet& set, const std::string_view label) {
    for (const double amount : set.amount) {
        requireState(isFinite(amount), std::string{label} + " amount must be finite");
        requireState(amount >= 0.0, std::string{label} + " amount must be non-negative");
    }
}

void validateProcessedMaterialSet(const ProcessedMaterialSet& set, const std::string_view label) {
    for (const double amount : set.amount) {
        requireState(isFinite(amount), std::string{label} + " amount must be finite");
        requireState(amount >= 0.0, std::string{label} + " amount must be non-negative");
    }
}


void validateBodyParentGraph(const std::vector<Body>& bodies) {
    constexpr int kMaximumParentChainDepth = 16;

    std::unordered_map<std::int64_t, const Body*> bodyById;
    bodyById.reserve(bodies.size());
    for (const Body& body : bodies) {
        bodyById.emplace(body.id.value, &body);
    }

    for (const Body& body : bodies) {
        std::unordered_set<std::int64_t> visited;
        const Body* current = &body;
        int depth = 0;

        while (current->parentBodyId.has_value()) {
            // Track the chain from each starting body independently. Seeing the
            // same parent twice means the rail hierarchy can never terminate at
            // a root/star and would recurse forever in map/transit projections.
            requireState(visited.insert(current->id.value).second,
                         "body parent graph must not contain cycles");
            requireState(depth < kMaximumParentChainDepth,
                         "body parent chain depth exceeds validation limit");

            const BodyId parentId = *current->parentBodyId;
            requireState(parentId != current->id, "body parent must not be itself");

            const auto parentIt = bodyById.find(parentId.value);
            requireState(parentIt != bodyById.end(), "body parent reference is missing");
            current = parentIt->second;
            ++depth;
        }
    }
}

void validateProcessingPolicy(const Colony& colony) {
    requireState(isValidProcessingPolicy(colony.processingPolicy), "colony processing policy must be valid");

    double manualWeightTotal = 0.0;
    for (const ProcessingAllocation& allocation : colony.manualProcessingAllocations) {
        requireState(isValidProcessedMaterial(allocation.material),
                     "manual processing allocation material must be valid");
        requireState(isFinite(allocation.weight), "manual processing allocation weight must be finite");
        requireState(allocation.weight >= 0.0, "manual processing allocation weight must be non-negative");
        manualWeightTotal += allocation.weight;
    }

    // Manual policy has no preset fallback. Require at least one positive weight
    // so a loaded or imported state cannot silently spend zero processor capacity.
    if (colony.processingPolicy == ProcessingPolicy::Manual) {
        requireState(manualWeightTotal > kProcessedMaterialComparisonEpsilon,
                     "manual processing policy requires positive total weight");
    }
}

void validatePersonCompetencies(const PersonCompetencies& competencies) {
    // Competencies are stored as non-negative levels only. They intentionally do
    // not influence simulation output until appointment/merit rules are added.
    requireState(competencies.logistics >= 0, "person logistics competency must be non-negative");
    requireState(competencies.industry >= 0, "person industry competency must be non-negative");
    requireState(competencies.survey >= 0, "person survey competency must be non-negative");
    requireState(competencies.command >= 0, "person command competency must be non-negative");
    requireState(competencies.administration >= 0, "person administration competency must be non-negative");
    requireState(competencies.engineering >= 0, "person engineering competency must be non-negative");
    requireState(competencies.intelligence >= 0, "person intelligence competency must be non-negative");
    requireState(competencies.crisisManagement >= 0, "person crisis management competency must be non-negative");
}

void validatePersonServiceRecord(const PersonServiceRecord& record) {
    requireState(record.successfulAssignments >= 0, "person successful assignments must be non-negative");
    requireState(record.failedAssignments >= 0, "person failed assignments must be non-negative");
    requireState(record.commendations >= 0, "person commendations must be non-negative");
    requireState(record.controversies >= 0, "person controversies must be non-negative");
}

[[nodiscard]] bool appointmentScopeExists(const GameState& state, const AppointmentScopeType scopeType, const std::int64_t scopeId) noexcept {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
        return containsId(state.fleets, FleetId{scopeId});
    case AppointmentScopeType::Colony:
        return containsId(state.colonies, ColonyId{scopeId});
    case AppointmentScopeType::Institution:
        return containsId(state.institutions, InstitutionId{scopeId});
    }
    return false;
}

void validateFleetOrder(const GameState& state, const Fleet& fleet) {
    requireState(isValidFleetOrderType(fleet.activeOrder.type), "fleet order type must be valid");
    requireState(fleet.activeOrder.daysRemaining >= 0, "fleet order days must be non-negative");

    if (fleet.activeOrder.type == FleetOrderType::None) {
        requireState(!fleet.destinationBodyId.has_value(), "idle fleet must not have a destination body");
        requireState(!fleet.activeOrder.targetBodyId.has_value(), "idle fleet must not have an order target");
        requireState(fleet.activeOrder.daysRemaining == 0, "idle fleet must have zero order days remaining");
    } else {
        requireState(fleet.destinationBodyId.has_value(), "moving fleet must have a destination body");
        requireState(fleet.activeOrder.targetBodyId.has_value(), "moving fleet must have an order target body");
        requireState(*fleet.destinationBodyId == *fleet.activeOrder.targetBodyId,
                     "moving fleet destination and target body must match");
        requireState(fleet.activeOrder.daysRemaining > 0, "moving fleet must have positive days remaining");
        requireState(fleet.activeOrder.departureBodyId.has_value(), "moving fleet must have a departure body");
        requireValidReference(containsId(state.bodies, *fleet.activeOrder.departureBodyId), *fleet.activeOrder.departureBodyId,
                              "fleet departure body");
        requireState(fleet.activeOrder.departureDay >= 0, "moving fleet departure day must be non-negative");
        requireState(fleet.activeOrder.arrivalDay > fleet.activeOrder.departureDay,
                     "moving fleet arrival day must follow departure day");
        requireState(fleet.activeOrder.arrivalDay >= state.date.day,
                     "moving fleet arrival day must not be in the past");
        requireState(fleet.activeOrder.arrivalDay - state.date.day == fleet.activeOrder.daysRemaining,
                     "moving fleet days remaining must match arrival day");
        requireState(isFinite(fleet.activeOrder.departurePosition.x) && isFinite(fleet.activeOrder.departurePosition.y),
                     "moving fleet departure position must be finite");
        requireState(isFinite(fleet.activeOrder.projectedArrivalPosition.x) &&
                         isFinite(fleet.activeOrder.projectedArrivalPosition.y),
                     "moving fleet projected arrival position must be finite");
        requireState(isFinite(fleet.activeOrder.routeCurveControlPoint.x) &&
                         isFinite(fleet.activeOrder.routeCurveControlPoint.y),
                     "moving fleet route curve control point must be finite");
        requireState(isFinite(fleet.activeOrder.transitDistanceKm) && fleet.activeOrder.transitDistanceKm > 0.0,
                     "moving fleet transit distance must be positive and finite");
        requireState(isFinite(fleet.activeOrder.burnAccelerationG) && fleet.activeOrder.burnAccelerationG > 0.0,
                     "moving fleet burn acceleration must be positive and finite");
        requireState(*fleet.destinationBodyId != fleet.currentBodyId, "moving fleet destination must differ from current body");
        requireValidReference(containsId(state.bodies, *fleet.destinationBodyId), *fleet.destinationBodyId,
                              "fleet destination body");
    }

    for (const QueuedFleetOrder& queuedOrder : fleet.queuedOrders) {
        requireState(isValidFleetOrderType(queuedOrder.type), "queued fleet order type must be valid");
        requireState(queuedOrder.type != FleetOrderType::None, "queued fleet order must be actionable");
        requireState(queuedOrder.targetBodyId.has_value(), "queued fleet order must have a target body");
        requireValidReference(containsId(state.bodies, *queuedOrder.targetBodyId), *queuedOrder.targetBodyId,
                              "queued fleet order target body");
    }
}

void validateEventPayload(const GameState& state, const SimEventPayload& payload) {
    std::visit([&state](const auto& event) {
        using Event = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            requireValidReference(containsId(state.colonies, event.colonyId), event.colonyId, "event colony");
            requireValidReference(containsId(state.bodies, event.bodyId), event.bodyId, "event body");
            requireState(isValidMineral(event.mineral), "event mineral must be valid");
            requireState(isFinite(event.amount) && event.amount > 0.0, "extracted amount must be positive and finite");
            requireState(isFinite(event.remainingDeposit) && event.remainingDeposit >= 0.0,
                         "remaining deposit must be finite and non-negative");
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            requireValidReference(containsId(state.shipyardOrders, event.orderId), event.orderId, "event shipyard order");
            requireValidReference(containsId(state.colonies, event.colonyId), event.colonyId, "event colony");
            requireValidReference(containsId(state.shipClasses, event.shipClassId), event.shipClassId, "event ship class");
            requireState(event.quantity > 0, "event quantity must be positive");
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            requireValidReference(containsId(state.shipyardOrders, event.orderId), event.orderId, "event shipyard order");
            requireValidReference(containsId(state.colonies, event.colonyId), event.colonyId, "event colony");
            requireValidReference(containsId(state.ships, event.shipId), event.shipId, "event ship");
            requireValidReference(containsId(state.fleets, event.fleetId), event.fleetId, "event fleet");
            requireValidReference(containsId(state.shipClasses, event.shipClassId), event.shipClassId, "event ship class");
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            requireValidReference(containsId(state.fleets, event.fleetId), event.fleetId, "event fleet");
            requireValidReference(containsId(state.bodies, event.originBodyId), event.originBodyId, "event origin body");
            requireValidReference(containsId(state.bodies, event.destinationBodyId), event.destinationBodyId,
                                  "event destination body");
            requireState(event.originBodyId != event.destinationBodyId,
                         "fleet-order event origin and destination must differ");
            requireState(event.daysRemaining > 0, "fleet-order event days remaining must be positive");
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            requireValidReference(containsId(state.fleets, event.fleetId), event.fleetId, "event fleet");
            requireValidReference(containsId(state.bodies, event.destinationBodyId), event.destinationBodyId,
                                  "event destination body");
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            requireValidReference(containsId(state.fleets, event.fleetId), event.fleetId, "event fleet");
            requireValidReference(containsId(state.bodies, event.bodyId), event.bodyId, "event survey body");
            requireState(event.depositsImproved > 0, "survey event must improve at least one deposit");
            requireState(isFinite(event.averageConfidenceBefore) && event.averageConfidenceBefore >= 0.0 &&
                             event.averageConfidenceBefore <= 1.0,
                         "survey event average before confidence must be between zero and one");
            requireState(isFinite(event.averageConfidenceAfter) && event.averageConfidenceAfter >= 0.0 &&
                             event.averageConfidenceAfter <= 1.0,
                         "survey event average after confidence must be between zero and one");
            requireState(event.averageConfidenceAfter >= event.averageConfidenceBefore,
                         "survey event confidence must not decrease");
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            requireState(!event.reason.empty(), "command-rejected event reason must be non-empty");
        }
    }, payload);
}

} // namespace

void validateGameState(const GameState& state) {
    requireState(state.date.day >= 0, "current day must be non-negative");

    validateIdsAndCounter<StarSystem, StarSystemId>(state.starSystems, state.ids.nextStarSystemId, "star system");
    validateIdsAndCounter<Body, BodyId>(state.bodies, state.ids.nextBodyId, "body");
    validateIdsAndCounter<Colony, ColonyId>(state.colonies, state.ids.nextColonyId, "colony");
    validateIdsAndCounter<Institution, InstitutionId>(state.institutions, state.ids.nextInstitutionId, "institution");
    validateIdsAndCounter<Person, PersonId>(state.people, state.ids.nextPersonId, "person");
    validateIdsAndCounter<ShipClass, ShipClassId>(state.shipClasses, state.ids.nextShipClassId, "ship class");
    validateIdsAndCounter<ShipyardOrder, ShipyardOrderId>(state.shipyardOrders,
                                                            state.ids.nextShipyardOrderId,
                                                            "shipyard order");
    validateIdsAndCounter<Ship, ShipId>(state.ships, state.ids.nextShipId, "ship");
    validateIdsAndCounter<Fleet, FleetId>(state.fleets, state.ids.nextFleetId, "fleet");
    validateIdsAndCounter<SimEvent, EventId>(state.eventLog, state.ids.nextEventId, "event");

    for (const StarSystem& system : state.starSystems) {
        requireState(!system.name.empty(), "star system name must be non-empty");
    }

    for (const Institution& institution : state.institutions) {
        requireState(!institution.name.empty(), "institution name must be non-empty");
        requireState(isValidInstitutionType(institution.type), "institution type must be valid");
    }

    for (const Person& person : state.people) {
        requireState(!person.name.empty(), "person name must be non-empty");
        requireValidReference(containsId(state.institutions, person.institutionId),
                              person.institutionId,
                              "person institution");
        validatePersonCompetencies(person.competencies);
        requireState(person.seniorityLevel >= 0, "person seniority level must be non-negative");
        validatePersonServiceRecord(person.serviceRecord);
    }

    validateBodyParentGraph(state.bodies);

    for (const Body& body : state.bodies) {
        requireValidReference(containsId(state.starSystems, body.systemId), body.systemId, "body system");
        requireState(!body.name.empty(), "body name must be non-empty");
        requireState(isValidBodyType(body.type), "body type must be valid");
        requireState(isValidStrategicZone(body.strategicZone), "body strategic zone must be valid");
        if (body.parentBodyId.has_value()) {
            requireValidReference(containsId(state.bodies, *body.parentBodyId), *body.parentBodyId, "body parent");
            requireState(*body.parentBodyId != body.id, "body parent must not be itself");
        }
        requireState(isFinite(body.orbitalRadiusKm) && body.orbitalRadiusKm >= 0.0,
                     "body orbital radius must be finite and non-negative");
        requireState(isFinite(body.orbitalPeriodDays) && body.orbitalPeriodDays >= 0.0,
                     "body orbital period must be finite and non-negative");
        requireState(isFinite(body.phaseRadians), "body phase must be finite");
        requireState(isFinite(body.displayRadius) && body.displayRadius > 0.0,
                     "body display radius must be finite and positive");
        requireState(isFinite(body.x) && isFinite(body.y), "body coordinates must be finite");
    }

    for (const Colony& colony : state.colonies) {
        requireValidReference(containsId(state.bodies, colony.bodyId), colony.bodyId, "colony body");
        if (colony.ownerInstitutionId.has_value()) {
            requireValidReference(containsId(state.institutions, *colony.ownerInstitutionId),
                                  *colony.ownerInstitutionId,
                                  "colony owner institution");
        }
        requireState(!colony.name.empty(), "colony name must be non-empty");
        validateMineralSet(colony.stockpile, "colony raw stockpile");
        validateProcessedMaterialSet(colony.processedStockpile, "colony processed stockpile");
        requireState(isFinite(colony.mines) && colony.mines >= 0.0, "colony mines must be finite and non-negative");
        requireState(isFinite(colony.processorCapacity) && colony.processorCapacity >= 0.0,
                     "processor capacity must be finite and non-negative");
        requireState(isFinite(colony.shipyardCapacity) && colony.shipyardCapacity >= 0.0,
                     "shipyard capacity must be finite and non-negative");
        validateProcessingPolicy(colony);
    }

    std::unordered_set<std::string> depositKeys;
    for (const MineralDeposit& deposit : state.mineralDeposits) {
        requireValidReference(containsId(state.bodies, deposit.bodyId), deposit.bodyId, "deposit body");
        requireState(isValidMineral(deposit.mineral), "deposit mineral must be valid");
        requireState(isFinite(deposit.remaining) && deposit.remaining >= 0.0,
                     "deposit remaining must be finite and non-negative");
        requireState(isFinite(deposit.accessibility) && deposit.accessibility >= 0.0,
                     "deposit accessibility must be finite and non-negative");
        requireState(isFinite(deposit.confidence) && deposit.confidence >= 0.0 && deposit.confidence <= 1.0,
                     "deposit confidence must be finite and between zero and one");
        const std::string key = std::to_string(deposit.bodyId.value) + ":" +
                                std::to_string(static_cast<std::size_t>(deposit.mineral));
        requireState(depositKeys.insert(key).second, "duplicate mineral deposit rows are invalid");
    }

    for (const ShipClass& shipClass : state.shipClasses) {
        requireState(!shipClass.name.empty(), "ship class name must be non-empty");
        requireState(isValidShipRole(shipClass.role), "ship role must be valid");
        validateProcessedMaterialSet(shipClass.buildCost, "ship class build cost");
        requireState(isFinite(shipClass.buildPoints) && shipClass.buildPoints > 0.0,
                     "ship class build points must be positive and finite");
        requireState(isFinite(shipClass.speedKmPerDay) && shipClass.speedKmPerDay >= 0.0,
                     "ship class speed must be finite and non-negative");
        requireState(isFinite(shipClass.fuelCapacity) && shipClass.fuelCapacity >= 0.0,
                     "ship class fuel capacity must be finite and non-negative");
    }

    for (const ShipyardOrder& order : state.shipyardOrders) {
        requireValidReference(containsId(state.colonies, order.colonyId), order.colonyId, "shipyard order colony");
        requireValidReference(containsId(state.shipClasses, order.shipClassId), order.shipClassId,
                              "shipyard order ship class");
        requireState(order.quantityRequested > 0, "shipyard order requested quantity must be positive");
        requireState(order.quantityCompleted >= 0, "shipyard order completed quantity must be non-negative");
        requireState(order.quantityCompleted <= order.quantityRequested,
                     "shipyard order completed quantity must not exceed requested quantity");
        requireState(isFinite(order.accumulatedBuildPoints) && order.accumulatedBuildPoints >= 0.0,
                     "shipyard order build points must be finite and non-negative");
        requireState(isValidShipyardOrderStatus(order.status), "shipyard order status must be valid");

        // Shipyard order status is a small state machine. Validate the lifecycle
        // shape explicitly so hand-edited saves cannot load states that the
        // production tick would never create on its own.
        if (order.status == ShipyardOrderStatus::Active) {
            requireState(order.quantityCompleted < order.quantityRequested,
                         "active shipyard order must not already be complete");
        } else if (order.status == ShipyardOrderStatus::Completed) {
            requireState(order.quantityCompleted == order.quantityRequested,
                         "completed shipyard order must have completed all requested ships");
            requireState(order.accumulatedBuildPoints == 0.0,
                         "completed shipyard order must not retain build progress");
        }
    }

    // Build a reverse index while validating fleets so every ship can be
    // listed by exactly one fleet. A ship duplicated across two fleet rosters
    // would otherwise appear to be in two places at once in UI/query views.
    std::unordered_map<std::int64_t, FleetId> listedShipFleetIds;
    for (const Fleet& fleet : state.fleets) {
        requireState(!fleet.name.empty(), "fleet name must be non-empty");
        if (fleet.ownerInstitutionId.has_value()) {
            requireValidReference(containsId(state.institutions, *fleet.ownerInstitutionId),
                                  *fleet.ownerInstitutionId,
                                  "fleet owner institution");
        }
        requireValidReference(containsId(state.bodies, fleet.currentBodyId), fleet.currentBodyId, "fleet current body");
        validateFleetOrder(state, fleet);

        std::unordered_set<std::int64_t> fleetShipIds;
        for (const ShipId shipId : fleet.shipIds) {
            requireValidReference(containsId(state.ships, shipId), shipId, "fleet ship");
            requireState(fleetShipIds.insert(shipId.value).second, "fleet ship IDs must be unique within a fleet");
            requireState(listedShipFleetIds.emplace(shipId.value, fleet.id).second,
                         "ship must not be listed by multiple fleets");
        }
    }

    for (const Ship& ship : state.ships) {
        const ShipClass* shipClass = findById(state.shipClasses, ship.shipClassId);
        requireValidReference(shipClass != nullptr, ship.shipClassId, "ship class");
        requireValidReference(containsId(state.fleets, ship.fleetId), ship.fleetId, "ship fleet");
        requireState(!ship.name.empty(), "ship name must be non-empty");
        requireState(isFinite(ship.fuel) && ship.fuel >= 0.0, "ship fuel must be finite and non-negative");
        requireState(shipClass == nullptr || ship.fuel <= shipClass->fuelCapacity + kFuelComparisonEpsilon,
                     "ship fuel must not exceed class fuel capacity");

        const auto listedFleetIt = listedShipFleetIds.find(ship.id.value);
        requireState(listedFleetIt != listedShipFleetIds.end(), "ship/fleet references must be bidirectional");
        requireState(listedFleetIt->second == ship.fleetId,
                     "ship fleet ID must match the fleet roster that lists it");
    }

    std::unordered_set<std::string> appointmentSlots;
    for (const Appointment& appointment : state.appointments) {
        requireState(isValidAppointmentRole(appointment.role), "appointment role must be valid");
        requireState(isValidAppointmentScopeType(appointment.scopeType), "appointment scope type must be valid");
        requireState(appointment.scopeId > 0, "appointment scope ID must be positive");
        requireState(appointment.appointedDay >= 0, "appointment day must be non-negative");
        requireState(appointment.appointedDay <= state.date.day, "appointment day must not exceed current day");
        requireState(appointmentScopeExists(state, appointment.scopeType, appointment.scopeId),
                     "appointment target scope reference is missing");

        const Person* person = findById(state.people, appointment.personId);
        requireValidReference(person != nullptr, appointment.personId, "appointment person");
        requireState(person == nullptr || containsId(state.institutions, person->institutionId),
                     "appointment person institution reference is missing");

        const std::string slotKey = std::to_string(static_cast<int>(appointment.role)) + ":" +
                                    std::to_string(static_cast<int>(appointment.scopeType)) + ":" +
                                    std::to_string(appointment.scopeId);
        requireState(appointmentSlots.insert(slotKey).second,
                     "appointment role/scope slots must be unique");
    }

    std::int64_t previousEventId = 0;
    std::int64_t previousEventDay = 0;
    for (const SimEvent& event : state.eventLog) {
        requireState(event.id.value > previousEventId, "event IDs must be stored in strictly increasing order");
        previousEventId = event.id.value;
        requireState(event.day >= 0, "event day must be non-negative");
        requireState(event.day <= state.date.day, "event day must not exceed current simulation day");
        requireState(event.day >= previousEventDay, "event days must be non-decreasing by event ID order");
        previousEventDay = event.day;
        requireState(isValidEventSeverity(event.severity), "event severity must be valid");
        validateEventPayload(state, event.payload);
    }
}

} // namespace deep
