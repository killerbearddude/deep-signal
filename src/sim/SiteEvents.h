#pragma once
// Typed development/operating audits. Operating records stay separate from P4A
// scientific batches and never carry hidden reserve/accessibility values.
#include "sim/ResourceSite.h"
namespace deep {
enum class SiteDevelopmentAuditKind {
    Authorized,
    Amended,
    Suspended,
    Resumed,
    Cancelled,
    LeaseAcquired,
    Embarked,
    Refueled,
    Departed,
    AssemblyWork,
    CommissioningWork,
    Commissioned,
    Disembarked,
    Closed,
    ReportPublished,
    IssueRaised,
    IssueAcknowledged
};
struct SiteDevelopmentAuditEvent {
    SiteDevelopmentProgramId programId;
    SiteDevelopmentAuditKind kind = SiteDevelopmentAuditKind::Authorized;
    SiteId siteId;
    std::optional<FleetId> fleetId;
    std::optional<MaintenanceTeamId> teamId;
    std::optional<ShipId> workshopShipId;
    std::optional<PersonId> leaderId;
    int charterRevision = 1;
    int packageRow = -1;
    double amount = 0;
    std::string detail;
};
enum class SiteOperatingAuditKind {
    PolicyAuthorized,
    PolicyAmended,
    Suspended,
    Resumed,
    DutyPerformed,
    ExtractionAttempted,
    ReportPublished,
    IssueRaised,
    IssueAcknowledged
};
struct SiteOperatingAuditEvent {
    SiteId siteId;
    SiteOperatingAuditKind kind = SiteOperatingAuditKind::PolicyAuthorized;
    int operatingRevision = 1;
    SiteOperatingIssueCause cause = SiteOperatingIssueCause::None;
    std::int64_t episodeStartedDay = 0;
    double amount = 0;
    std::string detail;
};
} // namespace deep
