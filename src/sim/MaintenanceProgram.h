#pragma once

// Stationary colony-supported instrument service. Programs lease only their
// tender/team. Jobs hold survey-owned clients without a second movement owner.
// Ship condition is current physical truth; targets and receipts are history.
#include "sim/Domain.h"
#include <optional>
#include <string>
#include <vector>

namespace deep {

enum class MaintenanceTeamLocation { Colony, Fleet };
// Development expertise is independent of equipment-family service training.
enum class EngineeringQualification { PrototypeInstrumentation };
struct MaintenanceTeam {
    MaintenanceTeamId id;
    std::string name;
    std::vector<EquipmentFamilyId> qualifiedFamilies{};
    std::vector<EngineeringQualification> engineeringQualifications{};
    double workdaysPerDay = 1.0;
    MaintenanceTeamLocation location = MaintenanceTeamLocation::Colony;
    std::optional<ColonyId> colonyId;
    std::optional<FleetId> fleetId;
};

struct MaintenanceSupplyPolicy {
    ProcessedMaterialSet floors;
    // Absent means no program cap. A present zero is zero consumption authority.
    std::optional<ProcessedMaterialSet> lifetimeAllowances;
};
struct MaintenanceProgramAmendment {
    std::string name;
    std::optional<FleetId> requestedTenderId;
    std::optional<MaintenanceTeamId> requestedTeamId;
    std::optional<PersonId> requestedLeaderId;
    std::vector<FleetId> clients{};
    MaintenanceSupplyPolicy policy;
};
struct MaintenanceProgramCharter {
    std::string name;
    ColonyId serviceColonyId;
    std::optional<FleetId> requestedTenderId;
    std::optional<MaintenanceTeamId> requestedTeamId;
    std::optional<PersonId> requestedLeaderId;
    std::vector<FleetId> clients{};
    MaintenanceSupplyPolicy policy;
};
enum class MaintenanceProgramLifecycle { Authorized, Suspended, Closed };
enum class ServiceJobOutcome { Active, Completed, Withdrawn };

struct ServiceJobTarget {
    ShipId shipId;
    ShipComponentId componentId;
    // Captured debt explains the full-service commitment, not a progress cache.
    double initialUsedDuty = 0.0;
};
struct ServiceJob {
    int number = 1;
    SurveyProgramId surveyProgramId;
    FleetId clientFleetId;
    ColonyId serviceColonyId;
    FleetId tenderFleetId;
    MaintenanceTeamId teamId;
    PersonId leaderId;
    int providerRevision = 1;
    int clientRevision = 1;
    std::int64_t startedDay = 0;
    std::optional<std::int64_t> endedDay;
    ServiceJobOutcome outcome = ServiceJobOutcome::Active;
    std::string endReason;
    std::vector<ServiceJobTarget> targets{};
    // Pin only a started group's workshop. Completion releases it; another
    // group/hull can be selected on a later day, never using today's leftovers.
    std::optional<int> activeTarget;
    std::optional<ShipId> workshopShipId;
};
struct MaintenanceWorkReceipt {
    int sequence = 1;
    int jobNumber = 1;
    std::int64_t day = 0;
    ShipId clientShipId;
    ShipComponentId componentId;
    ShipId workshopShipId;
    int installationQuantity = 1;
    double beforeUsedDuty = 0.0;
    double afterUsedDuty = 0.0;
    double restoredDuty = 0.0;
    double teamWorkdays = 0.0;
    ProcessedMaterialSet consumed;
    // Current authority at this work boundary; original participants/revisions
    // remain on the job even when a later limits-only amendment keeps it active.
    int providerRevision = 1;
    int clientRevision = 1;
};
struct MaintenanceProgramReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    int jobsCompleted = 0;
    int jobsWithdrawn = 0;
    double restoredDuty = 0.0;
    double teamWorkdays = 0.0;
    ProcessedMaterialSet consumed;
    MaintenanceSupplyPolicy policy;
    double availableTeamRate = 0.0;
    double compatibleWorkshopRate = 0.0;
    std::string waitingReason;
    // Global audit watermark at publication. Commands later on the same day
    // belong to the next report, rather than rewriting an already read report.
    std::int64_t auditThroughId = 0;
};
struct MaintenanceProgramIssue {
    std::string signature;
    std::string message;
    bool acknowledged = true;
};
struct MaintenanceProgram {
    MaintenanceProgramId id;
    MaintenanceProgramCharter charter;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    MaintenanceProgramLifecycle lifecycle = MaintenanceProgramLifecycle::Authorized;
    std::optional<std::int64_t> closedDay;
    std::optional<FleetId> leasedTenderId;
    std::optional<MaintenanceTeamId> leasedTeamId;
    int nextJobNumber = 1;
    // The last history row can be Active; there is no duplicate active-job copy.
    std::vector<ServiceJob> jobs{};
    std::vector<MaintenanceWorkReceipt> receipts{};
    ProcessedMaterialSet consumed;
    double restoredDuty = 0.0;
    double teamWorkdays = 0.0;
    std::int64_t nextReportDay = 30;
    std::int64_t reportStartDay = 0;
    std::vector<MaintenanceProgramReport> reports{};
    MaintenanceProgramIssue issue;
};

} // namespace deep
