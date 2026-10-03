// Projects one immutable design and its acquired equipment into owned display
// values. All totals come from the shared evaluator; current-day readiness uses
// existing P5 rules. This query neither selects supply nor samples hidden truth.
#include "app/SimulationQueries.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <tuple>

namespace deep {
namespace {
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

std::string roleName(ShipRole role) {
    switch (role) {
    case ShipRole::Survey:
        return "Survey";
    case ShipRole::Freighter:
        return "Freighter";
    case ShipRole::Escort:
        return "Escort";
    case ShipRole::Builder:
        return "Builder";
    }
    return "Unknown";
}

std::vector<ProcessedMaterialStockpileSummary> materialRows(const ProcessedMaterialSet& materials) {
    std::vector<ProcessedMaterialStockpileSummary> rows;
    rows.reserve(processedMaterialCount());
    for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
        const auto material = static_cast<ProcessedMaterial>(index);
        rows.push_back({material, std::string{toString(material)}, materials.get(material)});
    }
    return rows;
}

// Keep equality in the app projection rather than changing authoritative types
// for a display-only feature. Include all evaluator fields, including workshop
// rates that the default comparison scene does not happen to install.
bool sameDesign(const ShipDesignEvaluation& left, const ShipDesignEvaluation& right) {
    const auto fields = [](const ShipDesignEvaluation& row) {
        return std::tie(row.dryMass, row.usedVolume, row.volumeCapacity, row.availableVolume,
                        row.powerGeneration, row.powerDemand, row.powerMargin, row.propellantCapacity,
                        row.surveyCapability, row.cargoCapacity, row.cargoHandlingPerDay,
                        row.buildCost.amount, row.buildPoints, row.constructible, row.constraints);
    };
    return fields(left) == fields(right) &&
           std::equal(left.workshopRates.begin(), left.workshopRates.end(), right.workshopRates.begin(),
                      right.workshopRates.end(), [](const auto& a, const auto& b) {
                          return a.familyId == b.familyId && a.teamWorkdaysPerDay == b.teamWorkdaysPerDay;
                      });
}
} // namespace

bool ShipClassComparisonSummary::operator==(const ShipClassComparisonSummary& other) const {
    return std::tie(id, name, revision, roleName, poweredSurveyCapability, buildMaterials, instruments,
                    developedComponents, workshops) == std::tie(other.id, other.name, other.revision,
                                                                other.roleName, other.poweredSurveyCapability,
                                                                other.buildMaterials, other.instruments,
                                                                other.developedComponents, other.workshops) &&
           sameDesign(design, other.design);
}

std::optional<ShipClassComparisonSummary> SimulationQueries::shipClassComparison(ShipClassId id) const {
    const auto& state = service_.state();
    const auto* shipClass = find(state.shipClasses, id);
    if (!shipClass)
        return std::nullopt;

    ShipClassComparisonSummary row;
    row.id = shipClass->id;
    row.name = shipClass->name;
    row.revision = shipClass->revision;
    row.roleName = roleName(shipClass->role);
    row.design = evaluateShipDesign(state.shipComponents, shipClass->components);
    // Mirrors prepareSurveyDuty's class-power gate without fabricating a hull
    // or implying that this design has usable physical equipment condition.
    row.poweredSurveyCapability = row.design.powerMargin < 0.0 ? 0.0 : row.design.surveyCapability;
    row.buildMaterials = materialRows(row.design.buildCost);

    for (const auto& install : shipClass->components) {
        const auto* component = find(state.shipComponents, install.componentId);
        if (!component)
            continue;

        if (component->measurementProfileId || component->serviceProfile) {
            ShipClassInstrumentSummary instrument;
            instrument.componentId = component->id;
            instrument.componentName = component->name;
            instrument.quantity = install.quantity;
            if (component->measurementProfileId) {
                const auto* profile = find(state.measurementProfiles, *component->measurementProfileId);
                if (profile)
                    instrument.measurement = ShipClassMeasurementSummary{
                        profile->name, profile->methodVersion, profile->detectionThreshold,
                        profile->measuresAccessibility,
                        profile->measuresAccessibility ? "Coarse" : "Not measured"};
            }
            if (component->serviceProfile) {
                const auto& profile = *component->serviceProfile;
                const auto* family = find(state.equipmentFamilies, profile.familyId);
                instrument.service = ShipClassServiceSummary{
                    family ? family->name : "Unknown equipment family", profile.dutyCapacity,
                    profile.dutyCapacity * install.quantity, profile.teamWorkdaysPerDuty,
                    materialRows(profile.materialsPerDuty)};
            }
            row.instruments.push_back(std::move(instrument));
        }

        if (developedRevisionForComponent(state, component->id)) {
            ShipClassDevelopedComponentSummary developed;
            developed.componentId = component->id;
            developed.componentName = component->name;
            developed.quantity = install.quantity;
            // Stable colony/team order also coalesces multiple availability
            // records for one location without losing the physical unit count.
            for (const auto& colony : state.colonies) {
                if (serialProductionAvailable(state, component->id, colony.id, state.date.day))
                    developed.serialProductionColonyNames.push_back(colony.name);
                const auto prototypes =
                    availablePrototypeUnits(state, component->id, colony.id, state.date.day);
                if (!prototypes.empty())
                    developed.prototypeAvailability.push_back({colony.name, prototypes.size()});
            }
            if (component->serviceProfile)
                for (const auto& team : state.maintenanceTeams)
                    if (teamHasEffectiveSupportQualification(
                            state, team.id, component->serviceProfile->familyId, state.date.day))
                        developed.supportQualifiedTeamNames.push_back(team.name);
            row.developedComponents.push_back(std::move(developed));
        }

        if (!component->workshopRates.empty()) {
            ShipClassWorkshopSummary workshop;
            workshop.componentId = component->id;
            workshop.componentName = component->name;
            workshop.quantity = install.quantity;
            for (const auto& rate : component->workshopRates) {
                const auto* family = find(state.equipmentFamilies, rate.familyId);
                workshop.rates.push_back({family ? family->name : "Unknown equipment family",
                                          rate.teamWorkdaysPerDay,
                                          rate.teamWorkdaysPerDay * install.quantity});
            }
            row.workshops.push_back(std::move(workshop));
        }
    }
    return row;
}
} // namespace deep
