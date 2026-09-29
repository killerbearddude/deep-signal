// Knowledge isolation, finite arbitration and lifecycle adversaries. Expected
// outputs compare complete owned projections, never a second hidden-world query.
#include "app/SimulationQueries.h"
#include "app/ForecastService.h"
#include "sim/ScenarioFactory.h"
#include "sim/AnalysisProgramRules.h"
#include "sim/GameStateValidation.h"
#include "sim/EquipmentServiceRules.h"
#include "save/SaveGameRepository.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
GameState fixture() {
    auto s = createDelegatedSurveyScenario();
    s.colonies.back().analysisCapacity = 1;
    s.ships.front().fuel = 1000;
    for (auto& c : s.colonies) {
        c.mines = 0;
        c.processorCapacity = 0;
    }
    s.surveyTeams.push_back({SurveyTeamId{s.ids.nextSurveyTeamId++}, "Independent scientist",
                             SurveyTeamLocationKind::Colony, s.colonies.back().id, std::nullopt});
    return s;
}
AnalysisCharter fixed(const GameState& s, SurveyTeamId team, std::vector<ObservationBatchId> batches) {
    return {"Fixed science",     s.colonies.back().id, FixedBatchInput{batches}, team,
            s.people.front().id, std::nullopt};
}
void manual(Simulation& sim) {
    require(sim.execute(ResourceSurveyCommand{sim.state().fleets.front().id,
                                              sim.state().fleets.front().currentBodyId})
                .ok,
            "Manual observation acquired");
}
void step(Simulation& sim) {
    require(sim.advanceDaysDetailed(1).advancedDays == 1, "Expected one real day");
    validateGameState(sim.state());
}
void compareKnowledge(const SimulationService& a, const SimulationService& b) {
    SimulationQueries qa(a), qb(b);
    ForecastService fa(a), fb(b);
    require(qa.bodySystemOverview() == qb.bodySystemOverview(),
            "Body counts/metadata leaked hidden physical rows");
    require(qa.explorationIntelligence() == qb.explorationIntelligence(),
            "Intelligence ordering/optional values/advice leaked truth");
    for (const auto& body : qa.bodySystemOverview()) {
        require(qa.bodyDeposits(body.id) == qb.bodyDeposits(body.id),
                "Complete declared channel projection leaked truth");
        require(qa.resourceSurveyPreview(a.state().fleets.front().id, body.id) ==
                    qb.resourceSurveyPreview(b.state().fleets.front().id, body.id),
                "Preview enabled state or warnings leaked truth");
    }
    require(fa.mineralIncomePerDay() == fb.mineralIncomePerDay() &&
                fa.mineralForecastCauseChains() == fb.mineralForecastCauseChains() &&
                fa.depositExhaustionEstimates() == fb.depositExhaustionEstimates(),
            "Forecast rows/counts/limits leaked hidden truth");
}
void complete_projection_isolation() {
    auto a = fixture(), b = a;
    b.mineralDeposits.clear();
    SimulationService sa(a), sb(b);
    compareKnowledge(sa, sb);
    // Acquire through the real command, then change only unobserved physical
    // state. Identical sealed records must lead to identical later findings.
    Simulation capture(a);
    manual(capture);
    a = capture.state();
    b = a;
    for (auto& d : b.mineralDeposits) {
        d.remaining += 12345;
        d.accessibility = 2.0;
    }
    b.mineralDeposits.erase(b.mineralDeposits.begin());
    // Change the sampled subject itself from no detection to a strong signal;
    // resampling current truth would now produce a different conclusion.
    b.mineralDeposits.push_back({a.observations.front().bodyId, Mineral::WaterIce, 9000, 1});
    SimulationService knownA(a), knownB(b);
    compareKnowledge(knownA, knownB);
    Simulation first(a), second(b);
    const auto c = fixed(a, a.surveyTeams.back().id, {a.observations.front().id});
    require(first.execute(CreateAnalysisProgramCommand{c}).ok &&
                second.execute(CreateAnalysisProgramCommand{c}).ok,
            "Same interpretation authorized");
    for (int i = 0; i < 3; ++i) {
        step(first);
        step(second);
    }
    require(first.state().analysisFindings == second.state().analysisFindings &&
                first.state().assessments == second.state().assessments,
            "Analysis sampled changed truth");
    compareKnowledge(SimulationService(first.state()), SimulationService(second.state()));
    // Neither new observations nor assessment is an admission requirement.
    const auto order = AssignShipyardBuildCommand{a.colonies.front().id, a.shipClasses.front().id, 1};
    require(sa.execute(order).ok && sb.execute(order).ok, "Unknown geology gated construction intent");
}
void laboratory_and_release_arbitration() {
    for (double capacity : {1.0, 2.0}) {
        auto s = fixture();
        s.colonies.back().analysisCapacity = capacity;
        Simulation sim(s);
        manual(sim);
        for (const auto& t : s.surveyTeams)
            require(
                sim.execute(CreateAnalysisProgramCommand{fixed(sim.state(), t.id, {ObservationBatchId{1}})})
                    .ok,
                "Independent analysis authorizes");
        for (int day = 1; day <= 6; ++day) {
            step(sim);
            double work = 0;
            for (const auto& p : sim.state().analysisPrograms)
                for (const auto& r : p.receipts)
                    if (r.day == day)
                        work += r.work;
            require(work <= capacity, "Programs overspent one opening laboratory");
        }
        require(sim.state().assessments.size() == 2 && sim.state().assessments[1].revision == 2 &&
                    sim.state().assessments[1].findingIds.size() == 2,
                "Distinct stable same-body publications lost earlier findings");
        require(sim.state().assessments[1].repeatedEvidence && !sim.state().assessments[1].contentChanged,
                "Second analyst invented certainty");
        if (capacity == 2)
            require(sim.state().assessments[0].publishedDay == 3 &&
                        sim.state().assessments[1].publishedDay == 3,
                    "Parallel teams cannot complete before three actual days");
    }
    auto s = fixture();
    s.colonies.back().analysisCapacity = 2;
    Simulation sim(s);
    manual(sim);
    auto c = fixed(sim.state(), s.surveyTeams.front().id, {ObservationBatchId{1}});
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok &&
                sim.execute(CreateAnalysisProgramCommand{c}).ok,
            "Contending intents accepted");
    for (int i = 0; i < 3; ++i)
        step(sim);
    require(analysisWork(sim.state().analysisPrograms[1]) == 0,
            "Released analyst borrowed again in same opening");
    step(sim);
    require(analysisWork(sim.state().analysisPrograms[1]) == 1, "Released analyst usable next opening");
}
void unready_replacement_and_cancellation() {
    auto s = fixture();
    s.colonies.back().analysisCapacity = 0;
    Simulation sim(s);
    manual(sim);
    auto c = fixed(sim.state(), s.surveyTeams.front().id, {ObservationBatchId{1}});
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok, "Zero lab preserves intent");
    step(sim);
    require(!sim.state().analysisPrograms.front().leasedTeamId, "Zero capacity must not seize staff");
    auto ready = sim.state();
    ready.colonies.back().analysisCapacity = 1;
    sim = Simulation(ready);
    step(sim);
    auto id = sim.state().analysisPrograms.front().id;
    require(sim.execute(AmendAnalysisProgramCommand{
                            id, {"Replacement", s.surveyTeams.back().id, c.requestedLeaderId, 2.0}})
                .ok,
            "Replace real analyst preserving documented work");
    require(!sim.state().analysisPrograms.front().leasedTeamId,
            "Amend releases old analyst at command boundary");
    step(sim);
    const auto& work = sim.state().analysisPrograms.front().receipts;
    require(work.size() == 2 && work[0].teamId != work[1].teamId && work[0].charterRevision == 1 &&
                work[1].charterRevision == 2,
            "Replacement rewrote work provenance");
    require(sim.execute(AmendAnalysisProgramCommand{
                            id, {"Lower allowance", s.surveyTeams.back().id, c.requestedLeaderId, 0.0}})
                .ok,
            "Authority may be below spent work");
    step(sim);
    require(analysisWork(sim.state().analysisPrograms.front()) == 2,
            "Lower allowance erased or replayed labor");
    require(sim.execute(CancelAnalysisProgramCommand{id}).ok, "Cancel partial analysis");
    require(sim.state().analysisPrograms.front().jobs.back().outcome == AnalysisJobOutcome::Cancelled &&
                sim.state().assessments.empty() && sim.state().observations.size() == 1,
            "Cancellation fabricated findings or destroyed raw evidence");
    require(!sim.execute(ResumeAnalysisProgramCommand{id}).ok, "Closed program remains terminal");
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok,
            "Retained raw input available to another authorization");
    for (int i = 0; i < 3; ++i)
        step(sim);
    require(sim.state().assessments.size() == 1,
            "New program performs actual remaining independent interpretation");
    auto reportCount = sim.state().analysisPrograms.front().reports.size();
    for (int i = 0; i < 60; ++i)
        step(sim);
    require(sim.state().analysisPrograms.front().reports.size() == reportCount,
            "Closed analysis continued reporting");
}
void rejection_and_empty_source() {
    auto s = fixture();
    Simulation sim(s);
    SurveyProgramCharter survey;
    survey.name = "Unready source";
    survey.homeColonyId = s.colonies.back().id;
    survey.targets = {{s.bodies.back().id, 0, 1}};
    require(sim.execute(CreateSurveyProgramCommand{survey}).ok, "Source can lack assignments");
    AnalysisCharter c{"Unready follower", s.colonies.back().id, FollowSurveyInput{SurveyProgramId{1}},
                      std::nullopt,       std::nullopt,         0.0};
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok,
            "Unready analysis accepted without inputs, team, leader or authority");
    for (int i = 0; i < 4; ++i)
        step(sim);
    require(sim.state().analysisPrograms.front().jobs.empty(), "Empty follower fabricated jobs");
    const auto original = sim.state().analysisPrograms;
    for (int mode = 0; mode < 6; ++mode) {
        auto bad = c;
        if (mode == 0)
            bad.name.clear();
        if (mode == 1)
            bad.requestedTeamId = SurveyTeamId{999};
        if (mode == 2)
            bad.workAllowance = -1;
        if (mode == 3)
            bad.workAllowance = std::numeric_limits<double>::quiet_NaN();
        if (mode == 4)
            bad.source = FixedBatchInput{};
        if (mode == 5)
            bad.source = FollowSurveyInput{SurveyProgramId{999}};
        require(!sim.execute(CreateAnalysisProgramCommand{bad}).ok &&
                    sim.state().analysisPrograms == original,
                "Malformed intent partially created program");
    }
    require(sim.execute(CancelSurveyProgramCommand{SurveyProgramId{1}}).ok,
            "Cancel source without observations");
    step(sim);
    require(sim.state().analysisPrograms.front().lifecycle == AnalysisLifecycle::Closed &&
                analysisSourceStatus(sim.state(), sim.state().analysisPrograms.front())
                        .find("no observation batches") != std::string::npos &&
                sim.state().assessments.empty(),
            "Empty source end must not mean no resources");
}
void changing_instrument_exposure() {
    // Controlled detached readiness changes stand for a repaired second hull.
    // Actual earned days must remain attributed to the original installations.
    auto s = fixture();
    s.ships.front().equipmentCondition.front().usedDuty = 117;
    auto other = s.ships.front();
    other.id = ShipId{s.ids.nextShipId++};
    other.name = "Second instrument";
    other.equipmentCondition.front().usedDuty = 120;
    other.fuel = 0;
    s.ships.push_back(other);
    s.fleets.front().shipIds.push_back(other.id);
    Simulation sim(s);
    SurveyProgramCharter c;
    c.name = "Changed contributions";
    c.homeColonyId = s.colonies.back().id;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.colonies.back().bodyId, 0, 1}};
    require(sim.execute(CreateSurveyProgramCommand{c}).ok, "Mixed exposure fieldwork accepted");
    for (int i = 0; i < 10 && sim.state().surveyPrograms.front().workDaysCompleted < 3; ++i)
        step(sim);
    auto detached = sim.state();
    detached.ships.back().equipmentCondition.front().usedDuty = 118;
    detached.surveyPrograms.front().issue.acknowledged = true;
    sim = Simulation(detached);
    for (int i = 0; i < 10 && sim.state().observations.empty(); ++i)
        step(sim);
    require(sim.state().observations.size() == 1, "Changed instruments still complete physical pass");
    const auto& b = sim.state().observations.front();
    require(b.instruments.size() == 2 && b.instruments[0].exposure.workdays == 3 &&
                b.instruments[1].exposure.workdays == 2,
            "Actual contributors retain three/two days without quantity acceleration");
    for (const auto& instrument : b.instruments)
        for (const auto& channel : instrument.channels)
            require(channel.indication == ResourceIndication::InsufficientExposure,
                    "Ending fleet granted unearned full-profile sensitivity");
}

void typed_head_order() {
    // Pure dispatch does not sort stored vectors, including an older second row.
    GameState s;
    SurveyProgram newer, older;
    newer.id = SurveyProgramId{2};
    newer.createdDay = 4;
    older.id = SurveyProgramId{1};
    older.createdDay = 1;
    s.surveyPrograms = {newer, older};
    FreightProgram freight;
    freight.id = FreightProgramId{1};
    freight.createdDay = 4;
    s.freightPrograms = {freight};
    MaintenanceProgram maintenance;
    maintenance.id = MaintenanceProgramId{1};
    maintenance.createdDay = 4;
    s.maintenancePrograms = {maintenance};
    AnalysisProgram analyst;
    analyst.id = AnalysisProgramId{1};
    analyst.createdDay = 4;
    analyst.leasedTeamId = SurveyTeamId{1};
    s.analysisPrograms = {analyst};
    const std::vector<ProgramController> expected = {SurveyProgramId{2}, SurveyProgramId{1},
                                                     FreightProgramId{1}, MaintenanceProgramId{1},
                                                     AnalysisProgramId{1}};
    require(programOpeningOrder(s) == expected,
            "Four-kind head-only tie order changed predecessor semantics");
    require(controllingScientificTeam(s, SurveyTeamId{1}) == ProgramController{AnalysisProgramId{1}} &&
                controllingScientificTeam(s, SurveyTeamId{1}) != ProgramController{SurveyProgramId{1}},
            "Equal numeric scientist owner namespaces aliased");
    require(!controllingProgram(s, FleetId{1}), "Analysis invented movement ownership");
}

void consequential_allowance_reaches_all_entry_points() {
    auto s = fixture();
    Simulation sim(s);
    SurveyProgramCharter c;
    c.name = "Continuing field input";
    c.homeColonyId = s.colonies.back().id;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, 2}};
    require(sim.execute(CreateSurveyProgramCommand{c}).ok, "Source authorizes");
    require(sim.execute(CreateAnalysisProgramCommand{{"Limited follower", s.colonies.back().id,
                                                      FollowSurveyInput{SurveyProgramId{1}},
                                                      s.surveyTeams.back().id, s.people.front().id, 3.0}})
                .ok,
            "Follower allowance adequate for first unknown future input");
    const auto result = sim.advanceDaysDetailed(100);
    require(result.interrupted && result.issueProgramId == ProgramController{AnalysisProgramId{1}} &&
                analysisWork(sim.state().analysisPrograms.front()) == 3 &&
                sim.state().observations.size() == 2,
            "New acquired backlog after real allowance spending raises typed interruption");
    const auto stopped = sim.state().date.day;
    require(sim.advanceDaysDetailed(5).advancedDays == 0,
            "Direct detailed advance skipped pending analysis issue");
    sim.advanceDays(5);
    require(sim.state().date.day == stopped, "Direct event advance skipped analysis issue");
    require(sim.execute(AdvanceDaysCommand{5}).ok && sim.state().date.day == stopped,
            "Command envelope skipped issue");
    SimulationService service(sim.state());
    require(service.advanceDaysDetailed(5).advancedDays == 0, "Service skipped fourth-kind issue");
    require(sim.execute(AcknowledgeAnalysisIssueCommand{AnalysisProgramId{1}, "analysis-work-allowance"}).ok,
            "Typed cause acknowledged");
    sim.advanceDays(3);
    require(sim.state().date.day == stopped + 3 && analysisWork(sim.state().analysisPrograms.front()) == 3,
            "Acknowledgement fabricated labor or re-interrupted unchanged limit");
}

GameState mixed_operations(bool analyze) {
    auto s = createMaintenanceSupplyScenario();
    const auto home = s.colonies[s.colonies.size() - 2].id;
    s.colonies[s.colonies.size() - 2].analysisCapacity = 1;
    s.surveyTeams.push_back({SurveyTeamId{s.ids.nextSurveyTeamId++}, "Real home analyst",
                             SurveyTeamLocationKind::Colony, home, std::nullopt});
    Simulation sim(s);
    MaintenanceProgramCharter provider;
    provider.name = "Real service";
    provider.serviceColonyId = home;
    provider.requestedTenderId = s.fleets[1].id;
    provider.requestedTeamId = s.maintenanceTeams.front().id;
    provider.requestedLeaderId = s.people.front().id;
    provider.clients = {s.fleets.front().id};
    require(sim.execute(CreateMaintenanceProgramCommand{provider}).ok, "Mixed service authorizes");
    SurveyProgramCharter field;
    field.name = "Real fieldwork";
    field.homeColonyId = home;
    field.requestedFleetId = s.fleets.front().id;
    field.requestedTeamId = s.surveyTeams.front().id;
    field.requestedLeaderId = s.people.front().id;
    field.targets = {{s.bodies.back().id, 0, 6}};
    field.policy.maintenanceProgramId = MaintenanceProgramId{1};
    require(sim.execute(CreateSurveyProgramCommand{field}).ok, "Mixed survey authorizes");
    for (int i = 0; i < 2; ++i) {
        FreightProgramCharter freight;
        freight.name = "Real parts";
        freight.sourceColonyId = s.colonies.back().id;
        freight.destinationColonyId = home;
        freight.material = i == 0 ? ProcessedMaterial::Electronics : ProcessedMaterial::IndustrialComposites;
        freight.totalQuantity = 20;
        freight.requestedFleetId = s.fleets[static_cast<std::size_t>(i) + 2].id;
        freight.requestedLeaderId = s.people.front().id;
        require(sim.execute(CreateFreightProgramCommand{freight}).ok, "Mixed supply freight authorizes");
    }
    if (analyze)
        require(sim.execute(CreateAnalysisProgramCommand{
                                {"Mixed real analysis", home, FollowSurveyInput{SurveyProgramId{1}},
                                 s.surveyTeams.back().id, s.people.front().id, std::nullopt}})
                    .ok,
                "Mixed analyst authorizes");
    std::optional<Simulation> reloaded;
    for (int i = 0; i < 100; ++i) {
        step(sim);
        if (reloaded) {
            step(*reloaded);
            require(sim.state().observations == reloaded->state().observations &&
                        sim.state().analysisPrograms == reloaded->state().analysisPrograms &&
                        sim.state().assessments == reloaded->state().assessments,
                    "Mixed field/freight/service/analysis continuation changed scientific state");
            for (std::size_t c = 0; c < sim.state().colonies.size(); ++c)
                require(sim.state().colonies[c].processedStockpile.amount ==
                            reloaded->state().colonies[c].processedStockpile.amount,
                        "Mixed continuation changed physical supply accounting");
        }
        if (analyze && i == 15) {
            const auto path =
                std::filesystem::temp_directory_path() /
                ("deep_signal_p4a_mixed_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
            save::SaveGameRepository::save(path, sim.state());
            reloaded.emplace(save::SaveGameRepository::load(path));
            std::filesystem::remove(path);
        }
    }
    return sim.state();
}
void mixed_physical_conservation() {
    const auto physical = mixed_operations(false), science = mixed_operations(true);
    require(science.assessments.size() == 6 && analysisWork(science.analysisPrograms.front()) == 18,
            "Mixed source earned six finite analyses");
    require(science.observations == physical.observations,
            "Analysis changed physical acquisition timing/provenance");
    for (std::size_t i = 0; i < physical.colonies.size(); ++i)
        require(physical.colonies[i].processedStockpile.amount ==
                        science.colonies[i].processedStockpile.amount &&
                    physical.colonies[i].stockpile.amount == science.colonies[i].stockpile.amount,
                "Analysis manufactured or consumed physical stock");
    for (std::size_t i = 0; i < physical.ships.size(); ++i) {
        require(physical.ships[i].fuel == science.ships[i].fuel, "Analysis changed propulsion accounting");
        for (std::size_t j = 0; j < physical.ships[i].equipmentCondition.size(); ++j)
            require(physical.ships[i].equipmentCondition[j].usedDuty ==
                        science.ships[i].equipmentCondition[j].usedDuty,
                    "Analysis restored or consumed survey duty");
    }
    require(physical.maintenancePrograms.front().teamWorkdays ==
                    science.maintenancePrograms.front().teamWorkdays &&
                physical.surveyPrograms.front().totalWorkDays == science.surveyPrograms.front().totalWorkDays,
            "Analyst borrowed engineer or changed field labor");
}

void publication_failure_boundaries() {
    auto s = fixture();
    s.ids.nextObservationBatchId = std::numeric_limits<std::int64_t>::max();
    Simulation blocked(s);
    bool rejected = false;
    try {
        manual(blocked);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && blocked.state().observations.empty() &&
                blocked.state().ships.front().equipmentCondition.front().usedDuty == 0,
            "Failed observation allocation charged physical duty");
    s = fixture();
    s.ids.nextEventId = std::numeric_limits<std::int64_t>::max();
    Simulation noAuditRoom(s);
    rejected = false;
    try {
        manual(noAuditRoom);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && noAuditRoom.state().observations.empty() &&
                noAuditRoom.state().ships.front().equipmentCondition.front().usedDuty == 0,
            "Exhausted audit identity space charged duty or published incomplete record");
    s = fixture();
    Simulation sim(s);
    manual(sim);
    require(sim.execute(CreateAnalysisProgramCommand{
                            fixed(sim.state(), s.surveyTeams.front().id, {ObservationBatchId{1}})})
                .ok,
            "Completion-failure job authorized");
    step(sim);
    step(sim);
    auto before = sim.state();
    before.ids.nextAssessmentId = std::numeric_limits<std::int64_t>::max();
    sim = Simulation(before);
    rejected = false;
    try {
        sim.advanceDays(1);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && analysisWork(sim.state().analysisPrograms.front()) == 2 &&
                sim.state().assessments.empty(),
            "Failed publication charged missing work or created free assessment");
    auto resumed = sim.state();
    resumed.ids.nextAssessmentId = 1;
    sim = Simulation(resumed);
    step(sim);
    require(analysisWork(sim.state().analysisPrograms.front()) == 3 && sim.state().assessments.size() == 1,
            "Retry lost documented work or published twice");
}

void unmanaged_and_quantity_exposure() {
    auto s = fixture();
    for (auto& c : s.shipComponents)
        if (c.id == ShipComponentId{4})
            c.serviceProfile.reset();
    s.ships.front().equipmentCondition.clear();
    for (auto& install : s.shipClasses.front().components)
        if (install.componentId == ShipComponentId{4})
            install.quantity = 2;
    const auto plan = prepareSurveyDuty(s, s.fleets.front(), 5);
    require(plan.changes.empty() && plan.contributors.size() == 1,
            "Unmanaged installation disappeared from contributor plan");
    Simulation sim(s);
    manual(sim);
    require(sim.state().observations.front().instruments.size() == 1 &&
                sim.state().observations.front().instruments.front().exposure.workdays == 5 &&
                sim.state().ships.front().equipmentCondition.empty(),
            "Quantity fabricated independent exposures or unmanaged wear");
}

} // namespace
int main() {
    try {
        complete_projection_isolation();
        laboratory_and_release_arbitration();
        unready_replacement_and_cancellation();
        rejection_and_empty_source();
        changing_instrument_exposure();
        typed_head_order();
        consequential_allowance_reaches_all_entry_points();
        mixed_physical_conservation();
        publication_failure_boundaries();
        unmanaged_and_quantity_exposure();
        std::cout << "10 science integration groups passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
