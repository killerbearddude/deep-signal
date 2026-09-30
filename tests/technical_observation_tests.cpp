// P5 technical evidence remains separate from physical sampling: the acquired
// demonstrated threshold drives ordinary observation rules without a bonus.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/ObservationRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
GameState fixture(double achieved) {
    auto state = createHomeSystemScenario();
    state.technologyCandidateTruths.front().achievedDetectionThreshold = achieved;
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
        return row.id == state.technicalFacilities.front().colonyId;
    });
    colony.processedStockpile.amount.fill(1'000.0);
    colony.processorCapacity = 0.0;
    return state;
}
TechnicalDevelopmentCharter charter(const GameState& state) {
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    return {"Observation method development",
            state.technologyOpportunities.front().id,
            state.technicalFacilities.front().colonyId,
            state.technicalFacilities.front().id,
            team->id,
            state.people.front().id,
            TechnicalDevelopmentScope::DemonstratePrototype,
            {}};
}
GameState demonstrate(double achieved) {
    auto state = fixture(achieved);
    Simulation simulation(state);
    require(simulation.execute(CreateTechnicalDevelopmentCommand{charter(state)}).ok,
            "observation-method development accepted");
    for (int day = 0; day < 13; ++day) {
        const auto& program = simulation.state().technicalDevelopmentPrograms.front();
        if (!program.issue.acknowledged)
            require(
                simulation
                    .execute(AcknowledgeTechnicalDevelopmentIssueCommand{program.id, program.issue.signature})
                    .ok,
                "controlled disappointing test evidence acknowledged");
        require(simulation.advanceDaysDetailed(1).advancedDays == 1,
                "observation method earns its next physical day");
    }
    return simulation.state();
}
ResourceIndication waterIce(const std::vector<ObservationChannel>& channels) {
    return std::find_if(channels.begin(), channels.end(),
                        [](const auto& row) { return row.mineral == Mineral::WaterIce; })
        ->indication;
}
void pretest_truth_isolation() {
    SimulationService good(fixture(6.0)), miss(fixture(9.0));
    SimulationQueries a(good), b(miss);
    const auto left = a.technologyOpportunities(), right = b.technologyOpportunities();
    require(left.size() == right.size() && left.front().opportunity.name == right.front().opportunity.name &&
                left.front().opportunity.targetDetectionThreshold ==
                    right.front().opportunity.targetDetectionThreshold &&
                left.front().status == right.front().status && !left.front().demonstratedThreshold &&
                !right.front().demonstratedThreshold,
            "pre-test opportunity projection does not reveal achieved candidate truth");
    const auto lp = a.previewTechnicalDevelopment(charter(good.state()));
    const auto rp = b.previewTechnicalDevelopment(charter(miss.state()));
    require(lp.structurallyValid == rp.structurallyValid && lp.condition == rp.condition &&
                lp.requiredWork == rp.requiredWork &&
                lp.requiredMaterials.amount == rp.requiredMaterials.amount,
            "pre-test program preview is identical across hidden candidate truths");
    const auto leftCatalog = a.shipComponents();
    const auto rightCatalog = b.shipComponents();
    require(leftCatalog.size() == rightCatalog.size() &&
                std::none_of(leftCatalog.begin(), leftCatalog.end(),
                             [](const auto& row) { return row.demonstrated; }),
            "pre-test component catalog has no future demonstrated component");
}
void demonstrated_threshold_drives_sampling() {
    auto reference = demonstrate(6.0);
    const auto opportunity = reference.technologyOpportunities.front();
    const auto& establishedComponent =
        *std::find_if(reference.shipComponents.begin(), reference.shipComponents.end(),
                      [&](const auto& row) { return row.id == opportunity.baselineComponentId; });
    const auto& established =
        *std::find_if(reference.measurementProfiles.begin(), reference.measurementProfiles.end(),
                      [&](const auto& row) { return row.id == *establishedComponent.measurementProfileId; });
    const auto& developed = *std::find_if(
        reference.measurementProfiles.begin(), reference.measurementProfiles.end(), [&](const auto& row) {
            return row.id == reference.developedComponentRevisions.front().measurementProfileId;
        });
    SimulationService catalogService(reference);
    const auto catalog = SimulationQueries{catalogService}.shipComponents();
    const auto catalogRow =
        std::find_if(catalog.begin(), catalog.end(), [](const auto& row) { return row.demonstrated; });
    require(catalogRow != catalog.end() && catalogRow->publicTargetThreshold == 7.0 &&
                catalogRow->measurementProfile->detectionThreshold == 6.0 &&
                catalogRow->testProvenance.size() == 3 && catalogRow->serialProductionColonies.empty() &&
                catalogRow->availablePrototypeColonies.size() == 1,
            "designer distinguishes demonstrated result, test provenance, prototype, and serial readiness");
    const BodyId body{999};
    const std::vector<MineralDeposit> physical{{body, Mineral::WaterIce, 7.0, 1.0}};
    require(waterIce(sampleObservationChannels(physical, body, established, 5)) ==
                    ResourceIndication::NotDetectedWithinLimit &&
                waterIce(sampleObservationChannels(physical, body, developed, 5)) ==
                    ResourceIndication::Detected,
            "same signal7 world differs only through installed test-derived threshold10 versus6");

    const auto disappointing = demonstrate(9.0);
    const auto& profile = *std::find_if(
        disappointing.measurementProfiles.begin(), disappointing.measurementProfiles.end(),
        [&](const auto& row) {
            return row.id == disappointing.developedComponentRevisions.front().measurementProfileId;
        });
    require(profile.detectionThreshold == 9.0 &&
                waterIce(sampleObservationChannels(physical, body, profile, 5)) ==
                    ResourceIndication::NotDetectedWithinLimit,
            "engine uses disappointing demonstrated threshold9, never public target7");
}
void performance_decision_uses_every_time_entry() {
    const auto start = fixture(9.0);
    Simulation direct(start), command(start);
    SimulationService service(start);
    require(direct.execute(CreateTechnicalDevelopmentCommand{charter(start)}).ok &&
                command.execute(CreateTechnicalDevelopmentCommand{charter(start)}).ok &&
                service.execute(CreateTechnicalDevelopmentCommand{charter(start)}).ok,
            "equivalent performance-miss programs accepted");
    const auto a = direct.advanceDaysDetailed(30);
    const auto b = service.advanceDaysDetailed(30);
    const auto c = command.execute(AdvanceDaysCommand{30});
    require(a.interrupted && b.interrupted && a.advancedDays == 12 && b.advancedDays == 12 &&
                command.state().date.day == 12 && c.ok && c.message.find("stopped") != std::string::npos &&
                a.stopReason == b.stopReason,
            "direct, service, and command advancement stop at the same observed performance decision");
    for (auto* simulation : {&direct, &command}) {
        const auto& program = simulation->state().technicalDevelopmentPrograms.front();
        require(
            simulation
                ->execute(AcknowledgeTechnicalDevelopmentIssueCommand{program.id, program.issue.signature})
                .ok,
            "performance issue acknowledgment accepted");
        require(simulation->advanceDaysDetailed(1).advancedDays == 1,
                "acknowledgment permits continuation without changing evidence");
    }
}
void shipyard_query_names_local_supply_blocker() {
    auto state = demonstrate(6.0);
    const auto terra = state.technicalFacilities.front().colonyId;
    const auto mars = std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
                          return row.id != terra && row.shipyardCapacity > 0;
                      })->id;
    SimulationService service(state);
    auto installs = referenceSurveyCutterComponents();
    installs.at(3).componentId = state.developedComponentRevisions.front().componentId;
    require(service
                .execute(CreateShipClassRevisionCommand{"Remote precision order", ShipRole::Survey,
                                                        std::nullopt, installs})
                .ok,
            "remote developed design saved");
    require(service.execute(AssignShipyardBuildCommand{mars, service.state().shipClasses.back().id, 1}).ok,
            "remote supply-blocked order accepted");
    const auto backlog = SimulationQueries{service}.productionBacklog();
    require(backlog.back().blockedByComponentSupply && !backlog.back().etaDays &&
                backlog.back().statusName == "Waiting for developed-component supply" &&
                backlog.back().componentSupplyExplanation.find("local prototype") != std::string::npos,
            "shipyard query exposes honest local prototype/process blocker without fake ETA");
}
} // namespace

int main() {
    try {
        pretest_truth_isolation();
        demonstrated_threshold_drives_sampling();
        performance_decision_uses_every_time_entry();
        shipyard_query_names_local_supply_blocker();
        std::cout << "Technical observation: 2 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical observation failure: " << error.what() << '\n';
        return 1;
    }
}
