// Exercises the investigated supply path from new ship orders through finite
// science, field construction, freight collection and real processing output.
#include "app/P6ProvingFixture.h"
#include "app/SiteDevelopmentFixture.h"
#include "sim/GameStateValidation.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <variant>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void established_investigated_supply_chain() {
    const auto run = deep::earnP6EstablishedLoop();
    const auto& mature = run.mature;
    require(run.starting.ships.empty() && run.starting.observations.empty() &&
                run.starting.assessments.empty() && run.starting.resourceSites.empty(),
            "P6 starts without purpose-built hulls, acquired science or site hardware");
    require(run.shipsBuilt.ships.size() == 7 &&
                run.shipsBuilt.shipClasses.size() > run.starting.shipClasses.size(),
            "immutable design and all required hulls are earned through commands and shipyard work");
    require(!run.observationAcquired.observations.empty() && run.observationAcquired.assessments.empty() &&
                !run.assessmentPublished.assessments.empty(),
            "field observation precedes finite analysis and an assessment");
    require(run.siteAuthorized.resourceSites.size() == 1 &&
                run.siteAuthorized.resourceSites.front().installed.empty(),
            "site authorization creates no installed physical capability");
    require(!run.sitePartiallyAssembled.siteDevelopmentPrograms.front().workReceipts.empty() &&
                run.freightInTransit.date.day > run.siteAuthorized.date.day,
            "site labor and physical freight are earned at later boundaries");
    require(mature.siteDevelopmentPrograms.front().lifecycle == deep::SiteDevelopmentLifecycle::Closed &&
                mature.resourceSites.front().installed.size() == 5 &&
                !mature.resourceSites.front().extractionReceipts.empty() &&
                mature.freightPrograms.back().cargoDelivered > 0 &&
                mature.colonies.back().processedProductionTotals.get(deep::ProcessedMaterial::Propellant) > 0,
            "commissioned site supplies Ice for delivered industrial Propellant");
    const auto& assignment = run.siteAuthorized.siteDevelopmentPrograms.front().charter.assignments;
    const auto builder = std::find_if(mature.fleets.begin(), mature.fleets.end(),
                                      [&](const auto& fleet) { return fleet.id == *assignment.builderId; });
    const auto team = std::find_if(mature.maintenanceTeams.begin(), mature.maintenanceTeams.end(),
                                   [&](const auto& value) { return value.id == *assignment.teamId; });
    require(builder != mature.fleets.end() && team != mature.maintenanceTeams.end() &&
                builder->currentBodyId == mature.colonies.back().bodyId &&
                builder->activeOrder.type == deep::FleetOrderType::None &&
                team->location == deep::MaintenanceTeamLocation::Colony &&
                team->colonyId == mature.colonies.back().id,
            "builders physically return and disembark while commissioned site persists");
    require(mature.technicalDevelopmentPrograms.empty() && mature.developedComponentRevisions.empty(),
            "established equipment reaches a useful outcome without optional P5 work");
    deep::validateGameState(mature);
}

void blind_useful_and_zero_yield_twins() {
    const auto useful = deep::earnSiteDevelopmentFixture(90, true);
    const auto absent = deep::earnSiteDevelopmentFixture(90, false);
    require(useful.observations.empty() && absent.observations.empty() && useful.assessments.empty() &&
                absent.assessments.empty(),
            "neither blind investment was gated on scientific evidence");
    require(useful.siteDevelopmentPrograms.front().lifecycle == deep::SiteDevelopmentLifecycle::Closed &&
                absent.siteDevelopmentPrograms.front().lifecycle == deep::SiteDevelopmentLifecycle::Closed &&
                useful.resourceSites.front().installed == absent.resourceSites.front().installed,
            "identical unsurveyed construction succeeds physically in both hidden worlds");
    require(!useful.resourceSites.front().extractionReceipts.empty() &&
                useful.freightPrograms.back().cargoDelivered > 0 &&
                useful.colonies.back().processedProductionTotals.get(deep::ProcessedMaterial::Propellant) > 0,
            "blind useful investment recovers and delivers Ice for real production");
    require(absent.resourceSites.front().extractionReceipts.size() >= 5 &&
                absent.resourceSites.front().issue.cause == deep::SiteOperatingIssueCause::ZeroRecovery &&
                absent.freightPrograms.back().cargoDelivered == 0 &&
                absent.colonies.back().processedProductionTotals.get(deep::ProcessedMaterial::Propellant) ==
                    0,
            "blind absent investment pays to attempt operation and observes zero recovery");
    for (const auto& receipt : absent.resourceSites.front().extractionReceipts)
        require(receipt.recoveredIce == 0, "physical absence produces no fabricated Ice");
    deep::validateGameState(useful);
    deep::validateGameState(absent);
}

void acquired_evidence_does_not_increase_physical_yield() {
    deep::Simulation simulation(deep::earnSiteDevelopmentFixture(90, true));
    const auto base = simulation.state().colonies.back().id;
    const auto target = simulation.state().resourceSites.front().bodyId;
    require(simulation
                .execute(deep::AssignShipyardBuildCommand{base, simulation.state().shipClasses.front().id, 1})
                .ok,
            "ordinary survey hull ordered for information-only counterfactual");
    require(simulation.advanceDaysDetailed(1).advancedDays == 1, "survey hull is physically commissioned");
    const auto fleet = simulation.state().fleets.back().id;
    require(simulation.execute(deep::MoveFleetCommand{fleet, target}).ok,
            "field observation requires a real movement order");
    for (int day = 0; day < 20 && simulation.state().fleets.back().currentBodyId != target; ++day)
        require(simulation.advanceDaysDetailed(1).advancedDays == 1,
                "survey hull physically reaches the operating target");
    require(simulation.state().fleets.back().currentBodyId == target &&
                simulation.execute(deep::ResourceSurveyCommand{fleet, target}).ok,
            "real instrument acquires site evidence at the target");
    auto informed = simulation.state(), uninformed = informed;
    uninformed.observations.clear();
    std::erase_if(uninformed.eventLog, [](const auto& event) {
        return std::holds_alternative<deep::ResourceSurveyCompletedEvent>(event.payload);
    });
    deep::Simulation withEvidence(informed), withoutEvidence(uninformed);
    for (int day = 0; day < 15; ++day) {
        require(withEvidence.advanceDaysDetailed(1).advancedDays == 1 &&
                    withoutEvidence.advanceDaysDetailed(1).advancedDays == 1,
                "same physical worlds continue under the same existing site authority");
    }
    require(withEvidence.state().resourceSites.front().dutyReceipts ==
                    withoutEvidence.state().resourceSites.front().dutyReceipts &&
                withEvidence.state().resourceSites.front().extractionReceipts ==
                    withoutEvidence.state().resourceSites.front().extractionReceipts &&
                withEvidence.state().resourceSites.front().rawStock.amount ==
                    withoutEvidence.state().resourceSites.front().rawStock.amount,
            "survey knowledge changes decisions but supplies no physical extraction multiplier");
}
} // namespace

int main() {
    try {
        established_investigated_supply_chain();
        blind_useful_and_zero_yield_twins();
        acquired_evidence_does_not_increase_physical_yield();
        std::cout << "P6 established investigated whole loop passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 whole-loop failure: " << error.what() << '\n';
        return 1;
    }
}
