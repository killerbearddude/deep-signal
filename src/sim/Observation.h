#pragma once

// Immutable scientific records for P4A. These contain instrument outputs and
// acquisition provenance, never physical reserve amounts or deposit identities.
#include "sim/IdTypes.h"
#include "sim/Minerals.h"
#include <optional>
#include <string>
#include <vector>

namespace deep {

// Fictional normalized detection model, unrelated to service compatibility.
// Catalog revisions are immutable; observations retain the exact profile ID.
struct MeasurementProfile {
    MeasurementProfileId id;
    std::string name;
    int methodVersion = 1;
    double detectionThreshold = 50.0;
    bool measuresAccessibility = false;
    std::vector<Mineral> channels;
    bool operator==(const MeasurementProfile&) const = default;
};

enum class ObservationOrigin { TimedPass, ImmediateManual };
enum class ResourceIndication { InsufficientExposure, NotDetectedWithinLimit, Detected };
// High has the declared envelope [0.75, unbounded); no numeric infinity is saved.
enum class AccessibilityReading { NotMeasured, Low, Moderate, High };

struct ObservationChannel {
    Mineral mineral = Mineral::Iron;
    ResourceIndication indication = ResourceIndication::InsufficientExposure;
    AccessibilityReading accessibility = AccessibilityReading::NotMeasured;
    bool operator==(const ObservationChannel&) const = default;
};

// Installation quantities do not amplify exposure. Timed dates are unique and
// ordered; manual action uses a single date with five immediate exposure units.
struct InstrumentExposure {
    ShipId shipId;
    ShipClassId classId;
    ShipComponentId componentId;
    MeasurementProfileId profileId;
    int workdays = 0;
    std::vector<std::int64_t> dates;
    bool operator==(const InstrumentExposure&) const = default;
};

struct InstrumentObservation {
    InstrumentExposure exposure;
    // Snapshot is deliberate historical method provenance. Validation requires
    // agreement with the immutable catalog; analysis needs no world lookup.
    MeasurementProfile profile;
    std::vector<ObservationChannel> channels;
    bool operator==(const InstrumentObservation&) const = default;
};

struct ObservationBatch {
    ObservationBatchId id;
    BodyId bodyId;
    FleetId fleetId;
    ObservationOrigin origin = ObservationOrigin::TimedPass;
    std::optional<SurveyProgramId> surveyProgramId;
    std::optional<SurveyTeamId> teamId;
    int passNumber = 0;
    std::int64_t firstWorkDay = 0;
    std::int64_t acquiredDay = 0;
    std::int64_t availableDay = 1;
    // All actual fleet fieldwork dates, including days of partial contributors.
    std::vector<std::int64_t> workDates;
    std::vector<InstrumentObservation> instruments;
    bool operator==(const ObservationBatch&) const = default;
};

} // namespace deep
