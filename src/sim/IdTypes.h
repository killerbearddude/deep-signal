#pragma once

// Defines strongly typed integer IDs for simulation entities.
// IDs are stable handles into GameState-owned vectors; they do not imply pointer
// ownership and are persisted as integer values in SQLite rows.

#include <cstdint>

namespace deep {

// Lightweight tagged identifier used to prevent accidental cross-use of entity IDs.
// A value of 0 is reserved as invalid/unassigned; valid IDs are positive.
template <typename Tag>
struct Id {
    std::int64_t value = 0;

    constexpr auto operator<=>(const Id&) const = default;

    // Returns true when the ID is assigned. This is useful for validation paths
    // without allowing implicit conversion back to the raw integer value.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value > 0; }
};

// Empty tag types make each ID alias a distinct C++ type at compile time.
struct StarSystemTag;
struct BodyTag;
struct ColonyTag;
struct InstitutionTag;
struct PersonTag;
struct ShipClassTag;
struct ShipComponentTag;
struct ShipTag;
struct FleetTag;
struct ShipyardOrderTag;
struct SurveyProgramTag;
struct FreightProgramTag;
struct EquipmentFamilyTag;
struct MaintenanceTeamTag;
struct MaintenanceProgramTag;
struct SurveyTeamTag;
struct EventTag;
struct MeasurementProfileTag;
struct ObservationBatchTag;
struct AnalysisProgramTag;
struct AnalysisJobTag;
struct AssessmentTag;
struct SiteTag;
struct SiteDevelopmentProgramTag;

using StarSystemId = Id<StarSystemTag>;
using BodyId = Id<BodyTag>;
using ColonyId = Id<ColonyTag>;
using InstitutionId = Id<InstitutionTag>;
using PersonId = Id<PersonTag>;
using ShipClassId = Id<ShipClassTag>;
using ShipComponentId = Id<ShipComponentTag>;
using ShipId = Id<ShipTag>;
using FleetId = Id<FleetTag>;
using ShipyardOrderId = Id<ShipyardOrderTag>;
using SurveyProgramId = Id<SurveyProgramTag>;
using FreightProgramId = Id<FreightProgramTag>;
using EquipmentFamilyId = Id<EquipmentFamilyTag>;
using MaintenanceTeamId = Id<MaintenanceTeamTag>;
using MaintenanceProgramId = Id<MaintenanceProgramTag>;
using SurveyTeamId = Id<SurveyTeamTag>;
using EventId = Id<EventTag>;
using MeasurementProfileId = Id<MeasurementProfileTag>;
using ObservationBatchId = Id<ObservationBatchTag>;
using AnalysisProgramId = Id<AnalysisProgramTag>;
using AnalysisJobId = Id<AnalysisJobTag>;
using AssessmentId = Id<AssessmentTag>;
using SiteId = Id<SiteTag>;
using SiteDevelopmentProgramId = Id<SiteDevelopmentProgramTag>;

} // namespace deep
