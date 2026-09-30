// Reproducible whole-loop P5 fixture: engineering, prototype-backed and serial
// hulls, weak-signal observations, actual wear, and qualified physical service.
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <stdexcept>

namespace deep {
namespace {
void accept(Simulation& simulation, const SimCommand& command) {
    const auto result = simulation.execute(command);
    if (!result.ok)
        throw std::runtime_error("P5 fixture command failed: " + result.message);
}
void acknowledge(Simulation& simulation) {
    for (const auto& program : simulation.state().technicalDevelopmentPrograms)
        if (!program.issue.acknowledged)
            accept(simulation,
                   AcknowledgeTechnicalDevelopmentIssueCommand{program.id, program.issue.signature});
    for (const auto& program : simulation.state().maintenancePrograms)
        if (!program.issue.acknowledged)
            accept(simulation, AcknowledgeMaintenanceIssueCommand{program.id, program.issue.signature});
    for (const auto& program : simulation.state().surveyPrograms)
        if (!program.issue.acknowledged)
            accept(simulation, AcknowledgeSurveyProgramIssueCommand{program.id, program.issue.signature});
}
void step(Simulation& simulation) {
    acknowledge(simulation);
    const auto result = simulation.advanceDaysDetailed(1);
    if (result.advancedDays != 1)
        throw std::runtime_error("P5 fixture stalled: " + result.stopReason);
    validateGameState(simulation.state());
}
template <class Rows, class Predicate> const auto& first(const Rows& rows, Predicate predicate) {
    const auto it = std::find_if(rows.begin(), rows.end(), predicate);
    if (it == rows.end())
        throw std::runtime_error("P5 fixture could not resolve an authored asset");
    return *it;
}
} // namespace

GameState earnTechnicalDevelopmentFixture(double achievedThreshold) {
    auto state = createHomeSystemScenario();
    state.technologyCandidateTruths.front().achievedDetectionThreshold = achievedThreshold;
    const auto facility = state.technicalFacilities.front();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == facility.colonyId; });
    colony.processedStockpile.amount.fill(10'000.0);
    colony.processorCapacity = 0.0;
    colony.shipyardCapacity = 200.0;
    colony.mines = 0.0;
    const auto ice =
        std::find_if(state.mineralDeposits.begin(), state.mineralDeposits.end(), [&](const auto& row) {
            return row.bodyId == colony.bodyId && row.mineral == Mineral::WaterIce;
        });
    if (ice == state.mineralDeposits.end())
        state.mineralDeposits.push_back(MineralDeposit{colony.bodyId, Mineral::WaterIce, 7.0, 1.0});
    else {
        ice->remaining = 7.0;
        ice->accessibility = 1.0;
    }
    const auto& engineering = first(state.maintenanceTeams,
                                    [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    TechnicalDevelopmentCharter technical{"Precision characterization full development",
                                          state.technologyOpportunities.front().id,
                                          colony.id,
                                          facility.id,
                                          engineering.id,
                                          state.people.front().id,
                                          TechnicalDevelopmentScope::ProductionAndSupportReady,
                                          {}};
    Simulation simulation(state);
    accept(simulation, CreateTechnicalDevelopmentCommand{technical});

    std::optional<ShipClassId> precisionClass, establishedClass, tenderClass;
    bool ordersAuthorized = false;
    for (int day = 0; day < 120; ++day) {
        step(simulation);
        if (!ordersAuthorized && !simulation.state().developedComponentRevisions.empty()) {
            const auto developed = simulation.state().developedComponentRevisions.front().componentId;
            auto precision = referenceSurveyCutterComponents();
            precision.at(3).componentId = developed;
            accept(simulation, CreateShipClassRevisionCommand{"Precision Characterization Cutter",
                                                              ShipRole::Survey, std::nullopt, precision});
            precisionClass = simulation.state().shipClasses.back().id;
            const auto& specialist = first(simulation.state().shipComponents, [](const auto& row) {
                return row.name == "Specialist Survey Array";
            });
            auto established = referenceSurveyCutterComponents();
            established.at(3).componentId = specialist.id;
            accept(simulation, CreateShipClassRevisionCommand{"Established Characterization Cutter",
                                                              ShipRole::Survey, std::nullopt, established});
            establishedClass = simulation.state().shipClasses.back().id;
            const auto& specialistWorkshop = first(simulation.state().shipComponents, [](const auto& row) {
                return row.name == "Specialist Instrument Workshop";
            });
            auto tender = referenceTenderComponents();
            tender.back().componentId = specialistWorkshop.id;
            accept(simulation, CreateShipClassRevisionCommand{"Specialist Instrument Tender",
                                                              ShipRole::Freighter, std::nullopt, tender});
            tenderClass = simulation.state().shipClasses.back().id;
            accept(simulation, AssignShipyardBuildCommand{colony.id, *precisionClass, 2});
            accept(simulation, AssignShipyardBuildCommand{colony.id, *establishedClass, 1});
            accept(simulation, AssignShipyardBuildCommand{colony.id, *tenderClass, 1});
            ordersAuthorized = true;
        }
        const bool developmentClosed = !simulation.state().technicalDevelopmentPrograms.empty() &&
                                       simulation.state().technicalDevelopmentPrograms.front().lifecycle ==
                                           TechnicalDevelopmentLifecycle::Closed;
        if (ordersAuthorized && developmentClosed && simulation.state().ships.size() >= 4)
            break;
    }
    if (!precisionClass || !establishedClass || !tenderClass || simulation.state().ships.size() < 4)
        throw std::runtime_error("P5 fixture did not earn its development/shipyard artifacts");

    const auto shipForClass = [&](ShipClassId id, std::size_t occurrence = 0) -> const Ship& {
        std::size_t seen = 0;
        for (const auto& ship : simulation.state().ships)
            if (ship.shipClassId == id && seen++ == occurrence)
                return ship;
        throw std::runtime_error("P5 fixture could not resolve a produced ship");
    };
    const FleetId prototypeFleet = shipForClass(*precisionClass, 0).fleetId;
    const FleetId serialFleet = shipForClass(*precisionClass, 1).fleetId;
    const FleetId establishedFleet = shipForClass(*establishedClass).fleetId;
    const FleetId tenderFleet = shipForClass(*tenderClass).fleetId;
    accept(simulation, ResourceSurveyCommand{establishedFleet, colony.bodyId});
    accept(simulation, ResourceSurveyCommand{prototypeFleet, colony.bodyId});
    accept(simulation, ResourceSurveyCommand{serialFleet, colony.bodyId});

    MaintenanceProgramCharter maintenance;
    maintenance.name = "Qualified precision instrument support";
    maintenance.serviceColonyId = colony.id;
    maintenance.requestedTenderId = tenderFleet;
    maintenance.requestedTeamId = engineering.id;
    maintenance.requestedLeaderId = state.people.front().id;
    maintenance.clients = {prototypeFleet};
    accept(simulation, CreateMaintenanceProgramCommand{maintenance});
    const auto providerId = simulation.state().maintenancePrograms.back().id;
    const auto& surveyTeam =
        first(simulation.state().surveyTeams, [&](const auto& row) { return row.colonyId == colony.id; });
    SurveyProgramCharter survey;
    survey.name = "Precision maintenance qualification proof";
    survey.homeColonyId = colony.id;
    survey.requestedFleetId = prototypeFleet;
    survey.requestedLeaderId = state.people.front().id;
    survey.requestedTeamId = surveyTeam.id;
    survey.targets = {{colony.bodyId, 1, 2}};
    survey.policy.maintenanceProgramId = providerId;
    survey.policy.remainingDutyTrigger = 1.0;
    accept(simulation, CreateSurveyProgramCommand{survey});
    for (int day = 0; day < 30 && simulation.state().maintenancePrograms.front().receipts.empty(); ++day)
        step(simulation);
    if (simulation.state().maintenancePrograms.front().receipts.empty())
        throw std::runtime_error("P5 fixture did not perform qualified specialist maintenance");
    return simulation.state();
}

} // namespace deep
