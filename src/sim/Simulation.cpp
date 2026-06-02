#include "sim/Simulation.h"
#include "sim/GameStateValidation.h"

// Implements deterministic daily simulation rules and command validation.
// This file is intentionally self-contained within the sim layer; it should not
// include UI, database, threading, or platform-specific headers.

#include <algorithm>
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

void Simulation::simulateOneDay(std::vector<SimEvent>& emitted) {
    ++state_.date.day;

    // Daily tick order is stable by design: economy telemetry is recorded before
    // shipyard completion, and completed fleets can begin moving only on a later
    // command. This keeps tests and future save replays deterministic.
    simulateMining(emitted);
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

            // Mining is routine telemetry, not audit history. Store one
            // append-only row per extracted mineral so future UI panels can graph
            // daily flow without flooding the player-facing event log.
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

void Simulation::simulateShipyards(std::vector<SimEvent>& emitted) {
    constexpr double kBuildPointEpsilon = 1.0e-9;

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

        order.accumulatedBuildPoints += colony->shipyardCapacity;

        // Complete as many ships as accumulated capacity allows. Multi-ship
        // orders are important even in the prototype because they exercise ID
        // allocation, stockpile spending, and event ordering repeatedly.
        while (order.quantityCompleted < order.quantityRequested &&
               order.accumulatedBuildPoints + kBuildPointEpsilon >= shipClass->buildPoints) {
            if (!colony->stockpile.canPay(shipClass->buildCost)) {
                // Mineral shortages are temporary production pauses, not a
                // terminal order state. Keep the order Active so future mining
                // can satisfy the cost and complete the ship automatically.
                emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order waiting for sufficient minerals"});
                break;
            }

            colony->stockpile.subtract(shipClass->buildCost);
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
