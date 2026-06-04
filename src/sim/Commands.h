#pragma once

// Defines the only supported mutation requests for the headless simulation.
// UI, CLI, tests, and future automation must use commands rather than editing
// GameState records directly.

#include "sim/Domain.h"
#include "sim/IdTypes.h"

#include <cstdint>
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

// Requests an immediate resource survey at the fleet's current body. V1 has no
// survey duration or module requirement, so accepted commands synchronously
// improve deposit confidence and emit an audit event.
struct ResourceSurveyCommand {
    FleetId fleetId;
    BodyId bodyId;
};


// Assigns or replaces the current person responsible for one appointment slot.
// The target scope is identified by its scope type and raw typed-ID value so one
// command can cover fleets, colonies, and institutions without a variant payload.
struct AssignAppointmentCommand {
    AppointmentRole role = AppointmentRole::InstitutionHead;
    AppointmentScopeType scopeType = AppointmentScopeType::Institution;
    std::int64_t scopeId = 0;
    PersonId personId;
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
    ResourceSurveyCommand,
    AssignAppointmentCommand,
    SetColonyProcessingPolicyCommand
>;

} // namespace deep
