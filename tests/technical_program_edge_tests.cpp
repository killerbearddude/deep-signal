// P5 program-lifecycle and capacity regressions: accepted waiting intent,
// bounded facility work, sunk cancellation, artifact reuse, and opening stock.
#include "sim/GameStateValidation.h"
#include "sim/ProcessingAllocationRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Key {
    TechnologyOpportunityId opportunity;
    TechnicalFacilityId facility;
    MaintenanceTeamId team;
    ColonyId colony;
    PersonId leader;
};
Key key(const GameState& state) {
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    return {state.technologyOpportunities.front().id, state.technicalFacilities.front().id, team->id,
            state.technicalFacilities.front().colonyId, state.people.front().id};
}
TechnicalDevelopmentCharter charter(const GameState& state, TechnicalDevelopmentScope scope) {
    const auto k = key(state);
    return {"Bounded technical intent", k.opportunity, k.colony, k.facility, k.team, k.leader, scope, {}};
}
GameState supplied() {
    auto state = createHomeSystemScenario();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == key(state).colony; });
    colony.processedStockpile.amount.fill(1'000.0);
    colony.processorCapacity = 0.0;
    return state;
}
void step(Simulation& simulation) {
    const auto result = simulation.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "one program-edge day advances");
    validateGameState(simulation.state());
}
void accepted_wait_and_finite_facility() {
    auto empty = createHomeSystemScenario();
    auto waiting = charter(empty, TechnicalDevelopmentScope::DemonstratePrototype);
    waiting.requestedFacilityId.reset();
    waiting.requestedTeamId.reset();
    waiting.requestedLeaderId.reset();
    Simulation idle(empty);
    require(idle.execute(CreateTechnicalDevelopmentCommand{waiting}).ok,
            "missing resources remain valid technical intent");
    const auto result = idle.advanceDaysDetailed(30);
    require(result.advancedDays == 30 && !result.interrupted && idle.state().prototypeDesigns.empty() &&
                idle.state().technicalDevelopmentPrograms.front().reports.size() == 1,
            "initial ordinary wait neither fabricates progress nor stops time/reporting");
    const auto published = idle.state().technicalDevelopmentPrograms.front().reports.front();
    auto amended = waiting;
    amended.name = "Amended after report";
    amended.scope = TechnicalDevelopmentScope::ProductionReady;
    require(idle.execute(AmendTechnicalDevelopmentCommand{
                             idle.state().technicalDevelopmentPrograms.front().id, amended})
                .ok,
            "same-day post-report authority amendment accepted");
    const auto& retained = idle.state().technicalDevelopmentPrograms.front().reports.front();
    require(retained.scope == published.scope && retained.stage == published.stage &&
                retained.waitingReason == published.waitingReason &&
                retained.auditThroughId == published.auditThroughId,
            "same-day amendment does not rewrite published technical report snapshot");

    auto state = supplied();
    state.technicalFacilities.front().engineeringWorkdaysPerDay = 0.25;
    Simulation partial(state);
    require(partial
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(state, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "partial facility program accepted");
    for (int day = 0; day < 4; ++day)
        step(partial);
    const auto& program = partial.state().technicalDevelopmentPrograms.front();
    require(std::abs(program.stageWork - 1.0) < 1e-9 &&
                std::abs(program.stageConsumed.get(ProcessedMaterial::Electronics) - 2.0) < 1e-9 &&
                std::abs(program.stageConsumed.get(ProcessedMaterial::IndustrialComposites) - 1.0) < 1e-9,
            "work and proportional material use are bounded by actual facility throughput");
}
void cancellation_and_scope_continuation() {
    auto state = supplied();
    Simulation first(state);
    require(first
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(state, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "first bounded program accepted");
    for (int day = 0; day < 6; ++day)
        step(first);
    const auto firstId = first.state().technicalDevelopmentPrograms.front().id;
    require(first.state().prototypeDesigns.size() == 1 &&
                first.state().technicalDevelopmentPrograms.front().stage ==
                    TechnicalDevelopmentStage::PrototypeFabrication &&
                first.state().technicalDevelopmentPrograms.front().stageWork == 1.0,
            "concept artifact and partial fabrication earned before cancellation");
    require(first.execute(CancelTechnicalDevelopmentCommand{firstId}).ok,
            "partial development cancels without refund");
    require(first
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(first.state(), TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "new program continues from completed artifact");
    const auto& second = first.state().technicalDevelopmentPrograms.back();
    require(second.stage == TechnicalDevelopmentStage::PrototypeFabrication && second.stageWork == 0.0 &&
                second.stageConsumed.get(ProcessedMaterial::Electronics) == 0.0,
            "cancelled partial work is not inherited by a new program");

    auto splitState = supplied();
    Simulation split(splitState);
    require(split
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(splitState, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "demonstration scope accepted");
    for (int day = 0; day < 13; ++day)
        step(split);
    const auto prototypes = split.state().prototypeComponentUnits.size();
    const auto tests = split.state().technicalTestRecords.size();
    require(split
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(split.state(), TechnicalDevelopmentScope::ProductionReady)})
                .ok,
            "later local-process program accepted");
    require(split.state().technicalDevelopmentPrograms.back().stage ==
                TechnicalDevelopmentStage::ProductionQualification,
            "later scope begins at first missing durable artifact");
    for (int day = 0; day < 5; ++day)
        step(split);
    require(split.state().prototypeComponentUnits.size() == prototypes &&
                split.state().technicalTestRecords.size() == tests &&
                split.state().componentProductionCapabilities.size() == 1,
            "later production scope reuses demonstration without duplicates");
    auto alreadySupported = split.state();
    const auto opportunity = alreadySupported.technologyOpportunities.front();
    const auto baseline =
        std::find_if(alreadySupported.shipComponents.begin(), alreadySupported.shipComponents.end(),
                     [&](const auto& row) { return row.id == opportunity.baselineComponentId; });
    auto team =
        std::find_if(alreadySupported.maintenanceTeams.begin(), alreadySupported.maintenanceTeams.end(),
                     [&](const auto& row) { return row.id == key(alreadySupported).team; });
    team->qualifiedFamilies.push_back(baseline->serviceProfile->familyId);
    const auto electronics =
        std::find_if(alreadySupported.colonies.begin(), alreadySupported.colonies.end(),
                     [&](const auto& row) { return row.id == key(alreadySupported).colony; })
            ->processedStockpile.get(ProcessedMaterial::Electronics);
    Simulation noDuplicate(alreadySupported);
    require(noDuplicate
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(alreadySupported, TechnicalDevelopmentScope::ProductionAndSupportReady)})
                .ok,
            "already-supported full scope is accepted as satisfied");
    require(noDuplicate.state().technicalDevelopmentPrograms.back().lifecycle ==
                    TechnicalDevelopmentLifecycle::Closed &&
                noDuplicate.state().supportQualificationRecords.empty() &&
                std::find_if(
                    noDuplicate.state().colonies.begin(), noDuplicate.state().colonies.end(),
                    [&](const auto& row) {
                        return row.id == key(alreadySupported).colony;
                    })->processedStockpile.get(ProcessedMaterial::Electronics) == electronics,
            "existing specialist qualification creates no duplicate work, material, or record");
    const auto reportCount = noDuplicate.state().technicalDevelopmentPrograms.back().reports.size();
    require(noDuplicate.advanceDaysDetailed(60).advancedDays == 60 &&
                noDuplicate.state().technicalDevelopmentPrograms.back().reports.size() == reportCount,
            "closed technical program publishes no future periodic reports");
}
void opening_stock_and_shared_facility() {
    auto state = supplied();
    const auto k = key(state);
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(),
                                 [&](const auto& row) { return row.id == k.colony; });
    colony.processedStockpile.set(ProcessedMaterial::Electronics, 0.0);
    colony.stockpile.amount.fill(1'000.0);
    colony.processorCapacity = 10.0;
    colony.processingPolicy = ProcessingPolicy::Manual;
    colony.manualProcessingAllocations = {{ProcessedMaterial::Electronics, 1.0}};
    Simulation delayed(state);
    require(delayed
                .execute(CreateTechnicalDevelopmentCommand{
                    charter(state, TechnicalDevelopmentScope::DemonstratePrototype)})
                .ok,
            "processing-dependent intent accepted");
    step(delayed);
    require(delayed.state().technicalDevelopmentPrograms.front().stageWork == 0.0 &&
                delayed.state().colonies.front().processedStockpile.get(ProcessedMaterial::Electronics) >=
                    0.0,
            "later processing cannot retroactively fund opening technical work");
    step(delayed);
    require(delayed.state().technicalDevelopmentPrograms.front().stageWork > 0.0,
            "same intent uses real processed material at a later opening");

    auto shared = supplied();
    const auto base = shared.technologyOpportunities.front();
    TechnologyOpportunity second = base;
    second.id = TechnologyOpportunityId{shared.ids.nextTechnologyOpportunityId++};
    second.name = "Controlled second instrumentation objective";
    shared.technologyOpportunities.push_back(second);
    shared.technologyCandidateTruths.push_back({second.id, 6.0});
    auto& otherTeam = shared.maintenanceTeams.front();
    otherTeam.engineeringQualifications = {EngineeringQualification::PrototypeInstrumentation};
    otherTeam.qualifiedFamilies.clear();
    otherTeam.colonyId = key(shared).colony;
    Simulation arbitration(shared);
    auto firstCharter = charter(shared, TechnicalDevelopmentScope::DemonstratePrototype);
    auto secondCharter = firstCharter;
    secondCharter.name = "Second facility claimant";
    secondCharter.opportunityId = second.id;
    secondCharter.requestedTeamId = otherTeam.id;
    require(arbitration.execute(CreateTechnicalDevelopmentCommand{firstCharter}).ok &&
                arbitration.execute(CreateTechnicalDevelopmentCommand{secondCharter}).ok,
            "two independent technical intents accepted");
    step(arbitration);
    require(arbitration.state().technicalDevelopmentPrograms.front().stageWork == 1.0 &&
                arbitration.state().technicalDevelopmentPrograms.back().stageWork == 0.0,
            "stable program order spends one finite facility budget once");
}
void maintenance_ownership_is_shared_and_typed() {
    auto state = createTenderMaintenanceScenario();
    auto& engineer = state.maintenanceTeams.front();
    engineer.engineeringQualifications = {EngineeringQualification::PrototypeInstrumentation};
    const auto home = state.colonies.back().id;
    const TechnicalFacilityId facility{state.ids.nextTechnicalFacilityId++};
    state.technicalFacilities.push_back({facility, home, "Controlled shared engineering laboratory", 1.0,
                                         TechnicalFacilityCapability::PrototypeInstrumentation});
    MaintenanceProgramCharter maintenance;
    maintenance.name = "Actual earlier maintenance custody";
    maintenance.serviceColonyId = home;
    maintenance.requestedTenderId = state.fleets.back().id;
    maintenance.requestedTeamId = engineer.id;
    maintenance.requestedLeaderId = state.people.front().id;
    Simulation simulation(state);
    require(simulation.execute(CreateMaintenanceProgramCommand{maintenance}).ok,
            "maintenance controller accepted");
    step(simulation);
    const auto maintenanceId = simulation.state().maintenancePrograms.front().id;
    require(controllingEngineeringTeam(simulation.state(), engineer.id) == ProgramController{maintenanceId},
            "maintenance owns the one real engineering team");
    TechnicalDevelopmentCharter technical{"Waiting on maintenance-owned engineer",
                                          state.technologyOpportunities.front().id,
                                          home,
                                          facility,
                                          engineer.id,
                                          state.people.front().id,
                                          TechnicalDevelopmentScope::DemonstratePrototype,
                                          {}};
    require(simulation.execute(CreateTechnicalDevelopmentCommand{technical}).ok,
            "technical intent survives real team contention");
    const auto technicalId = simulation.state().technicalDevelopmentPrograms.front().id;
    require(maintenanceId.value == technicalId.value &&
                ProgramController{maintenanceId} != ProgramController{technicalId},
            "equal numeric maintenance/technical IDs retain typed identity");
    require(technicalDevelopmentCondition(simulation.state(),
                                          simulation.state().technicalDevelopmentPrograms.front())
                    .find("maintenance program") != std::string::npos,
            "technical wait names the actual shared engineering controller");
    step(simulation);
    require(simulation.state().technicalDevelopmentPrograms.front().receipts.empty(),
            "shared team performs no duplicate technical work");
}
void actual_freight_delivery_waits_for_next_opening() {
    auto state = createDelegatedFreightScenario();
    auto& source = state.colonies.at(state.colonies.size() - 2);
    auto& destination = state.colonies.back();
    source.processedStockpile.set(ProcessedMaterial::Electronics, 10.0);
    source.processedStockpile.set(ProcessedMaterial::Propellant, 3'000.0);
    destination.processedStockpile.set(ProcessedMaterial::Electronics, 0.0);
    destination.processedStockpile.set(ProcessedMaterial::IndustrialComposites, 20.0);
    destination.processorCapacity = 0.0;
    state.technicalFacilities.front().colonyId = destination.id;
    auto prototypeTeam =
        std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                     [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    prototypeTeam->colonyId = destination.id;
    FreightProgramCharter freight;
    freight.name = "Actual development Electronics delivery";
    freight.source = source.id;
    freight.destination = destination.id;
    freight.operatingBaseColonyId = source.id;
    freight.commodity = ProcessedMaterial::Electronics;
    freight.totalQuantity = 10.0;
    freight.requestedFleetId = state.fleets.back().id;
    freight.requestedLeaderId = state.people.front().id;
    TechnicalDevelopmentCharter technical{"Waiting on actual freight",
                                          state.technologyOpportunities.front().id,
                                          destination.id,
                                          state.technicalFacilities.front().id,
                                          prototypeTeam->id,
                                          state.people.front().id,
                                          TechnicalDevelopmentScope::DemonstratePrototype,
                                          {}};
    Simulation simulation(state);
    require(simulation.execute(CreateFreightProgramCommand{freight}).ok &&
                simulation.execute(CreateTechnicalDevelopmentCommand{technical}).ok,
            "freight-backed technical intents accepted");
    bool delivered = false;
    for (int day = 0; day < 120; ++day) {
        step(simulation);
        if (simulation.state().freightPrograms.front().cargoDelivered > 0.0) {
            delivered = true;
            require(simulation.state().technicalDevelopmentPrograms.front().stageWork == 0.0,
                    "actual opening unload cannot retroactively fund later technical dispatch");
            break;
        }
    }
    require(delivered, "actual finite freight reaches the development colony");
    step(simulation);
    require(simulation.state().technicalDevelopmentPrograms.front().stageWork > 0.0,
            "same technical intent consumes delivered material on the next opening");
}
} // namespace

int main() {
    try {
        accepted_wait_and_finite_facility();
        cancellation_and_scope_continuation();
        opening_stock_and_shared_facility();
        maintenance_ownership_is_shared_and_typed();
        actual_freight_delivery_waits_for_next_opening();
        std::cout << "Technical program edges: 3 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical program edge failure: " << error.what() << '\n';
        return 1;
    }
}
