#pragma once
// Completion-boundary bridge from actual physical fieldwork to a sealed record.
// Preparation allocates detached results; callers reserve storage before duty.
#include "sim/GameState.h"
namespace deep {
// Validates identity/date bounds, then samples only the named body's channels.
// Does not increment counters, charge wear or append an authoritative record.
[[nodiscard]] ObservationBatch prepareObservationBatch(const GameState& state, FleetId fleet, BodyId body,
                                                       const std::vector<InstrumentExposure>& exposures,
                                                       const std::vector<std::int64_t>& workDates,
                                                       std::optional<SurveyProgramId> program = std::nullopt,
                                                       std::optional<SurveyTeamId> team = std::nullopt,
                                                       int pass = 0);
} // namespace deep
