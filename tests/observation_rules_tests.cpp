// Independently specified measurement limits, chronology and evidence repetition.
// The expected values are literal P4A fixture outcomes, never a second sampler.
#include "sim/ObservationRules.h"
#include "sim/AssessmentRules.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char* why) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(why);
}
ObservationBatch capture(double remaining, double access, int profile, std::int64_t day, int id = 1) {
    const auto profiles = referenceMeasurementProfiles();
    const std::vector<MineralDeposit> physical{{BodyId{1}, Mineral::WaterIce, remaining, access}};
    const auto& p = profiles.at(static_cast<std::size_t>(profile));
    ObservationBatch b;
    b.id = ObservationBatchId{id};
    b.bodyId = BodyId{1};
    b.fleetId = FleetId{1};
    b.origin = ObservationOrigin::ImmediateManual;
    b.firstWorkDay = day;
    b.acquiredDay = day;
    b.availableDay = day + 1;
    b.workDates = {day};
    b.instruments.push_back({{ShipId{1}, ShipClassId{1}, ShipComponentId{4}, p.id, 5, {day}},
                             p,
                             sampleObservationChannels(physical, b.bodyId, p, 5)});
    return b;
}
const ObservationChannel& water(const ObservationBatch& b) {
    return b.instruments[0].channels[11];
}
void thresholds_and_limits() {
    auto b = capture(100, .4, 0, 0);
    require(water(b).indication == ResourceIndication::NotDetectedWithinLimit, "40 below recon50");
    b = capture(100, .4, 1, 0);
    require(water(b).indication == ResourceIndication::Detected &&
                water(b).accessibility == AccessibilityReading::Moderate,
            "40 above characterization10");
    b = capture(200, .4, 0, 0);
    require(water(b).indication == ResourceIndication::Detected &&
                water(b).accessibility == AccessibilityReading::NotMeasured,
            "recon80 has no access measurement");
    const auto profiles = referenceMeasurementProfiles();
    for (const auto& p : profiles) {
        const std::vector<MineralDeposit> low{{BodyId{1}, Mineral::WaterIce, 5, .4}};
        const std::vector<MineralDeposit> inaccessible{{BodyId{1}, Mineral::WaterIce, 1000, 0}};
        const auto absent = sampleObservationChannels({}, BodyId{1}, p, 5);
        require(absent.size() == 14 && absent == sampleObservationChannels(low, BodyId{1}, p, 5) &&
                    absent == sampleObservationChannels(inaccessible, BodyId{1}, p, 5),
                "truth must not affect negative shape");
        const std::vector<MineralDeposit> boundary{{BodyId{1}, Mineral::WaterIce, p.detectionThreshold, 1}};
        require(sampleObservationChannels(boundary, BodyId{1}, p, 5)[11].indication ==
                    ResourceIndication::Detected,
                "exact threshold detects");
    }
    require(water(capture(1000, .249, 1, 0)).accessibility == AccessibilityReading::Low,
            "low upper exclusive");
    require(water(capture(1000, .25, 1, 0)).accessibility == AccessibilityReading::Moderate,
            "moderate lower inclusive");
    require(water(capture(1000, .75, 1, 0)).accessibility == AccessibilityReading::High,
            "high lower inclusive");
    require(water(capture(1000, 2, 1, 0)).accessibility == AccessibilityReading::High,
            "physical access above one valid");
    auto unsupported = profiles[1];
    unsupported.channels = {Mineral::Iron};
    require(sampleObservationChannels({}, BodyId{1}, unsupported, 5).size() == 1,
            "unsupported channels not fabricated");
    for (double bad :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto p = profiles[0];
        p.detectionThreshold = bad;
        rejects([&] { validateMeasurementProfile(p); }, "bad threshold accepted");
    }
}
void exposure_and_provenance() {
    auto b = capture(200, .4, 1, 5);
    b.origin = ObservationOrigin::TimedPass;
    b.surveyProgramId = SurveyProgramId{1};
    b.teamId = SurveyTeamId{1};
    b.passNumber = 1;
    b.firstWorkDay = 1;
    b.workDates = {1, 2, 3, 4, 5};
    auto& i = b.instruments[0];
    i.exposure.workdays = 3;
    i.exposure.dates = {1, 3, 5};
    i.channels = sampleObservationChannels({}, b.bodyId, i.profile, 3);
    validateObservationBatch(b);
    auto f = interpretObservationBatch(b, AnalysisJobId{1}, 8);
    const auto a = assembleAssessmentRevision(std::vector{f}, b.bodyId, AssessmentId{1}, f.jobId, 8, nullptr);
    require(a.claims[11].indication == IndicationAssessment::Unknown, "short exposure is not non-detection");
    auto malformed = b;
    malformed.instruments[0].channels[0].indication = ResourceIndication::Detected;
    rejects([&] { validateObservationBatch(malformed); }, "short exposure granted full profile");
    malformed = b;
    malformed.availableDay = 5;
    rejects([&] { validateObservationBatch(malformed); }, "same opening data delivery accepted");
    std::vector<InstrumentExposure> exposure;
    const InstrumentExposure group{ShipId{1}, ShipClassId{1}, ShipComponentId{4}, MeasurementProfileId{1}, 0,
                                   {}};
    accumulateObservationExposure(exposure, group, 1, 1);
    accumulateObservationExposure(exposure, group, 4, 1);
    require(exposure[0].workdays == 2 && exposure[0].dates == std::vector<std::int64_t>{1, 4},
            "detour retains exposure dates");
    rejects([&] { accumulateObservationExposure(exposure, group, 4, 1); }, "duplicate exposure allowed");
}
void chronological_interpretation() {
    const auto old = capture(100, .4, 0, 1, 1);
    const auto recent = capture(100, .4, 1, 10, 2);
    std::vector<AnalysisFinding> findings{interpretObservationBatch(recent, AnalysisJobId{1}, 13)};
    const auto first =
        assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{1}, AnalysisJobId{1}, 13, nullptr);
    findings.push_back(interpretObservationBatch(old, AnalysisJobId{2}, 16));
    const auto late =
        assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{2}, AnalysisJobId{2}, 16, &first);
    require(late.claims[11].indicationDay == 10 &&
                late.claims[11].indication == IndicationAssessment::Indicated &&
                late.claims[11].accessibility == AccessibilityAssessment::Moderate && !late.contentChanged,
            "older analysis displaced newer scientific epoch");
    findings.push_back(interpretObservationBatch(recent, AnalysisJobId{3}, 19));
    const auto repeat =
        assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{3}, AnalysisJobId{3}, 19, &late);
    require(repeat.repeatedEvidence && !repeat.contentChanged && repeat.findingIds.size() == 3,
            "repeat fabricated certainty or lost provenance");
    require(repeat.claims[11].quantity == QuantityAssessment::Unmeasured &&
                repeat.claims[11].site == SiteAssessment::Unassessed,
            "unsupported claims promoted");
    auto negative = capture(1, .4, 1, 20, 3);
    findings.push_back(interpretObservationBatch(negative, AnalysisJobId{4}, 23));
    auto latest =
        assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{4}, AnalysisJobId{4}, 23, &repeat);
    require(latest.claims[11].earlierIndication && latest.claims[11].indicationDay == 20,
            "new negative must retain earlier indication without claiming depletion");
    // Same-date different methods preserve a negative qualification to detection.
    auto recon = capture(100, .4, 0, 30, 4), character = capture(100, .4, 1, 30, 5);
    findings.push_back(interpretObservationBatch(recon, AnalysisJobId{5}, 33));
    findings.push_back(interpretObservationBatch(character, AnalysisJobId{6}, 33));
    latest = assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{5}, AnalysisJobId{6}, 33, &latest);
    require(latest.claims[11].indication == IndicationAssessment::Indicated &&
                latest.claims[11].indicationInputs.size() == 2,
            "compatible methods lost alternatives");
    auto low = capture(1000, .2, 1, 40, 6), high = capture(1000, .9, 1, 40, 7);
    findings.push_back(interpretObservationBatch(low, AnalysisJobId{7}, 43));
    findings.push_back(interpretObservationBatch(high, AnalysisJobId{8}, 43));
    latest = assembleAssessmentRevision(findings, BodyId{1}, AssessmentId{6}, AnalysisJobId{8}, 43, &latest);
    require(latest.claims[11].accessibility == AccessibilityAssessment::Mixed &&
                latest.claims[11].accessibilityInputs.size() == 2,
            "Same-epoch mixed accessibility must retain alternatives");
    // Pure signatures make a changed world unavailable to interpretation.
    require(interpretObservationBatch(recent, AnalysisJobId{1}, 13) == findings.front(),
            "record changed during interpretation");
}
} // namespace
int main() {
    try {
        thresholds_and_limits();
        exposure_and_provenance();
        chronological_interpretation();
        std::cout << "3 observation/assessment rules groups passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
