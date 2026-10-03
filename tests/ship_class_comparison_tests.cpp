// Comparison projections expose acquired design consequences and current physical
// readiness. These regressions keep quantity semantics and hidden technical truth
// separate, and verify inspection cannot change durable gameplay state.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace deep;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 1e-8, message);
}

template <class Rows, class Predicate> const auto& first(const Rows& rows, Predicate predicate) {
    const auto found = std::find_if(rows.begin(), rows.end(), predicate);
    if (found == rows.end())
        throw std::runtime_error("Comparison fixture row is missing");
    return *found;
}

const ShipClass& classNamed(const GameState& state, const char* name) {
    return first(state.shipClasses, [&](const auto& row) { return row.name == name; });
}

const ShipComponentDefinition& componentNamed(const GameState& state, const char* name) {
    return first(state.shipComponents, [&](const auto& row) { return row.name == name; });
}

ShipClassComparisonSummary comparison(const SimulationService& service, ShipClassId id) {
    const auto result = SimulationQueries{service}.shipClassComparison(id);
    require(result.has_value(), "known immutable class must remain comparable");
    return *result;
}

void checkMaterialRows(const std::vector<ProcessedMaterialStockpileSummary>& rows,
                       const ProcessedMaterialSet& expected) {
    require(rows.size() == processedMaterialCount(), "material rows include every enum entry, even zero");
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto material = static_cast<ProcessedMaterial>(index);
        require(rows[index].material == material && rows[index].materialName == toString(material),
                "material identities and resolved names retain stable enum order");
        near(rows[index].amount, expected.get(material), "material amount matches authoritative recipe");
    }
}

void checkDesign(const ShipClassComparisonSummary& row, const GameState& state) {
    const auto& source = first(state.shipClasses, [&](const auto& value) { return value.id == row.id; });
    auto expected = row;
    expected.design = evaluateShipDesign(state.shipComponents, source.components);
    require(row == expected, "all projected physical fields match the authoritative design evaluator");
    checkMaterialRows(row.buildMaterials, expected.design.buildCost);
}

void established_and_command_earned_precision(const GameState& state) {
    const SimulationService service(state);
    const auto& established = classNamed(state, "Established Characterization Cutter");
    const auto& precision = classNamed(state, "Precision Characterization Cutter");
    const auto a = comparison(service, established.id), b = comparison(service, precision.id);
    require(a.name == established.name && a.id == established.id && a.revision == established.revision &&
                a.roleName == "Survey" && b.name == precision.name && b.revision == precision.revision,
            "comparison resolves immutable identity, revision, and role");
    checkDesign(a, state);
    checkDesign(b, state);
    require(a.instruments.size() == 1 && b.instruments.size() == 1 && a.instruments.front().measurement &&
                b.instruments.front().measurement && a.instruments.front().service &&
                b.instruments.front().service,
            "both characterization designs expose acquired measurement and service profiles");
    const auto& establishedInstrument = a.instruments.front();
    const auto& precisionInstrument = b.instruments.front();
    const auto& developed = state.developedComponentRevisions.front();
    const auto& profile = first(state.measurementProfiles,
                                [&](const auto& row) { return row.id == developed.measurementProfileId; });
    require(precisionInstrument.componentId == developed.componentId &&
                precisionInstrument.measurement->profileName == profile.name &&
                precisionInstrument.measurement->methodVersion == profile.methodVersion,
            "Precision comparison uses the acquired profile catalog and exact installed component");
    near(establishedInstrument.measurement->detectionThreshold, 10.0, "established threshold is 10");
    near(precisionInstrument.measurement->detectionThreshold, 6.0, "demonstrated threshold is 6");
    require(precisionInstrument.measurement->detectionThreshold !=
                state.technologyOpportunities.front().targetDetectionThreshold,
            "demonstrated performance must not be replaced by the public target");
    require(establishedInstrument.measurement->measuresAccessibility &&
                establishedInstrument.measurement->accessibilityName == "Coarse" &&
                precisionInstrument.measurement->accessibilityName == "Coarse",
            "accessibility capability has an explicit human-readable meaning");
    const auto& family = first(state.equipmentFamilies, [&](const auto& row) {
        return row.id == componentNamed(state, "Specialist Survey Array").serviceProfile->familyId;
    });
    require(establishedInstrument.service->familyName == family.name &&
                precisionInstrument.service->familyName == family.name,
            "equipment-family identity is resolved before reaching the UI");
    near(establishedInstrument.service->dutyCapacityPerUnit, 120.0, "established unit duty is 120");
    near(precisionInstrument.service->totalDutyCapacity, 90.0, "Precision total duty is 90");
    near(establishedInstrument.service->teamWorkdaysPerRestoredDutyPerUnit, 0.2,
         "established service work stays per restored duty per unit");
    near(precisionInstrument.service->teamWorkdaysPerRestoredDutyPerUnit, 0.25,
         "Precision acquired service work is exposed");
    near(b.design.dryMass - a.design.dryMass, 5.0, "Precision mass consequence");
    near(b.design.usedVolume - a.design.usedVolume, 20.0, "Precision volume consequence");
    near(b.design.powerDemand - a.design.powerDemand, 15.0, "Precision power consequence");
    near(b.design.buildPoints - a.design.buildPoints, 20.0, "Precision construction work consequence");
    near(b.design.buildCost.get(ProcessedMaterial::Electronics) -
             a.design.buildCost.get(ProcessedMaterial::Electronics),
         20.0, "Precision Electronics consequence");
    require(a.developedComponents.empty() && b.developedComponents.size() == 1,
            "established catalog components do not fabricate P5 qualification rows");
    const auto& readiness = b.developedComponents.front();
    const auto& colony = first(state.colonies, [&](const auto& row) {
        return row.id == state.componentProductionCapabilities.front().colonyId;
    });
    const auto& team = first(state.maintenanceTeams, [&](const auto& row) {
        return row.id == state.supportQualificationRecords.front().teamId;
    });
    require(readiness.serialProductionColonyNames == std::vector<std::string>{colony.name} &&
                readiness.supportQualifiedTeamNames == std::vector<std::string>{team.name},
            "developed production and support resolve the actual colony and real team names");
    require(readiness.prototypeAvailability.empty(), "an integrated prototype is no longer available stock");
    require(a.design.constructible && b.design.constructible && a.poweredSurveyCapability > 0 &&
                b.poweredSurveyCapability > 0,
            "reference designs expose constructibility and powered capability");
}

void multiple_instruments_quantities_and_workshops() {
    auto state = createHomeSystemScenario();
    const auto standard = componentNamed(state, "Wide-Area Survey Array");
    const auto specialist = componentNamed(state, "Specialist Survey Array");
    auto measurementOnly = standard;
    measurementOnly.id = ShipComponentId{state.ids.nextShipComponentId++};
    measurementOnly.name = "Unmanaged comparison instrument";
    measurementOnly.serviceProfile.reset();
    state.shipComponents.push_back(measurementOnly);
    auto workshop = componentNamed(state, "Standard Instrument Workshop");
    workshop.id = ShipComponentId{state.ids.nextShipComponentId++};
    workshop.name = "Two-family comparison workshop";
    workshop.workshopRates.push_back({specialist.serviceProfile->familyId, 0.5});
    state.shipComponents.push_back(workshop);
    auto installs = referenceSurveyCutterComponents();
    installs.at(3).quantity = 2;
    installs.push_back({specialist.id, 3});
    installs.push_back({measurementOnly.id, 1});
    installs.push_back({workshop.id, 2});
    SimulationService service(std::move(state));
    require(service
                .execute(CreateShipClassRevisionCommand{"Multiple physical instruments", ShipRole::Survey,
                                                        std::nullopt, installs})
                .ok,
            "valid overflow/power-deficit revision can be saved");
    const auto row = comparison(service, service.state().shipClasses.back().id);
    require(row.instruments.size() == 3 && row.instruments[0].componentId == standard.id &&
                row.instruments[1].componentId == specialist.id &&
                row.instruments[2].componentId == measurementOnly.id,
            "instrument rows preserve all installed types in class-installation order");
    require(row.instruments[0].quantity == 2 && row.instruments[1].quantity == 3 &&
                row.instruments[2].measurement && !row.instruments[2].service,
            "quantity and independent measurement/service presence remain explicit");
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& instrument = row.instruments[index];
        const auto& source = index == 0 ? standard : specialist;
        const auto& profile = first(service.state().measurementProfiles, [&](const auto& value) {
            return value.id == source.measurementProfileId;
        });
        require(instrument.measurement && instrument.service, "managed instrument retains both profiles");
        near(instrument.measurement->detectionThreshold, profile.detectionThreshold,
             "quantity does not improve an instrument's measurement threshold");
        near(instrument.service->dutyCapacityPerUnit, source.serviceProfile->dutyCapacity,
             "unit duty capacity remains the service profile value");
        near(instrument.service->totalDutyCapacity, source.serviceProfile->dutyCapacity * instrument.quantity,
             "installed quantity scales total duty capacity");
        near(instrument.service->teamWorkdaysPerRestoredDutyPerUnit,
             source.serviceProfile->teamWorkdaysPerDuty,
             "quantity must not alter per-unit restoration labor physics");
        checkMaterialRows(instrument.service->materialsPerDutyPerUnit,
                          source.serviceProfile->materialsPerDuty);
    }
    require(row.instruments[0].measurement->detectionThreshold !=
                row.instruments[1].measurement->detectionThreshold,
            "unlike instrument thresholds remain separate instead of averaged or selected");
    require(!row.instruments[0].measurement->measuresAccessibility &&
                row.instruments[0].measurement->accessibilityName == "Not measured",
            "reconnaissance must not claim unmeasured accessibility");
    require(row.workshops.size() == 1 && row.workshops.front().quantity == 2 &&
                row.workshops.front().rates.size() == 2,
            "multifamily workshop capacity remains explicit");
    for (std::size_t index = 0; index < workshop.workshopRates.size(); ++index) {
        const auto& source = workshop.workshopRates[index];
        const auto& rate = row.workshops.front().rates[index];
        const auto& family = first(service.state().equipmentFamilies,
                                   [&](const auto& value) { return value.id == source.familyId; });
        require(rate.familyName == family.name, "workshop family names are resolved in app projection");
        near(rate.teamWorkdaysPerDayPerUnit, source.teamWorkdaysPerDay,
             "workshop per-unit rate is unchanged");
        near(rate.totalTeamWorkdaysPerDay, source.teamWorkdaysPerDay * 2,
             "workshop total scales by quantity");
    }
    checkDesign(row, service.state());
    require(!row.design.constructible && !row.design.constraints.empty() && row.design.powerMargin < 0 &&
                row.design.surveyCapability > 0 && row.poweredSurveyCapability == 0,
            "overflow remains comparable while a power deficit disables installed survey capability");
}

void hidden_candidate_truth_does_not_change_comparison(const GameState& earned) {
    auto firstWorld = createHomeSystemScenario(), secondWorld = firstWorld;
    firstWorld.technologyCandidateTruths.front().achievedDetectionThreshold = 6.0;
    secondWorld.technologyCandidateTruths.front().achievedDetectionThreshold = 9.0;
    const SimulationService firstService(firstWorld), secondService(secondWorld);
    const SimulationQueries a(firstService), b(secondService);
    const auto classes = a.shipClasses();
    require(!classes.empty() && classes.size() == b.shipClasses().size(), "public class lists agree");
    for (const auto& cls : classes) {
        const auto left = a.shipClassComparison(cls.id), right = b.shipClassComparison(cls.id);
        require(left && right && *left == *right,
                "every player-facing comparison field must be independent of untested candidate truth");
        require(left->developedComponents.empty(), "comparison cannot invent an undemonstrated component");
    }

    // Acquired results retain their recorded method even if an unobserved
    // candidate fixture changes afterward; comparison must not resample it.
    auto changedTruth = earned;
    changedTruth.technologyCandidateTruths.front().achievedDetectionThreshold = 9.0;
    const SimulationService acquired(earned), altered(changedTruth);
    for (const auto& cls : earned.shipClasses)
        require(comparison(acquired, cls.id) == comparison(altered, cls.id),
                "acquired class performance remains frozen independently of current candidate truth");
}

void current_day_readiness_and_physical_prototype_stock() {
    auto state = createHomeSystemScenario();
    const auto facility = state.technicalFacilities.front();
    const auto engineer = first(state.maintenanceTeams,
                                [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& row) { return row.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(5'000.0);
    colony->processorCapacity = 0;
    colony->shipyardCapacity = 50;
    const std::string colonyName = colony->name;
    const TechnicalDevelopmentCharter charter{"Comparison readiness proof",
                                              state.technologyOpportunities.front().id,
                                              facility.colonyId,
                                              facility.id,
                                              engineer.id,
                                              state.people.front().id,
                                              TechnicalDevelopmentScope::ProductionAndSupportReady,
                                              {}};
    SimulationService service(std::move(state));
    require(service.execute(CreateTechnicalDevelopmentCommand{charter}).ok, "P5 intent is authorized");
    std::optional<ShipClassId> classId;
    bool futurePrototype = false, availablePrototype = false, reservedPrototype = false;
    bool consumedPrototype = false, futureProcess = false, effectiveProcess = false;
    bool futureSupport = false, effectiveSupport = false, ordered = false;
    for (int opening = 0; opening < 35; ++opening) {
        require(service.advanceDaysDetailed(1).advancedDays == 1, "one physical readiness opening advances");
        validateGameState(service.state());
        if (service.state().developedComponentRevisions.empty())
            continue;
        if (!classId) {
            auto installs = referenceSurveyCutterComponents();
            installs.at(3).componentId = service.state().developedComponentRevisions.front().componentId;
            require(service
                        .execute(CreateShipClassRevisionCommand{"Readiness precision cutter",
                                                                ShipRole::Survey, std::nullopt, installs})
                        .ok,
                    "demonstrated component permits an immutable class before readiness is effective");
            classId = service.state().shipClasses.back().id;
        }
        const auto row = comparison(service, *classId);
        require(row.design.constructible && row.developedComponents.size() == 1,
                "production/support readiness never becomes a design-admission constraint");
        const auto& readiness = row.developedComponents.front();
        const auto& prototype = service.state().prototypeComponentUnits.front();
        if (*prototype.availableDay > service.state().date.day) {
            futurePrototype = true;
            require(readiness.prototypeAvailability.empty(), "tomorrow's prototype is not available today");
        } else if (prototype.state == PrototypeComponentState::Available) {
            availablePrototype = true;
            require(readiness.prototypeAvailability.size() == 1 &&
                        readiness.prototypeAvailability.front().colonyName == colonyName &&
                        readiness.prototypeAvailability.front().quantityAvailable == 1,
                    "one physical local prototype is counted once under its resolved colony name");
            if (!ordered) {
                require(readiness.serialProductionColonyNames.empty(),
                        "prototype hull starts before serial qualification");
                require(service.execute(AssignShipyardBuildCommand{facility.colonyId, *classId, 1}).ok,
                        "prototype-backed shipyard intent is accepted");
                ordered = true;
            }
        } else {
            reservedPrototype |= prototype.state == PrototypeComponentState::ReservedForShipyard;
            consumedPrototype |= prototype.state == PrototypeComponentState::Consumed;
            require(readiness.prototypeAvailability.empty(),
                    "reserved or consumed prototypes are not available inventory");
        }
        if (!service.state().componentProductionCapabilities.empty()) {
            if (service.state().componentProductionCapabilities.front().availableDay >
                service.state().date.day) {
                futureProcess = true;
                require(readiness.serialProductionColonyNames.empty(),
                        "same-day qualification is effective next opening");
            } else {
                effectiveProcess = true;
                require(readiness.serialProductionColonyNames == std::vector<std::string>{colonyName},
                        "effective production names the actual local colony");
            }
        }
        if (!service.state().supportQualificationRecords.empty()) {
            if (service.state().supportQualificationRecords.front().availableDay > service.state().date.day) {
                futureSupport = true;
                require(readiness.supportQualifiedTeamNames.empty(),
                        "support entry cannot bypass its D+1 effective date");
            } else {
                effectiveSupport = true;
                require(readiness.supportQualifiedTeamNames == std::vector<std::string>{engineer.name},
                        "effective support identifies the actual qualified team");
            }
        }
        if (consumedPrototype && effectiveSupport)
            break;
    }
    require(futurePrototype && availablePrototype && reservedPrototype && consumedPrototype &&
                futureProcess && effectiveProcess && futureSupport && effectiveSupport,
            "command-earned readiness proof visits future, effective, reserved, and consumed boundaries");
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "ship-comparison-XXXXXX").string();
        const auto created = mkdtemp(pattern.data());
        if (!created)
            throw std::runtime_error("Cannot create unique comparison snapshot directory");
        path = created;
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "snapshot file must be readable");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void missing_id_and_read_only_inspection(const GameState& state) {
    SimulationService service(state);
    const SimulationQueries queries(service);
    const TemporaryDirectory temporary;
    const auto before = temporary.path / "before.sqlite", after = temporary.path / "after.sqlite";
    require(service.saveGame(before).ok, "save pre-inspection durable state");
    const auto day = service.state().date.day;
    const auto events = service.state().eventLog.size();
    require(!queries.shipClassComparison(ShipClassId{9'999'999}), "missing class returns nullopt");
    for (const auto& cls : queries.shipClasses()) {
        const auto original = comparison(service, cls.id);
        auto editableCopy = original;
        editableCopy.name = "Local display copy";
        editableCopy.design.buildPoints = -1;
        editableCopy.buildMaterials.clear();
        editableCopy.instruments.clear();
        require(comparison(service, cls.id) == original, "comparison returns independent owned values");
    }
    require(service.state().date.day == day && service.state().eventLog.size() == events,
            "inspection emits no command audit or time advancement");
    require(service.saveGame(after).ok, "save post-inspection durable state");
    // Fresh deterministic snapshots cover all counters, stock, programs, class
    // compositions, shipyard binding, and physical prototype records together.
    require(fileBytes(before) == fileBytes(after), "comparison must not mutate any persisted gameplay field");
}
} // namespace

int main() {
    try {
        const auto earned = earnTechnicalDevelopmentFixture(6.0);
        established_and_command_earned_precision(earned);
        multiple_instruments_quantities_and_workshops();
        hidden_candidate_truth_does_not_change_comparison(earned);
        current_day_readiness_and_physical_prototype_stock();
        missing_id_and_read_only_inspection(earned);
        std::cout << "Ship-class comparison: 5 focused scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ship-class comparison failure: " << error.what() << '\n';
        return 1;
    }
}
