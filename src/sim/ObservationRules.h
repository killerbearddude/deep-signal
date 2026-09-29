#pragma once

// The only scientific feature entry point allowed to inspect physical deposits.
// Interpretation uses Observation records, never this sampler's physical inputs.
#include "sim/Domain.h"
#include "sim/Observation.h"
#include <span>

namespace deep {
// Throws on malformed profile data. It neither normalizes nor reorders channels.
void validateMeasurementProfile(const MeasurementProfile& profile);
// Authors the two explicitly specified P4A fixtures in stable channel order.
[[nodiscard]] std::vector<MeasurementProfile> referenceMeasurementProfiles();
// Samples completion-boundary truth, not an average over the acquisition dates.
// Returned channel count/order depends only on the declared profile.
[[nodiscard]] std::vector<ObservationChannel>
sampleObservationChannels(std::span<const MineralDeposit> deposits, BodyId body,
                          const MeasurementProfile& profile, int exposureWorkdays);
// Adds actual contributing units/dates to a detached pass, preserving first-seen
// group order. Rejects identity changes, duplicate days and excess exposure.
void accumulateObservationExposure(std::vector<InstrumentExposure>& exposures,
                                   const InstrumentExposure& contributor, std::int64_t day, int units);
// Validates sealed shapes and chronology without resampling live geology.
void validateObservationBatch(const ObservationBatch& batch);
} // namespace deep
