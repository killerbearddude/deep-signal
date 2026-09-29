#pragma once
// Daily bounded analysis executor and report bookkeeping. Actual work consumes
// one scientist and opening laboratory throughput, never a fleet or inventory.
#include "sim/AnalysisProgramRules.h"
#include <functional>
namespace deep {
struct AnalysisExecutionHooks {
    std::function<void(EventSeverity, SimEventPayload)> emit;
    // Reserves both audit destinations before applying coupled labor/publication.
    std::function<void(std::size_t)> prepareEvents{};
};
// Advances at most one job once, with preallocated publication before mutation.
void runAnalysisOpeningDay(GameState&, AnalysisProgram&, OpeningProgramContext&,
                           const AnalysisExecutionHooks&);
// Publishes due reports and stable allowance issues; performs no analyst work.
void finishAnalysisDay(GameState&, const AnalysisExecutionHooks&);
} // namespace deep
