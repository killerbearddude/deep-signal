#pragma once

// Pure scientific interpretation. No GameState, deposits or physical queries
// are accepted; all conclusions are reproducible from sealed records alone.
#include "sim/Observation.h"
#include <span>

namespace deep {
enum class IndicationAssessment { Unknown, Indicated, NotDetectedWithinReportedLimits };
enum class AccessibilityAssessment { Unmeasured, Low, Moderate, High, Mixed };
enum class QuantityAssessment { Unmeasured };
enum class SiteAssessment { Unassessed };

// An independently inspectable method-specific finding. Semantic equivalence
// deliberately ignores installation, analyst, batch identity and date.
struct AnalyzedReading {
    Mineral mineral = Mineral::Iron;
    MeasurementProfileId profileId;
    int methodVersion = 1;
    double threshold = 0.0;
    bool measuresAccessibility = false;
    ResourceIndication indication = ResourceIndication::InsufficientExposure;
    AccessibilityReading accessibility = AccessibilityReading::NotMeasured;
    bool operator==(const AnalyzedReading&) const = default;
};
struct AnalysisFinding {
    AnalysisJobId jobId;
    ObservationBatchId batchId;
    BodyId bodyId;
    std::int64_t acquiredDay = 0;
    std::int64_t publishedDay = 0;
    std::vector<AnalyzedReading> readings;
    bool operator==(const AnalysisFinding&) const = default;
};
struct AssessedClaim {
    Mineral mineral = Mineral::Iron;
    IndicationAssessment indication = IndicationAssessment::Unknown;
    AccessibilityAssessment accessibility = AccessibilityAssessment::Unmeasured;
    QuantityAssessment quantity = QuantityAssessment::Unmeasured;
    SiteAssessment site = SiteAssessment::Unassessed;
    std::optional<std::int64_t> indicationDay;
    std::optional<std::int64_t> accessibilityDay;
    bool earlierIndication = false;
    // Includes all methods at the applicable latest epoch. Negative methods
    // remain qualifications to positive ones; different limits are not conflict.
    std::vector<AnalysisJobId> indicationInputs;
    std::vector<AnalysisJobId> accessibilityInputs;
    std::vector<AnalyzedReading> alternatives;
    bool operator==(const AssessedClaim&) const = default;
};
struct AssessmentRevision {
    AssessmentId id;
    BodyId bodyId;
    int revision = 1;
    int methodVersion = 1;
    std::int64_t publishedDay = 0;
    AnalysisJobId triggeringJobId;
    std::optional<AssessmentId> previousId;
    std::vector<AnalysisJobId> findingIds;
    std::vector<AssessedClaim> claims;
    bool contentChanged = true;
    bool repeatedEvidence = false;
    bool operator==(const AssessmentRevision&) const = default;
};
// Called only after three actual team-workdays. Does not publish or mutate data.
[[nodiscard]] AnalysisFinding interpretObservationBatch(const ObservationBatch& batch, AnalysisJobId job,
                                                        std::int64_t publicationDay);
// Builds one immutable revision from already completed findings in publication
// order. Older acquisition dates never displace newer scientific observations.
[[nodiscard]] AssessmentRevision assembleAssessmentRevision(std::span<const AnalysisFinding> findings,
                                                            BodyId body, AssessmentId id,
                                                            AnalysisJobId trigger, std::int64_t day,
                                                            const AssessmentRevision* previous);
// Shared science-content equality excludes provenance and acquisition chronology.
[[nodiscard]] bool equivalentEvidence(const AnalyzedReading& a, const AnalyzedReading& b);
} // namespace deep
