#pragma once

// Defines the only supported mutation requests for the headless simulation.
// UI, CLI, tests, and future automation submit commands to a running Simulation;
// only scenario construction and loading assemble detached GameState records.

#include "sim/Domain.h"
#include "sim/IdTypes.h"
#include "sim/SurveyProgram.h"
#include "sim/FreightProgram.h"
#include "sim/MaintenanceProgram.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace deep {

// Requests deterministic time advancement. days must be greater than zero.
struct AdvanceDaysCommand {
    int days = 1;
};

// Requests a new shipyard production order. The colony and ship class IDs must
// refer to existing records, and quantity must be positive. Zero capacity leaves
// the accepted order waiting. Materials are paid per hull, not reserved here.
struct AssignShipyardBuildCommand {
    ColonyId colonyId;
    ShipClassId shipClassId;
    int quantity = 1;
};

// Commits a complete draft as a new immutable class. Invalid composition is
// rejected, while volume overflow is retained as a non-constructible design.
struct CreateShipClassRevisionCommand {
    std::string name;
    ShipRole role = ShipRole::Survey;
    std::optional<ShipClassId> basedOnClassId;
    std::vector<ShipComponentInstall> components;
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

// Requests an immediate resource survey at the fleet's current body. Powered
// instruments must support five usable duty units for this complete action.
// Accepted commands debit contributing rows once, then publish the result.
struct ResourceSurveyCommand {
    FleetId fleetId;
    BodyId bodyId;
};

// Authorizes durable delegated intent. Current lack of fleet/team/leader/fuel
// is a waiting condition; supplied references and charter policy must be valid.
struct CreateSurveyProgramCommand {
    SurveyProgramCharter charter;
};

// Replaces the requested charter with a dated revision. Current physical work
// keeps its stored task/route until the next safe planning boundary.
struct AmendSurveyProgramCommand {
    SurveyProgramId programId;
    SurveyProgramCharter charter;
};

struct SuspendSurveyProgramCommand { SurveyProgramId programId; };
struct ResumeSurveyProgramCommand { SurveyProgramId programId; };
struct CancelSurveyProgramCommand { SurveyProgramId programId; };

// Acknowledges one stable issue identity without relaxing its fuel or assignment
// constraint, permitting later time advancement under the same limitation.
struct AcknowledgeSurveyProgramIssueCommand {
    SurveyProgramId programId;
    std::string signature;
};

// Freight authoring records intent without reserving goods or acquiring assets.
// An amendment exposes only mutable fields: route and commodity are fixed.
struct CreateFreightProgramCommand { FreightProgramCharter charter; };
struct AmendFreightProgramCommand { FreightProgramId programId; FreightProgramAmendment amendment; };
struct SuspendFreightProgramCommand { FreightProgramId programId; };
struct ResumeFreightProgramCommand { FreightProgramId programId; };
// Cancels future pickups while authorizing only the declared cargo settlement.
struct CancelFreightProgramCommand { FreightProgramId programId; };
struct AcknowledgeFreightProgramIssueCommand { FreightProgramId programId; std::string signature; };

// Standing colony-supported service intent. These commands never perform a
// repair; stops withdraw claims while preserving already completed daily work.
struct CreateMaintenanceProgramCommand { MaintenanceProgramCharter charter; };
struct AmendMaintenanceProgramCommand { MaintenanceProgramId programId; MaintenanceProgramAmendment amendment; };
struct SuspendMaintenanceProgramCommand { MaintenanceProgramId programId; };
struct ResumeMaintenanceProgramCommand { MaintenanceProgramId programId; };
struct CancelMaintenanceProgramCommand { MaintenanceProgramId programId; };
struct AcknowledgeMaintenanceIssueCommand { MaintenanceProgramId programId; std::string signature; };


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
    CreateShipClassRevisionCommand,
    MoveFleetCommand,
    QueueFleetMoveOrderCommand,
    ClearFleetOrderQueueCommand,
    CancelFleetOrderCommand,
    ResourceSurveyCommand,
    CreateSurveyProgramCommand,
    AmendSurveyProgramCommand,
    SuspendSurveyProgramCommand,
    ResumeSurveyProgramCommand,
    CancelSurveyProgramCommand,
    AcknowledgeSurveyProgramIssueCommand,
    CreateFreightProgramCommand,
    AmendFreightProgramCommand,
    SuspendFreightProgramCommand,
    ResumeFreightProgramCommand,
    CancelFreightProgramCommand,
    AcknowledgeFreightProgramIssueCommand,
    CreateMaintenanceProgramCommand,
    AmendMaintenanceProgramCommand,
    SuspendMaintenanceProgramCommand,
    ResumeMaintenanceProgramCommand,
    CancelMaintenanceProgramCommand,
    AcknowledgeMaintenanceIssueCommand,
    AssignAppointmentCommand,
    SetColonyProcessingPolicyCommand
>;

} // namespace deep
