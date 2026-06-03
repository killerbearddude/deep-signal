#include "sim/Simulation.h"
#include "sim/GameStateValidation.h"

// Implements deterministic daily simulation rules and command validation.
// This file is intentionally self-contained within the sim layer; it should not
// include UI, database, threading, or platform-specific headers.

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
    // The order is deterministic and intentionally mirrors ProcessedMaterial
    // order so forecasts and simulation agree without a separate queue type.
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
        } else if constexpr (std::is_same_v<Command, CancelFleetOrderCommand>) {
            return cancelFleetOrder(concreteCommand);
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

    const BodyId originBodyId = fleet->currentBodyId;
    fleet->destinationBodyId = command.destinationBodyId;
    fleet->activeOrder = FleetOrder{
        .type = FleetOrderType::MoveToBody,
        .targetBodyId = command.destinationBodyId,
        .daysRemaining = kPrototypeMoveDurationDays
    };

    appendEvent(EventSeverity::Info, FleetOrderAssignedEvent{
        .fleetId = fleet->id,
        .originBodyId = originBodyId,
        .destinationBodyId = command.destinationBodyId,
        .daysRemaining = kPrototypeMoveDurationDays
    });

    return CommandResult::success("Fleet movement order accepted");
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

    // Prototype movement is not interpolated yet, so cancelling simply drops the
    // pending target and leaves the fleet at its current body/origin. Future
    // route planning can replace this with partial-progress handling.
    fleet->destinationBodyId = std::nullopt;
    fleet->activeOrder = FleetOrder{};

    return CommandResult::success("Fleet order cancelled");
}

CommandResult Simulation::setColonyProcessingPolicy(const SetColonyProcessingPolicyCommand& command) {
    Colony* colony = findColony(command.colonyId);
    if (colony == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony does not exist"});
        return CommandResult::failure("Colony does not exist");
    }

    double manualWeightTotal = 0.0;
    for (const ProcessingAllocation& allocation : command.manualAllocations) {
        if (!isValidProcessedMaterial(allocation.material)) {
            appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Manual processing allocation has invalid material"});
            return CommandResult::failure("Manual processing allocation has invalid material");
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
    colony->manualProcessingAllocations = command.manualAllocations;

    return CommandResult::success("Colony processing policy updated");
}

void Simulation::simulateOneDay(std::vector<SimEvent>& emitted) {
    ++state_.date.day;

    // Daily tick order is stable by design: economy telemetry is recorded before
    // shipyard completion, and completed fleets can begin moving only on a later
    // command. This keeps tests and future save replays deterministic.
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

            // Prototype formula: each mine contributes one base unit per day,
            // scaled by accessibility, and extraction cannot exceed the deposit.
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

            // TODO: Research will eventually modify recipe efficiency and unlock
            // advanced processed materials.
            // TODO: Colony buildings will eventually increase processor capacity.
            // TODO: Inter-body logistics will eventually determine whether remote
            // raw resources are available to this colony's processing chain.
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
            .remainingBuildPoints = std::max(0.0, colony.shipyardCapacity)
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
                // Mineral shortages are temporary production pauses, not a
                // terminal order state. Keep the order Active so future mining
                // can satisfy the cost and complete the ship automatically. The
                // blocked FIFO order also holds the queue for this colony today;
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
                .activeOrder = FleetOrder{}
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

        --fleet.activeOrder.daysRemaining;
        if (fleet.activeOrder.daysRemaining <= 0) {
            const BodyId destination = *fleet.activeOrder.targetBodyId;
            fleet.currentBodyId = destination;
            fleet.destinationBodyId = std::nullopt;
            fleet.activeOrder = FleetOrder{};

            emitEvent(emitted, EventSeverity::Info, FleetArrivedEvent{
                .fleetId = fleet.id,
                .destinationBodyId = destination
            });
        }
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
    // to the caller. This preserves a complete state history without exposing
    // mutable references to GameState internals.
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
