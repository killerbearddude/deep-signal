#pragma once
// Read-only intent/readiness rules. These inspect acquired records, canonical
// leases and installed lab capacity; no function consults physical geology.
#include "sim/ProgramControl.h"
namespace deep {
// Structural errors reject authorization; absent resources remain waiting.
[[nodiscard]] std::optional<std::string> validateAnalysisCharter(const GameState&, const AnalysisCharter&);
// Acquired source order, including records not yet delivered to analysts.
[[nodiscard]] std::vector<ObservationBatchId> analysisSourceBatches(const GameState&, const AnalysisProgram&);
// Receipts are the single authority for completed job and lifetime work.
[[nodiscard]] double analysisWork(const AnalysisProgram&, std::optional<AnalysisJobId> job = std::nullopt);
[[nodiscard]] const AnalysisJob* activeAnalysisJob(const AnalysisProgram&);
[[nodiscard]] bool analysisSourceEnded(const GameState&, const AnalysisProgram&);
[[nodiscard]] std::string analysisSourceStatus(const GameState&, const AnalysisProgram&);
// Typed eligibility keeps gameplay independent of the wording shown to players.
// These values are transient projections, never saved scheduling state.
enum class AnalysisWaitCause {
    None,
    Closed,
    Suspended,
    NoInput,
    NoLeader,
    NoTeam,
    TeamControlled,
    TeamLocation,
    NoLaboratory,
    WorkAllowance,
    TeamOccupied,
    InputUnavailable,
    LaboratoryContention,
    PartialLaboratoryShare,
    NumericLimit
};
struct AnalysisReadiness {
    bool canAttemptWork = false;
    AnalysisWaitCause cause = AnalysisWaitCause::None;
    std::string explanation;
    std::optional<ObservationBatchId> batchId;
    // Positive only when evaluated against an opening budget; team-workdays.
    double workThisOpening = 0.0;
};
// Mechanical readiness at the current date, before transient resource contention.
[[nodiscard]] AnalysisReadiness analysisReadiness(const GameState&, const AnalysisProgram&);
// Applies the actual/proposed opening's scientist occupancy, delivered input IDs
// and remaining lab budget. Does not debit budgets, acquire staff or create jobs.
[[nodiscard]] AnalysisReadiness analysisOpeningReadiness(const GameState&, const AnalysisProgram&,
                                                         const OpeningProgramContext&,
                                                         std::int64_t openingDay);
// One next-opening projection in existing dispatch order, using current leases
// and already acquired inputs available by D+1. Does not simulate future field
// actions/releases, acquire assets, reserve capacity, or predict future batches.
[[nodiscard]] AnalysisReadiness analysisNextOpeningReadiness(const GameState&, const AnalysisProgram&);
// Display adapter for live commands/reports; all decisions use the typed result.
[[nodiscard]] std::string analysisExecutionCondition(const GameState&, const AnalysisProgram&);
// Captures acknowledged limitations at a decision without changing authority.
void acknowledgeKnownAnalysisLimit(const GameState&, AnalysisProgram&);
} // namespace deep
