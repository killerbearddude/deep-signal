#pragma once

// Defines the aggregate mutable state for one running simulation.
// GameState owns all domain records by value; references between records are
// stable typed IDs rather than owning pointers.

#include "sim/Domain.h"
#include "sim/Events.h"
#include "sim/GameDate.h"
#include "sim/SurveyProgram.h"
#include "sim/FreightProgram.h"
#include "sim/MaintenanceProgram.h"
#include "sim/AnalysisProgram.h"

#include <cstdint>
#include <vector>

namespace deep {

// Monotonic ID counters for each entity type. Counters are part of save state so
// loaded games can continue allocating IDs without collisions.
struct IdCounters {
    std::int64_t nextStarSystemId = 1;
    std::int64_t nextBodyId = 1;
    std::int64_t nextColonyId = 1;
    std::int64_t nextInstitutionId = 1;
    std::int64_t nextPersonId = 1;
    std::int64_t nextShipClassId = 1;
    std::int64_t nextShipComponentId = 1;
    std::int64_t nextShipyardOrderId = 1;
    std::int64_t nextShipId = 1;
    std::int64_t nextFleetId = 1;
    std::int64_t nextEventId = 1;
    std::int64_t nextSurveyProgramId = 1;
    std::int64_t nextSurveyTeamId = 1;
    std::int64_t nextFreightProgramId = 1;
    std::int64_t nextEquipmentFamilyId = 1;
    std::int64_t nextMaintenanceTeamId = 1;
    std::int64_t nextMaintenanceProgramId = 1;
    std::int64_t nextMeasurementProfileId = 1;
    std::int64_t nextObservationBatchId = 1;
    std::int64_t nextAnalysisProgramId = 1;
    std::int64_t nextAnalysisJobId = 1;
    std::int64_t nextAssessmentId = 1;
};

// Complete state snapshot for the headless simulation. Public vectors are kept
// simple for prototype inspectability; mutation is still routed through
// Simulation so invariants and events stay centralized. Scenario builders and
// persistence may assemble detached snapshots; Simulation validates them on entry.
// Vector order is meaningful for FIFO production and pooled fleet fuel payment,
// so persistence must preserve it rather than arbitrarily sorting records.
struct GameState {
    GameDate date;
    IdCounters ids;

    std::vector<StarSystem> starSystems;
    std::vector<Institution> institutions;
    std::vector<Person> people;
    std::vector<Appointment> appointments;
    std::vector<Body> bodies;
    std::vector<MineralDeposit> mineralDeposits;
    std::vector<Colony> colonies;
    std::vector<ShipComponentDefinition> shipComponents;
    std::vector<ShipClass> shipClasses;
    std::vector<ShipyardOrder> shipyardOrders;
    std::vector<Ship> ships;
    std::vector<Fleet> fleets;
    std::vector<SurveyTeam> surveyTeams;
    std::vector<SurveyProgram> surveyPrograms;
    std::vector<FreightProgram> freightPrograms;
    std::vector<EquipmentFamily> equipmentFamilies;
    std::vector<MaintenanceTeam> maintenanceTeams;
    std::vector<MaintenanceProgram> maintenancePrograms;
    std::vector<MeasurementProfile> measurementProfiles;
    std::vector<ObservationBatch> observations;
    std::vector<AnalysisProgram> analysisPrograms;
    std::vector<AnalysisFinding> analysisFindings;
    std::vector<AssessmentRevision> assessments;

    // Runtime-only economy telemetry is separated from the audit log so routine
    // mining can feed current-session UI, forecasts, and debugging without
    // overwhelming event views. Persistence intentionally does not store these
    // snapshots; loaded games start with this vector empty until more days run.
    std::vector<DailyEconomySnapshot> dailyEconomySnapshots;

    std::vector<SimEvent> eventLog;
};

} // namespace deep
