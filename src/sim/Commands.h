#pragma once

// Defines the only supported mutation requests for the headless simulation.
// UI, CLI, tests, and future automation must use commands rather than editing
// GameState records directly.

#include "sim/Domain.h"
#include "sim/IdTypes.h"

#include <variant>
#include <vector>

namespace deep {

// Requests deterministic time advancement. days must be greater than zero.
struct AdvanceDaysCommand {
    int days = 1;
};

// Requests a new shipyard production order. The colony and ship class IDs must
// refer to existing records, and quantity must be greater than zero.
struct AssignShipyardBuildCommand {
    ColonyId colonyId;
    ShipClassId shipClassId;
    int quantity = 1;
};

// Requests fixed-duration fleet movement to another body. The fleet must be idle
// and the destination must differ from the fleet's current body.
struct MoveFleetCommand {
    FleetId fleetId;
    BodyId destinationBodyId;
};

// Requests a move order be appended to a fleet's visible order queue. If the
// fleet is idle, the simulation immediately starts the queued order.
struct QueueFleetMoveOrderCommand {
    FleetId fleetId;
    BodyId destinationBodyId;
};

// Requests removal of all queued orders for a fleet without touching the current
// active order. This lets UI cancel future intent separately from current motion.
struct ClearFleetOrderQueueCommand {
    FleetId fleetId;
};

// Requests cancellation of the fleet's current active order. The prototype keeps
// movement all-or-nothing, so cancellation leaves the fleet at its current body.
struct CancelFleetOrderCommand {
    FleetId fleetId;
};

// Requests a processing policy change for one colony. Manual allocations are
// relative weights; the simulation normalizes them during the daily processing
// pass rather than storing percentages that can drift due to rounding.
struct SetColonyProcessingPolicyCommand {
    ColonyId colonyId;
    ProcessingPolicy policy = ProcessingPolicy::Balanced;
    std::vector<ProcessingAllocation> manualAllocations;
};

// Command envelope used by Simulation::execute. Each variant alternative must
// have a validation branch in Simulation.cpp.
using SimCommand = std::variant<
    AdvanceDaysCommand,
    AssignShipyardBuildCommand,
    MoveFleetCommand,
    QueueFleetMoveOrderCommand,
    ClearFleetOrderQueueCommand,
    CancelFleetOrderCommand,
    SetColonyProcessingPolicyCommand
>;

} // namespace deep
