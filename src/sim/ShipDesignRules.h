#pragma once

// Pure ship-design calculations shared by simulation, forecasts, and UI queries.
// All quantities come from GameState catalog definitions; drafts are evaluated
// without mutating state or reserving construction inventory.

#include "sim/GameState.h"

#include <string>
#include <vector>

namespace deep {

struct ShipDesignEvaluation {
    double dryMass = 0.0;
    double usedVolume = 0.0;
    double volumeCapacity = 0.0;
    double availableVolume = 0.0;
    double powerGeneration = 0.0;
    double powerDemand = 0.0;
    double powerMargin = 0.0;
    double propellantCapacity = 0.0;
    double surveyCapability = 0.0;
    double cargoCapacity = 0.0;
    // Installed rate; operational handling is zero when powerMargin < 0.
    double cargoHandlingPerDay = 0.0;
    std::vector<WorkshopFamilyRate> workshopRates{};
    ProcessedMaterialSet buildCost;
    double buildPoints = 0.0;
    bool constructible = false;
    std::vector<std::string> constraints;
};

struct FleetSurveyEvaluation {
    double installedCapability = 0.0;
    double poweredCapability = 0.0;
    double operationalCapability = 0.0;
};

[[nodiscard]] bool validShipComponentDefinition(const ShipComponentDefinition& definition) noexcept;
[[nodiscard]] ShipDesignEvaluation evaluateShipDesign(
    const std::vector<ShipComponentDefinition>& catalog,
    const std::vector<ShipComponentInstall>& components);
// Required duty distinguishes a timed workday from an immediate complete pass.
[[nodiscard]] FleetSurveyEvaluation evaluateFleetSurvey(const GameState& state, const Fleet& fleet,
                                                       double requiredDuty = 1.0);

// Authoritative starting catalog and immutable reference compositions. Current
// development saves have no legacy aggregate conversion path.
[[nodiscard]] std::vector<ShipComponentDefinition> standardShipComponentCatalog();
[[nodiscard]] std::vector<ShipComponentInstall> referenceSurveyCutterComponents();
[[nodiscard]] std::vector<ShipComponentInstall> referenceFreighterComponents();
[[nodiscard]] std::vector<ShipComponentInstall> referenceTenderComponents();

} // namespace deep
