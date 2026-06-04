#include "app/SimulationQueries.h"

#include "app/ForecastService.h"

// Implements read-only query projection from GameState into app-layer DTOs.
// The functions in this file are intentionally presentation-adjacent but UI-free:
// they resolve names and flatten variants without depending on ImGui or SDL.

#include "sim/GameState.h"
#include "sim/Minerals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>

namespace deep {
namespace {

// Finds a read-only record by ID for name resolution. The returned pointer is
// non-owning and never escapes the query function that requested it.
template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

[[nodiscard]] std::string bodyName(const GameState& state, const BodyId id) {
    const Body* body = findById(state.bodies, id);
    return body == nullptr ? std::string{"<unknown body>"} : body->name;
}

[[nodiscard]] std::string institutionName(const GameState& state, const InstitutionId id) {
    const Institution* institution = findById(state.institutions, id);
    return institution == nullptr ? std::string{"<unknown institution>"} : institution->name;
}

[[nodiscard]] std::string optionalInstitutionName(const GameState& state, const std::optional<InstitutionId> id) {
    return id.has_value() ? institutionName(state, *id) : std::string{};
}

[[nodiscard]] const Body* bodyById(const GameState& state, const BodyId id) noexcept {
    return findById(state.bodies, id);
}

[[nodiscard]] std::string bodyTypeName(const BodyType type) {
    switch (type) {
    case BodyType::Star:
        return "Star";
    case BodyType::Terrestrial:
        return "Terrestrial";
    case BodyType::GasGiant:
        return "Gas Giant";
    case BodyType::Moon:
        return "Moon";
    case BodyType::Asteroid:
        return "Asteroid";
    }

    return "Unknown";
}

[[nodiscard]] std::string colonyName(const GameState& state, const ColonyId id) {
    const Colony* colony = findById(state.colonies, id);
    return colony == nullptr ? std::string{"<unknown colony>"} : colony->name;
}

[[nodiscard]] std::string shipClassName(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? std::string{"<unknown ship class>"} : shipClass->name;
}

[[nodiscard]] std::string shipRoleName(const ShipRole role) {
    switch (role) {
    case ShipRole::Survey:
        return "Survey";
    case ShipRole::Freighter:
        return "Freighter";
    case ShipRole::Escort:
        return "Escort";
    }

    return "Unknown";
}

[[nodiscard]] double shipClassBuildPoints(const GameState& state, const ShipClassId id) noexcept {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? 0.0 : shipClass->buildPoints;
}

[[nodiscard]] std::string statusName(const ShipyardOrderStatus status) {
    switch (status) {
    case ShipyardOrderStatus::Active:
        return "Active";
    case ShipyardOrderStatus::Completed:
        return "Completed";
    }

    return "Unknown";
}

// TEMP: Fleet movement uses the simulation's fixed five-day prototype duration
// until route distance and speed rules exist. Fuel v1 uses map distance but does
// not yet change movement duration.
constexpr int kPrototypeQueuedMoveDurationDays = 5;

struct FleetFuelTotals {
    double currentFuel = 0.0;
    double fuelCapacity = 0.0;
};

[[nodiscard]] double bodyDistance(const Body& origin, const Body& destination) noexcept {
    return std::hypot(destination.x - origin.x, destination.y - origin.y);
}

[[nodiscard]] double moveFuelCost(const GameState& state, const BodyId originBodyId, const BodyId destinationBodyId) noexcept {
    if (originBodyId == destinationBodyId) {
        return 0.0;
    }

    const Body* origin = bodyById(state, originBodyId);
    const Body* destination = bodyById(state, destinationBodyId);
    if (origin == nullptr || destination == nullptr) {
        return 0.0;
    }

    return std::max(1.0, bodyDistance(*origin, *destination) * kPrototypeFuelPerMapUnit);
}

[[nodiscard]] FleetFuelTotals fleetFuelTotals(const GameState& state, const Fleet& fleet) noexcept {
    FleetFuelTotals totals;
    for (const ShipId shipId : fleet.shipIds) {
        const Ship* ship = findById(state.ships, shipId);
        if (ship == nullptr) {
            continue;
        }

        totals.currentFuel += ship->fuel;
        const ShipClass* shipClass = findById(state.shipClasses, ship->shipClassId);
        if (shipClass != nullptr) {
            totals.fuelCapacity += shipClass->fuelCapacity;
        }
    }
    return totals;
}

[[nodiscard]] BodyId projectedQueueOrigin(const Fleet& fleet) noexcept {
    if (fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.activeOrder.targetBodyId.has_value()) {
        return *fleet.activeOrder.targetBodyId;
    }
    return fleet.currentBodyId;
}

[[nodiscard]] std::string fleetOrderName(const FleetOrderType type) {
    switch (type) {
    case FleetOrderType::None:
        return "None";
    case FleetOrderType::MoveToBody:
        return "MoveToBody";
    }

    return "Unknown";
}

[[nodiscard]] std::string processingPolicyName(const ProcessingPolicy policy) {
    switch (policy) {
    case ProcessingPolicy::Balanced:
        return "Balanced";
    case ProcessingPolicy::ShipbuildingFocus:
        return "Shipbuilding Focus";
    case ProcessingPolicy::FuelFocus:
        return "Fuel Focus";
    case ProcessingPolicy::ElectronicsFocus:
        return "Electronics Focus";
    case ProcessingPolicy::StockpileRecovery:
        return "Stockpile Recovery";
    case ProcessingPolicy::Manual:
        return "Manual";
    }

    return "Unknown";
}

[[nodiscard]] std::string processedMaterialName(const ProcessedMaterial material) {
    return std::string{toString(material)};
}

using ProcessingShares = std::array<double, processedMaterialCount()>;

void addProcessingWeight(ProcessingShares& weights, const ProcessedMaterial material, const double weight) noexcept {
    if (weight <= 0.0 || processedMaterialIndex(material) >= processedMaterialCount()) {
        return;
    }

    weights[processedMaterialIndex(material)] += weight;
}

[[nodiscard]] double processingWeightTotal(const ProcessingShares& weights) noexcept {
    double total = 0.0;
    for (const double weight : weights) {
        total += std::max(0.0, weight);
    }
    return total;
}

[[nodiscard]] ProcessingShares balancedProcessingWeights() noexcept {
    ProcessingShares weights{};
    for (double& weight : weights) {
        weight = 1.0;
    }
    return weights;
}

[[nodiscard]] ProcessingShares processingWeightsForPolicy(const Colony& colony, const ProcessingPolicy policy) noexcept {
    ProcessingShares weights{};

    switch (policy) {
    case ProcessingPolicy::Balanced:
        return balancedProcessingWeights();
    case ProcessingPolicy::ShipbuildingFocus:
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 4.0);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::OrdnanceMaterials, 0.5);
        break;
    case ProcessingPolicy::FuelFocus:
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 0.5);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 0.5);
        break;
    case ProcessingPolicy::ElectronicsFocus:
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 1.0);
        break;
    case ProcessingPolicy::StockpileRecovery:
        for (std::size_t i = 0; i < weights.size(); ++i) {
            weights[i] = 1.0 / (1.0 + std::max(0.0, colony.processedStockpile.amount[i]));
        }
        break;
    case ProcessingPolicy::Manual:
        for (const ProcessingAllocation& allocation : colony.manualProcessingAllocations) {
            addProcessingWeight(weights, allocation.material, allocation.weight);
        }
        break;
    }

    return weights;
}

[[nodiscard]] std::vector<ProcessingAllocationSummary> summarizeProcessingWeights(const ProcessingShares& weights) {
    std::vector<ProcessingAllocationSummary> summaries;
    summaries.reserve(weights.size());

    const double totalWeight = processingWeightTotal(weights);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        const double weight = std::max(0.0, weights[i]);
        summaries.push_back(ProcessingAllocationSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .weight = weight,
            .normalizedPercent = totalWeight <= kProcessedMaterialComparisonEpsilon ? 0.0 : weight * 100.0 / totalWeight
        });
    }

    return summaries;
}

[[nodiscard]] std::vector<ProcessingAllocationSummary> summarizeManualProcessingAllocations(const Colony& colony) {
    ProcessingShares weights{};
    for (const ProcessingAllocation& allocation : colony.manualProcessingAllocations) {
        addProcessingWeight(weights, allocation.material, allocation.weight);
    }
    return summarizeProcessingWeights(weights);
}

[[nodiscard]] std::vector<ProcessedMaterialStockpileSummary> summarizeProcessedStockpiles(const Colony& colony) {
    std::vector<ProcessedMaterialStockpileSummary> summaries;
    summaries.reserve(processedMaterialCount());

    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        summaries.push_back(ProcessedMaterialStockpileSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .amount = colony.processedStockpile.amount[i]
        });
    }

    return summaries;
}

[[nodiscard]] std::vector<ProcessedMaterialStockpileSummary> summarizeMaterialRequirements(const ProcessedMaterialSet& materials) {
    std::vector<ProcessedMaterialStockpileSummary> summaries;
    summaries.reserve(processedMaterialCount());

    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const double amount = materials.amount[i];
        if (amount <= kProcessedMaterialComparisonEpsilon) {
            continue;
        }

        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        summaries.push_back(ProcessedMaterialStockpileSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .amount = amount
        });
    }

    return summaries;
}

[[nodiscard]] std::string severityName(const EventSeverity severity) {
    switch (severity) {
    case EventSeverity::Info:
        return "Info";
    case EventSeverity::Warning:
        return "Warning";
    case EventSeverity::Critical:
        return "Critical";
    }

    return "Unknown";
}

[[nodiscard]] std::string idText(const std::int64_t value) {
    return std::to_string(value);
}


[[nodiscard]] double totalMinerals(const MineralSet& minerals) noexcept {
    double total = 0.0;
    for (const double amount : minerals.amount) {
        total += amount;
    }
    return total;
}

[[nodiscard]] double totalProcessedMaterials(const ProcessedMaterialSet& materials) noexcept {
    double total = 0.0;
    for (const double amount : materials.amount) {
        total += amount;
    }
    return total;
}

[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            return "mineral_extracted";
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            return "shipyard_order_created";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            return "ship_completed";
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            return "fleet_order_assigned";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            return "fleet_arrived";
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            return "command_rejected";
        }
    }, payload);
}

[[nodiscard]] std::string eventMessage(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        std::ostringstream out;
        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            out << "Extracted " << event.amount << ' ' << toString(event.mineral)
                << " at body " << idText(event.bodyId.value);
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            out << "Created shipyard order " << idText(event.orderId.value)
                << " for " << event.quantity << " ship(s)";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            out << "Completed ship " << idText(event.shipId.value)
                << " for order " << idText(event.orderId.value);
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            out << "Assigned fleet " << idText(event.fleetId.value)
                << " to body " << idText(event.destinationBodyId.value)
                << " in " << event.daysRemaining << " day(s)";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            out << "Fleet " << idText(event.fleetId.value)
                << " arrived at body " << idText(event.destinationBodyId.value);
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            out << event.reason;
        }
        return out.str();
    }, payload);
}

[[nodiscard]] EventLogEntrySummary summarizeEvent(const SimEvent& event) {
    return EventLogEntrySummary{
        .id = event.id,
        .day = event.day,
        .severity = event.severity,
        .severityName = severityName(event.severity),
        .eventType = eventTypeName(event.payload),
        .message = eventMessage(event.payload)
    };
}

} // namespace

SimulationQueries::SimulationQueries(const SimulationService& service) noexcept
    : service_{service} {}

std::vector<ColonySummary> SimulationQueries::colonies() const {
    const GameState& state = service_.state();
    std::vector<ColonySummary> summaries;
    summaries.reserve(state.colonies.size());

    for (const Colony& colony : state.colonies) {
        summaries.push_back(ColonySummary{
            .id = colony.id,
            .bodyId = colony.bodyId,
            .name = colony.name,
            .bodyName = bodyName(state, colony.bodyId),
            .mines = colony.mines,
            .processorCapacity = colony.processorCapacity,
            .processingPolicy = colony.processingPolicy,
            .processingPolicyName = processingPolicyName(colony.processingPolicy),
            .ownerInstitutionId = colony.ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, colony.ownerInstitutionId),
            .manualProcessingAllocations = summarizeManualProcessingAllocations(colony),
            .effectiveProcessingAllocations = summarizeProcessingWeights(processingWeightsForPolicy(colony, colony.processingPolicy)),
            .processedStockpiles = summarizeProcessedStockpiles(colony),
            .shipyardCapacity = colony.shipyardCapacity,
            .totalRawStockpile = totalMinerals(colony.stockpile),
            .totalProcessedStockpile = totalProcessedMaterials(colony.processedStockpile)
        });
    }

    return summaries;
}

std::vector<ShipyardOrderSummary> SimulationQueries::shipyardOrders() const {
    const GameState& state = service_.state();
    std::vector<ShipyardOrderSummary> summaries;
    summaries.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        summaries.push_back(ShipyardOrderSummary{
            .id = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .quantityRequested = order.quantityRequested,
            .quantityCompleted = order.quantityCompleted,
            .accumulatedBuildPoints = order.accumulatedBuildPoints,
            .requiredBuildPoints = shipClassBuildPoints(state, order.shipClassId),
            .status = order.status,
            .statusName = statusName(order.status)
        });
    }

    return summaries;
}

std::vector<ProductionBacklogSummary> SimulationQueries::productionBacklog() const {
    const ForecastService forecasts{service_};
    const std::vector<ProductionBacklogForecast> backlog = forecasts.productionBacklog();

    std::vector<ProductionBacklogSummary> summaries;
    summaries.reserve(backlog.size());
    for (const ProductionBacklogForecast& row : backlog) {
        summaries.push_back(ProductionBacklogSummary{
            .orderId = row.orderId,
            .colonyId = row.colonyId,
            .shipClassId = row.shipClassId,
            .colonyName = row.colonyName,
            .shipClassName = row.shipClassName,
            .quantityRequested = row.quantityRequested,
            .quantityCompleted = row.quantityCompleted,
            .queuePosition = row.queuePosition,
            .shipsRemaining = row.shipsRemaining,
            .accumulatedBuildPoints = row.accumulatedBuildPoints,
            .buildPointsRemaining = row.buildPointsRemaining,
            .requiredMaterialsRemaining = summarizeMaterialRequirements(row.requiredMaterialsRemaining),
            .etaDays = row.etaDays,
            .blockingMaterialName = row.blockingMaterialName,
            .statusName = row.statusName
        });
    }

    return summaries;
}

std::vector<ShipClassSummary> SimulationQueries::shipClasses() const {
    const GameState& state = service_.state();
    std::vector<ShipClassSummary> summaries;
    summaries.reserve(state.shipClasses.size());

    for (const ShipClass& shipClass : state.shipClasses) {
        summaries.push_back(ShipClassSummary{
            .id = shipClass.id,
            .name = shipClass.name,
            .role = shipClass.role,
            .roleName = shipRoleName(shipClass.role),
            .buildPoints = shipClass.buildPoints
        });
    }

    return summaries;
}

std::vector<FleetSummary> SimulationQueries::fleets() const {
    const GameState& state = service_.state();
    std::vector<FleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const bool hasActiveOrder = fleet.activeOrder.type != FleetOrderType::None;
        const int activeOrderEtaDays = hasActiveOrder ? std::max(0, fleet.activeOrder.daysRemaining) : 0;
        const std::int64_t currentDay = state.date.day;
        std::int64_t nextStartDay = currentDay + activeOrderEtaDays;
        const FleetFuelTotals fuel = fleetFuelTotals(state, fleet);
        double projectedFuelRemaining = fuel.currentFuel;
        BodyId projectedOrigin = projectedQueueOrigin(fleet);

        std::vector<FleetQueuedOrderSummary> queuedOrders;
        queuedOrders.reserve(fleet.queuedOrders.size());
        for (std::size_t i = 0; i < fleet.queuedOrders.size(); ++i) {
            const QueuedFleetOrder& order = fleet.queuedOrders.at(i);
            const std::int64_t startDay = nextStartDay;
            const std::int64_t arrivalDay = startDay + kPrototypeQueuedMoveDurationDays;
            const double fuelCost = order.targetBodyId.has_value()
                ? moveFuelCost(state, projectedOrigin, *order.targetBodyId)
                : 0.0;
            projectedFuelRemaining -= fuelCost;

            queuedOrders.push_back(FleetQueuedOrderSummary{
                .queuePosition = i + 1U,
                .orderType = order.type,
                .orderName = fleetOrderName(order.type),
                .destinationBodyId = order.targetBodyId,
                .destinationBodyName = order.targetBodyId.has_value()
                    ? bodyName(state, *order.targetBodyId)
                    : std::string{},
                .etaDays = static_cast<int>(arrivalDay - currentDay),
                .projectedStartDay = startDay,
                .projectedArrivalDay = arrivalDay,
                .fuelCost = fuelCost,
                .projectedFuelRemaining = std::max(0.0, projectedFuelRemaining),
                .fuelAffordable = projectedFuelRemaining + kFuelComparisonEpsilon >= 0.0
            });

            // Queued v1 moves execute serially. Each preview row starts when the
            // previous active/queued move is projected to arrive.
            nextStartDay = arrivalDay;
            if (order.targetBodyId.has_value()) {
                projectedOrigin = *order.targetBodyId;
            }
        }

        const int totalRouteDurationDays = static_cast<int>(std::max<std::int64_t>(0, nextStartDay - currentDay));
        const std::int64_t activeArrivalDay = hasActiveOrder ? currentDay + activeOrderEtaDays : currentDay;

        summaries.push_back(FleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .currentBodyName = bodyName(state, fleet.currentBodyId),
            .destinationBodyId = fleet.destinationBodyId,
            .destinationBodyName = fleet.destinationBodyId.has_value()
                ? bodyName(state, *fleet.destinationBodyId)
                : std::string{},
            .shipCount = fleet.shipIds.size(),
            .ownerInstitutionId = fleet.ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, fleet.ownerInstitutionId),
            .activeOrderType = fleet.activeOrder.type,
            .activeOrderName = fleetOrderName(fleet.activeOrder.type),
            .hasActiveOrder = hasActiveOrder,
            .daysRemaining = fleet.activeOrder.daysRemaining,
            .currentFuel = fuel.currentFuel,
            .fuelCapacity = fuel.fuelCapacity,
            .fuelPercent = fuel.fuelCapacity <= kFuelComparisonEpsilon ? 0.0 : (fuel.currentFuel * 100.0 / fuel.fuelCapacity),
            .currentRange = fuel.currentFuel / kPrototypeFuelPerMapUnit,
            .activeOrderEtaDays = hasActiveOrder ? std::optional<int>{activeOrderEtaDays} : std::optional<int>{},
            .totalRouteDurationDays = totalRouteDurationDays,
            .activeOrderProjectedArrivalDay = activeArrivalDay,
            .queuedOrders = std::move(queuedOrders)
        });
    }

    return summaries;
}


std::vector<PersonSummary> SimulationQueries::personnel() const {
    const GameState& state = service_.state();
    std::vector<PersonSummary> summaries;
    summaries.reserve(state.people.size());

    for (const Person& person : state.people) {
        summaries.push_back(PersonSummary{
            .id = person.id,
            .name = person.name,
            .institutionId = person.institutionId,
            .institutionName = institutionName(state, person.institutionId),
            .competencies = person.competencies,
            .seniorityLevel = person.seniorityLevel,
            .serviceRecord = person.serviceRecord
        });
    }

    return summaries;
}


std::string SimulationQueries::institutionDisplayName(const InstitutionId id) const {
    return institutionName(service_.state(), id);
}

std::optional<FleetSummary> SimulationQueries::fleet(const FleetId id) const {
    const std::vector<FleetSummary> summaries = fleets();
    const auto it = std::find_if(summaries.begin(), summaries.end(), [id](const FleetSummary& summary) {
        return summary.id == id;
    });
    return it == summaries.end() ? std::optional<FleetSummary>{} : std::optional<FleetSummary>{*it};
}


std::optional<FleetMovePreview> SimulationQueries::fleetMovePreview(const FleetId fleetId, const BodyId destinationBodyId) const {
    const GameState& state = service_.state();
    const Fleet* fleet = findById(state.fleets, fleetId);
    const Body* destination = bodyById(state, destinationBodyId);
    if (fleet == nullptr || destination == nullptr) {
        return std::nullopt;
    }

    FleetFuelTotals fuel = fleetFuelTotals(state, *fleet);
    BodyId projectedOrigin = projectedQueueOrigin(*fleet);
    double queuedFuelRequired = 0.0;

    for (const QueuedFleetOrder& queuedOrder : fleet->queuedOrders) {
        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            return std::nullopt;
        }
        queuedFuelRequired += moveFuelCost(state, projectedOrigin, *queuedOrder.targetBodyId);
        projectedOrigin = *queuedOrder.targetBodyId;
    }

    const double newMoveCost = moveFuelCost(state, projectedOrigin, destinationBodyId);
    queuedFuelRequired += newMoveCost;
    const double projectedRemaining = fuel.currentFuel - queuedFuelRequired;
    const bool canAfford = projectedRemaining + kFuelComparisonEpsilon >= 0.0;

    std::string warning;
    if (!canAfford) {
        warning = "Insufficient fuel for queued route.";
    } else if (fuel.fuelCapacity <= kFuelComparisonEpsilon) {
        warning = "Fleet has no fuel capacity.";
    }

    return FleetMovePreview{
        .fleetId = fleetId,
        .destinationBodyId = destinationBodyId,
        .destinationBodyName = destination->name,
        .fuelAvailable = fuel.currentFuel,
        .queuedFuelRequired = queuedFuelRequired,
        .newMoveFuelCost = newMoveCost,
        .projectedFuelRemaining = std::max(0.0, projectedRemaining),
        .canAfford = canAfford,
        .warningText = std::move(warning)
    };
}

std::vector<BodySystemSummary> SimulationQueries::bodySystemOverview() const {
    const GameState& state = service_.state();
    std::vector<BodySystemSummary> summaries;
    summaries.reserve(state.bodies.size());

    for (const Body& body : state.bodies) {
        const auto onBody = [body](const auto& item) {
            return item.bodyId == body.id;
        };
        const auto fleetAtBody = [body](const Fleet& fleet) {
            return fleet.currentBodyId == body.id;
        };

        // Counts are derived here rather than in the UI so the Bodies/System
        // panel remains a read-only projection over stable app DTOs.
        summaries.push_back(BodySystemSummary{
            .id = body.id,
            .name = body.name,
            .type = body.type,
            .typeName = bodyTypeName(body.type),
            .colonyCount = static_cast<std::size_t>(std::count_if(state.colonies.begin(), state.colonies.end(), onBody)),
            .mineralDepositCount = static_cast<std::size_t>(std::count_if(state.mineralDeposits.begin(), state.mineralDeposits.end(), onBody)),
            .fleetCount = static_cast<std::size_t>(std::count_if(state.fleets.begin(), state.fleets.end(), fleetAtBody))
        });
    }

    return summaries;
}

std::vector<StrategicBodySummary> SimulationQueries::strategicBodies() const {
    const GameState& state = service_.state();
    std::vector<StrategicBodySummary> summaries;
    summaries.reserve(state.bodies.size());

    for (const Body& body : state.bodies) {
        summaries.push_back(StrategicBodySummary{
            .id = body.id,
            .name = body.name,
            .type = body.type,
            .typeName = bodyTypeName(body.type),
            .x = body.x,
            .y = body.y
        });
    }

    return summaries;
}


std::optional<StrategicBodySummary> SimulationQueries::strategicBody(const BodyId id) const {
    const std::vector<StrategicBodySummary> summaries = strategicBodies();
    const auto it = std::find_if(summaries.begin(), summaries.end(), [id](const StrategicBodySummary& summary) {
        return summary.id == id;
    });
    return it == summaries.end() ? std::optional<StrategicBodySummary>{} : std::optional<StrategicBodySummary>{*it};
}

std::vector<StrategicFleetSummary> SimulationQueries::strategicFleets() const {
    const GameState& state = service_.state();
    std::vector<StrategicFleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const Body* currentBody = bodyById(state, fleet.currentBodyId);
        const Body* destinationBody = fleet.destinationBodyId.has_value()
            ? bodyById(state, *fleet.destinationBodyId)
            : nullptr;

        summaries.push_back(StrategicFleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .x = currentBody == nullptr ? 0.0 : currentBody->x,
            .y = currentBody == nullptr ? 0.0 : currentBody->y,
            .destinationBodyId = fleet.destinationBodyId,
            .destinationX = destinationBody == nullptr ? 0.0 : destinationBody->x,
            .destinationY = destinationBody == nullptr ? 0.0 : destinationBody->y,
            .moving = fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.destinationBodyId.has_value(),
            .daysRemaining = fleet.activeOrder.daysRemaining
        });
    }

    return summaries;
}

std::vector<EventLogEntrySummary> SimulationQueries::recentEvents(const std::size_t limit) const {
    const std::vector<SimEvent>& events = service_.state().eventLog;
    if (limit == 0 || events.empty()) {
        return {};
    }

    // Keep chronological order inside the visible tail so append-only UI widgets
    // can render the returned rows without reversing them first.
    const std::size_t start = events.size() > limit ? events.size() - limit : 0;

    std::vector<EventLogEntrySummary> summaries;
    summaries.reserve(events.size() - start);
    for (std::size_t index = start; index < events.size(); ++index) {
        summaries.push_back(summarizeEvent(events[index]));
    }

    return summaries;
}

} // namespace deep
