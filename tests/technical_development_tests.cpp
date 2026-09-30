// Focused P5 engineering proof: public/hidden separation, exact paid stages,
// deterministic test evidence, reusable catalog publication, and one physical
// prototype-backed hull without double-charging its embodied component.
#include "sim/GameStateValidation.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/ShipDesignRules.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 1e-8, message);
}
void requireThrows(const char* message, const auto& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
struct FixtureIds {
    TechnologyOpportunityId opportunity;
    TechnicalFacilityId facility;
    MaintenanceTeamId team;
    ColonyId colony;
    PersonId leader;
};
FixtureIds ids(const GameState& state) {
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    return {state.technologyOpportunities.front().id, state.technicalFacilities.front().id, team->id,
            state.technicalFacilities.front().colonyId, state.people.front().id};
}
GameState fixture(double achieved = 6.0) {
    auto state = createHomeSystemScenario();
    state.technologyCandidateTruths.front().achievedDetectionThreshold = achieved;
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
        return row.id == state.technicalFacilities.front().colonyId;
    });
    colony.processedStockpile.amount.fill(2'000.0);
    colony.processorCapacity = 0.0;
    return state;
}
TechnicalDevelopmentCharter
charter(const GameState& state,
        TechnicalDevelopmentScope scope = TechnicalDevelopmentScope::ProductionAndSupportReady) {
    const auto key = ids(state);
    TechnicalDevelopmentCharter result;
    result.name = "Precision characterization development";
    result.opportunityId = key.opportunity;
    result.developmentColonyId = key.colony;
    result.requestedFacilityId = key.facility;
    result.requestedTeamId = key.team;
    result.requestedLeaderId = key.leader;
    result.scope = scope;
    return result;
}
void advanceOne(Simulation& simulation) {
    const auto result = simulation.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "one technical-development day must advance");
    validateGameState(simulation.state());
}
void exact_full_chain() {
    const auto initial = fixture();
    const auto key = ids(initial);
    const auto baseline = std::find_if(initial.shipComponents.begin(), initial.shipComponents.end(),
                                       [](const auto& row) { return row.name == "Specialist Survey Array"; });
    const auto baselineProfile =
        std::find_if(initial.measurementProfiles.begin(), initial.measurementProfiles.end(),
                     [&](const auto& row) { return row.id == *baseline->measurementProfileId; });
    require(baseline->mass == 30.0 && baseline->volume == 80.0 && baseline->powerDemand == 40.0 &&
                baseline->buildPoints == 70.0 &&
                baseline->buildCost.get(ProcessedMaterial::Electronics) == 40.0 &&
                baseline->serviceProfile->dutyCapacity == 120.0 &&
                baselineProfile->detectionThreshold == 10.0,
            "established Specialist comparator retains exact baseline values");
    const auto initialTeam = std::find_if(initial.maintenanceTeams.begin(), initial.maintenanceTeams.end(),
                                          [&](const auto& row) { return row.id == key.team; });
    require(!maintenanceTeamQualified(*initialTeam, baseline->serviceProfile->familyId),
            "development expertise does not initially confer specialist maintenance skill");
    const auto before = std::find_if(initial.colonies.begin(), initial.colonies.end(), [&](const auto& row) {
                            return row.id == key.colony;
                        })->processedStockpile;
    Simulation simulation(initial);
    require(simulation.execute(CreateTechnicalDevelopmentCommand{charter(initial)}).ok,
            "full-scope development intent accepted");
    for (int day = 0; day < 19; ++day)
        advanceOne(simulation);
    const auto& state = simulation.state();
    const auto& program = state.technicalDevelopmentPrograms.front();
    require(program.lifecycle == TechnicalDevelopmentLifecycle::Closed &&
                program.closure == TechnicalDevelopmentClosure::Completed,
            "18 paid workdays close on the following opening");
    double work = 0.0;
    ProcessedMaterialSet consumed;
    for (const auto& receipt : program.receipts) {
        work += receipt.work;
        consumed.addSet(receipt.consumed);
    }
    near(work, 18.0, "full reference scope uses exactly 18 engineering workdays");
    near(consumed.get(ProcessedMaterial::StructuralAlloys), 40.0, "full scope alloys");
    near(consumed.get(ProcessedMaterial::Electronics), 145.0, "full scope electronics");
    near(consumed.get(ProcessedMaterial::IndustrialComposites), 61.0, "full scope composites");
    const auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                       [&](const auto& row) { return row.id == key.colony; });
    near(before.get(ProcessedMaterial::Electronics) -
             colony.processedStockpile.get(ProcessedMaterial::Electronics),
         145.0, "actual Electronics debit matches receipts");
    require(state.prototypeDesigns.size() == 1 && state.prototypeComponentUnits.size() == 1 &&
                state.technicalTestRecords.size() == 3 && state.developedComponentRevisions.size() == 1 &&
                state.componentProductionCapabilities.size() == 1 &&
                state.supportQualificationRecords.size() == 1,
            "each completed stage creates one bounded durable artifact");
    const auto& developed = state.developedComponentRevisions.front();
    const auto& component = *std::find_if(state.shipComponents.begin(), state.shipComponents.end(),
                                          [&](const auto& row) { return row.id == developed.componentId; });
    const auto* profile =
        std::find_if(state.measurementProfiles.begin(), state.measurementProfiles.end(),
                     [&](const auto& row) { return row.id == developed.measurementProfileId; })
            .operator->();
    near(profile->detectionThreshold, 6.0, "demonstrated threshold comes from test truth");
    require(component.mass == 35.0 && component.volume == 100.0 && component.powerDemand == 55.0 &&
                component.buildPoints == 90.0 &&
                component.buildCost.get(ProcessedMaterial::Electronics) == 60.0 &&
                component.buildCost.get(ProcessedMaterial::IndustrialComposites) == 10.0 &&
                component.serviceProfile->dutyCapacity == 90.0 &&
                component.serviceProfile->teamWorkdaysPerDuty == 0.25 &&
                component.serviceProfile->materialsPerDuty.get(ProcessedMaterial::Electronics) == 0.75,
            "demonstrated component publishes exact frozen P5 tradeoffs");
    require(state.componentProductionCapabilities.front().availableDay ==
                state.componentProductionCapabilities.front().qualifiedDay + 1,
            "local production begins on the next opening");
    const auto* team =
        std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(), [&](const auto& row) {
            return row.id == key.team;
        }).operator->();
    require(maintenanceTeamQualified(*team, baseline->serviceProfile->familyId),
            "support work qualifies the exact real team for the specialist family");
    require(state.ships.empty() && state.fleets.empty() &&
                state.equipmentFamilies.size() == initial.equipmentFamilies.size() &&
                state.shipComponents.size() == initial.shipComponents.size() + 1,
            "support qualification creates no tender, workshop, supplies, or duplicate family");
}
void performance_miss_and_public_isolation() {
    const auto good = fixture(6.0), disappointing = fixture(9.0);
    require(good.technologyOpportunities.front().targetDetectionThreshold ==
                    disappointing.technologyOpportunities.front().targetDetectionThreshold &&
                technicalStageCost(TechnicalDevelopmentStage::PrototypeTesting).amount ==
                    technicalStageCost(TechnicalDevelopmentStage::PrototypeTesting).amount,
            "public opportunity/work estimates are equal before tests");
    Simulation simulation(disappointing);
    require(simulation
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(disappointing, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "disappointing deterministic opportunity remains valid intent");
    const auto result = simulation.advanceDaysDetailed(20);
    require(result.interrupted && result.advancedDays == 12,
            "performance miss interrupts exactly after the third real test");
    const auto& program = simulation.state().technicalDevelopmentPrograms.front();
    require(program.issue.message.find("9.000000") != std::string::npos &&
                program.issue.message.find("7.000000") != std::string::npos,
            "issue reports observed threshold and public target");
    require(simulation.state().developedComponentRevisions.size() == 1,
            "target miss does not delete demonstrated physical capability");
    require(
        simulation.execute(AcknowledgeTechnicalDevelopmentIssueCommand{program.id, program.issue.signature})
            .ok,
        "player can acknowledge disappointing evidence");
    advanceOne(simulation);
    require(simulation.state().technicalDevelopmentPrograms.front().lifecycle ==
                TechnicalDevelopmentLifecycle::Closed,
            "acknowledged demonstration-only program closes without forced production");
}
void prototype_backed_shipyard() {
    auto state = fixture();
    Simulation development(state);
    require(development
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(state, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "prototype-only program accepted");
    for (int day = 0; day < 13; ++day)
        advanceOne(development);
    state = development.state();
    const auto component = state.developedComponentRevisions.front().componentId;
    const auto colonyId = state.technicalFacilities.front().colonyId;
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == colonyId; });
    colony.shipyardCapacity = 0.0;
    std::vector<ShipComponentInstall> installs = referenceSurveyCutterComponents();
    installs.at(3).componentId = component;
    Simulation simulation(state);
    require(simulation
                .execute(CreateShipClassRevisionCommand{"Precision prototype cutter", ShipRole::Survey,
                                                        std::nullopt, installs})
                .ok,
            "demonstrated component is a normal immutable design option");
    const auto classId = simulation.state().shipClasses.back().id;
    require(simulation.execute(AssignShipyardBuildCommand{colonyId, classId, 1}).ok,
            "unproducible developed-component order is retained");
    advanceOne(simulation);
    require(simulation.state().prototypeComponentUnits.front().state == PrototypeComponentState::Available &&
                !simulation.state().shipyardOrders.back().currentHullSupplyPlan,
            "zero yard capacity reserves no prototype");
    auto mutableState = simulation.state();
    auto& mutableColony = *std::find_if(mutableState.colonies.begin(), mutableState.colonies.end(),
                                        [&](const auto& row) { return row.id == colonyId; });
    mutableColony.shipyardCapacity = 100.0;
    const auto electronicsBefore = mutableColony.processedStockpile.get(ProcessedMaterial::Electronics);
    Simulation building(mutableState);
    advanceOne(building);
    require(building.state().prototypeComponentUnits.front().state ==
                    PrototypeComponentState::ReservedForShipyard &&
                building.state().shipyardOrders.back().currentHullSupplyPlan,
            "first positive build capacity binds and reserves the exact prototype");
    for (int day = 0; day < 10 && building.state().ships.empty(); ++day)
        advanceOne(building);
    require(building.state().ships.size() == 1 &&
                building.state().prototypeComponentUnits.front().state == PrototypeComponentState::Consumed &&
                building.state().prototypeIntegrationReceipts.size() == 1,
            "prototype is integrated and consumed exactly once");
    const auto& completedColony =
        *std::find_if(building.state().colonies.begin(), building.state().colonies.end(),
                      [&](const auto& row) { return row.id == colonyId; });
    near(electronicsBefore - completedColony.processedStockpile.get(ProcessedMaterial::Electronics), 40.0,
         "prototype hull does not pay the embodied 60 Electronics twice");
}
void malformed_and_every_stage_suspension() {
    auto invalid = fixture();
    Simulation rejected(invalid);
    auto bad = charter(invalid);
    bad.opportunityId = TechnologyOpportunityId{99999};
    require(!rejected.execute(CreateTechnicalDevelopmentCommand{bad}).ok &&
                rejected.state().technicalDevelopmentPrograms.empty(),
            "invalid opportunity rejects without partial program mutation");
    bad = charter(invalid);
    bad.policy.floors.set(ProcessedMaterial::Electronics, std::numeric_limits<double>::quiet_NaN());
    require(!rejected.execute(CreateTechnicalDevelopmentCommand{bad}).ok &&
                rejected.state().technicalDevelopmentPrograms.empty(),
            "nonfinite technical authority rejects without partial mutation");
    auto duplicate = fixture();
    duplicate.technologyCandidateTruths.push_back(duplicate.technologyCandidateTruths.front());
    requireThrows("duplicate candidate truth was accepted", [&] { Simulation malformed(duplicate); });

    for (auto target :
         {TechnicalDevelopmentStage::ConceptEngineering, TechnicalDevelopmentStage::PrototypeFabrication,
          TechnicalDevelopmentStage::PrototypeTesting, TechnicalDevelopmentStage::ProductionQualification,
          TechnicalDevelopmentStage::SupportQualification}) {
        auto state = fixture();
        Simulation simulation(state);
        require(simulation.execute(CreateTechnicalDevelopmentCommand{charter(state)}).ok,
                "stage suspension fixture accepted");
        for (int day = 0; day < 30 && simulation.state().technicalDevelopmentPrograms.front().stage != target;
             ++day)
            advanceOne(simulation);
        auto& program = simulation.state().technicalDevelopmentPrograms.front();
        require(program.stage == target, "fixture reached requested technical stage");
        if (program.stageWork == 0.0)
            advanceOne(simulation);
        const auto id = simulation.state().technicalDevelopmentPrograms.front().id;
        const double work = simulation.state().technicalDevelopmentPrograms.front().stageWork;
        const auto consumed = simulation.state().technicalDevelopmentPrograms.front().stageConsumed;
        require(simulation.execute(SuspendTechnicalDevelopmentCommand{id}).ok, "partial stage suspends");
        require(simulation.advanceDaysDetailed(3).advancedDays == 3,
                "suspended technical intent does not stop world time");
        require(simulation.state().technicalDevelopmentPrograms.front().stageWork == work &&
                    simulation.state().technicalDevelopmentPrograms.front().stageConsumed.amount ==
                        consumed.amount,
                "suspension preserves partial work/materials exactly");
        require(simulation.execute(ResumeTechnicalDevelopmentCommand{id}).ok, "partial stage resumes");
        advanceOne(simulation);
        require(simulation.state().technicalDevelopmentPrograms.front().stage != target ||
                    simulation.state().technicalDevelopmentPrograms.front().stageWork > work,
                "resumed stage continues without replay or free completion");
    }
}
} // namespace

int main() {
    try {
        exact_full_chain();
        performance_miss_and_public_isolation();
        prototype_backed_shipyard();
        malformed_and_every_stage_suspension();
        std::cout << "Technical development: 3 focused scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical development failure: " << error.what() << '\n';
        return 1;
    }
}
