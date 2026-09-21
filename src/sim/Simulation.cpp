#include "sim/Simulation.h"
#include "sim/GameStateValidation.h"
#include "sim/TransitPlanning.h"

// Implements deterministic daily simulation rules and command validation.
// This file is intentionally self-contained within the sim layer; it should not
// include UI, database, threading, or platform-specific headers.
// State transitions run serially; callers must not observe or mutate state during
// a command/tick. Collection order is part of deterministic resource allocation.

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deep {
namespace {

// Finds a mutable object by typed ID. The returned pointer is non-owning and may
// be null when validation fails; callers must not store it across vector mutation.
template <typename T, typename IdT>
[[nodiscard]] T* findById(std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// Finds a read-only object by typed ID. This overload preserves const-correctness
// for validation paths that must inspect state without mutation.
template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// Creates deterministic display names from an entity prefix and ID. IDs are used
// instead of counters local to a function so save/load and tests remain stable.
[[nodiscard]] std::string idLabel(const std::string_view prefix, const std::int64_t value) {
    std::ostringstream out;
    out << prefix << ' ' << value;
    return out.str();
}


struct ProcessingRecipe {
    ProcessedMaterial output;
    MineralSet rawCostPerUnit;
};

[[nodiscard]] MineralSet makeRawCost(const std::initializer_list<std::pair<Mineral, double>> inputs) {
    MineralSet cost;
    for (const auto& [mineral, amount] : inputs) {
        cost.set(mineral, amount);
    }
    return cost;
}

[[nodiscard]] std::vector<ProcessingRecipe> processingRecipes() {
    // Fixed v1 processing chains. Each recipe produces one processed-material
    // unit per processor-capacity point and consumes the listed raw resources.
    // The order mirrors ProcessedMaterial and determines who consumes shared
    // inputs first. ForecastService currently duplicates these recipes and must
    // be updated with rule changes until the calculations share an implementation.
    return {
        ProcessingRecipe{ProcessedMaterial::StructuralAlloys,
                         makeRawCost({{Mineral::Iron, 1.0}, {Mineral::Nickel, 0.5}, {Mineral::Titanium, 0.25}})},
        ProcessingRecipe{ProcessedMaterial::Electronics,
                         makeRawCost({{Mineral::Copper, 0.5}, {Mineral::Silicon, 0.5}, {Mineral::RareEarthElements, 0.1}})},
        ProcessingRecipe{ProcessedMaterial::Propellant,
                         makeRawCost({{Mineral::WaterIce, 1.0}, {Mineral::Volatiles, 0.5}})},
        ProcessingRecipe{ProcessedMaterial::ReactorFuel,
                         makeRawCost({{Mineral::Uranium, 0.2}})},
        ProcessingRecipe{ProcessedMaterial::IndustrialComposites,
                         makeRawCost({{Mineral::CarbonCompounds, 0.5}, {Mineral::Aluminum, 0.5}})},
        ProcessingRecipe{ProcessedMaterial::OrdnanceMaterials,
                         makeRawCost({{Mineral::PlatinumGroupMetals, 0.1}, {Mineral::CarbonCompounds, 0.5}})}
    };
}

[[nodiscard]] double maxRecipeOutput(const MineralSet& stockpile, const MineralSet& rawCostPerUnit) noexcept {
    double maxOutput = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < rawCostPerUnit.amount.size(); ++i) {
        const double cost = rawCostPerUnit.amount[i];
        if (cost <= 0.0) {
            continue;
        }
        maxOutput = std::min(maxOutput, stockpile.amount[i] / cost);
    }
    return std::isinf(maxOutput) ? 0.0 : std::max(0.0, maxOutput);
}

[[nodiscard]] MineralSet scaledMineralCost(const MineralSet& cost, const double scale) noexcept {
    MineralSet result;
    if (scale <= 0.0) {
        return result;
    }
    for (std::size_t i = 0; i < result.amount.size(); ++i) {
        result.amount[i] = cost.amount[i] * scale;
    }
    return result;
}

using ProcessingShares = std::array<double, processedMaterialCount()>;

[[nodiscard]] bool isValidProcessedMaterial(const ProcessedMaterial material) noexcept {
    return processedMaterialIndex(material) < processedMaterialCount();
}

[[nodiscard]] bool isValidProcessingPolicy(const ProcessingPolicy policy) noexcept {
    switch (policy) {
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

[[nodiscard]] bool isValidAppointmentRole(const AppointmentRole role) noexcept {
    switch (role) {
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

[[nodiscard]] bool isValidAppointmentScopeType(const AppointmentScopeType scopeType) noexcept {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
    case AppointmentScopeType::Colony:
    case AppointmentScopeType::Institution:
        return true;
    }
    return false;
}

void addProcessingWeight(ProcessingShares& weights, const ProcessedMaterial material, const double weight) noexcept {
    if (weight <= 0.0 || !isValidProcessedMaterial(material)) {
        return;
    }

    weights[processedMaterialIndex(material)] += weight;
}

[[nodiscard]] ProcessingShares balancedProcessingWeights() noexcept {
    ProcessingShares weights{};
    for (std::size_t i = 0; i < weights.size(); ++i) {
        weights[i] = 1.0;
    }
    return weights;
}

[[nodiscard]] ProcessingShares policyProcessingWeights(const Colony& colony) noexcept {
    ProcessingShares weights{};

    switch (colony.processingPolicy) {
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
            // Lower stockpiles receive more capacity while every material keeps a
            // non-zero share. This is intentionally simple until automation has
            // explicit shortage targets and logistics context.
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

[[nodiscard]] ProcessingShares normalizedProcessingShares(const Colony& colony) noexcept {
    // FIXME: Finite individual manual weights can still overflow their sum (or
    // a repeated-material subtotal), producing zero/NaN shares. Bound or scale
    // weights before normalizing when tightening the processing input contract.
    ProcessingShares weights = policyProcessingWeights(colony);
    double totalWeight = 0.0;
    for (const double weight : weights) {
        totalWeight += weight;
    }

    if (totalWeight <= kProcessedMaterialComparisonEpsilon) {
        return {};
    }

    for (double& weight : weights) {
        weight /= totalWeight;
    }
    return weights;
}


[[nodiscard]] const Appointment* activeAppointmentFor(const GameState& state,
                                                      const AppointmentRole role,
                                                      const AppointmentScopeType scopeType,
                                                      const std::int64_t scopeId) noexcept {
    const auto it = std::find_if(state.appointments.begin(), state.appointments.end(), [role, scopeType, scopeId](const Appointment& appointment) {
        return appointment.role == role && appointment.scopeType == scopeType && appointment.scopeId == scopeId;
    });
    return it == state.appointments.end() ? nullptr : &(*it);
}

[[nodiscard]] const Person* appointedPersonFor(const GameState& state,
                                               const AppointmentRole role,
                                               const AppointmentScopeType scopeType,
                                               const std::int64_t scopeId) noexcept {
    const Appointment* appointment = activeAppointmentFor(state, role, scopeType, scopeId);
    return appointment == nullptr ? nullptr : findById(state.people, appointment->personId);
}

[[nodiscard]] double appointmentModifierFor(const GameState& state,
                                            const AppointmentRole role,
                                            const AppointmentScopeType scopeType,
                                            const std::int64_t scopeId) noexcept {
    const Person* person = appointedPersonFor(state, role, scopeType, scopeId);
    return person == nullptr ? 0.0 : appointmentOperationalModifier(*person, role);
}

[[nodiscard]] double effectiveShipyardCapacity(const GameState& state, const Colony& colony) noexcept {
    const double baseCapacity = std::max(0.0, colony.shipyardCapacity);
    const double modifier = appointmentModifierFor(state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value);
    return std::max(0.0, baseCapacity * (1.0 + modifier));
}

[[nodiscard]] double adjustedMoveFuelCost(const GameState& state,
                                          const Fleet& fleet,
                                          const BodyId originBodyId,
                                          const BodyId destinationBodyId,
                                          const std::int64_t departureDay) noexcept {
    const double baseCost = moveFuelCost(state, originBodyId, destinationBodyId, departureDay);
    if (!std::isfinite(baseCost)) {
        return baseCost;
    }

    // Positive FleetCommander modifiers improve fuel efficiency, so they reduce
    // the fuel consumed by starting a movement order. Negative modifiers increase
    // cost slightly within the shared appointment cap.
    const double modifier = appointmentModifierFor(state, AppointmentRole::FleetCommander, AppointmentScopeType::Fleet, fleet.id.value);
    return std::max(0.0, baseCost * (1.0 - modifier));
}

[[nodiscard]] double fleetCurrentFuel(const GameState& state, const Fleet& fleet) noexcept {
    double total = 0.0;
    for (const ShipId shipId : fleet.shipIds) {
        const Ship* ship = findById(state.ships, shipId);
        if (ship != nullptr) {
            total += ship->fuel;
        }
    }
    return total;
}

[[nodiscard]] double queuedRouteFuelRequirement(const GameState& state,
                                                const Fleet& fleet,
                                                const BodyId newDestinationBodyId) noexcept {
    // The active leg has already paid its fuel. Project only queued/new legs,
    // using today's commander modifier; each leg is checked again when started
    // because appointments or a cancellation can change the actual future route.
    BodyId projectedOrigin = fleet.currentBodyId;
    std::int64_t projectedDepartureDay = state.date.day;
    if (fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.activeOrder.targetBodyId.has_value()) {
        projectedOrigin = *fleet.activeOrder.targetBodyId;
        projectedDepartureDay = std::max(projectedDepartureDay, fleet.activeOrder.arrivalDay);
    }

    double requiredFuel = 0.0;
    const auto addProjectedMove = [&](const BodyId destinationBodyId) {
        const FleetOrder projectedPlan = planFleetTransit(state, projectedOrigin, destinationBodyId, projectedDepartureDay);
        const double cost = adjustedMoveFuelCost(state, fleet, projectedOrigin, destinationBodyId, projectedDepartureDay);
        requiredFuel += cost;
        projectedOrigin = destinationBodyId;
        projectedDepartureDay = projectedPlan.arrivalDay > projectedDepartureDay ? projectedPlan.arrivalDay : projectedDepartureDay + 1;
    };

    for (const QueuedFleetOrder& queuedOrder : fleet.queuedOrders) {
        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            return std::numeric_limits<double>::infinity();
        }
        addProjectedMove(*queuedOrder.targetBodyId);
    }

    addProjectedMove(newDestinationBodyId);
    return requiredFuel;
}

[[nodiscard]] bool fleetHasFuelFor(const GameState& state, const Fleet& fleet, const double fuelCost) noexcept {
    return fleetCurrentFuel(state, fleet) + kFuelComparisonEpsilon >= fuelCost;
}

bool consumeFleetFuel(GameState& state, const Fleet& fleet, const double fuelCost) noexcept {
    // Fuel is one fleet-wide pool, drawn from hulls in persisted roster order.
    // There is no per-hull travel requirement or proportional sharing in v1.
    if (fuelCost <= kFuelComparisonEpsilon) {
        return true;
    }

    if (!fleetHasFuelFor(state, fleet, fuelCost)) {
        return false;
    }

    double remainingCost = fuelCost;
    for (const ShipId shipId : fleet.shipIds) {
        Ship* ship = findById(state.ships, shipId);
        if (ship == nullptr || ship->fuel <= 0.0) {
            continue;
        }

        const double consumed = std::min(ship->fuel, remainingCost);
        ship->fuel -= consumed;
        remainingCost -= consumed;

        if (ship->fuel < kFuelComparisonEpsilon) {
            ship->fuel = 0.0;
        }
        if (remainingCost <= kFuelComparisonEpsilon) {
            return true;
        }
    }

    return remainingCost <= kFuelComparisonEpsilon;
}

[[nodiscard]] bool hasSurveyableDeposit(const GameState& state, const BodyId bodyId) noexcept {
    return std::any_of(state.mineralDeposits.begin(), state.mineralDeposits.end(), [bodyId](const MineralDeposit& deposit) {
        return deposit.bodyId == bodyId && !isDepositKnown(deposit);
    });
}

} // namespace

Simulation::Simulation(GameState initialState)
    : state_{std::move(initialState)} {
    // External snapshots are an explicit trust boundary. Validate before the
    // simulation can allocate IDs, append events, or advance time from them.
    validateGameState(state_);
}

const GameState& Simulation::state() const noexcept {
    return state_;
}

CommandResult Simulation::execute(const SimCommand& command) {
    return std::visit([this](const auto& concreteCommand) -> CommandResult {
        using Command = std::decay_t<decltype(concreteCommand)>;

        // Keep command dispatch explicit so adding a new SimCommand alternative
        // forces a visible validation branch in this central mutation boundary.
        if constexpr (std::is_same_v<Command, AdvanceDaysCommand>) {
            if (concreteCommand.days <= 0) {
                appendEvent(EventSeverity::Warning, CommandRejectedEvent{"AdvanceDaysCommand requires days > 0"});
                return CommandResult::failure("AdvanceDaysCommand requires days > 0");
            }
            (void)advanceDays(concreteCommand.days);
            return CommandResult::success("Advanced simulation time");
        } else if constexpr (std::is_same_v<Command, AssignShipyardBuildCommand>) {
            return assignShipyardBuild(concreteCommand);
        } else if constexpr (std::is_same_v<Command, MoveFleetCommand>) {
            return moveFleet(concreteCommand);
        } else if constexpr (std::is_same_v<Command, QueueFleetMoveOrderCommand>) {
            return queueFleetMoveOrder(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ClearFleetOrderQueueCommand>) {
            return clearFleetOrderQueue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CancelFleetOrderCommand>) {
            return cancelFleetOrder(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ResourceSurveyCommand>) {
            return resourceSurvey(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AssignAppointmentCommand>) {
            return assignAppointment(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SetColonyProcessingPolicyCommand>) {
            return setColonyProcessingPolicy(concreteCommand);
        }
    }, command);
}

std::vector<SimEvent> Simulation::advanceDays(const int days) {
    std::vector<SimEvent> emitted;

    if (days <= 0) {
        emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"advanceDays requires days > 0"});
        return emitted;
    }

    for (int i = 0; i < days; ++i) {
        simulateOneDay(emitted);
    }

    return emitted;
}

CommandResult Simulation::assignShipyardBuild(const AssignShipyardBuildCommand& command) {
    if (command.quantity <= 0) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Shipyard build quantity must be greater than zero"});
        return CommandResult::failure("Shipyard build quantity must be greater than zero");
    }

    const Colony* colony = findColony(command.colonyId);
    if (colony == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony does not exist"});
        return CommandResult::failure("Colony does not exist");
    }

    if (colony->shipyardCapacity <= 0.0) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony has no shipyard capacity"});
        return CommandResult::failure("Colony has no shipyard capacity");
    }

    if (findShipClass(command.shipClassId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Ship class does not exist"});
        return CommandResult::failure("Ship class does not exist");
    }

    ShipyardOrder order{
        .id = allocateShipyardOrderId(),
        .colonyId = command.colonyId,
        .shipClassId = command.shipClassId,
        .quantityRequested = command.quantity,
        .quantityCompleted = 0,
        .accumulatedBuildPoints = 0.0,
        .status = ShipyardOrderStatus::Active
    };

    // Capture the ID before moving/copying the order into GameState so the event
    // precisely identifies the persisted order record.
    const ShipyardOrderId orderId = order.id;
    state_.shipyardOrders.push_back(order);

    appendEvent(EventSeverity::Info, ShipyardOrderCreatedEvent{
        .orderId = orderId,
        .colonyId = command.colonyId,
        .shipClassId = command.shipClassId,
        .quantity = command.quantity
    });

    return CommandResult::success("Shipyard build order accepted");
}

CommandResult Simulation::moveFleet(const MoveFleetCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (findBody(command.destinationBodyId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Destination body does not exist"});
        return CommandResult::failure("Destination body does not exist");
    }

    if (fleet->currentBodyId == command.destinationBodyId) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet is already at destination body"});
        return CommandResult::failure("Fleet is already at destination body");
    }

    if (fleet->activeOrder.type != FleetOrderType::None) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet already has an active order"});
        return CommandResult::failure("Fleet already has an active order");
    }

    const double fuelCost = adjustedMoveFuelCost(state_, *fleet, fleet->currentBodyId, command.destinationBodyId, state_.date.day);
    if (!fleetHasFuelFor(state_, *fleet, fuelCost)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet has insufficient fuel for move"});
        return CommandResult::failure("Fleet has insufficient fuel for move");
    }

    const BodyId originBodyId = fleet->currentBodyId;
    FleetOrder plannedOrder = planFleetTransit(state_, originBodyId, command.destinationBodyId, state_.date.day);
    static_cast<void>(consumeFleetFuel(state_, *fleet, fuelCost));
    fleet->destinationBodyId = command.destinationBodyId;
    fleet->activeOrder = plannedOrder;

    appendEvent(EventSeverity::Info, FleetOrderAssignedEvent{
        .fleetId = fleet->id,
        .originBodyId = originBodyId,
        .destinationBodyId = command.destinationBodyId,
        .daysRemaining = plannedOrder.daysRemaining
    });

    return CommandResult::success("Fleet movement order accepted");
}

CommandResult Simulation::queueFleetMoveOrder(const QueueFleetMoveOrderCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (findBody(command.destinationBodyId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Destination body does not exist"});
        return CommandResult::failure("Destination body does not exist");
    }

    if (fleet->activeOrder.type == FleetOrderType::None && fleet->currentBodyId == command.destinationBodyId) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet is already at destination body"});
        return CommandResult::failure("Fleet is already at destination body");
    }

    const double projectedFuelRequired = queuedRouteFuelRequirement(state_, *fleet, command.destinationBodyId);
    if (!fleetHasFuelFor(state_, *fleet, projectedFuelRequired)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet has insufficient fuel for queued move"});
        return CommandResult::failure("Fleet has insufficient fuel for queued move");
    }

    fleet->queuedOrders.push_back(QueuedFleetOrder{
        .type = FleetOrderType::MoveToBody,
        .targetBodyId = command.destinationBodyId
    });

    // Idle fleets should not force the player to advance time before the first
    // queued order becomes the current order. Active fleets keep the new order in
    // the queue until movement completion starts it from the new location.
    if (fleet->activeOrder.type == FleetOrderType::None) {
        static_cast<void>(startNextQueuedFleetOrder(*fleet, nullptr));
        return CommandResult::success("Queued fleet move order started");
    }

    return CommandResult::success("Fleet move order queued");
}

CommandResult Simulation::clearFleetOrderQueue(const ClearFleetOrderQueueCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    fleet->queuedOrders.clear();
    return CommandResult::success("Fleet order queue cleared");
}

CommandResult Simulation::cancelFleetOrder(const CancelFleetOrderCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (fleet->activeOrder.type == FleetOrderType::None) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet has no active order"});
        return CommandResult::failure("Fleet has no active order");
    }

    // Map positions interpolate the route, but the logical currentBodyId stays
    // at departure until arrival. Cancellation clears the plan, returning the
    // display to that body; fuel paid at departure is not refunded.
    // FIXME: Remaining queued legs stay dormant because the daily movement pass
    // skips idle fleets. A later queue command can restart this retained route;
    // cancellation needs an explicit resume/clear policy before that is changed.
    fleet->destinationBodyId = std::nullopt;
    fleet->activeOrder = FleetOrder{};

    return CommandResult::success("Fleet order cancelled");
}

CommandResult Simulation::resourceSurvey(const ResourceSurveyCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (findBody(command.bodyId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey target body does not exist"});
        return CommandResult::failure("Survey target body does not exist");
    }

    if (fleet->activeOrder.type != FleetOrderType::None || fleet->destinationBodyId.has_value()) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet must be stationary to survey"});
        return CommandResult::failure("Fleet must be stationary to survey");
    }

    if (fleet->currentBodyId != command.bodyId) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet must be at survey target body"});
        return CommandResult::failure("Fleet must be at survey target body");
    }

    if (!hasSurveyableDeposit(state_, command.bodyId)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey target has no low-confidence deposits"});
        return CommandResult::failure("Survey target has no low-confidence deposits");
    }

    int improvedDeposits = 0;
    double confidenceBeforeTotal = 0.0;
    double confidenceAfterTotal = 0.0;
    for (MineralDeposit& deposit : state_.mineralDeposits) {
        if (deposit.bodyId != command.bodyId || isDepositKnown(deposit)) {
            continue;
        }

        const double before = deposit.confidence;
        const double after = surveyedDepositConfidence(deposit);
        if (after <= before) {
            continue;
        }

        deposit.confidence = after;
        confidenceBeforeTotal += before;
        confidenceAfterTotal += after;
        ++improvedDeposits;
    }

    appendEvent(EventSeverity::Info, ResourceSurveyCompletedEvent{
        .fleetId = fleet->id,
        .bodyId = command.bodyId,
        .depositsImproved = improvedDeposits,
        .averageConfidenceBefore = confidenceBeforeTotal / static_cast<double>(improvedDeposits),
        .averageConfidenceAfter = confidenceAfterTotal / static_cast<double>(improvedDeposits)
    });

    return CommandResult::success("Resource survey completed");
}

CommandResult Simulation::assignAppointment(const AssignAppointmentCommand& command) {
    if (!isValidAppointmentRole(command.role)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment role is invalid"});
        return CommandResult::failure("Appointment role is invalid");
    }

    if (!isValidAppointmentScopeType(command.scopeType)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment scope type is invalid"});
        return CommandResult::failure("Appointment scope type is invalid");
    }

    if (command.scopeId <= 0) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment scope ID must be positive"});
        return CommandResult::failure("Appointment scope ID must be positive");
    }

    const Person* person = findPerson(command.personId);
    if (person == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment person does not exist"});
        return CommandResult::failure("Appointment person does not exist");
    }

    if (findInstitution(person->institutionId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment person institution does not exist"});
        return CommandResult::failure("Appointment person institution does not exist");
    }

    const bool scopeExists = [this, &command] {
        switch (command.scopeType) {
        case AppointmentScopeType::Fleet:
            return findFleet(FleetId{command.scopeId}) != nullptr;
        case AppointmentScopeType::Colony:
            return findColony(ColonyId{command.scopeId}) != nullptr;
        case AppointmentScopeType::Institution:
            return findInstitution(InstitutionId{command.scopeId}) != nullptr;
        }
        return false;
    }();

    if (!scopeExists) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment target scope does not exist"});
        return CommandResult::failure("Appointment target scope does not exist");
    }

    const auto sameSlot = [&command](const Appointment& appointment) {
        return appointment.role == command.role &&
               appointment.scopeType == command.scopeType &&
               appointment.scopeId == command.scopeId;
    };

    const auto it = std::find_if(state_.appointments.begin(), state_.appointments.end(), sameSlot);
    if (it != state_.appointments.end()) {
        // Appointments represent current slots, not history yet. Reassigning a
        // slot updates the responsible person while preserving the uniqueness
        // invariant that validation enforces for imported/saved states.
        it->personId = command.personId;
        it->appointedDay = state_.date.day;
    } else {
        state_.appointments.push_back(Appointment{
            .role = command.role,
            .scopeType = command.scopeType,
            .scopeId = command.scopeId,
            .personId = command.personId,
            .appointedDay = state_.date.day
        });
    }

    return CommandResult::success("Appointment assigned");
}

CommandResult Simulation::setColonyProcessingPolicy(const SetColonyProcessingPolicyCommand& command) {
    Colony* colony = findColony(command.colonyId);
    if (colony == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony does not exist"});
        return CommandResult::failure("Colony does not exist");
    }

    if (!isValidProcessingPolicy(command.policy)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Processing policy is invalid"});
        return CommandResult::failure("Processing policy is invalid");
    }

    double manualWeightTotal = 0.0;
    for (const ProcessingAllocation& allocation : command.manualAllocations) {
        if (!isValidProcessedMaterial(allocation.material)) {
            appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Manual processing allocation has invalid material"});
            return CommandResult::failure("Manual processing allocation has invalid material");
        }

        if (!std::isfinite(allocation.weight)) {
            appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Manual processing allocation weight must be finite"});
            return CommandResult::failure("Manual processing allocation weight must be finite");
        }

        if (allocation.weight < 0.0) {
            appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Manual processing allocation weight cannot be negative"});
            return CommandResult::failure("Manual processing allocation weight cannot be negative");
        }

        manualWeightTotal += allocation.weight;
    }

    if (command.policy == ProcessingPolicy::Manual && manualWeightTotal <= kProcessedMaterialComparisonEpsilon) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Manual processing policy requires positive allocation weight"});
        return CommandResult::failure("Manual processing policy requires positive allocation weight");
    }

    colony->processingPolicy = command.policy;
    if (command.policy == ProcessingPolicy::Manual) {
        // Manual weights are persistent player intent. Preset policies derive
        // their own weights every day, so do not overwrite the last manual
        // setup when the player temporarily switches to a preset.
        colony->manualProcessingAllocations = command.manualAllocations;
    }

    return CommandResult::success("Colony processing policy updated");
}

bool Simulation::startNextQueuedFleetOrder(Fleet& fleet, std::vector<SimEvent>* emitted) {
    while (!fleet.queuedOrders.empty()) {
        const QueuedFleetOrder queuedOrder = fleet.queuedOrders.front();
        fleet.queuedOrders.erase(fleet.queuedOrders.begin());

        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            // Defensive repair for imported/future states. Validation rejects
            // malformed queues, but this keeps runtime robust if a queue is
            // mutated before validation has a chance to run.
            const CommandRejectedEvent rejected{"Queued fleet order was invalid"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const BodyId destinationBodyId = *queuedOrder.targetBodyId;
        if (findBody(destinationBodyId) == nullptr) {
            const CommandRejectedEvent rejected{"Queued fleet order referenced missing destination body"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        if (fleet.currentBodyId == destinationBodyId) {
            const CommandRejectedEvent rejected{"Queued fleet order already at destination body"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const double fuelCost = adjustedMoveFuelCost(state_, fleet, fleet.currentBodyId, destinationBodyId, state_.date.day);
        if (!consumeFleetFuel(state_, fleet, fuelCost)) {
            const CommandRejectedEvent rejected{"Queued fleet order lacked sufficient fuel"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const BodyId originBodyId = fleet.currentBodyId;
        FleetOrder plannedOrder = planFleetTransit(state_, originBodyId, destinationBodyId, state_.date.day);
        fleet.destinationBodyId = destinationBodyId;
        fleet.activeOrder = plannedOrder;

        FleetOrderAssignedEvent assigned{
            .fleetId = fleet.id,
            .originBodyId = originBodyId,
            .destinationBodyId = destinationBodyId,
            .daysRemaining = plannedOrder.daysRemaining
        };
        if (emitted == nullptr) {
            appendEvent(EventSeverity::Info, assigned);
        } else {
            emitEvent(*emitted, EventSeverity::Info, assigned);
        }
        return true;
    }

    return false;
}

void Simulation::simulateOneDay(std::vector<SimEvent>& emitted) {
    ++state_.date.day;

    // Ordering is gameplay: today's mining feeds today's processing, and its
    // output can pay for today's ship completions. New fleets remain idle until
    // a later command, while existing arrivals can start their next queued leg.
    // All telemetry/events below carry the newly advanced simulation day.
    simulateMining(emitted);
    simulateProcessing();
    simulateShipyards(emitted);
    simulateFleetMovement(emitted);
}

void Simulation::simulateMining(std::vector<SimEvent>&) {
    for (Colony& colony : state_.colonies) {
        for (MineralDeposit& deposit : state_.mineralDeposits) {
            if (deposit.bodyId != colony.bodyId || deposit.remaining <= 0.0) {
                continue;
            }

            // Each mine contributes one base unit per day to every local deposit,
            // scaled by accessibility and capped by remaining material. Confidence
            // affects displayed knowledge only; mining does not require a survey.
            const double potentialExtraction = colony.mines * deposit.accessibility;
            const double extracted = std::min(deposit.remaining, std::max(0.0, potentialExtraction));
            if (extracted <= 0.0) {
                continue;
            }

            deposit.remaining -= extracted;
            colony.stockpile.add(deposit.mineral, extracted);

            // Mining is routine runtime telemetry, not persisted audit history.
            // Store one append-only row per extracted mineral for current-session
            // UI/forecast/debug views without flooding the player-facing event log.
            state_.dailyEconomySnapshots.push_back(DailyEconomySnapshot{
                .day = state_.date.day,
                .colonyId = colony.id,
                .bodyId = colony.bodyId,
                .mineral = deposit.mineral,
                .amount = extracted,
                .remainingDeposit = deposit.remaining
            });
        }
    }
}

void Simulation::simulateProcessing() {
    const std::vector<ProcessingRecipe> recipes = processingRecipes();

    for (Colony& colony : state_.colonies) {
        const double dailyCapacity = std::max(0.0, colony.processorCapacity);
        if (dailyCapacity <= 0.0) {
            continue;
        }

        const ProcessingShares shares = normalizedProcessingShares(colony);
        for (const ProcessingRecipe& recipe : recipes) {
            const double targetOutput = dailyCapacity * shares[processedMaterialIndex(recipe.output)];
            if (targetOutput <= kProcessedMaterialComparisonEpsilon) {
                continue;
            }

            // Capacity shares are fixed for this day. Missing inputs reduce this
            // recipe's output; unused capacity is not redistributed to others.
            // Only the colony's local raw stockpile is available. Research,
            // building upgrades, and inter-body supply are outside this model.
            const double producible = std::min(targetOutput, maxRecipeOutput(colony.stockpile, recipe.rawCostPerUnit));
            if (producible <= kMineralComparisonEpsilon) {
                continue;
            }

            colony.stockpile.subtract(scaledMineralCost(recipe.rawCostPerUnit, producible));
            colony.processedStockpile.add(recipe.output, producible);
        }
    }
}

void Simulation::simulateShipyards(std::vector<SimEvent>& emitted) {
    constexpr double kBuildPointEpsilon = 1.0e-9;

    struct ShipyardCapacityPool {
        ColonyId colonyId;
        double remainingBuildPoints = 0.0;
    };

    std::vector<ShipyardCapacityPool> capacityPools;
    capacityPools.reserve(state_.colonies.size());
    for (const Colony& colony : state_.colonies) {
        // Each colony has exactly one daily shipyard pool. Orders draw down this
        // pool in persistent order-vector order, which gives deterministic FIFO
        // behavior without adding a separate production-queue type yet.
        capacityPools.push_back(ShipyardCapacityPool{
            .colonyId = colony.id,
            .remainingBuildPoints = effectiveShipyardCapacity(state_, colony)
        });
    }

    for (ShipyardOrder& order : state_.shipyardOrders) {
        if (order.status != ShipyardOrderStatus::Active) {
            continue;
        }

        Colony* colony = findColony(order.colonyId);
        const ShipClass* shipClass = findShipClass(order.shipClassId);
        if (colony == nullptr || shipClass == nullptr) {
            // Valid GameState snapshots cannot contain dangling production
            // references. If defensive runtime code still encounters one, skip
            // the order without inventing a persistent zombie status.
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order references missing colony or ship class"});
            continue;
        }

        const auto poolIt = std::find_if(capacityPools.begin(), capacityPools.end(), [colony](const ShipyardCapacityPool& pool) {
            return pool.colonyId == colony->id;
        });
        if (poolIt == capacityPools.end()) {
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order references missing colony capacity pool"});
            continue;
        }

        // Complete as many ships as the order's existing progress plus this
        // colony's remaining daily capacity allows. When this order still needs
        // build points, it consumes the colony pool before later orders at the
        // same colony can receive any capacity.
        while (order.quantityCompleted < order.quantityRequested) {
            if (order.accumulatedBuildPoints + kBuildPointEpsilon < shipClass->buildPoints) {
                if (poolIt->remainingBuildPoints <= kBuildPointEpsilon) {
                    break;
                }

                const double buildPointsNeeded = shipClass->buildPoints - order.accumulatedBuildPoints;
                const double allocatedBuildPoints = std::min(poolIt->remainingBuildPoints, buildPointsNeeded);
                order.accumulatedBuildPoints += allocatedBuildPoints;
                poolIt->remainingBuildPoints -= allocatedBuildPoints;

                if (order.accumulatedBuildPoints + kBuildPointEpsilon < shipClass->buildPoints) {
                    break;
                }
            }

            if (!colony->processedStockpile.canPay(shipClass->buildCost)) {
                // Processed-material shortages are temporary production pauses.
                // Keep the order Active so future processing can satisfy the
                // cost and complete the ship automatically. The blocked FIFO
                // order also holds the queue for this colony today;
                // later orders should not leapfrog a material-starved order.
                poolIt->remainingBuildPoints = 0.0;
                emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order waiting for sufficient processed materials"});
                break;
            }

            colony->processedStockpile.subtract(shipClass->buildCost);
            order.accumulatedBuildPoints -= shipClass->buildPoints;
            ++order.quantityCompleted;

            const FleetId fleetId = allocateFleetId();
            const ShipId shipId = allocateShipId();

            Fleet fleet{
                .id = fleetId,
                .name = idLabel("Survey Cutter Fleet", fleetId.value),
                .currentBodyId = colony->bodyId,
                .destinationBodyId = std::nullopt,
                .shipIds = {shipId},
                .activeOrder = FleetOrder{},
                .queuedOrders = {},
                .ownerInstitutionId = colony->ownerInstitutionId
            };

            Ship ship{
                .id = shipId,
                .shipClassId = shipClass->id,
                .name = idLabel(shipClass->name, shipId.value),
                .fleetId = fleetId,
                .fuel = shipClass->fuelCapacity
            };

            state_.fleets.push_back(std::move(fleet));
            state_.ships.push_back(std::move(ship));

            emitEvent(emitted, EventSeverity::Info, ShipCompletedEvent{
                .orderId = order.id,
                .colonyId = colony->id,
                .shipId = shipId,
                .fleetId = fleetId,
                .shipClassId = shipClass->id
            });
        }

        if (order.quantityCompleted >= order.quantityRequested) {
            order.status = ShipyardOrderStatus::Completed;
            order.accumulatedBuildPoints = 0.0;
        }
    }
}

void Simulation::simulateFleetMovement(std::vector<SimEvent>& emitted) {
    for (Fleet& fleet : state_.fleets) {
        if (fleet.activeOrder.type != FleetOrderType::MoveToBody) {
            continue;
        }

        if (!fleet.activeOrder.targetBodyId.has_value()) {
            // Defensive repair for corrupted or future-loaded state: an active
            // movement order without a target cannot be completed safely.
            fleet.activeOrder = FleetOrder{};
            fleet.destinationBodyId = std::nullopt;
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Fleet move order had no target body"});
            continue;
        }

        if (fleet.activeOrder.arrivalDay > state_.date.day) {
            fleet.activeOrder.daysRemaining = static_cast<int>(fleet.activeOrder.arrivalDay - state_.date.day);
            continue;
        }

        const BodyId destination = *fleet.activeOrder.targetBodyId;
        fleet.currentBodyId = destination;
        fleet.destinationBodyId = std::nullopt;
        fleet.activeOrder = FleetOrder{};

        emitEvent(emitted, EventSeverity::Info, FleetArrivedEvent{
            .fleetId = fleet.id,
            .destinationBodyId = destination
        });

        // Starting the next queued order on the same tick keeps the queue
        // actionable: a completed current order immediately promotes the
        // next queued move and records the promotion in the event log.
        static_cast<void>(startNextQueuedFleetOrder(fleet, &emitted));
    }
}

SimEvent Simulation::makeEvent(const EventSeverity severity, SimEventPayload payload) {
    return SimEvent{
        .id = allocateEventId(),
        .day = state_.date.day,
        .severity = severity,
        .payload = std::move(payload)
    };
}

void Simulation::emitEvent(std::vector<SimEvent>& emitted, const EventSeverity severity, SimEventPayload payload) {
    SimEvent event = makeEvent(severity, std::move(payload));

    // Store a copy in the authoritative log and return the emitted event by value
    // to the caller. This preserves the emitted audit history without exposing
    // mutable references; the log is not a replay stream of every state mutation.
    state_.eventLog.push_back(event);
    emitted.push_back(std::move(event));
}

void Simulation::appendEvent(const EventSeverity severity, SimEventPayload payload) {
    SimEvent event = makeEvent(severity, std::move(payload));
    state_.eventLog.push_back(std::move(event));
}

Colony* Simulation::findColony(const ColonyId id) noexcept {
    return findById(state_.colonies, id);
}

const Colony* Simulation::findColony(const ColonyId id) const noexcept {
    return findById(state_.colonies, id);
}

Body* Simulation::findBody(const BodyId id) noexcept {
    return findById(state_.bodies, id);
}

const Body* Simulation::findBody(const BodyId id) const noexcept {
    return findById(state_.bodies, id);
}

ShipClass* Simulation::findShipClass(const ShipClassId id) noexcept {
    return findById(state_.shipClasses, id);
}

const ShipClass* Simulation::findShipClass(const ShipClassId id) const noexcept {
    return findById(state_.shipClasses, id);
}

Fleet* Simulation::findFleet(const FleetId id) noexcept {
    return findById(state_.fleets, id);
}

const Fleet* Simulation::findFleet(const FleetId id) const noexcept {
    return findById(state_.fleets, id);
}

const Institution* Simulation::findInstitution(const InstitutionId id) const noexcept {
    return findById(state_.institutions, id);
}

const Person* Simulation::findPerson(const PersonId id) const noexcept {
    return findById(state_.people, id);
}

ShipyardOrderId Simulation::allocateShipyardOrderId() noexcept {
    return ShipyardOrderId{state_.ids.nextShipyardOrderId++};
}

ShipId Simulation::allocateShipId() noexcept {
    return ShipId{state_.ids.nextShipId++};
}

FleetId Simulation::allocateFleetId() noexcept {
    return FleetId{state_.ids.nextFleetId++};
}

EventId Simulation::allocateEventId() noexcept {
    return EventId{state_.ids.nextEventId++};
}

} // namespace deep
