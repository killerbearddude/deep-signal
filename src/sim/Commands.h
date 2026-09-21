#pragma once

// Defines the only supported mutation requests for the headless simulation.
// UI, CLI, tests, and future automation submit commands to a running Simulation;
// only scenario construction and loading assemble detached GameState records.

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
// refer to existing records, the colony must have shipyard capacity, and quantity
// must be positive. Materials are paid per completed hull, not reserved here.
struct AssignShipyardBuildCommand {
    ColonyId colonyId;
    ShipClassId shipClassId;
    int quantity = 1;
};

// Requests a sustained-burn transit to another body. The fleet must be idle,
// the destination must differ from its current body, and pooled ship fuel must
// cover the route. Acceptance plans the arrival and pays the fuel immediately;
// existing queued legs are preserved.
struct MoveFleetCommand {
    FleetId fleetId;
    BodyId destinationBodyId;
};

// Requests a move order be appended to a fleet's visible order queue. If the
// fleet is idle, the simulation immediately starts the queued order.
// Acceptance checks fuel for the projected remaining route without reserving it;
// each leg is replanned and paid when it starts.
struct QueueFleetMoveOrderCommand {
    FleetId fleetId;
    BodyId destinationBodyId;
};

// Requests removal of all queued orders for a fleet without touching the current
// active order. This lets UI cancel future intent separately from current motion.
struct ClearFleetOrderQueueCommand {
    FleetId fleetId;
};

// Requests cancellation of the fleet's current active order. Logical location
// remains at the departure body until arrival, so cancellation returns the map
// display there without refunding fuel. Queued legs remain stored but do not
// restart simply by advancing days.
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
// Slot identity is (role, scopeType, scopeId); one person may hold multiple slots.
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
