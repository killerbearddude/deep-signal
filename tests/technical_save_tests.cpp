// v18 round-trip and continuation proof for partial technical work, physical
// prototype/test evidence, dynamic component/profile publication, and counters.
#include "save/SaveGameRepository.h"
#include "save/Database.h"
#include "app/SimulationService.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
TechnicalDevelopmentCharter charter(const GameState& state) {
    const auto team = std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    return {.name = "Persisted precision development",
            .opportunityId = state.technologyOpportunities.front().id,
            .developmentColonyId = state.technicalFacilities.front().colonyId,
            .requestedFacilityId = state.technicalFacilities.front().id,
            .requestedTeamId = team->id,
            .requestedLeaderId = state.people.front().id,
            .scope = TechnicalDevelopmentScope::ProductionAndSupportReady,
            .policy = {}};
}
GameState fixture() {
    auto state = createHomeSystemScenario();
    auto& colony = *std::find_if(state.colonies.begin(), state.colonies.end(), [&](const auto& row) {
        return row.id == state.technicalFacilities.front().colonyId;
    });
    colony.processedStockpile.amount.fill(2'000.0);
    colony.processorCapacity = 0.0;
    return state;
}
void compare(const GameState& a, const GameState& b) {
    require(a.date.day == b.date.day && a.shipComponents.size() == b.shipComponents.size() &&
                a.measurementProfiles.size() == b.measurementProfiles.size() &&
                a.technicalDevelopmentPrograms.size() == b.technicalDevelopmentPrograms.size() &&
                a.prototypeDesigns.size() == b.prototypeDesigns.size() &&
                a.prototypeComponentUnits.size() == b.prototypeComponentUnits.size() &&
                a.technicalTestRecords.size() == b.technicalTestRecords.size() &&
                a.developedComponentRevisions.size() == b.developedComponentRevisions.size() &&
                a.componentProductionCapabilities.size() == b.componentProductionCapabilities.size() &&
                a.supportQualificationRecords.size() == b.supportQualificationRecords.size() &&
                a.prototypeIntegrationReceipts.size() == b.prototypeIntegrationReceipts.size() &&
                a.eventLog.size() == b.eventLog.size(),
            "v18 technical state cardinality differs after round trip");
    const auto& ap = a.technicalDevelopmentPrograms.front();
    const auto& bp = b.technicalDevelopmentPrograms.front();
    require(ap.stage == bp.stage && ap.stageWork == bp.stageWork &&
                ap.stageConsumed.amount == bp.stageConsumed.amount &&
                ap.receipts.size() == bp.receipts.size() && ap.issue.signature == bp.issue.signature,
            "v18 partial technical program differs after round trip");
    require(a.ids.nextTechnicalTestId == b.ids.nextTechnicalTestId &&
                a.ids.nextShipComponentId == b.ids.nextShipComponentId &&
                a.ids.nextMeasurementProfileId == b.ids.nextMeasurementProfileId,
            "v18 dynamic identity counters differ after round trip");
    require(a.shipyardOrders.size() == b.shipyardOrders.size(), "v18 shipyard order cardinality differs");
    if (!a.shipyardOrders.empty())
        require(a.shipyardOrders.back().currentHullSupplyPlan ==
                    b.shipyardOrders.back().currentHullSupplyPlan,
                "v18 frozen current-hull supply plan differs");
    if (!a.prototypeComponentUnits.empty())
        require(a.prototypeComponentUnits.front().state == b.prototypeComponentUnits.front().state &&
                    a.prototypeComponentUnits.front().reservedOrderId ==
                        b.prototypeComponentUnits.front().reservedOrderId,
                "v18 prototype reservation state differs");
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void malformedSnapshotsReject(const GameState& valid, const std::filesystem::path& source) {
    const std::vector<std::string> mutations{
        "UPDATE technical_test_records SET measured=measured+1 WHERE sequence=1;",
        "UPDATE prototype_component_units SET "
        "state=1,reserved_order_id=NULL,reserved_hull=999,consumed_ship_id=NULL;",
        "UPDATE component_production_capabilities SET available_day=qualified_day;",
        "UPDATE support_qualifications SET available_day=qualified_day;",
        "UPDATE prototype_integration_receipts SET hull_number=999;",
        "UPDATE technical_development_programs SET ordinal=9 WHERE ordinal=0;"};
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto bad = source.parent_path() / ("deep-signal-p5-bad-" + std::to_string(index) + ".sqlite");
        std::filesystem::copy_file(source, bad, std::filesystem::copy_options::overwrite_existing);
        {
            save::Database db(bad);
            db.execute("PRAGMA ignore_check_constraints=ON;");
            db.execute(mutations[index]);
        }
        const auto before = bytes(bad);
        SimulationService active(valid);
        const auto load = active.loadGame(bad);
        if (load.ok || active.state().date.day != valid.date.day)
            throw std::runtime_error("Malformed v18 case " + std::to_string(index) +
                                     " accepted or changed active world: " + mutations[index] + " / " +
                                     load.message);
        bool refused = false;
        try {
            save::SaveGameRepository::save(bad, valid);
        } catch (const std::exception&) {
            refused = true;
        }
        require(refused && bytes(bad) == before, "malformed v18 destination was overwritten or modified");
        std::filesystem::remove(bad);
    }
}
void malformedPlanReject(const GameState& valid, const std::filesystem::path& source) {
    const std::vector<std::string> mutations{
        "UPDATE shipyard_current_supply_plans SET effective_bp=effective_bp+1;",
        "UPDATE shipyard_supply_plan_costs SET amount=amount+1 WHERE material=1;"};
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto bad =
            source.parent_path() / ("deep-signal-p5-bad-plan-" + std::to_string(index) + ".sqlite");
        std::filesystem::copy_file(source, bad, std::filesystem::copy_options::overwrite_existing);
        {
            save::Database db(bad);
            db.execute("PRAGMA ignore_check_constraints=ON;");
            db.execute(mutations[index]);
        }
        const auto before = bytes(bad);
        SimulationService active(valid);
        require(!active.loadGame(bad).ok && active.state().date.day == valid.date.day,
                "malformed v18 current-hull plan was accepted or changed active world");
        bool refused = false;
        try {
            save::SaveGameRepository::save(bad, valid);
        } catch (const std::exception&) {
            refused = true;
        }
        require(refused && bytes(bad) == before, "malformed v18 current-hull plan destination was modified");
        std::filesystem::remove(bad);
    }
}
void reportRoundTrip(const std::filesystem::path& path) {
    auto state = fixture();
    auto waiting = charter(state);
    waiting.requestedFacilityId.reset();
    waiting.requestedTeamId.reset();
    waiting.requestedLeaderId.reset();
    Simulation simulation(state);
    require(simulation.execute(CreateTechnicalDevelopmentCommand{waiting}).ok,
            "waiting technical report fixture accepted");
    require(simulation.advanceDaysDetailed(90).advancedDays == 90 &&
                simulation.state().technicalDevelopmentPrograms.front().reports.size() == 3 &&
                simulation.state().technicalDevelopmentPrograms.front().reports.back().isNinetyDayReview,
            "waiting program publishes day-30/60 reports and a day-90 review");
    save::SaveGameRepository::save(path, simulation.state());
    const auto loaded = save::SaveGameRepository::load(path);
    require(
        loaded.technicalDevelopmentPrograms.front().reports.size() == 3 &&
            loaded.technicalDevelopmentPrograms.front().reports.front().waitingReason ==
                simulation.state().technicalDevelopmentPrograms.front().reports.front().waitingReason &&
            loaded.technicalDevelopmentPrograms.front().reports.front().periodConsumed.amount ==
                simulation.state().technicalDevelopmentPrograms.front().reports.front().periodConsumed.amount,
        "v18 technical report policy/material/waiting snapshot round-trips");
    {
        save::Database db(path);
        db.execute("PRAGMA ignore_check_constraints=ON;");
        db.execute("UPDATE technical_development_reports SET period_work=period_work+1 WHERE ordinal=0;");
    }
    const auto before = bytes(path);
    SimulationService active(simulation.state());
    require(!active.loadGame(path).ok && active.state().date.day == simulation.state().date.day &&
                bytes(path) == before,
            "malformed technical report is rejected without changing active world or source file");
}
} // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("deep-signal-p5-save-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    try {
        Simulation simulation(fixture());
        require(simulation.execute(CreateTechnicalDevelopmentCommand{charter(simulation.state())}).ok,
                "technical intent accepted before persistence checkpoints");
        for (int day = 0; day < 10; ++day) {
            require(simulation.advanceDaysDetailed(1).advancedDays == 1,
                    "daily technical persistence checkpoint advances");
            save::SaveGameRepository::save(path, simulation.state());
            auto daily = save::SaveGameRepository::load(path);
            compare(simulation.state(), daily);
            simulation = Simulation(std::move(daily));
        }
        require(simulation.state().prototypeComponentUnits.size() == 1 &&
                    simulation.state().componentProductionCapabilities.empty() &&
                    simulation.state().developedComponentRevisions.empty(),
                "physical prototype exists before demonstration and creates no factory/catalog entry");
        validateGameState(simulation.state());
        save::SaveGameRepository::save(path, simulation.state());
        auto loaded = save::SaveGameRepository::load(path);
        compare(simulation.state(), loaded);
        Simulation left(simulation.state()), right(loaded);
        require(left.advanceDaysDetailed(2).advancedDays == right.advanceDaysDetailed(2).advancedDays,
                "same-input post-load testing continuation elapsed differently");
        const auto component = left.state().developedComponentRevisions.front().componentId;
        auto installs = referenceSurveyCutterComponents();
        installs.at(3).componentId = component;
        for (Simulation* branch : {&left, &right}) {
            require(branch
                        ->execute(CreateShipClassRevisionCommand{"Persisted prototype hull", ShipRole::Survey,
                                                                 std::nullopt, installs})
                        .ok,
                    "prototype class persists through command boundary");
            require(
                branch
                    ->execute(AssignShipyardBuildCommand{branch->state().technicalFacilities.front().colonyId,
                                                         branch->state().shipClasses.back().id, 1})
                    .ok,
                "prototype order persists through command boundary");
            require(branch->advanceDaysDetailed(1).advancedDays == 1,
                    "prototype reservation opening advances");
        }
        require(left.state().prototypeComponentUnits.front().state ==
                    PrototypeComponentState::ReservedForShipyard,
                "representative checkpoint contains a real prototype reservation");
        compare(left.state(), right.state());
        save::SaveGameRepository::save(path, right.state());
        compare(right.state(), save::SaveGameRepository::load(path));
        malformedPlanReject(right.state(), path);
        right = Simulation(save::SaveGameRepository::load(path));
        require(left.advanceDaysDetailed(6).advancedDays == right.advanceDaysDetailed(6).advancedDays,
                "same-input reserved-prototype continuation elapsed differently");
        compare(left.state(), right.state());
        validateGameState(right.state());
        save::SaveGameRepository::save(path, right.state());
        compare(right.state(), save::SaveGameRepository::load(path));
        malformedSnapshotsReject(right.state(), path);
        reportRoundTrip(path);
        std::filesystem::remove(path);
        std::cout << "Technical v18 save/continuation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Technical save failure: " << error.what() << '\n';
        return 1;
    }
}
