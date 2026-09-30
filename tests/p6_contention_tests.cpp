// Simultaneous valid intentions contend for actual teams, fleets and a finite
// facility. Later intent remains durable and displays its real current blocker.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace deep;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void accept(SimulationService& app, const SimCommand& command) {
    const auto result = app.execute(command);
    require(result.ok, "P6 finite contention intent should remain valid");
}

void scientific_team_stays_physical() {
    auto state = createDelegatedSurveyScenario();
    state.colonies.back().analysisCapacity = 1;
    SimulationService app(state);
    accept(app, ResourceSurveyCommand{state.fleets.front().id, state.fleets.front().currentBodyId});
    SurveyProgramCharter survey;
    survey.name = "P6 field custody";
    survey.homeColonyId = state.colonies.back().id;
    survey.requestedFleetId = state.fleets.back().id;
    survey.requestedTeamId = state.surveyTeams.front().id;
    survey.requestedLeaderId = state.people.front().id;
    survey.targets = {{state.bodies.back().id, 1, 1}};
    accept(app, CreateSurveyProgramCommand{survey});
    AnalysisCharter analysis{"P6 competing interpretation",
                             state.colonies.back().id,
                             FixedBatchInput{{app.state().observations.back().id}},
                             state.surveyTeams.front().id,
                             state.people.front().id,
                             std::nullopt};
    accept(app, CreateAnalysisProgramCommand{analysis});
    require(app.advanceDaysDetailed(1).advancedDays == 1, "shared scientific opening advances once");
    require(app.state().analysisPrograms.front().receipts.empty() &&
                app.state().analysisPrograms.front().lifecycle == AnalysisLifecycle::Authorized,
            "field owner prevents duplicate analysis labor without cancelling intent");
    const auto view = SimulationQueries(app).analysisPrograms().front();
    require(view.condition.find("survey") != std::string::npos ||
                view.condition.find("Survey") != std::string::npos,
            "scientific wait names actual field controller");
    validateGameState(app.state());
}

void technical_facility_is_one_opening_budget() {
    auto state = createHomeSystemScenario();
    const auto facility = state.technicalFacilities.front();
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& row) { return row.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(1'000);
    colony->processorCapacity = 0;
    auto firstTeam = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                  [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    const auto firstTeamId = firstTeam->id;
    auto secondTeam = *firstTeam;
    secondTeam.id = MaintenanceTeamId{state.ids.nextMaintenanceTeamId++};
    secondTeam.name = "Second actual prototype engineer";
    state.maintenanceTeams.push_back(secondTeam);
    auto secondOpportunity = state.technologyOpportunities.front();
    secondOpportunity.id = TechnologyOpportunityId{state.ids.nextTechnologyOpportunityId++};
    secondOpportunity.name = "Second bounded objective";
    state.technologyOpportunities.push_back(secondOpportunity);
    state.technologyCandidateTruths.push_back({secondOpportunity.id, 6.0});
    TechnicalDevelopmentCharter first{"First P6 technical work",
                                      state.technologyOpportunities.front().id,
                                      facility.colonyId,
                                      facility.id,
                                      firstTeamId,
                                      state.people.front().id,
                                      TechnicalDevelopmentScope::DemonstratePrototype,
                                      {}};
    auto second = first;
    second.name = "Second P6 technical work";
    second.opportunityId = secondOpportunity.id;
    second.requestedTeamId = secondTeam.id;
    SimulationService app(state);
    accept(app, CreateTechnicalDevelopmentCommand{first});
    accept(app, CreateTechnicalDevelopmentCommand{second});
    require(app.advanceDaysDetailed(1).advancedDays == 1, "two technical intents share one physical opening");
    require(app.state().technicalDevelopmentPrograms.front().receipts.size() == 1 &&
                app.state().technicalDevelopmentPrograms.back().receipts.empty() &&
                app.state().technicalDevelopmentPrograms.back().lifecycle ==
                    TechnicalDevelopmentLifecycle::Authorized,
            "earlier real work spends all facility throughput without deleting later intent");
    const auto conditions = SimulationQueries(app).technicalDevelopments();
    require(conditions.back().condition.find("technical-facility throughput") != std::string::npos,
            "later technical intent identifies the finite shared facility as its current wait");
    const auto laterId = app.state().technicalDevelopmentPrograms.back().id;
    accept(app, SuspendTechnicalDevelopmentCommand{app.state().technicalDevelopmentPrograms.front().id});
    require(app.advanceDaysDetailed(1).advancedDays == 1 &&
                app.state().technicalDevelopmentPrograms.back().id == laterId &&
                app.state().technicalDevelopmentPrograms.back().receipts.size() == 1,
            "same later technical intent begins real work when facility capacity is released");
    validateGameState(app.state());
}

void remaining_facility_share_is_not_reported_as_fully_blocked() {
    auto state = createHomeSystemScenario();
    const auto facility = state.technicalFacilities.front();
    const auto firstTeam =
        std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                     [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    const auto firstTeamId = firstTeam->id;
    firstTeam->workdaysPerDay = 0.25;
    auto secondTeam = *firstTeam;
    secondTeam.id = MaintenanceTeamId{state.ids.nextMaintenanceTeamId++};
    secondTeam.name = "P6 partial-share engineer";
    secondTeam.workdaysPerDay = 1.0;
    state.maintenanceTeams.push_back(secondTeam);
    auto opportunity = state.technologyOpportunities.front();
    opportunity.id = TechnologyOpportunityId{state.ids.nextTechnologyOpportunityId++};
    opportunity.name = "P6 partial-share objective";
    state.technologyOpportunities.push_back(opportunity);
    state.technologyCandidateTruths.push_back({opportunity.id, 6.0});
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& row) { return row.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(1'000);
    colony->processorCapacity = 0;
    SimulationService app(state);
    TechnicalDevelopmentCharter first{"Finite first program",
                                      state.technologyOpportunities.front().id,
                                      facility.colonyId,
                                      facility.id,
                                      firstTeamId,
                                      state.people.front().id,
                                      TechnicalDevelopmentScope::DemonstratePrototype,
                                      {}};
    accept(app, CreateTechnicalDevelopmentCommand{first});
    require(app.advanceDaysDetailed(19).advancedDays == 19 &&
                app.state().technicalDevelopmentPrograms.front().stageWork == 4.75,
            "real quarter-day team earns 4.75 concept work over nineteen days");
    auto second = first;
    second.name = "Finite later program";
    second.opportunityId = opportunity.id;
    second.requestedTeamId = secondTeam.id;
    accept(app, CreateTechnicalDevelopmentCommand{second});
    const auto preview = SimulationQueries(app).technicalDevelopments();
    require(preview.back().condition.find("partial technical work") != std::string::npos,
            "positive remaining facility share is explained as partial, not fully blocked");
    require(app.advanceDaysDetailed(1).advancedDays == 1 &&
                app.state().technicalDevelopmentPrograms.back().stageWork == 0.75,
            "earlier quarter-day action leaves exactly 0.75 real facility work for later intent");
    validateGameState(app.state());
}

void freight_fleet_and_stock_are_single_custody() {
    auto state = createDelegatedFreightScenario();
    const auto source = state.colonies.at(state.colonies.size() - 2).id;
    const auto destination = state.colonies.back().id;
    auto first = FreightProgramCharter{};
    first.name = "P6 first actual freight custody";
    first.source = source;
    first.destination = destination;
    first.operatingBaseColonyId = source;
    first.commodity = ProcessedMaterial::Electronics;
    first.totalQuantity = 20;
    first.requestedFleetId = state.fleets.back().id;
    first.requestedLeaderId = state.people.front().id;
    auto second = first;
    second.name = "P6 later shared-fleet request";
    SimulationService app(state);
    accept(app, CreateFreightProgramCommand{first});
    accept(app, CreateFreightProgramCommand{second});
    require(app.advanceDaysDetailed(1).advancedDays == 1, "freight arbitration performs one opening");
    const auto& programs = app.state().freightPrograms;
    require(programs.front().leasedFleetId && !programs.back().leasedFleetId &&
                programs.back().lifecycle == FreightProgramLifecycle::Authorized,
            "one fleet cannot become two program-owned cargo carriers");
    const auto view = SimulationQueries(app).freightPrograms().back();
    require(view.condition.find("controlled") != std::string::npos ||
                view.condition.find("freight") != std::string::npos,
            "later freight intent names actual fleet custody");
    validateGameState(app.state());
}

void separate_freighters_cannot_load_one_opening_stock_twice() {
    auto state = createDelegatedFreightScenario();
    const auto source = state.colonies.at(state.colonies.size() - 2).id;
    const auto destination = state.colonies.back().id;
    const FleetId originalFreighter = state.fleets.back().id;
    state.ships.back().fuel = 1'000; // Existing S6 carrier begins physically fueled.
    auto& colony = state.colonies.at(state.colonies.size() - 2);
    const auto cost = evaluateShipDesign(state.shipComponents, state.shipClasses.at(1).components).buildCost;
    colony.shipyardCapacity = 1'000;
    colony.processedStockpile.amount.fill(1'000);
    colony.processedStockpile.set(ProcessedMaterial::Electronics,
                                  cost.get(ProcessedMaterial::Electronics) + 20);
    colony.processedStockpile.set(ProcessedMaterial::Propellant, 5'000);
    SimulationService app(state);
    accept(app, AssignShipyardBuildCommand{source, state.shipClasses.at(1).id, 1});
    require(app.advanceDaysDetailed(1).advancedDays == 1 &&
                app.state().ships.size() == state.ships.size() + 1,
            "second carrier is physically built from its evaluated cost");
    const FleetId secondFreighter = app.state().fleets.back().id;
    const auto charter = [&](const char* name, FleetId fleet) {
        FreightProgramCharter request;
        request.name = name;
        request.source = source;
        request.destination = destination;
        request.operatingBaseColonyId = source;
        request.commodity = ProcessedMaterial::Electronics;
        request.totalQuantity = 20;
        request.requestedFleetId = fleet;
        request.requestedLeaderId = state.people.front().id;
        return request;
    };
    accept(app, CreateFreightProgramCommand{charter("P6 first finite stock", originalFreighter)});
    accept(app, CreateFreightProgramCommand{charter("P6 later finite stock", secondFreighter)});
    for (int day = 0; day < 10 && app.state().freightPrograms.front().cargoLoaded == 0; ++day) {
        const auto result = app.advanceDaysDetailed(1);
        if (result.advancedDays != 1)
            throw std::runtime_error("bounded freight opening stopped: " + result.stopReason);
        if (result.interrupted) {
            bool acceptedStockLimit = false;
            for (const auto& program : app.state().freightPrograms)
                if (!program.issue.acknowledged &&
                    program.issue.message.find("source stock") != std::string::npos) {
                    accept(app, AcknowledgeFreightProgramIssueCommand{program.id, program.issue.signature});
                    acceptedStockLimit = true;
                }
            require(acceptedStockLimit,
                    "only the declared observed source-stock limit may interrupt contention proof");
        }
    }
    const auto firstLoaded = app.state().freightPrograms.front().cargoLoaded;
    const auto secondLoaded = app.state().freightPrograms.back().cargoLoaded;
    const auto remainingStock = app.state()
                                    .colonies.at(state.colonies.size() - 2)
                                    .processedStockpile.get(ProcessedMaterial::Electronics);
    if (firstLoaded != 20 || secondLoaded != 0 || remainingStock != 0)
        throw std::runtime_error(
            "finite stock result: first=" + std::to_string(firstLoaded) +
            " second=" + std::to_string(secondLoaded) + " source=" + std::to_string(remainingStock) +
            " first_condition=" + SimulationQueries(app).freightPrograms().front().condition +
            " second_condition=" + SimulationQueries(app).freightPrograms().back().condition);
    require(app.state().freightPrograms.back().lifecycle == FreightProgramLifecycle::Authorized,
            "unfunded later freight intent stays authorized");
    const auto condition = SimulationQueries(app).freightPrograms().back().condition;
    require(condition.find("stock") != std::string::npos || condition.find("Stock") != std::string::npos,
            "later independent carrier sees actual source-stock constraint");
    validateGameState(app.state());
}
} // namespace

int main() {
    try {
        scientific_team_stays_physical();
        technical_facility_is_one_opening_budget();
        remaining_facility_share_is_not_reported_as_fully_blocked();
        freight_fleet_and_stock_are_single_custody();
        separate_freighters_cannot_load_one_opening_stock_twice();
        std::cout << "P6 finite contention passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 contention proof failed: " << error.what() << '\n';
        return 1;
    }
}
