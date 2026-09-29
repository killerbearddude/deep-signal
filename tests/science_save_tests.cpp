// Every-day field/analysis checkpoints compare all new domain fields directly,
// then continue under identical inputs. Unique paths avoid predecessor collisions.
#include "sim/ScenarioFactory.h"
#include "app/SimulationService.h"
#include "sim/Simulation.h"
#include "save/SaveGameRepository.h"
#include "save/Database.h"
#include "save/EventJson.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void equivalent(const GameState& a, const GameState& b) {
    require(a.measurementProfiles == b.measurementProfiles && a.observations == b.observations &&
                a.analysisPrograms == b.analysisPrograms && a.analysisFindings == b.analysisFindings &&
                a.assessments == b.assessments,
            "Scientific records differ across save boundary");
    require(a.ids.nextMeasurementProfileId == b.ids.nextMeasurementProfileId &&
                a.ids.nextObservationBatchId == b.ids.nextObservationBatchId &&
                a.ids.nextAnalysisProgramId == b.ids.nextAnalysisProgramId &&
                a.ids.nextAnalysisJobId == b.ids.nextAnalysisJobId &&
                a.ids.nextAssessmentId == b.ids.nextAssessmentId,
            "Science counters differ");
    for (std::size_t i = 0; i < a.surveyPrograms.size(); ++i)
        require(a.surveyPrograms[i].exposures == b.surveyPrograms[i].exposures &&
                    a.surveyPrograms[i].workDates == b.surveyPrograms[i].workDates,
                "Partial exposure provenance differs");
    require(a.eventLog.size() == b.eventLog.size(), "Audit count differs");
    for (std::size_t i = 0; i < a.eventLog.size(); ++i)
        require(a.eventLog[i].id == b.eventLog[i].id && a.eventLog[i].day == b.eventLog[i].day &&
                    save::eventPayloadToJson(a.eventLog[i].payload) ==
                        save::eventPayloadToJson(b.eventLog[i].payload),
                "Audit content/order differs");
}
void checkpoint(const GameState& state, const std::filesystem::path& path) {
    save::SaveGameRepository::save(path, state);
    Simulation direct(state), loaded(save::SaveGameRepository::load(path));
    equivalent(direct.state(), loaded.state());
    const auto bulk = direct.advanceDaysDetailed(4);
    int elapsed = 0;
    std::optional<ProgramController> issue;
    for (int i = 0; i < 4; ++i) {
        const auto day = loaded.advanceDaysDetailed(1);
        elapsed += day.advancedDays;
        issue = day.issueProgramId;
    }
    require(bulk.advancedDays == elapsed && bulk.issueProgramId == issue,
            "Bulk versus daily checkpoint interruption differs");
    equivalent(direct.state(), loaded.state());
}
void lifecycle_checkpoints(const std::filesystem::path& path) {
    auto s = createDelegatedSurveyScenario();
    s.colonies.back().analysisCapacity = 0;
    s.surveyTeams.push_back({SurveyTeamId{s.ids.nextSurveyTeamId++}, "Replacement scientist",
                             SurveyTeamLocationKind::Colony, s.colonies.back().id, std::nullopt});
    Simulation sim(s);
    require(sim.execute(ResourceSurveyCommand{s.fleets.front().id, s.fleets.front().currentBodyId}).ok,
            "Checkpoint manual acquisition");
    AnalysisCharter c{
        "Checkpoint interpretation", s.colonies.back().id, FixedBatchInput{{ObservationBatchId{1}}},
        s.surveyTeams.front().id,    s.people.front().id,  1.5};
    require(sim.execute(CreateAnalysisProgramCommand{c}).ok, "Checkpoint zero-capacity intent");
    checkpoint(sim.state(), path);
    sim.advanceDays(1);
    checkpoint(sim.state(), path);
    auto detached = sim.state();
    detached.colonies.back().analysisCapacity = .5;
    sim = Simulation(detached);
    sim.advanceDays(2);
    checkpoint(sim.state(), path);
    require(sim.execute(SuspendAnalysisProgramCommand{AnalysisProgramId{1}}).ok, "Checkpoint suspension");
    checkpoint(sim.state(), path);
    require(sim
                .execute(AmendAnalysisProgramCommand{
                    AnalysisProgramId{1}, {"Reassigned", s.surveyTeams.back().id, c.requestedLeaderId, 1.5}})
                .ok,
            "Checkpoint reassignment");
    require(sim.execute(ResumeAnalysisProgramCommand{AnalysisProgramId{1}}).ok, "Checkpoint resume");
    sim.advanceDays(1);
    checkpoint(sim.state(), path);
    require(sim.execute(AcknowledgeAnalysisIssueCommand{AnalysisProgramId{1}, "analysis-work-allowance"}).ok,
            "Checkpoint acknowledgement");
    sim.advanceDays(26);
    require(sim.state().date.day == 30, "Fixed report boundary");
    checkpoint(sim.state(), path);
    const auto report = sim.state().analysisPrograms.front().reports;
    require(sim.execute(AmendAnalysisProgramCommand{
                            AnalysisProgramId{1},
                            {"Same-day changed policy", s.surveyTeams.front().id, s.people.back().id, 10.0}})
                .ok,
            "Same-day amendment after publication");
    require(sim.state().analysisPrograms.front().reports == report,
            "Later same-day amendment rewrote report");
    checkpoint(sim.state(), path);
    require(sim.execute(CancelAnalysisProgramCommand{AnalysisProgramId{1}}).ok, "Checkpoint cancellation");
    checkpoint(sim.state(), path);
}

void simultaneous_publication_checkpoint(const std::filesystem::path& path) {
    auto s = createDelegatedSurveyScenario();
    s.colonies.back().analysisCapacity = 2;
    s.surveyTeams.push_back({SurveyTeamId{s.ids.nextSurveyTeamId++}, "Second analyst",
                             SurveyTeamLocationKind::Colony, s.colonies.back().id, std::nullopt});
    Simulation sim(s);
    require(sim.execute(ResourceSurveyCommand{s.fleets.front().id, s.fleets.front().currentBodyId}).ok,
            "Same-body input acquired");
    for (const auto& team : s.surveyTeams)
        require(sim.execute(CreateAnalysisProgramCommand{{"Parallel publication", s.colonies.back().id,
                                                          FixedBatchInput{{ObservationBatchId{1}}}, team.id,
                                                          s.people.front().id, std::nullopt}})
                    .ok,
                "Independent parallel analyst");
    sim.advanceDays(2);
    checkpoint(sim.state(), path);
    auto bad = sim.state();
    bad.analysisPrograms.back().leasedTeamId = bad.analysisPrograms.front().leasedTeamId;
    bad.analysisPrograms.back().charter.requestedTeamId =
        bad.analysisPrograms.front().charter.requestedTeamId;
    bool rejected = false;
    try {
        Simulation invalid(bad);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Duplicate canonical scientific leases accepted");
    sim.advanceDays(1);
    checkpoint(sim.state(), path);
    require(sim.state().assessments.size() == 2 &&
                sim.state().assessments[0].publishedDay == sim.state().assessments[1].publishedDay,
            "Both ordered immutable publications persisted on same day");
}

void run(const std::filesystem::path& path) {
    auto s = createDelegatedSurveyScenario();
    s.colonies.back().analysisCapacity = 1;
    s.ships.front().fuel = 1000;
    Simulation original(s);
    SurveyProgramCharter field;
    field.name = "Two passes";
    field.homeColonyId = s.colonies.back().id;
    field.requestedFleetId = s.fleets.front().id;
    field.requestedTeamId = s.surveyTeams.front().id;
    field.requestedLeaderId = s.people.front().id;
    field.targets = {{s.bodies.back().id, 0, 2}};
    require(original.execute(CreateSurveyProgramCommand{field}).ok, "Field authorization");
    AnalysisCharter charter{"Follower",
                            s.colonies.back().id,
                            FollowSurveyInput{SurveyProgramId{1}},
                            s.surveyTeams.front().id,
                            s.people.front().id,
                            std::nullopt};
    require(original.execute(CreateAnalysisProgramCommand{charter}).ok, "Follower authorization");
    for (int day = 0; day < 65; ++day) {
        save::SaveGameRepository::save(path, original.state());
        Simulation loaded(save::SaveGameRepository::load(path));
        equivalent(original.state(), loaded.state());
        original.advanceDays(1);
        loaded.advanceDays(1);
        equivalent(original.state(), loaded.state());
    }
    require(original.state().assessments.size() == 2, "Continuation published once per batch");
    save::SaveGameRepository::save(path, original.state());
    // Independent SQL corruptions cover method, channel order, source, work,
    // provenance, claims and counters. Failed app Load preserves its live world.
    const char* mutations[] = {
        "UPDATE finding_readings SET threshold=threshold+1 WHERE ordinal=0;",
        "UPDATE measurement_profiles SET threshold=0 WHERE id=1;",
        "UPDATE measurement_channels SET ordinal=90 WHERE ordinal=13 AND profile_id=1;",
        "DELETE FROM colony_laboratories WHERE capacity=0;",
        "DELETE FROM component_measurements WHERE component_id=1;",
        "UPDATE observation_batches SET available_day=acquired_day;",
        "UPDATE observation_batches SET origin=99;",
        "UPDATE observation_instruments SET workdays=4 WHERE parent_id=1;",
        "UPDATE observation_exposure_dates SET day=999 WHERE parent_id=1 AND ordinal=0;",
        "UPDATE observation_channels SET indication=99 WHERE batch_id=1 AND ordinal=0;",
        "UPDATE observation_channels SET accessibility=2 WHERE batch_id=1 AND ordinal=0;",
        "UPDATE analysis_programs SET source_kind=99;",
        "UPDATE analysis_programs SET source_survey=NULL;",
        "UPDATE analysis_programs SET leased_team=requested_team WHERE id=1;",
        "UPDATE analysis_jobs SET required=2 WHERE id=1;",
        "UPDATE analysis_work SET work=2 WHERE ordinal=0;",
        "UPDATE analysis_work SET day=0 WHERE ordinal=0;",
        "UPDATE analysis_work SET capacity=0.1 WHERE ordinal=0;",
        "UPDATE analysis_work SET revision=999 WHERE ordinal=0;",
        "UPDATE analysis_findings SET acquired=999 WHERE ordinal=0;",
        "UPDATE assessment_claims SET quantity=1 WHERE ordinal=0;",
        "UPDATE assessment_claims SET indication_day=999 WHERE ordinal=0;",
        "UPDATE assessments SET changed=1-changed WHERE ordinal=1;",
        "UPDATE assessment_findings SET ordinal=9 WHERE assessment_id=1 AND ordinal=0;",
        "UPDATE id_counters SET value=1 WHERE key='next_observation_batch_id';"};
    for (const auto* mutation : mutations) {
        const auto broken = std::filesystem::path(path.string() + ".broken");
        std::filesystem::copy_file(path, broken, std::filesystem::copy_options::overwrite_existing);
        {
            save::Database db(broken);
            db.execute("PRAGMA foreign_keys=OFF; PRAGMA ignore_check_constraints=ON;");
            db.execute(mutation);
        }
        SimulationService active(original.state());
        require(!active.loadGame(broken).ok, "Malformed science snapshot accepted by app Load");
        equivalent(active.state(), original.state());
        std::filesystem::remove(broken);
    }
    // An unsupported development file is rejected before reconstruction and
    // before overwrite. Its byte image remains identical for both operations.
    const auto old = std::filesystem::path(path.string() + ".old");
    {
        save::Database db(old);
        db.execute("CREATE TABLE schema_version(id INTEGER PRIMARY KEY,version INTEGER); INSERT INTO "
                   "schema_version VALUES(1,15);");
    }
    const auto bytes = [&] {
        std::ifstream in(old, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto before = bytes();
    bool loadRejected = false, saveRejected = false;
    try {
        (void)save::SaveGameRepository::load(old);
    } catch (const std::exception& e) {
        loadRejected = std::string(e.what()).find("Unsupported") != std::string::npos ||
                       std::string(e.what()).find("unsupported") != std::string::npos;
    }
    try {
        save::SaveGameRepository::save(old, original.state());
    } catch (const std::exception&) {
        saveRejected = true;
    }
    require(loadRejected && saveRejected && bytes() == before,
            "Old schema rejection modified file or failed unclearly");
    std::filesystem::remove(old);
}
} // namespace
int main() {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("deep_signal_p4a_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".sqlite");
    try {
        run(path);
        lifecycle_checkpoints(path);
        simultaneous_publication_checkpoint(path);
        std::filesystem::remove(path);
        std::cout << "Science every-day round-trip/continuation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove(path);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
