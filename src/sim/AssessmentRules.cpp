// Interprets only recorded outputs. Repetition preserves history without a
// certainty score, inferred reserve amount or hidden-truth tie breaker.
#include "sim/AssessmentRules.h"
#include "sim/ObservationRules.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace deep {
namespace {
template <class T> void uniqueAppend(std::vector<T>& values, T value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}
void semanticAppend(std::vector<AnalyzedReading>& values, const AnalyzedReading& value) {
    if (std::none_of(values.begin(), values.end(),
                     [&](const auto& r) { return equivalentEvidence(r, value); }))
        values.push_back(value);
}
bool sameContent(const AssessedClaim& a, const AssessedClaim& b) {
    if (a.mineral != b.mineral || a.indication != b.indication || a.accessibility != b.accessibility ||
        a.earlierIndication != b.earlierIndication || a.alternatives.size() != b.alternatives.size())
        return false;
    return std::all_of(a.alternatives.begin(), a.alternatives.end(), [&](const auto& reading) {
        return std::any_of(b.alternatives.begin(), b.alternatives.end(),
                           [&](const auto& other) { return equivalentEvidence(reading, other); });
    });
}
} // namespace
bool equivalentEvidence(const AnalyzedReading& a, const AnalyzedReading& b) {
    return a.mineral == b.mineral && a.profileId == b.profileId && a.methodVersion == b.methodVersion &&
           a.threshold == b.threshold && a.measuresAccessibility == b.measuresAccessibility &&
           a.indication == b.indication && a.accessibility == b.accessibility;
}
AnalysisFinding interpretObservationBatch(const ObservationBatch& b, AnalysisJobId job, std::int64_t day) {
    validateObservationBatch(b);
    if (!job || day < b.availableDay)
        throw std::runtime_error("Analysis precedes data availability");
    AnalysisFinding finding{job, b.id, b.bodyId, b.acquiredDay, day, {}};
    for (const auto& instrument : b.instruments)
        for (const auto& channel : instrument.channels)
            finding.readings.push_back(
                {channel.mineral, instrument.profile.id, instrument.profile.methodVersion,
                 instrument.profile.detectionThreshold, instrument.profile.measuresAccessibility,
                 channel.indication, channel.accessibility});
    return finding;
}
AssessmentRevision assembleAssessmentRevision(std::span<const AnalysisFinding> findings, BodyId body,
                                              AssessmentId id, AnalysisJobId trigger, std::int64_t day,
                                              const AssessmentRevision* previous) {
    if (!body || !id || !trigger || day < 0 ||
        (previous && (previous->bodyId != body || previous->revision == std::numeric_limits<int>::max())))
        throw std::runtime_error("Invalid assessment publication identity");
    AssessmentRevision result;
    result.id = id;
    result.bodyId = body;
    result.triggeringJobId = trigger;
    result.publishedDay = day;
    if (previous) {
        result.previousId = previous->id;
        result.revision = previous->revision + 1;
    }
    const AnalysisFinding* triggering = nullptr;
    for (const auto& f : findings) {
        if (f.bodyId != body)
            continue;
        if (f.publishedDay > day)
            throw std::runtime_error("Assessment includes future analytical work");
        uniqueAppend(result.findingIds, f.jobId);
        if (f.jobId == trigger)
            triggering = &f;
    }
    if (!triggering)
        throw std::runtime_error("Assessment lacks its completed triggering finding");
    // A repeated analysis or pass is identified by method/output equivalence,
    // independent of its later date, additional hardware or analyst identity.
    result.repeatedEvidence =
        !triggering->readings.empty() &&
        std::all_of(triggering->readings.begin(), triggering->readings.end(), [&](const auto& r) {
            return std::any_of(findings.begin(), findings.end(), [&](const auto& old) {
                return old.bodyId == body && old.jobId != trigger &&
                       std::any_of(old.readings.begin(), old.readings.end(),
                                   [&](const auto& earlier) { return equivalentEvidence(r, earlier); });
            });
        });
    for (std::size_t m = 0; m < mineralCount(); ++m) {
        AssessedClaim c;
        c.mineral = static_cast<Mineral>(m);
        for (const auto& f : findings)
            if (f.bodyId == body)
                for (const auto& r : f.readings)
                    if (r.mineral == c.mineral) {
                        if (r.indication != ResourceIndication::InsufficientExposure &&
                            (!c.indicationDay || f.acquiredDay > *c.indicationDay))
                            c.indicationDay = f.acquiredDay;
                        if (r.accessibility != AccessibilityReading::NotMeasured &&
                            (!c.accessibilityDay || f.acquiredDay > *c.accessibilityDay))
                            c.accessibilityDay = f.acquiredDay;
                    }
        std::set<AccessibilityReading> classes;
        bool earlierDetected = false;
        for (const auto& f : findings)
            if (f.bodyId == body)
                for (const auto& r : f.readings)
                    if (r.mineral == c.mineral) {
                        if (c.indicationDay && f.acquiredDay < *c.indicationDay &&
                            r.indication == ResourceIndication::Detected)
                            earlierDetected = true;
                        if (c.indicationDay == f.acquiredDay &&
                            r.indication != ResourceIndication::InsufficientExposure) {
                            uniqueAppend(c.indicationInputs, f.jobId);
                            semanticAppend(c.alternatives, r);
                            if (r.indication == ResourceIndication::Detected)
                                c.indication = IndicationAssessment::Indicated;
                            else if (c.indication == IndicationAssessment::Unknown)
                                c.indication = IndicationAssessment::NotDetectedWithinReportedLimits;
                        }
                        if (c.accessibilityDay == f.acquiredDay &&
                            r.accessibility != AccessibilityReading::NotMeasured) {
                            classes.insert(r.accessibility);
                            uniqueAppend(c.accessibilityInputs, f.jobId);
                            semanticAppend(c.alternatives, r);
                        }
                    }
        c.earlierIndication =
            earlierDetected && c.indication == IndicationAssessment::NotDetectedWithinReportedLimits;
        if (classes.size() > 1)
            c.accessibility = AccessibilityAssessment::Mixed;
        else if (!classes.empty())
            c.accessibility = static_cast<AccessibilityAssessment>(*classes.begin());
        result.claims.push_back(std::move(c));
    }
    result.contentChanged =
        !previous || previous->claims.size() != result.claims.size() ||
        !std::equal(result.claims.begin(), result.claims.end(), previous->claims.begin(), sameContent);
    return result;
}
} // namespace deep
