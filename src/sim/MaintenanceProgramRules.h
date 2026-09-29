#pragma once

// Known-data support requests and compatible one-group work plans. These pure
// projections are shared by dispatch, owned queries and durable report reasons.
#include "sim/EquipmentServiceRules.h"

namespace deep {
struct OpeningProgramContext;
struct SurveyServiceRequest {
    bool requested = false;
    std::vector<ServiceJobTarget> targets{};
    std::string condition;
};
struct MaintenanceWorkPlan {
    bool ready = false;
    std::string condition;
    int targetIndex = -1;
    ShipId workshopShipId;
    double workshopRate = 0.0;
    double teamRate = 0.0;
    EquipmentServicePlan service;
};

// Returns only the single active last job; historical jobs remain inspectable.
[[nodiscard]] const ServiceJob* activeServiceJob(const MaintenanceProgram& program);
[[nodiscard]] ServiceJob* activeServiceJob(MaintenanceProgram& program);
[[nodiscard]] const ServiceJob* clientServiceJob(const GameState& state, SurveyProgramId client);
// Requires a valid charter, not present resources. Service colony is fixed.
[[nodiscard]] std::optional<std::string> validateMaintenanceCharter(const GameState&,
                                                                    const MaintenanceProgramCharter&);
[[nodiscard]] MaintenanceProgramAmendment maintenanceAmendmentFromCharter(const MaintenanceProgramCharter&);
void applyMaintenanceAmendment(MaintenanceProgramCharter&, const MaintenanceProgramAmendment&);
// Healthy/finished clients need no service even if a selected provider closed.
[[nodiscard]] SurveyServiceRequest surveyServiceRequest(const GameState&, const SurveyProgram&);
// Physical eligibility is stricter than policy intent and requires actual client
// survey control; this never creates a hold on another program's fleet.
[[nodiscard]] bool eligibleServiceClient(const GameState&, const SurveyProgram&);
[[nodiscard]] std::string surveySupportCondition(const GameState&, const SurveyProgram&);
[[nodiscard]] MaintenanceWorkPlan planMaintenanceWork(const GameState&, const MaintenanceProgram&,
                                                      const ServiceJob&,
                                                      const OpeningProgramContext* = nullptr);
[[nodiscard]] std::string maintenanceExecutionCondition(const GameState&, const MaintenanceProgram&);
} // namespace deep
