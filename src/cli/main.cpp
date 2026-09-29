#include "sim/Commands.h"
#include "sim/Events.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/EquipmentServiceRules.h"
#include "save/SaveGameRepository.h"

// CLI smoke runner for the headless simulation.
// This executable is a developer-facing verification tool, not the final UI. It
// exercises build, advance-time, fleet movement, and event printing paths.
// The explicit --write-freight-fixture option exports the dedicated P3B scenario
// as a current-format save for inspecting the same physical loop in the UI.

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Visitor used to print typed event payloads without converting the event model
// into unstructured strings inside the simulation core.
struct EventPrinter {
    void operator()(const deep::MineralExtractedEvent& event) const {
        std::cout << "  Mineral extracted: colony=" << event.colonyId.value
                  << " mineral=" << deep::toString(event.mineral)
                  << " amount=" << event.amount << '\n';
    }

    void operator()(const deep::ShipyardOrderCreatedEvent& event) const {
        std::cout << "  Shipyard order created: order=" << event.orderId.value
                  << " quantity=" << event.quantity << '\n';
    }

    void operator()(const deep::ShipClassRevisionCreatedEvent& event) const {
        std::cout << "  Ship class revision created: class=" << event.shipClassId.value
                  << " revision=" << event.revision << '\n';
    }

    void operator()(const deep::ShipCompletedEvent& event) const {
        std::cout << "  Ship completed: ship=" << event.shipId.value
                  << " fleet=" << event.fleetId.value << '\n';
    }

    void operator()(const deep::FleetOrderAssignedEvent& event) const {
        std::cout << "  Fleet order assigned: fleet=" << event.fleetId.value
                  << " destination=" << event.destinationBodyId.value
                  << " eta_days=" << event.daysRemaining << '\n';
    }

    void operator()(const deep::FleetArrivedEvent& event) const {
        std::cout << "  Fleet arrived: fleet=" << event.fleetId.value
                  << " body=" << event.destinationBodyId.value << '\n';
    }

    void operator()(const deep::ResourceSurveyCompletedEvent& event) const {
        std::cout << "  Resource survey completed: fleet=" << event.fleetId.value
                  << " body=" << event.bodyId.value
                  << " observation_batch=" << event.observationBatchId.value << '\n';
    }

    void operator()(const deep::SurveyProgramAuditEvent& event) const {
        std::cout << "  Survey program " << event.programId.value
                  << " audit " << static_cast<int>(event.kind)
                  << ": " << event.detail << '\n';
    }

    void operator()(const deep::FreightProgramAuditEvent& event) const {
        std::cout << "  Freight program " << event.programId.value
                  << " shipment=" << event.shipmentNumber
                  << " audit=" << static_cast<int>(event.kind)
                  << " amount=" << event.amount << ": " << event.detail << '\n';
    }
    void operator()(const deep::EquipmentDutyUsedEvent& event) const {
        std::cout << "  Survey duty: ship=" << event.shipId.value << " component=" << event.componentId.value << " duty=" << event.duty << '\n';
    }
    void operator()(const deep::MaintenanceProgramAuditEvent& event) const {
        std::cout << "  Maintenance program " << event.programId.value << " job=" << event.jobNumber << ": " << event.detail << '\n';
    }

    void operator()(const deep::AnalysisProgramAuditEvent& event) const {
        std::cout << "  Analysis program " << event.programId.value << " job=" << event.jobId.value << ": " << event.detail << '\n';
    }

    void operator()(const deep::CommandRejectedEvent& event) const {
        std::cout << "  Command rejected: " << event.reason << '\n';
    }
};

// Prints the events returned by time advancement. Events appended by execute()
// live in the state log and are not automatically part of these returned batches.
void printEvents(const std::vector<deep::SimEvent>& events) {
    for (const deep::SimEvent& event : events) {
        std::cout << "Day " << event.day << " event " << event.id.value << ':' << '\n';
        std::visit(EventPrinter{}, event.payload);
    }
}

// All CLI time requests report actual elapsed days and typed issue ownership,
// using the same interruption-aware runner as service and UI callers.
void printAdvance(const deep::Simulation& sim, const deep::AdvanceResult& result) {
    printEvents(result.events);
    std::cout << "Advanced " << result.advancedDays << " of " << result.requestedDays << " days\n";
    if (result.interrupted) {
        if (result.issueProgramId) std::cout << deep::programControllerLabel(sim.state(), *result.issueProgramId) << ": ";
        std::cout << result.stopReason << '\n';
    }
}

} // namespace

// Runs the deterministic smoke scenario. Rejected commands and a missing built
// fleet return non-zero; the final location is printed but not asserted. Use the
// regression tests for arrival correctness; exit zero alone does not prove arrival.
int main(int argc, char** argv) {
    if (argc == 3 && (std::string_view{argv[1]} == "--write-evidence-fixture" ||
                       std::string_view{argv[1]} == "--write-evidence-concurrent-fixture")) {
        try {
            // Authored physical assets only. Every observation and assessment
            // below is earned through ordinary survey/analysis commands and days.
            const bool separate=std::string_view{argv[1]}=="--write-evidence-concurrent-fixture";
            auto state=deep::createDelegatedSurveyScenario();
            state.colonies.back().analysisCapacity=1;
            state.ships.front().fuel=1000;
            auto revision=state.shipClasses.front();revision.id=deep::ShipClassId{state.ids.nextShipClassId++};
            revision.name="Characterization Cutter";revision.basedOnClassId.reset();revision.revision=1;
            for(auto& install:revision.components)if(install.componentId==deep::ShipComponentId{4})install.componentId=deep::ShipComponentId{7};
            state.shipClasses.push_back(revision);
            auto ship=state.ships.front();ship.id=deep::ShipId{state.ids.nextShipId++};ship.shipClassId=revision.id;
            ship.name="Characterization instrument";ship.equipmentCondition.clear();
            deep::initializeShipEquipmentCondition(state,ship);state.ships.push_back(ship);state.fleets.front().shipIds.push_back(ship.id);
            state.mineralDeposits.push_back({state.colonies.back().bodyId,deep::Mineral::WaterIce,100,.4});
            state.mineralDeposits.push_back({state.bodies.back().id,deep::Mineral::WaterIce,100,.4});
            if(separate)state.surveyTeams.push_back({deep::SurveyTeamId{state.ids.nextSurveyTeamId++},"Home laboratory scientist",
                deep::SurveyTeamLocationKind::Colony,state.colonies.back().id,std::nullopt});
            deep::Simulation fixture(state);
            const auto accept=[&](const deep::SimCommand& c){auto r=fixture.execute(c);if(!r.ok)throw std::runtime_error(r.message);};
            accept(deep::ResourceSurveyCommand{state.fleets.front().id,state.colonies.back().bodyId});
            accept(deep::CreateAnalysisProgramCommand{{"Initial local interpretation",state.colonies.back().id,
                deep::FixedBatchInput{{deep::ObservationBatchId{1}}},state.surveyTeams.front().id,state.people.front().id,std::nullopt}});
            fixture.advanceDays(3);
            deep::SurveyProgramCharter field;field.name="Two transmitted field passes";field.homeColonyId=state.colonies.back().id;
            field.requestedFleetId=state.fleets.front().id;field.requestedTeamId=state.surveyTeams.front().id;
            field.requestedLeaderId=state.people.front().id;field.targets={{state.bodies.back().id,0,2}};
            accept(deep::CreateSurveyProgramCommand{field});
            accept(deep::CreateAnalysisProgramCommand{{"Standing field analysis",state.colonies.back().id,
                deep::FollowSurveyInput{deep::SurveyProgramId{1}},state.surveyTeams.back().id,state.people.front().id,std::nullopt}});
            for(int n=0;n<100 && fixture.state().observations.size()<2;++n) {
                const auto r=fixture.advanceDaysDetailed(1);
                if(r.interrupted)throw std::runtime_error(r.stopReason);
            }
            fixture.advanceDays(1); // Makes the first transmitted batch eligible.
            deep::save::SaveGameRepository::save(argv[2],fixture.state());
            std::cout<<"Wrote v16 earned evidence fixture: "<<argv[2]<<'\n';return 0;
        } catch(const std::exception& e){std::cerr<<"Evidence fixture export failed: "<<e.what()<<'\n';return 1;}
    }
    if (argc == 3 && std::string_view{argv[1]} == "--write-maintenance-fixture") {
        try {
            deep::Simulation fixture{deep::createMaintenanceSupplyScenario()};
            const auto& s=fixture.state();
            const auto home=s.colonies.at(s.colonies.size()-2).id;
            deep::MaintenanceProgramCharter support;
            support.name="Colony instrument servicing"; support.serviceColonyId=home;
            support.requestedTenderId=s.fleets.at(1).id; support.requestedTeamId=s.maintenanceTeams.front().id;
            support.requestedLeaderId=s.people.front().id; support.clients={s.fleets.front().id};
            if(!fixture.execute(deep::CreateMaintenanceProgramCommand{support}).ok) throw std::runtime_error("Provider authorization failed");
            deep::SurveyProgramCharter survey;
            survey.name="Survey awaiting freight-supplied service"; survey.homeColonyId=home;
            survey.requestedFleetId=s.fleets.front().id; survey.requestedTeamId=s.surveyTeams.front().id;
            survey.requestedLeaderId=s.people.front().id; survey.targets={{s.bodies.back().id,0,6}};
            survey.policy.maintenanceProgramId=s.maintenancePrograms.front().id;
            if(!fixture.execute(deep::CreateSurveyProgramCommand{survey}).ok) throw std::runtime_error("Survey authorization failed");
            for(int i=0;i<2;++i) {
                deep::FreightProgramCharter freight;
                freight.name=i==0?"Supply Electronics":"Supply Composites";
                freight.sourceColonyId=s.colonies.back().id; freight.destinationColonyId=home;
                freight.material=i==0?deep::ProcessedMaterial::Electronics:deep::ProcessedMaterial::IndustrialComposites;
                freight.totalQuantity=20.0; freight.requestedFleetId=s.fleets.at(static_cast<std::size_t>(i)+2).id;
                freight.requestedLeaderId=s.people.front().id;
                if(!fixture.execute(deep::CreateFreightProgramCommand{freight}).ok) throw std::runtime_error("Parts freight authorization failed");
            }
            deep::save::SaveGameRepository::save(argv[2],fixture.state());
            std::cout<<"Wrote current-schema maintenance supply fixture: "<<argv[2]<<'\n'; return 0;
        } catch(const std::exception& error) { std::cerr<<"Fixture export failed: "<<error.what()<<'\n'; return 1; }
    }
    if (argc == 3 && std::string_view{argv[1]} == "--write-freight-fixture") {
        try {
            deep::Simulation fixture{deep::createDelegatedFreightScenario()};
            const auto& state = fixture.state();
            deep::SurveyProgramCharter survey;
            survey.name = "Receiving survey awaiting supplied Propellant";
            survey.homeColonyId = state.colonies.back().id;
            survey.requestedFleetId = state.fleets.front().id;
            survey.requestedTeamId = state.surveyTeams.front().id;
            survey.requestedLeaderId = state.people.front().id;
            survey.targets = {{state.bodies.back().id, 0, 2}};
            deep::FreightProgramCharter freight;
            freight.name = "Deliver 500 Propellant to receiving survey base";
            freight.sourceColonyId = state.colonies.at(state.colonies.size()-2).id;
            freight.destinationColonyId = state.colonies.back().id;
            freight.material = deep::ProcessedMaterial::Propellant;
            freight.totalQuantity = 500.0;
            freight.requestedFleetId = state.fleets.back().id;
            freight.requestedLeaderId = state.people.front().id;
            if (!fixture.execute(deep::CreateSurveyProgramCommand{survey}).ok ||
                !fixture.execute(deep::CreateFreightProgramCommand{freight}).ok) {
                throw std::runtime_error("Freight inspection fixture authorization failed");
            }
            deep::save::SaveGameRepository::save(argv[2], fixture.state());
            std::cout << "Wrote current-schema freight inspection fixture: " << argv[2] << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Fixture export failed: " << error.what() << '\n'; return 1;
        }
    }
    if (argc != 1) {
        std::cerr << "Usage: deep_signal_cli [--write-freight-fixture PATH | --write-maintenance-fixture PATH | --write-evidence-fixture PATH | --write-evidence-concurrent-fixture PATH]\n";
        return 1;
    }
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    const auto buildResult = sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    });

    if (!buildResult.ok) {
        std::cerr << "Failed to assign build order: " << buildResult.message << '\n';
        return 1;
    }

    printAdvance(sim, sim.advanceDaysDetailed(5));

    if (sim.state().fleets.empty()) {
        std::cerr << "Expected shipyard to create one fleet after five days.\n";
        return 1;
    }

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    // Scenario-specific ordering: Terra is first and Mars second. Resolve by an
    // explicit scenario identifier before allowing alternate scenarios here.
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    const auto moveResult = sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    });

    if (!moveResult.ok) {
        std::cerr << "Failed to assign move order: " << moveResult.message << '\n';
        return 1;
    }

    printAdvance(sim, sim.advanceDaysDetailed(5));

    std::cout << "\nDeep Signal smoke run complete\n";
    std::cout << "Day: " << sim.state().date.day << '\n';
    std::cout << "Ships: " << sim.state().ships.size() << '\n';
    std::cout << "Fleets: " << sim.state().fleets.size() << '\n';
    std::cout << "Fleet body: " << sim.state().fleets.front().currentBodyId.value << '\n';
    std::cout << "Event log: " << sim.state().eventLog.size() << '\n';

    return 0;
}
