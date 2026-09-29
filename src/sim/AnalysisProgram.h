#pragma once

// Finite laboratory work using the existing SurveyTeam pool. Source and lab
// remain fixed; actual leases and immutable dated receipts account for all work.
#include "sim/AssessmentRules.h"
#include <variant>

namespace deep {
struct FollowSurveyInput {
    SurveyProgramId programId;
    bool operator==(const FollowSurveyInput&) const = default;
};
struct FixedBatchInput {
    std::vector<ObservationBatchId> batches;
    bool operator==(const FixedBatchInput&) const = default;
};
using AnalysisInputSource = std::variant<FollowSurveyInput, FixedBatchInput>;
struct AnalysisAmendment {
    std::string name;
    std::optional<SurveyTeamId> requestedTeamId;
    std::optional<PersonId> requestedLeaderId;
    // Scientific team-workdays over the lifetime; absent means uncapped.
    std::optional<double> workAllowance;
    bool operator==(const AnalysisAmendment&) const = default;
};
struct AnalysisCharter {
    std::string name;
    ColonyId colonyId;
    AnalysisInputSource source;
    std::optional<SurveyTeamId> requestedTeamId;
    std::optional<PersonId> requestedLeaderId;
    std::optional<double> workAllowance;
    bool operator==(const AnalysisCharter&) const = default;
};
enum class AnalysisLifecycle { Authorized, Suspended, Closed };
enum class AnalysisClosure { None, Completed, Cancelled };
enum class AnalysisJobOutcome { Active, Completed, Cancelled };
inline constexpr double kAnalysisJobWorkdays = 3.0;
struct AnalysisJob {
    AnalysisJobId id;
    ObservationBatchId batchId;
    std::int64_t startedDay = 0;
    std::optional<std::int64_t> endedDay;
    AnalysisJobOutcome outcome = AnalysisJobOutcome::Active;
    double requiredWork = kAnalysisJobWorkdays;
    // Progress derives from receipts, with no independently mutable cache.
    bool operator==(const AnalysisJob&) const = default;
};
struct AnalysisWorkReceipt {
    AnalysisJobId jobId;
    std::int64_t day = 0;
    ColonyId colonyId;
    SurveyTeamId teamId;
    PersonId leaderId;
    int charterRevision = 1;
    double work = 0;
    // Historical installed capacity supports reconciliation after capacity
    // changes; this does not constitute another live laboratory budget.
    double labCapacity = 0;
    bool operator==(const AnalysisWorkReceipt&) const = default;
};
struct AnalysisReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    std::optional<double> workAllowance;
    double workPerformed = 0;
    double lifetimeWork = 0;
    int jobsCompleted = 0;
    int inputBacklog = 0;
    std::string sourceStatus;
    std::string waitingReason;
    std::string limitations;
    std::int64_t auditThroughId = 0;
    bool operator==(const AnalysisReport&) const = default;
};
struct AnalysisIssue {
    std::string signature;
    std::string message;
    bool acknowledged = true;
    bool operator==(const AnalysisIssue&) const = default;
};
struct AnalysisProgram {
    AnalysisProgramId id;
    AnalysisCharter charter;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    AnalysisLifecycle lifecycle = AnalysisLifecycle::Authorized;
    AnalysisClosure closure = AnalysisClosure::None;
    std::optional<std::int64_t> closedDay;
    std::optional<SurveyTeamId> leasedTeamId;
    std::vector<AnalysisJob> jobs;
    std::vector<AnalysisWorkReceipt> receipts;
    std::int64_t nextReportDay = 30;
    std::int64_t reportStartDay = 0;
    std::vector<AnalysisReport> reports;
    AnalysisIssue issue;
    bool operator==(const AnalysisProgram&) const = default;
};
} // namespace deep
