#pragma once

// Durable state for the bounded Precision Characterization Array development
// chain. Public objectives, hidden candidate truth, engineering evidence,
// physical prototypes, local production, and support skill stay distinct.
#include "sim/Domain.h"
#include "sim/MaintenanceProgram.h"
#include "sim/Observation.h"

#include <optional>
#include <string>
#include <vector>

namespace deep {

enum class TechnicalFacilityCapability { PrototypeInstrumentation };
struct TechnologyOpportunity {
    TechnologyOpportunityId id;
    std::string name;
    ShipComponentId baselineComponentId;
    double targetDetectionThreshold = 7.0;
    bool requiresAccessibility = true;
    std::string objective;
    std::string knownTradeoff;
};

// Scenario truth is executor-only until three prototype test records establish
// the result. Queries and command admission must never inspect this value.
struct TechnologyCandidateTruth {
    TechnologyOpportunityId opportunityId;
    double achievedDetectionThreshold = 6.0;
};

struct TechnicalFacility {
    TechnicalFacilityId id;
    ColonyId colonyId;
    std::string name;
    double engineeringWorkdaysPerDay = 0.0;
    TechnicalFacilityCapability capability = TechnicalFacilityCapability::PrototypeInstrumentation;
};

enum class TechnicalDevelopmentScope { DemonstratePrototype, ProductionReady, ProductionAndSupportReady };
enum class TechnicalDevelopmentLifecycle { Authorized, Suspended, Closed };
enum class TechnicalDevelopmentClosure { None, Completed, Cancelled };
enum class TechnicalDevelopmentStage {
    ConceptEngineering,
    PrototypeFabrication,
    PrototypeTesting,
    ProductionQualification,
    SupportQualification,
    Complete
};

struct TechnicalDevelopmentPolicy {
    ProcessedMaterialSet floors;
    std::optional<ProcessedMaterialSet> lifetimeAllowances;
    bool operator==(const TechnicalDevelopmentPolicy&) const = default;
};
struct TechnicalDevelopmentCharter {
    std::string name;
    TechnologyOpportunityId opportunityId;
    ColonyId developmentColonyId;
    std::optional<TechnicalFacilityId> requestedFacilityId;
    std::optional<MaintenanceTeamId> requestedTeamId;
    std::optional<PersonId> requestedLeaderId;
    TechnicalDevelopmentScope scope = TechnicalDevelopmentScope::DemonstratePrototype;
    TechnicalDevelopmentPolicy policy;
    bool operator==(const TechnicalDevelopmentCharter&) const = default;
};

struct TechnicalWorkReceipt {
    int sequence = 1;
    std::int64_t day = 0;
    int charterRevision = 1;
    TechnicalDevelopmentStage stage = TechnicalDevelopmentStage::ConceptEngineering;
    TechnicalFacilityId facilityId;
    MaintenanceTeamId teamId;
    PersonId leaderId;
    double work = 0.0;
    ProcessedMaterialSet consumed;
};
struct TechnicalDevelopmentIssue {
    std::string signature;
    std::string message;
    bool acknowledged = true;
};
struct TechnicalDevelopmentReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    TechnicalDevelopmentScope scope = TechnicalDevelopmentScope::DemonstratePrototype;
    TechnicalDevelopmentStage stage = TechnicalDevelopmentStage::ConceptEngineering;
    std::optional<TechnicalFacilityId> facilityId;
    std::optional<MaintenanceTeamId> teamId;
    std::optional<PersonId> leaderId;
    double periodWork = 0.0;
    double lifetimeWork = 0.0;
    ProcessedMaterialSet periodConsumed;
    ProcessedMaterialSet lifetimeConsumed;
    std::optional<PrototypeComponentUnitId> prototypeId;
    int testCount = 0;
    std::optional<double> demonstratedThreshold;
    std::optional<ShipComponentId> componentId;
    bool localProductionReady = false;
    bool supportQualified = false;
    std::string waitingReason;
    std::int64_t auditThroughId = 0;
};
struct TechnicalDevelopmentProgram {
    TechnicalDevelopmentProgramId id;
    TechnicalDevelopmentCharter charter;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    TechnicalDevelopmentLifecycle lifecycle = TechnicalDevelopmentLifecycle::Authorized;
    TechnicalDevelopmentClosure closure = TechnicalDevelopmentClosure::None;
    std::optional<std::int64_t> closedDay;
    TechnicalDevelopmentStage stage = TechnicalDevelopmentStage::ConceptEngineering;
    // Actual paid work by this program only. Previously completed prototype tests
    // reduce its required testing work; cancelled fractional work is never copied.
    double stageWork = 0.0;
    ProcessedMaterialSet stageConsumed;
    std::optional<MaintenanceTeamId> leasedTeamId;
    // Receipts also pin a started production qualification's facility and a
    // started support qualification's exact team. These are derived commitments.
    std::vector<TechnicalWorkReceipt> receipts;
    std::vector<TechnicalDevelopmentReport> reports;
    std::int64_t reportStartDay = 0;
    std::int64_t nextReportDay = 30;
    TechnicalDevelopmentIssue issue;
};

// Frozen candidate design produced by concept engineering. Sensitivity remains
// absent because it is acquired evidence rather than a design aspiration.
struct PrototypeDesignRecord {
    PrototypeDesignId id;
    TechnologyOpportunityId opportunityId;
    TechnicalDevelopmentProgramId programId;
    std::int64_t createdDay = 0;
    std::string name;
    double mass = 35.0;
    double volume = 100.0;
    double powerDemand = 55.0;
    double surveyCapability = 1.0;
    ProcessedMaterialSet serialBuildCost;
    double componentBuildPoints = 90.0;
    EquipmentServiceProfile serviceProfile;
};

enum class PrototypeComponentState { Available, ReservedForShipyard, Consumed };
struct PrototypeComponentUnit {
    PrototypeComponentUnitId id;
    TechnologyOpportunityId opportunityId;
    PrototypeDesignId designId;
    TechnicalDevelopmentProgramId programId;
    ColonyId colonyId;
    std::int64_t fabricationDay = 0;
    PrototypeComponentState state = PrototypeComponentState::Available;
    std::optional<ShipComponentId> componentId;
    std::optional<std::int64_t> availableDay;
    std::optional<ShipyardOrderId> reservedOrderId;
    std::optional<int> reservedHullNumber;
    std::optional<ShipId> consumedShipId;
};

struct TechnicalTestRecord {
    TechnicalTestId id;
    TechnologyOpportunityId opportunityId;
    PrototypeComponentUnitId prototypeId;
    TechnicalDevelopmentProgramId programId;
    TechnicalFacilityId facilityId;
    MaintenanceTeamId teamId;
    PersonId leaderId;
    int sequence = 1;
    std::int64_t day = 0;
    double measuredDetectionThreshold = 0.0;
    double targetDetectionThreshold = 0.0;
    bool meetsTarget = false;
    std::string limitation;
};

struct DevelopedComponentRevision {
    DevelopedComponentRevisionId id;
    TechnologyOpportunityId opportunityId;
    PrototypeDesignId designId;
    PrototypeComponentUnitId prototypeId;
    std::vector<TechnicalTestId> testIds;
    MeasurementProfileId measurementProfileId;
    ShipComponentId componentId;
    std::int64_t demonstratedDay = 0;
    std::int64_t availableDay = 1;
};

struct ComponentProductionCapability {
    ComponentProductionCapabilityId id;
    ShipComponentId componentId;
    TechnologyOpportunityId opportunityId;
    ColonyId colonyId;
    TechnicalFacilityId facilityId;
    TechnicalDevelopmentProgramId qualifyingProgramId;
    std::int64_t qualifiedDay = 0;
    std::int64_t availableDay = 1;
};

struct SupportQualificationRecord {
    SupportQualificationId id;
    TechnologyOpportunityId opportunityId;
    MaintenanceTeamId teamId;
    EquipmentFamilyId familyId;
    TechnicalDevelopmentProgramId programId;
    std::int64_t qualifiedDay = 0;
    std::int64_t availableDay = 1;
};

struct PrototypeIntegrationReceipt {
    PrototypeComponentUnitId prototypeId;
    ShipyardOrderId orderId;
    int hullNumber = 1;
    ShipId shipId;
    std::int64_t day = 0;
};

} // namespace deep
