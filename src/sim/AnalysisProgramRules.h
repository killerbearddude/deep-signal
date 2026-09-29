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
[[nodiscard]] std::string analysisExecutionCondition(const GameState&, const AnalysisProgram&);
// Captures acknowledged limitations at a decision without changing authority.
void acknowledgeKnownAnalysisLimit(const GameState&, AnalysisProgram&);
} // namespace deep
