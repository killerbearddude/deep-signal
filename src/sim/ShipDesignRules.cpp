#include "sim/ShipDesignRules.h"
#include "sim/EquipmentServiceRules.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace deep {
namespace {

[[nodiscard]] bool nonnegativeFinite(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] const ShipComponentDefinition* componentById(
    const std::vector<ShipComponentDefinition>& catalog, const ShipComponentId id) {
    const auto it = std::find_if(catalog.begin(), catalog.end(), [id](const auto& row) { return row.id == id; });
    return it == catalog.end() ? nullptr : &*it;
}

[[nodiscard]] ProcessedMaterialSet cost(const double alloys, const double electronics,
                                        const double reactorFuel, const double composites) {
    ProcessedMaterialSet result;
    result.set(ProcessedMaterial::StructuralAlloys, alloys);
    result.set(ProcessedMaterial::Electronics, electronics);
    result.set(ProcessedMaterial::ReactorFuel, reactorFuel);
    result.set(ProcessedMaterial::IndustrialComposites, composites);
    return result;
}

} // namespace

bool validShipComponentDefinition(const ShipComponentDefinition& definition) noexcept {
    if (!definition.id || definition.name.empty() ||
        definition.kind < ShipComponentKind::Hull || definition.kind > ShipComponentKind::Workshop ||
        !nonnegativeFinite(definition.mass) || !nonnegativeFinite(definition.volume) ||
        !nonnegativeFinite(definition.internalVolumeCapacity) ||
        !nonnegativeFinite(definition.powerGeneration) || !nonnegativeFinite(definition.powerDemand) ||
        !nonnegativeFinite(definition.propellantCapacity) ||
        !nonnegativeFinite(definition.surveyCapability) || !nonnegativeFinite(definition.cargoCapacity) ||
        !nonnegativeFinite(definition.cargoHandlingPerDay) || !nonnegativeFinite(definition.buildPoints)) {
        return false;
    }
    for (const double amount : definition.buildCost.amount) {
        if (!nonnegativeFinite(amount)) return false;
    }
    if (definition.serviceProfile && (!validServiceProfile(*definition.serviceProfile) ||
        definition.kind != ShipComponentKind::SurveySensor || definition.surveyCapability <= 0.0)) return false;
    std::unordered_set<std::int64_t> families;
    for (const auto& rate : definition.workshopRates) {
        if (definition.kind != ShipComponentKind::Workshop || !rate.familyId ||
            !nonnegativeFinite(rate.teamWorkdaysPerDay) || !families.insert(rate.familyId.value).second) return false;
    }
    return definition.kind == ShipComponentKind::Hull || definition.internalVolumeCapacity == 0.0;
}

ShipDesignEvaluation evaluateShipDesign(const std::vector<ShipComponentDefinition>& catalog,
                                        const std::vector<ShipComponentInstall>& components) {
    ShipDesignEvaluation result;
    std::int64_t hullCount = 0;
    std::unordered_set<std::int64_t> seen;
    for (const ShipComponentInstall& install : components) {
        if (!install.componentId || install.quantity <= 0 || !seen.insert(install.componentId.value).second) {
            result.constraints.push_back("Invalid or duplicate component installation");
            continue;
        }
        const ShipComponentDefinition* definition = componentById(catalog, install.componentId);
        if (definition == nullptr || !validShipComponentDefinition(*definition)) {
            result.constraints.push_back("Missing or invalid component definition");
            continue;
        }
        const double count = static_cast<double>(install.quantity);
        if (definition->kind == ShipComponentKind::Hull) hullCount += install.quantity;
        result.dryMass += definition->mass * count;
        result.usedVolume += definition->volume * count;
        result.volumeCapacity += definition->internalVolumeCapacity * count;
        result.powerGeneration += definition->powerGeneration * count;
        result.powerDemand += definition->powerDemand * count;
        result.propellantCapacity += definition->propellantCapacity * count;
        result.surveyCapability += definition->surveyCapability * count;
        result.cargoCapacity += definition->cargoCapacity * count;
        result.cargoHandlingPerDay += definition->cargoHandlingPerDay * count;
        for (const auto& rate : definition->workshopRates) {
            auto row = std::find_if(result.workshopRates.begin(), result.workshopRates.end(),
                                   [&](const auto& r) { return r.familyId == rate.familyId; });
            if (row == result.workshopRates.end()) result.workshopRates.push_back({rate.familyId, rate.teamWorkdaysPerDay * count});
            else row->teamWorkdaysPerDay += rate.teamWorkdaysPerDay * count;
        }
        result.buildPoints += definition->buildPoints * count;
        if (definition->serviceProfile) {
            const auto& profile=*definition->serviceProfile;
            if (!std::isfinite(count*profile.dutyCapacity*profile.teamWorkdaysPerDuty) ||
                std::any_of(profile.materialsPerDuty.amount.begin(),profile.materialsPerDuty.amount.end(),
                    [&](double amount){return !std::isfinite(count*profile.dutyCapacity*amount);})) {
                result.constraints.push_back("Derived service requirements exceed finite limits");
            }
        }
        for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
            result.buildCost.amount[i] += definition->buildCost.amount[i] * count;
        }
    }
    result.availableVolume = result.volumeCapacity - result.usedVolume;
    result.powerMargin = result.powerGeneration - result.powerDemand;
    if (hullCount != 1) result.constraints.push_back("Design requires exactly one hull");
    if (result.usedVolume > result.volumeCapacity) result.constraints.push_back("Internal volume exceeds hull capacity");
    if (!nonnegativeFinite(result.dryMass) || !nonnegativeFinite(result.usedVolume) ||
        !nonnegativeFinite(result.volumeCapacity) || !nonnegativeFinite(result.powerGeneration) ||
        !nonnegativeFinite(result.powerDemand) || !nonnegativeFinite(result.propellantCapacity) ||
        !nonnegativeFinite(result.surveyCapability) || !nonnegativeFinite(result.cargoCapacity) ||
        !nonnegativeFinite(result.cargoHandlingPerDay) || !nonnegativeFinite(result.buildPoints) ||
        std::any_of(result.workshopRates.begin(), result.workshopRates.end(),
                    [](const auto& r) { return !nonnegativeFinite(r.teamWorkdaysPerDay); }) ||
        std::any_of(result.buildCost.amount.begin(), result.buildCost.amount.end(),
                    [](double amount) { return !nonnegativeFinite(amount); })) {
        result.constraints.push_back("Derived design values exceed finite limits");
    }
    result.constructible = result.constraints.empty() && result.buildPoints > 0.0;
    if (result.constraints.empty() && result.buildPoints <= 0.0) {
        result.constraints.push_back("Design needs positive build points");
    }
    return result;
}

FleetSurveyEvaluation evaluateFleetSurvey(const GameState& state, const Fleet& fleet, double requiredDuty) {
    const auto duty = prepareSurveyDuty(state, fleet, requiredDuty);
    return {duty.nominalCapability, duty.poweredCapability, duty.usableCapability};
}

std::vector<ShipComponentDefinition> standardShipComponentCatalog() {
    return {
        ShipComponentDefinition{.id = ShipComponentId{1}, .name = "Compact Survey Hull",
            .kind = ShipComponentKind::Hull, .mass = 300.0, .internalVolumeCapacity = 1000.0,
            .buildCost = cost(200.0, 0.0, 0.0, 20.0), .buildPoints = 200.0},
        ShipComponentDefinition{.id = ShipComponentId{2}, .name = "Standard R-60 Reactor",
            .kind = ShipComponentKind::Reactor, .mass = 80.0, .volume = 120.0,
            .powerGeneration = 120.0, .buildCost = cost(0.0, 30.0, 20.0, 0.0), .buildPoints = 100.0},
        ShipComponentDefinition{.id = ShipComponentId{3}, .name = "Standard Propellant Tank",
            .kind = ShipComponentKind::PropellantTank, .mass = 50.0, .volume = 100.0,
            .propellantCapacity = 1000.0, .buildCost = cost(30.0, 0.0, 0.0, 10.0), .buildPoints = 80.0},
        ShipComponentDefinition{.id = ShipComponentId{4}, .name = "Wide-Area Survey Array",
            .kind = ShipComponentKind::SurveySensor, .mass = 30.0, .volume = 80.0,
            .powerDemand = 40.0, .surveyCapability = 1.0,
            .buildCost = cost(0.0, 40.0, 0.0, 0.0), .buildPoints = 70.0,
            .serviceProfile = EquipmentServiceProfile{EquipmentFamilyId{1}, 120.0, 0.2, cost(0.0, 0.5, 0.0, 0.5)}},
        ShipComponentDefinition{.id = ShipComponentId{5}, .name = "General Ship Systems",
            .kind = ShipComponentKind::Utility, .mass = 40.0, .volume = 50.0,
            .powerDemand = 20.0, .buildCost = cost(20.0, 10.0, 0.0, 20.0), .buildPoints = 50.0},
        ShipComponentDefinition{.id = ShipComponentId{6}, .name = "Standard Cargo Bay",
            .kind = ShipComponentKind::CargoBay, .mass = 60.0, .volume = 200.0,
            .powerDemand = 20.0, .cargoCapacity = 100.0, .cargoHandlingPerDay = 25.0,
            .buildCost = cost(40.0, 10.0, 0.0, 20.0), .buildPoints = 60.0},
        ShipComponentDefinition{.id = ShipComponentId{7}, .name = "Specialist Survey Array",
            .kind = ShipComponentKind::SurveySensor, .mass = 30.0, .volume = 80.0,
            .powerDemand = 40.0, .surveyCapability = 1.0,
            .buildCost = cost(0.0, 40.0, 0.0, 0.0), .buildPoints = 70.0,
            .serviceProfile = EquipmentServiceProfile{EquipmentFamilyId{2}, 120.0, 0.2, cost(0.0, 0.5, 0.0, 0.5)}},
        ShipComponentDefinition{.id = ShipComponentId{8}, .name = "Standard Instrument Workshop",
            .kind = ShipComponentKind::Workshop, .mass = 100.0, .volume = 200.0, .powerDemand = 30.0,
            .buildCost = cost(40.0, 20.0, 0.0, 20.0), .buildPoints = 100.0,
            .workshopRates = {{EquipmentFamilyId{1}, 1.0}}},
        ShipComponentDefinition{.id = ShipComponentId{9}, .name = "Specialist Instrument Workshop",
            .kind = ShipComponentKind::Workshop, .mass = 100.0, .volume = 200.0, .powerDemand = 30.0,
            .buildCost = cost(40.0, 20.0, 0.0, 20.0), .buildPoints = 100.0,
            .workshopRates = {{EquipmentFamilyId{2}, 1.0}}}
    };
}

std::vector<ShipComponentInstall> referenceSurveyCutterComponents() {
    return {{ShipComponentId{1}, 1}, {ShipComponentId{2}, 1}, {ShipComponentId{3}, 1},
            {ShipComponentId{4}, 1}, {ShipComponentId{5}, 1}};
}

std::vector<ShipComponentInstall> referenceFreighterComponents() {
    return {{ShipComponentId{1}, 1}, {ShipComponentId{2}, 1}, {ShipComponentId{3}, 1},
            {ShipComponentId{5}, 1}, {ShipComponentId{6}, 2}};
}

std::vector<ShipComponentInstall> referenceTenderComponents() {
    return {{ShipComponentId{1}, 1}, {ShipComponentId{2}, 1}, {ShipComponentId{3}, 1},
            {ShipComponentId{5}, 1}, {ShipComponentId{8}, 1}};
}

} // namespace deep
