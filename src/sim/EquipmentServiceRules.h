#pragma once

// Pure survey-duty and instrument-service arithmetic. Profiles are campaign
// data, condition belongs to physical hulls, and recipes use processed units.
#include "sim/GameState.h"
#include <string>
#include <vector>

namespace deep {

inline constexpr double kEquipmentAbsoluteTolerance = 1e-9;
inline constexpr double kEquipmentRelativeTolerance = 1e-10;
// Comparison policy for representable conservative action deltas and history.
[[nodiscard]] bool equipmentNearlyEqual(double a, double b) noexcept;
// Local profile shape; the snapshot validator also verifies catalog references.
[[nodiscard]] bool validServiceProfile(const EquipmentServiceProfile& profile) noexcept;

// Initializes an empty NEW hull from its exact immutable revision. Throws for
// existing condition or invalid references; Load must read, never initialize.
void initializeShipEquipmentCondition(const GameState& state, Ship& ship);

struct OperatingDutyChange {
    ShipId shipId;
    ShipComponentId componentId;
    double beforeUsedDuty = 0.0;
    double afterUsedDuty = 0.0;
};
struct SurveyDutyEvaluation {
    double nominalCapability = 0.0;
    double poweredCapability = 0.0;
    double usableCapability = 0.0;
    double requiredDuty = 1.0;
    std::vector<OperatingDutyChange> changes{};
    // All actual contributors, including unmanaged instruments without a wear
    // delta. Exposure is earned by work, never inferred from the ending fleet.
    std::vector<InstrumentExposure> contributors{};
    std::string condition;
};

// D=1 for a timed fieldwork day, D=5 for an immediate complete manual pass.
// Every eligible powered row contributes; short-lived rows are not rotated.
[[nodiscard]] SurveyDutyEvaluation prepareSurveyDuty(const GameState& state, const Fleet& fleet,
                                                     double requiredDuty);
// Rechecks the prepared identities/before values before changing any condition.
// Called only at an authorized actual survey action; preview never applies it.
void applySurveyDuty(GameState& state, const SurveyDutyEvaluation& prepared);

// Per-hull family throughput: installed rates add only within this powered hull.
[[nodiscard]] double operationalWorkshopRate(const GameState& state, const Ship& ship,
                                             EquipmentFamilyId familyId);
// Qualification is explicit membership, independent from the team's location.
[[nodiscard]] bool maintenanceTeamQualified(const MaintenanceTeam& team, EquipmentFamilyId familyId);

struct EquipmentServicePlan {
    bool ready = false;
    std::string condition;
    ShipId shipId;
    ShipComponentId componentId;
    EquipmentFamilyId familyId;
    int quantity = 0;
    double beforeUsedDuty = 0.0;
    double afterUsedDuty = 0.0;
    double restoredDuty = 0.0;
    double teamWorkdays = 0.0;
    ProcessedMaterialSet consumed;
};

// Plans one installation-group repair with already bounded available materials.
// Quantity scales labor/materials, while restoredDuty is the per-unit condition.
[[nodiscard]] EquipmentServicePlan planEquipmentService(const GameState& state, ShipId shipId,
                                                        ShipComponentId componentId, double workshopRate,
                                                        double teamRate,
                                                        const ProcessedMaterialSet& available);

// Rejects missing/extra/out-of-order rows and invalid profiles/condition; valid
// wear and absent operational capability remain ordinary execution conditions.
void validateEquipmentState(const GameState& state);

} // namespace deep
