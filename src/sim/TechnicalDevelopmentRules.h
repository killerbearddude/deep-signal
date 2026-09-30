#pragma once

// Pure readiness, artifact lookup, and stage-accounting rules for the single
// authored P5 opportunity. These functions never inspect hidden candidate truth.
#include "sim/GameState.h"

namespace deep {

struct OpeningProgramContext;

inline constexpr double kConceptEngineeringWork = 5.0;
inline constexpr double kPrototypeFabricationWork = 4.0;
inline constexpr double kPrototypeTestingWork = 3.0;
inline constexpr double kProductionQualificationWork = 4.0;
inline constexpr double kSupportQualificationWork = 2.0;

enum class TechnicalWaitCause {
    None,
    Complete,
    Suspended,
    NoFacility,
    FacilityCapacity,
    NoLeader,
    NoEngineeringTeam,
    TeamControlled,
    TeamLocation,
    MissingEngineeringQualification,
    MaterialStock,
    MaterialFloor,
    MaterialAllowance,
    PerformanceDecision,
    NumericLimit
};
struct TechnicalReadiness {
    bool canWork = false;
    TechnicalWaitCause cause = TechnicalWaitCause::Complete;
    std::string explanation;
    double work = 0.0;
    ProcessedMaterialSet consumed;
    std::optional<TechnicalFacilityId> facilityId;
    std::optional<MaintenanceTeamId> teamId;
};

// Returns the full paid bill for one complete stage; execution scales it by work.
[[nodiscard]] ProcessedMaterialSet technicalStageCost(TechnicalDevelopmentStage stage);
// Returns the engineering/facility workdays required by one complete stage.
[[nodiscard]] double technicalStageRequiredWork(TechnicalDevelopmentStage stage) noexcept;
// Engineering expertise is independent from equipment-family service training.
[[nodiscard]] bool teamHasEngineeringQualification(const MaintenanceTeam&, EngineeringQualification) noexcept;
// Starts new intent from completed global artifacts, never cancelled partial work.
[[nodiscard]] TechnicalDevelopmentStage firstMissingTechnicalStage(const GameState&, TechnologyOpportunityId,
                                                                   ColonyId, std::optional<MaintenanceTeamId>,
                                                                   TechnicalDevelopmentScope);
// Projects one next-opening step from public artifacts and actual resources.
[[nodiscard]] TechnicalReadiness
technicalDevelopmentReadiness(const GameState&, const TechnicalDevelopmentProgram&,
                              const OpeningProgramContext* opening = nullptr);
// Checks structural intent; missing executable resources remain valid waiting state.
[[nodiscard]] std::optional<std::string>
validateTechnicalDevelopmentCharter(const GameState&, const TechnicalDevelopmentCharter&,
                                    bool amendment = false);
// Produces display wording from typed readiness without exposing candidate truth.
[[nodiscard]] std::string technicalDevelopmentCondition(const GameState&, const TechnicalDevelopmentProgram&);

// Resolve immutable demonstration provenance by component or opportunity.
[[nodiscard]] const DevelopedComponentRevision* developedRevisionForComponent(const GameState&,
                                                                              ShipComponentId) noexcept;
[[nodiscard]] const DevelopedComponentRevision*
developedRevisionForOpportunity(const GameState&, TechnologyOpportunityId) noexcept;
// Local serial production is effective only at/after its explicit available day.
[[nodiscard]] bool serialProductionAvailable(const GameState&, ShipComponentId, ColonyId,
                                             std::int64_t day) noexcept;
// Returns unreserved, demonstrated, effective physical units at the exact colony.
[[nodiscard]] std::vector<PrototypeComponentUnitId> availablePrototypeUnits(const GameState&, ShipComponentId,
                                                                            ColonyId, std::int64_t day);
// Established skill is effective immediately; P5-earned support observes D+1.
[[nodiscard]] bool teamHasEffectiveSupportQualification(const GameState&, MaintenanceTeamId,
                                                        EquipmentFamilyId, std::int64_t day) noexcept;

} // namespace deep
