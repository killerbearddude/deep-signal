// Bounded deterministic instrument sampling and acquisition integrity. Hidden
// quantities never escape this file as scientific outputs or error messages.
#include "sim/ObservationRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace deep {
namespace {
void require(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
} // namespace
void validateMeasurementProfile(const MeasurementProfile& p) {
    require(bool(p.id) && !p.name.empty() && p.methodVersion > 0 && std::isfinite(p.detectionThreshold) &&
                p.detectionThreshold > 0,
            "Invalid measurement profile identity or detection threshold");
    require(!p.channels.empty(), "Measurement profile requires declared channels");
    std::set<Mineral> unique;
    for (const auto mineral : p.channels)
        require(mineralIndex(mineral) < mineralCount() && unique.insert(mineral).second,
                "Invalid or duplicate declared measurement channel");
}

std::vector<MeasurementProfile> referenceMeasurementProfiles() {
    std::vector<Mineral> channels;
    for (std::size_t i = 0; i < mineralCount(); ++i)
        channels.push_back(static_cast<Mineral>(i));
    return {{MeasurementProfileId{1}, "Reconnaissance", 1, 50, false, channels},
            {MeasurementProfileId{2}, "Characterization", 1, 10, true, channels}};
}

std::vector<ObservationChannel> sampleObservationChannels(std::span<const MineralDeposit> deposits,
                                                          BodyId body, const MeasurementProfile& p,
                                                          int exposureWorkdays) {
    validateMeasurementProfile(p);
    require(bool(body) && exposureWorkdays > 0 && exposureWorkdays <= 5,
            "Invalid sampling subject or exposure");
    std::vector<ObservationChannel> result;
    result.reserve(p.channels.size());
    for (Mineral mineral : p.channels) {
        ObservationChannel channel{mineral};
        if (exposureWorkdays < 5) {
            result.push_back(channel);
            continue;
        }
        channel.indication = ResourceIndication::NotDetectedWithinLimit;
        const auto deposit = std::find_if(deposits.begin(), deposits.end(), [&](const auto& d) {
            return d.bodyId == body && d.mineral == mineral;
        });
        if (deposit != deposits.end()) {
            require(std::isfinite(deposit->remaining) && deposit->remaining >= 0 &&
                        std::isfinite(deposit->accessibility) && deposit->accessibility >= 0,
                    "Invalid physical sampling input");
            // Multiplication by at most one cannot overflow finite remaining.
            const double signal = deposit->remaining * std::min(deposit->accessibility, 1.0);
            if (signal >= p.detectionThreshold) {
                channel.indication = ResourceIndication::Detected;
                if (p.measuresAccessibility)
                    channel.accessibility = deposit->accessibility < .25   ? AccessibilityReading::Low
                                            : deposit->accessibility < .75 ? AccessibilityReading::Moderate
                                                                           : AccessibilityReading::High;
            }
        }
        result.push_back(channel);
    }
    return result;
}

void accumulateObservationExposure(std::vector<InstrumentExposure>& rows,
                                   const InstrumentExposure& contributor, std::int64_t day, int units) {
    require(bool(contributor.shipId) && bool(contributor.classId) && bool(contributor.componentId) &&
                bool(contributor.profileId) && day >= 0 && units > 0 && units <= 5,
            "Invalid observation contributor");
    auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& r) {
        return r.shipId == contributor.shipId && r.componentId == contributor.componentId;
    });
    if (it == rows.end()) {
        auto row = contributor;
        row.workdays = units;
        row.dates = {day};
        rows.push_back(std::move(row));
    } else {
        require(it->classId == contributor.classId && it->profileId == contributor.profileId &&
                    it->workdays <= 5 - units && (it->dates.empty() || it->dates.back() < day),
                "Observation exposure identity, date or work limit changed");
        it->dates.push_back(day);
        it->workdays += units;
    }
}

void validateObservationBatch(const ObservationBatch& b) {
    require(bool(b.id) && bool(b.bodyId) && bool(b.fleetId), "Invalid observation identity");
    require(b.origin == ObservationOrigin::TimedPass || b.origin == ObservationOrigin::ImmediateManual,
            "Invalid observation origin");
    require(b.firstWorkDay >= 0 && b.firstWorkDay <= b.acquiredDay &&
                b.acquiredDay < std::numeric_limits<std::int64_t>::max() &&
                b.availableDay == b.acquiredDay + 1,
            "Invalid observation delivery dates");
    const bool manual = b.origin == ObservationOrigin::ImmediateManual;
    require(manual ? !b.surveyProgramId && !b.teamId && b.passNumber == 0
                   : b.surveyProgramId && bool(*b.surveyProgramId) && b.teamId && bool(*b.teamId) &&
                         b.passNumber > 0,
            "Invalid observation source identity");
    require(b.workDates.size() == (manual ? 1U : 5U) && b.workDates.front() == b.firstWorkDay &&
                b.workDates.back() == b.acquiredDay,
            "Observation must describe actual complete fieldwork");
    require(std::adjacent_find(b.workDates.begin(), b.workDates.end(), std::greater_equal<>()) ==
                b.workDates.end(),
            "Observation work dates are not strictly ordered");
    require(!b.instruments.empty(), "Observation has no contributing instruments");
    std::set<std::pair<std::int64_t, std::int64_t>> groups;
    for (const auto& instrument : b.instruments) {
        const auto& e = instrument.exposure;
        validateMeasurementProfile(instrument.profile);
        require(bool(e.shipId) && bool(e.classId) && bool(e.componentId) &&
                    e.profileId == instrument.profile.id && e.workdays > 0 && e.workdays <= 5 &&
                    groups.emplace(e.shipId.value, e.componentId.value).second,
                "Invalid or duplicate instrument provenance");
        require(e.dates.size() == (manual ? 1U : static_cast<std::size_t>(e.workdays)) &&
                    (!manual || e.workdays == 5) &&
                    std::adjacent_find(e.dates.begin(), e.dates.end(), std::greater_equal<>()) ==
                        e.dates.end(),
                "Invalid instrument exposure dates");
        for (auto day : e.dates)
            require(std::find(b.workDates.begin(), b.workDates.end(), day) != b.workDates.end(),
                    "Instrument exposure is outside actual fieldwork");
        require(instrument.channels.size() == instrument.profile.channels.size(),
                "Incomplete declared observation channels");
        for (std::size_t i = 0; i < instrument.channels.size(); ++i) {
            const auto& c = instrument.channels[i];
            require(c.mineral == instrument.profile.channels[i] &&
                        c.indication >= ResourceIndication::InsufficientExposure &&
                        c.indication <= ResourceIndication::Detected &&
                        c.accessibility >= AccessibilityReading::NotMeasured &&
                        c.accessibility <= AccessibilityReading::High,
                    "Invalid observation result or channel order");
            require((e.workdays < 5) == (c.indication == ResourceIndication::InsufficientExposure),
                    "Incomplete exposure cannot provide a full measurement");
            require((c.accessibility != AccessibilityReading::NotMeasured) ==
                        (instrument.profile.measuresAccessibility &&
                         c.indication == ResourceIndication::Detected),
                    "Accessibility is not supported by the recorded method and result");
        }
    }
}
} // namespace deep
