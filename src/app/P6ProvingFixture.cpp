// Earns the investigated established-technology path through ordinary player
// commands and elapsed physical execution. This is inspection/test tooling;
// it is not an autonomous runtime player or a new simulation rule.
#include "app/P6ProvingFixture.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace deep {
namespace {
void accept(Simulation& simulation, const SimCommand& command) {
    const auto result = simulation.execute(command);
    if (!result.ok)
        throw std::runtime_error("P6 command rejected: " + result.message);
}

void step(Simulation& simulation) {
    const auto result = simulation.advanceDaysDetailed(1);
    if (result.advancedDays != 1)
        throw std::runtime_error("P6 established loop interrupted: " + result.stopReason);
    if (result.interrupted) {
        bool acceptedKnownSupplyWait = false;
        for (const auto& program : simulation.state().siteDevelopmentPrograms)
            if (!program.issue.acknowledged &&
                program.issue.message.find("construction material stock") != std::string::npos &&
                !simulation.state().freightPrograms.empty()) {
                accept(simulation,
                       AcknowledgeSiteDevelopmentIssueCommand{program.id, program.issue.signature});
                acceptedKnownSupplyWait = true;
            }
        if (!acceptedKnownSupplyWait)
            throw std::runtime_error("P6 established loop needs an unplanned decision: " + result.stopReason);
    }
    validateGameState(simulation.state());
}

template <class Predicate> void advanceUntil(Simulation& simulation, Predicate done, int limit) {
    for (int day = 0; day < limit && !done(); ++day)
        step(simulation);
    if (!done())
        throw std::runtime_error("P6 established loop did not reach its command-earned checkpoint");
}

ShipComponentId componentNamed(const GameState& state, const char* name) {
    const auto row = std::find_if(state.shipComponents.begin(), state.shipComponents.end(),
                                  [&](const auto& value) { return value.name == name; });
    if (row == state.shipComponents.end())
        throw std::runtime_error("P6 established component is missing");
    return row->id;
}
} // namespace

GameState createP6EstablishedStartingWorld(bool usefulIce) {
    GameState state = createSiteDevelopmentScenario(usefulIce);
    const ColonyId base = state.colonies.back().id;
    state.colonies.back().analysisCapacity = 1.0;
    state.surveyTeams.push_back({SurveyTeamId{state.ids.nextSurveyTeamId++}, "P6 field scientist",
                                 SurveyTeamLocationKind::Colony, base, std::nullopt});
    state.surveyTeams.push_back({SurveyTeamId{state.ids.nextSurveyTeamId++}, "P6 laboratory scientist",
                                 SurveyTeamLocationKind::Colony, base, std::nullopt});
    validateGameState(state);
    return state;
}

P6EstablishedCheckpoints earnP6EstablishedLoop() {
    P6EstablishedCheckpoints checkpoints;
    checkpoints.starting = createP6EstablishedStartingWorld();
    Simulation simulation(checkpoints.starting);
    const ColonyId base = simulation.state().colonies.back().id;
    const BodyId target = simulation.state().bodies.back().id;
    const PersonId leader = simulation.state().people.front().id;
    const MaintenanceTeamId builders = simulation.state().maintenanceTeams.back().id;
    const SurveyTeamId field =
        simulation.state().surveyTeams.at(simulation.state().surveyTeams.size() - 2).id;
    const SurveyTeamId analyst = simulation.state().surveyTeams.back().id;

    auto surveyComponents = referenceSurveyCutterComponents();
    surveyComponents.at(3).componentId = componentNamed(simulation.state(), "Specialist Survey Array");
    accept(simulation, CreateShipClassRevisionCommand{"P6 Characterization Cutter", ShipRole::Survey,
                                                      std::nullopt, surveyComponents});
    const ShipClassId surveyClass = simulation.state().shipClasses.back().id;
    accept(simulation, AssignShipyardBuildCommand{base, simulation.state().shipClasses.at(2).id, 1});
    accept(simulation, AssignShipyardBuildCommand{base, simulation.state().shipClasses.at(1).id, 5});
    accept(simulation, AssignShipyardBuildCommand{base, surveyClass, 1});
    step(simulation);
    if (simulation.state().ships.size() != 7)
        throw std::runtime_error("P6 required hulls did not commission through the shipyard");
    checkpoints.shipsBuilt = simulation.state();
    const auto& fleets = simulation.state().fleets;
    const FleetId surveyFleet = fleets.back().id;
    const FleetId builderFleet = fleets.front().id;
    std::vector<FleetId> freightFleets;
    for (std::size_t index = 1; index <= 5; ++index)
        freightFleets.push_back(fleets.at(index).id);

    SurveyProgramCharter survey;
    survey.name = "P6 investigated ice prospect";
    survey.homeColonyId = base;
    survey.requestedFleetId = surveyFleet;
    survey.requestedTeamId = field;
    survey.requestedLeaderId = leader;
    survey.targets = {{target, 1, 1}};
    accept(simulation, CreateSurveyProgramCommand{survey});
    const SurveyProgramId surveyId = simulation.state().surveyPrograms.back().id;
    AnalysisCharter analysis{
        "P6 interpreted characterization", base, FollowSurveyInput{surveyId}, analyst, leader, std::nullopt};
    accept(simulation, CreateAnalysisProgramCommand{analysis});
    advanceUntil(simulation, [&] { return !simulation.state().observations.empty(); }, 60);
    checkpoints.observationAcquired = simulation.state();
    advanceUntil(simulation, [&] { return !simulation.state().assessments.empty(); }, 20);
    checkpoints.assessmentPublished = simulation.state();

    SiteDevelopmentCharter development;
    development.supportColonyId = base;
    development.package = referenceSitePackage();
    development.assignments.name = "P6 investigated ice site";
    development.assignments.builderId = builderFleet;
    development.assignments.teamId = builders;
    development.assignments.leaderId = leader;
    NewResourceSite site;
    site.bodyId = target;
    site.name = "P6 remote ice working site";
    site.operatingPolicy.leaderId = leader;
    accept(simulation, CreateSiteDevelopmentCommand{development, site});
    const SiteId siteId = simulation.state().resourceSites.front().id;
    const std::pair<ProcessedMaterial, double> deliveries[] = {
        {ProcessedMaterial::StructuralAlloys, 240},
        {ProcessedMaterial::Electronics, 70},
        {ProcessedMaterial::IndustrialComposites, 2'500},
        {ProcessedMaterial::ReactorFuel, 2'500}};
    for (std::size_t index = 0; index < std::size(deliveries); ++index) {
        const auto [material, quantity] = deliveries[index];
        FreightProgramCharter charter;
        charter.name = "P6 site supply: " + std::string(toString(material));
        charter.source = base;
        charter.destination = siteId;
        charter.operatingBaseColonyId = base;
        charter.commodity = material;
        charter.totalQuantity = quantity;
        charter.requestedFleetId = freightFleets.at(index);
        charter.requestedLeaderId = leader;
        accept(simulation, CreateFreightProgramCommand{charter});
    }
    FreightProgramCharter collect;
    collect.name = "P6 standing Ice collection";
    collect.source = siteId;
    collect.destination = base;
    collect.operatingBaseColonyId = base;
    collect.commodity = Mineral::WaterIce;
    collect.totalQuantity = 10'000;
    collect.requestedFleetId = freightFleets.back();
    collect.requestedLeaderId = leader;
    accept(simulation, CreateFreightProgramCommand{collect});
    checkpoints.siteAuthorized = simulation.state();

    bool recordedPartial = false, recordedTransit = false, recordedIceDelivery = false;
    for (int day = 0; day < 180; ++day) {
        GameState beforeDelivery;
        if (!recordedIceDelivery)
            beforeDelivery = simulation.state();
        step(simulation);
        const auto& state = simulation.state();
        if (!recordedPartial && !state.siteDevelopmentPrograms.front().workReceipts.empty()) {
            checkpoints.sitePartiallyAssembled = state;
            recordedPartial = true;
        }
        if (!recordedTransit &&
            std::any_of(state.freightPrograms.begin(), state.freightPrograms.end(), [&](const auto& program) {
                if (program.task != FreightProgramTask::Outbound || !program.leasedFleetId)
                    return false;
                const auto fleet =
                    std::find_if(state.fleets.begin(), state.fleets.end(),
                                 [&](const auto& row) { return row.id == *program.leasedFleetId; });
                return fleet != state.fleets.end() && fleet->activeOrder.type == FleetOrderType::MoveToBody;
            })) {
            checkpoints.freightInTransit = state;
            recordedTransit = true;
        }
        if (!recordedIceDelivery && state.freightPrograms.back().cargoDelivered > 0) {
            checkpoints.beforeFirstIceDelivery = std::move(beforeDelivery);
            checkpoints.firstIceDelivery = state;
            recordedIceDelivery = true;
        }
        if (state.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed &&
            state.colonies.back().processedProductionTotals.get(ProcessedMaterial::Propellant) > 0 &&
            state.date.day >= 90) {
            checkpoints.mature = state;
            break;
        }
    }
    if (!recordedPartial || !recordedTransit || !recordedIceDelivery || checkpoints.mature.date.day < 90)
        throw std::runtime_error("P6 established loop lacks physical construction, transit or industry");
    return checkpoints;
}

} // namespace deep
