#pragma once

// Finite field construction with retained physical participants and a fixed
// package/route. Commissioned assets live on ResourceSite after builder return.
#include "sim/ResourceSite.h"

namespace deep {
struct SiteDevelopmentPolicy {
    double homePropellantFloor = 0;
    std::optional<double> additionalPropellantAllowance;
    double returnContingencyFraction = 0;
    ProcessedMaterialSet constructionFloors;
    std::optional<ProcessedMaterialSet> materialAllowances;
};
struct SiteDevelopmentAssignments {
    std::string name;
    std::optional<FleetId> builderId;
    std::optional<MaintenanceTeamId> teamId;
    std::optional<PersonId> leaderId;
    SiteDevelopmentPolicy policy;
};
struct SiteDevelopmentCharter {
    SiteId siteId;
    ColonyId supportColonyId;
    int catalogVersion = kSiteModuleCatalogVersion;
    std::vector<SiteModuleInstall> package;
    SiteDevelopmentAssignments assignments;
};
enum class SiteDevelopmentLifecycle { Authorized, Suspended, Closing, Closed };
enum class SiteDevelopmentClosure { None, Completed, Cancelled };
enum class SiteDevelopmentTask {
    None,
    Embark,
    Preparing,
    Outbound,
    Assembly,
    Commissioning,
    Return,
    Disembark
};
enum class SiteDevelopmentWorkKind { Assembly, Commissioning };

// Each started row pins its actual hull; lack of power cannot silently rotate
// the same row to a different workshop while its participants remain committed.
struct SiteAssemblyRow {
    double workCompleted = 0;
    ProcessedMaterialSet consumed;
    std::optional<ShipId> workshopShipId;
};
struct SiteDevelopmentWorkReceipt {
    std::int64_t day = 0;
    int charterRevision = 1;
    SiteDevelopmentWorkKind kind = SiteDevelopmentWorkKind::Assembly;
    int packageRow = 0;
    FleetId builderId;
    ShipId workshopShipId;
    MaintenanceTeamId teamId;
    PersonId leaderId;
    double work = 0;
    ProcessedMaterialSet consumed;
};
struct SiteDevelopmentReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    SiteDevelopmentPolicy policy;
    double assemblyWork = 0;
    double commissioningWork = 0;
    ProcessedMaterialSet consumed;
    double fuelLoaded = 0;
    double fuelBurned = 0;
    std::optional<FleetId> builderId;
    std::optional<MaintenanceTeamId> teamId;
    std::optional<BodyId> bodyId;
    bool commissioned = false;
    std::string waitingReason;
    std::int64_t auditThroughId = 0;
};
struct SiteDevelopmentIssue {
    std::string signature;
    std::string message;
    bool acknowledged = true;
};
struct SiteDevelopmentProgram {
    SiteDevelopmentProgramId id;
    SiteDevelopmentCharter charter;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    SiteDevelopmentLifecycle lifecycle = SiteDevelopmentLifecycle::Authorized;
    SiteDevelopmentClosure closure = SiteDevelopmentClosure::None;
    std::optional<std::int64_t> closedDay;
    // Canonical leases; the site's reverse construction owner is derived.
    std::optional<FleetId> leasedBuilderId;
    std::optional<MaintenanceTeamId> leasedTeamId;
    bool holdsSiteConstruction = false;
    // Task identities remain original until physical return/disembark/release.
    SiteDevelopmentTask task = SiteDevelopmentTask::None;
    std::optional<FleetId> taskBuilderId;
    std::optional<MaintenanceTeamId> taskTeamId;
    std::vector<SiteAssemblyRow> rows;
    double commissioningWork = 0;
    std::optional<ShipId> commissioningWorkshopId;
    std::optional<std::int64_t> commissionedDay;
    std::vector<SiteDevelopmentWorkReceipt> workReceipts;
    double fuelLoaded = 0;
    double fuelBurned = 0;
    std::vector<SiteDevelopmentReport> reports;
    std::int64_t reportStartDay = 0;
    std::int64_t nextReportDay = 30;
    SiteDevelopmentIssue issue;
};
} // namespace deep
