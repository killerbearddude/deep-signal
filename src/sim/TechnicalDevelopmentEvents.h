#pragma once

// Typed audit events for P5 authorizations, physical engineering work, acquired
// test evidence, reusable artifacts, and prototype shipyard integration.
#include "sim/TechnicalDevelopment.h"

namespace deep {

enum class TechnicalDevelopmentAuditKind {
    Authorized,
    Amended,
    Suspended,
    Resumed,
    Cancelled,
    LeaseAcquired,
    WorkPerformed,
    DesignCompleted,
    PrototypeFabricated,
    TestCompleted,
    ComponentDemonstrated,
    ProductionQualified,
    SupportQualified,
    ReportPublished,
    IssueRaised,
    IssueAcknowledged,
    Closed
};
struct TechnicalDevelopmentAuditEvent {
    TechnicalDevelopmentProgramId programId;
    TechnicalDevelopmentAuditKind kind = TechnicalDevelopmentAuditKind::Authorized;
    TechnologyOpportunityId opportunityId;
    TechnicalDevelopmentStage stage = TechnicalDevelopmentStage::ConceptEngineering;
    int charterRevision = 1;
    double amount = 0.0;
    std::string detail;
};

enum class PrototypeIntegrationAuditKind { Reserved, Consumed };
struct PrototypeIntegrationAuditEvent {
    PrototypeIntegrationAuditKind kind = PrototypeIntegrationAuditKind::Reserved;
    PrototypeComponentUnitId prototypeId;
    ShipyardOrderId orderId;
    int hullNumber = 1;
    std::optional<ShipId> shipId;
};

} // namespace deep
