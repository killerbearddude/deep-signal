#pragma once

// Durable, purpose-specific records for home-supported survey programs.
// Charters record requested intent. Leases, team location, tasks, receipts,
// reports, and issues record what physical execution actually accomplished.

#include "sim/Domain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep {

enum class SurveyTeamLocationKind { Colony, Fleet };

struct SurveyTeam {
    SurveyTeamId id;
    std::string name;
    SurveyTeamLocationKind locationKind = SurveyTeamLocationKind::Colony;
    std::optional<ColonyId> colonyId = std::nullopt;
    std::optional<FleetId> fleetId = std::nullopt;
};

// Charter row order breaks equal-priority ties. Body identity is stable across
// amendments and remains on completed receipts if a later charter drops it.
struct SurveyProgramTarget {
    BodyId bodyId;
    int priority = 0;
    int requestedPasses = 1;
};

struct SurveyProgramPolicy {
    // Empty means no program-specific transfer cap. This does not create fuel;
    // real stock, tank capacity, and planned need still limit every transfer.
    std::optional<double> maxAdditionalPropellant = std::nullopt;
    double homeStockFloor = 0.0;
    double returnContingencyFraction = 0.0;
    std::optional<MaintenanceProgramId> maintenanceProgramId = std::nullopt;
    double remainingDutyTrigger = 0.25;
};

struct SurveyProgramCharter {
    std::string name;
    ColonyId homeColonyId;
    std::optional<FleetId> requestedFleetId = std::nullopt;
    std::optional<PersonId> requestedLeaderId = std::nullopt;
    std::optional<SurveyTeamId> requestedTeamId = std::nullopt;
    std::vector<SurveyProgramTarget> targets{};
    SurveyProgramPolicy policy{};
};

enum class SurveyProgramLifecycle { Authorized, Suspended, Closing, Closed };
enum class SurveyProgramClosure { None, Completed, Cancelled };
enum class SurveyProgramTask { None, Outbound, Survey, Return };

struct SurveyVisitReceipt {
    BodyId bodyId;
    int passNumber = 1;
    FleetId fleetId;
    SurveyTeamId teamId;
    std::optional<PersonId> leaderId = std::nullopt;
    SurveyPlanningApproach approach = SurveyPlanningApproach::CoverageFirst;
    std::int64_t firstWorkDay = 0;
    std::int64_t completedDay = 0;
    int workDays = 0;
    int depositsImproved = 0;
    double averageConfidenceBefore = 0.0;
    double averageConfidenceAfter = 0.0;
};

struct SurveyProgramReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    std::optional<PersonId> leaderId = std::nullopt;
    SurveyPlanningApproach approach = SurveyPlanningApproach::CoverageFirst;
    int visitsCompleted = 0;
    std::int64_t workDays = 0;
    double fuelLoaded = 0.0;
    double fuelBurned = 0.0;
    std::optional<FleetId> fleetId = std::nullopt;
    std::optional<SurveyTeamId> teamId = std::nullopt;
    std::optional<BodyId> fleetBodyId = std::nullopt;
    std::string waitingReason{};
};

struct SurveyProgramIssue {
    // Empty signature means no current issue. Acknowledgment preserves the
    // same constraint while permitting later world time to advance.
    std::string signature{};
    std::string message{};
    bool acknowledged = true;
};

struct SurveyProgram {
    SurveyProgramId id;
    SurveyProgramCharter charter;
    // A requested home change during an indivisible sortie takes effect only
    // after the old return obligation reaches a safe planning boundary.
    std::optional<ColonyId> pendingHomeColonyId = std::nullopt;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    SurveyProgramLifecycle lifecycle = SurveyProgramLifecycle::Authorized;
    SurveyProgramClosure closure = SurveyProgramClosure::None;

    // Canonical actual leases live only here. Fleet/team views derive their
    // reverse ownership; requested IDs alone confer no control.
    std::optional<FleetId> leasedFleetId = std::nullopt;
    std::optional<SurveyTeamId> leasedTeamId = std::nullopt;

    SurveyProgramTask task = SurveyProgramTask::None;
    std::optional<BodyId> taskBodyId = std::nullopt;
    // A partial visit retains the physical asset identities that earned its
    // completed workdays. Reacquisition cannot silently credit a new crew.
    std::optional<FleetId> taskFleetId = std::nullopt;
    std::optional<SurveyTeamId> taskTeamId = std::nullopt;
    std::optional<PersonId> taskLeaderId = std::nullopt;
    SurveyPlanningApproach taskApproach = SurveyPlanningApproach::CoverageFirst;
    int taskPassNumber = 0;
    int workDaysCompleted = 0;
    std::int64_t firstWorkDay = 0;
    std::string lastSelectionReason{};
    // A maintenance return preserves the original partial visit and participants.
    // It is cleared only when home is reached, not by finishing transit elsewhere.
    bool maintenanceReturn = false;
    std::vector<SurveyVisitReceipt> receipts{};

    double fuelLoaded = 0.0;
    double fuelBurned = 0.0;
    std::int64_t totalWorkDays = 0;
    std::int64_t nextReportDay = 30;
    std::int64_t reportStartDay = 0;
    double reportedFuelLoaded = 0.0;
    double reportedFuelBurned = 0.0;
    std::int64_t reportedWorkDays = 0;
    int reportedVisits = 0;
    std::vector<SurveyProgramReport> reports{};
    SurveyProgramIssue issue{};
};

} // namespace deep
