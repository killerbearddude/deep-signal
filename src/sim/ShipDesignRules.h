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
    ProcessedMaterialSet buildCost;
    double buildPoints = 0.0;
    bool constructible = false;
    std::vector<std::string> constraints;
};

struct FleetSurveyEvaluation {
    double installedCapability = 0.0;
    double operationalCapability = 0.0;
};

[[nodiscard]] bool validShipComponentDefinition(const ShipComponentDefinition& definition) noexcept;
[[nodiscard]] ShipDesignEvaluation evaluateShipDesign(
    const std::vector<ShipComponentDefinition>& catalog,
    const std::vector<ShipComponentInstall>& components);
[[nodiscard]] FleetSurveyEvaluation evaluateFleetSurvey(const GameState& state, const Fleet& fleet);

// Fixed starting catalog and reference composition. These are also used to
// translate legacy aggregate Survey Cutter saves into component revisions.
[[nodiscard]] std::vector<ShipComponentDefinition> standardShipComponentCatalog();
[[nodiscard]] std::vector<ShipComponentInstall> referenceSurveyCutterComponents();

} // namespace deep
