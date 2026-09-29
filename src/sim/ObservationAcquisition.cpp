// The sole acquisition bridge that reads GameState physical geology. Analysis
// never calls it, and Load validates old outputs without re-sampling them.
#include "sim/ObservationAcquisition.h"
#include "sim/ObservationRules.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace deep {
ObservationBatch prepareObservationBatch(const GameState& s, FleetId fleet, BodyId body,
                                         const std::vector<InstrumentExposure>& exposures,
                                         const std::vector<std::int64_t>& dates,
                                         std::optional<SurveyProgramId> program,
                                         std::optional<SurveyTeamId> team, int pass) {
    if (s.ids.nextObservationBatchId <= 0 ||
        s.ids.nextObservationBatchId == std::numeric_limits<std::int64_t>::max() ||
        s.date.day == std::numeric_limits<std::int64_t>::max() || dates.empty())
        throw std::runtime_error("Observation identity or delivery date limit reached");
    ObservationBatch b;
    b.id = ObservationBatchId{s.ids.nextObservationBatchId};
    b.bodyId = body;
    b.fleetId = fleet;
    b.origin = program ? ObservationOrigin::TimedPass : ObservationOrigin::ImmediateManual;
    b.surveyProgramId = program;
    b.teamId = team;
    b.passNumber = pass;
    b.firstWorkDay = dates.front();
    b.acquiredDay = s.date.day;
    b.availableDay = s.date.day + 1;
    b.workDates = dates;
    for (const auto& e : exposures) {
        const auto p = std::find_if(s.measurementProfiles.begin(), s.measurementProfiles.end(),
                                    [&](const auto& profile) { return profile.id == e.profileId; });
        if (p == s.measurementProfiles.end())
            throw std::runtime_error("Missing instrument measurement profile");
        b.instruments.push_back({e, *p, sampleObservationChannels(s.mineralDeposits, body, *p, e.workdays)});
    }
    validateObservationBatch(b);
    return b;
}
} // namespace deep
