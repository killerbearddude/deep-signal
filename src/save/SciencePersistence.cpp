// Explicit v16 scientific snapshot tables; no migration or gameplay execution.
#include "save/SciencePersistence.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace deep::save {
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void reuse(Statement& q) {
    q.reset();
    q.clearBindings();
}
template <class T> std::int64_t int64(T value) {
    if constexpr (requires { value.value; })
        return value.value;
    else
        return static_cast<std::int64_t>(value);
}
int integer(const Statement& q, int col) {
    auto v = q.columnInt64Strict(col);
    check(v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max(),
          "Science integer overflow");
    return static_cast<int>(v);
}
bool boolean(const Statement& q, int col) {
    auto v = integer(q, col);
    check(v == 0 || v == 1, "Invalid science boolean");
    return v != 0;
}
void next(const Statement& q, int col, std::size_t n) {
    check(q.columnInt64Strict(col) == static_cast<std::int64_t>(n),
          "Missing, duplicate or gapped science ordinal");
}
template <class T> void optional(Statement& q, int col, const std::optional<T>& v) {
    if (!v)
        q.bindNull(col);
    else if constexpr (std::is_same_v<T, double>)
        q.bindDouble(col, *v);
    else
        q.bindInt64(col, int64(*v));
}
template <class T, class ID> T& byId(std::vector<T>& rows, ID id) {
    auto i = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.id == id; });
    check(i != rows.end(), "Missing science parent");
    return *i;
}
template <class T> T& child(std::vector<T>& rows, const Statement& q, int col) {
    auto i = q.columnInt64Strict(col);
    check(i >= 0 && static_cast<std::size_t>(i) < rows.size(), "Missing science child parent");
    return rows[static_cast<std::size_t>(i)];
}
AnalysisFinding& finding(GameState& s, AnalysisJobId id) {
    auto i = std::find_if(s.analysisFindings.begin(), s.analysisFindings.end(),
                          [&](const auto& f) { return f.jobId == id; });
    check(i != s.analysisFindings.end(), "Missing finding parent");
    return *i;
}
std::optional<SurveyProgramId> sourceSurvey(const AnalysisProgram& p) {
    if (auto f = std::get_if<FollowSurveyInput>(&p.charter.source))
        return f->programId;
    return std::nullopt;
}
} // namespace

void createScienceSchema(Database& db) {
    db.execute(R"sql(
CREATE TABLE measurement_profiles (
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    id INTEGER NOT NULL CHECK(id IS NULL OR typeof(id) = 'integer'),
    name TEXT NOT NULL,
    version INTEGER NOT NULL CHECK(version IS NULL OR typeof(version) = 'integer'),
    threshold REAL NOT NULL CHECK(threshold IS NULL OR typeof(threshold) IN ('integer','real')),
    access INTEGER NOT NULL CHECK(access IS NULL OR typeof(access) = 'integer'),
    PRIMARY KEY(id), UNIQUE(ordinal)
);
CREATE TABLE measurement_channels (
    profile_id INTEGER NOT NULL CHECK(profile_id IS NULL OR typeof(profile_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    mineral INTEGER NOT NULL CHECK(mineral IS NULL OR typeof(mineral) = 'integer'),
    PRIMARY KEY(profile_id,ordinal), UNIQUE(profile_id,mineral), FOREIGN KEY(profile_id) REFERENCES measurement_profiles(id)
);
CREATE TABLE colony_laboratories (
    colony_id INTEGER NOT NULL CHECK(colony_id IS NULL OR typeof(colony_id) = 'integer'),
    capacity REAL NOT NULL CHECK(capacity IS NULL OR typeof(capacity) IN ('integer','real')),
    PRIMARY KEY(colony_id), FOREIGN KEY(colony_id) REFERENCES colonies(id)
);
CREATE TABLE component_measurements (
    component_id INTEGER NOT NULL CHECK(component_id IS NULL OR typeof(component_id) = 'integer'),
    profile_id INTEGER CHECK(profile_id IS NULL OR typeof(profile_id) = 'integer'),
    PRIMARY KEY(component_id), FOREIGN KEY(component_id) REFERENCES ship_components(id), FOREIGN KEY(profile_id) REFERENCES measurement_profiles(id)
);
CREATE TABLE observation_batches (
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    id INTEGER NOT NULL CHECK(id IS NULL OR typeof(id) = 'integer'),
    body_id INTEGER NOT NULL CHECK(body_id IS NULL OR typeof(body_id) = 'integer'),
    fleet_id INTEGER NOT NULL CHECK(fleet_id IS NULL OR typeof(fleet_id) = 'integer'),
    origin INTEGER NOT NULL CHECK(origin IS NULL OR typeof(origin) = 'integer'),
    survey_program_id INTEGER CHECK(survey_program_id IS NULL OR typeof(survey_program_id) = 'integer'),
    team_id INTEGER CHECK(team_id IS NULL OR typeof(team_id) = 'integer'),
    pass INTEGER NOT NULL CHECK(pass IS NULL OR typeof(pass) = 'integer'),
    first_day INTEGER NOT NULL CHECK(first_day IS NULL OR typeof(first_day) = 'integer'),
    acquired_day INTEGER NOT NULL CHECK(acquired_day IS NULL OR typeof(acquired_day) = 'integer'),
    available_day INTEGER NOT NULL CHECK(available_day IS NULL OR typeof(available_day) = 'integer'),
    PRIMARY KEY(id), UNIQUE(ordinal), FOREIGN KEY(body_id) REFERENCES bodies(id), FOREIGN KEY(fleet_id) REFERENCES fleets(id), FOREIGN KEY(survey_program_id) REFERENCES survey_programs(id), FOREIGN KEY(team_id) REFERENCES survey_teams(id)
);
CREATE TABLE observation_work_dates (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    day INTEGER NOT NULL CHECK(day IS NULL OR typeof(day) = 'integer'),
    PRIMARY KEY(parent_id,ordinal), FOREIGN KEY(parent_id) REFERENCES observation_batches(id)
);
CREATE TABLE observation_instruments (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    ship_id INTEGER NOT NULL CHECK(ship_id IS NULL OR typeof(ship_id) = 'integer'),
    class_id INTEGER NOT NULL CHECK(class_id IS NULL OR typeof(class_id) = 'integer'),
    component_id INTEGER NOT NULL CHECK(component_id IS NULL OR typeof(component_id) = 'integer'),
    profile_id INTEGER NOT NULL CHECK(profile_id IS NULL OR typeof(profile_id) = 'integer'),
    workdays INTEGER NOT NULL CHECK(workdays IS NULL OR typeof(workdays) = 'integer'),
    PRIMARY KEY(parent_id,ordinal), UNIQUE(parent_id,ship_id,component_id), FOREIGN KEY(parent_id) REFERENCES observation_batches(id), FOREIGN KEY(profile_id) REFERENCES measurement_profiles(id), FOREIGN KEY(ship_id) REFERENCES ships(id), FOREIGN KEY(class_id) REFERENCES ship_classes(id), FOREIGN KEY(component_id) REFERENCES ship_components(id)
);
CREATE TABLE observation_exposure_dates (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    instrument INTEGER NOT NULL CHECK(instrument IS NULL OR typeof(instrument) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    day INTEGER NOT NULL CHECK(day IS NULL OR typeof(day) = 'integer'),
    PRIMARY KEY(parent_id,instrument,ordinal), FOREIGN KEY(parent_id,instrument) REFERENCES observation_instruments(parent_id,ordinal)
);
CREATE TABLE field_work_dates (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    day INTEGER NOT NULL CHECK(day IS NULL OR typeof(day) = 'integer'),
    PRIMARY KEY(parent_id,ordinal), FOREIGN KEY(parent_id) REFERENCES survey_programs(id)
);
CREATE TABLE field_instruments (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    ship_id INTEGER NOT NULL CHECK(ship_id IS NULL OR typeof(ship_id) = 'integer'),
    class_id INTEGER NOT NULL CHECK(class_id IS NULL OR typeof(class_id) = 'integer'),
    component_id INTEGER NOT NULL CHECK(component_id IS NULL OR typeof(component_id) = 'integer'),
    profile_id INTEGER NOT NULL CHECK(profile_id IS NULL OR typeof(profile_id) = 'integer'),
    workdays INTEGER NOT NULL CHECK(workdays IS NULL OR typeof(workdays) = 'integer'),
    PRIMARY KEY(parent_id,ordinal), UNIQUE(parent_id,ship_id,component_id), FOREIGN KEY(parent_id) REFERENCES survey_programs(id), FOREIGN KEY(profile_id) REFERENCES measurement_profiles(id), FOREIGN KEY(ship_id) REFERENCES ships(id), FOREIGN KEY(class_id) REFERENCES ship_classes(id), FOREIGN KEY(component_id) REFERENCES ship_components(id)
);
CREATE TABLE field_exposure_dates (
    parent_id INTEGER NOT NULL CHECK(parent_id IS NULL OR typeof(parent_id) = 'integer'),
    instrument INTEGER NOT NULL CHECK(instrument IS NULL OR typeof(instrument) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    day INTEGER NOT NULL CHECK(day IS NULL OR typeof(day) = 'integer'),
    PRIMARY KEY(parent_id,instrument,ordinal), FOREIGN KEY(parent_id,instrument) REFERENCES field_instruments(parent_id,ordinal)
);
CREATE TABLE observation_channels (
    batch_id INTEGER NOT NULL CHECK(batch_id IS NULL OR typeof(batch_id) = 'integer'),
    instrument INTEGER NOT NULL CHECK(instrument IS NULL OR typeof(instrument) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    mineral INTEGER NOT NULL CHECK(mineral IS NULL OR typeof(mineral) = 'integer'),
    indication INTEGER NOT NULL CHECK(indication IS NULL OR typeof(indication) = 'integer'),
    accessibility INTEGER NOT NULL CHECK(accessibility IS NULL OR typeof(accessibility) = 'integer'),
    PRIMARY KEY(batch_id,instrument,ordinal), FOREIGN KEY(batch_id,instrument) REFERENCES observation_instruments(parent_id,ordinal)
);
CREATE TABLE survey_observation_links (
    program_id INTEGER NOT NULL CHECK(program_id IS NULL OR typeof(program_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    batch_id INTEGER NOT NULL CHECK(batch_id IS NULL OR typeof(batch_id) = 'integer'),
    PRIMARY KEY(program_id,ordinal), UNIQUE(batch_id), FOREIGN KEY(batch_id) REFERENCES observation_batches(id), FOREIGN KEY(program_id,ordinal) REFERENCES survey_program_receipts(program_id,ordinal)
);
CREATE TABLE analysis_programs (
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    id INTEGER NOT NULL CHECK(id IS NULL OR typeof(id) = 'integer'),
    name TEXT NOT NULL,
    colony_id INTEGER NOT NULL CHECK(colony_id IS NULL OR typeof(colony_id) = 'integer'),
    source_kind INTEGER NOT NULL CHECK(source_kind IS NULL OR typeof(source_kind) = 'integer'),
    source_survey INTEGER CHECK(source_survey IS NULL OR typeof(source_survey) = 'integer'),
    requested_team INTEGER CHECK(requested_team IS NULL OR typeof(requested_team) = 'integer'),
    leader_id INTEGER CHECK(leader_id IS NULL OR typeof(leader_id) = 'integer'),
    allowance REAL CHECK(allowance IS NULL OR typeof(allowance) IN ('integer','real')),
    created_day INTEGER NOT NULL CHECK(created_day IS NULL OR typeof(created_day) = 'integer'),
    revision INTEGER NOT NULL CHECK(revision IS NULL OR typeof(revision) = 'integer'),
    lifecycle INTEGER NOT NULL CHECK(lifecycle IS NULL OR typeof(lifecycle) = 'integer'),
    closure INTEGER NOT NULL CHECK(closure IS NULL OR typeof(closure) = 'integer'),
    closed_day INTEGER CHECK(closed_day IS NULL OR typeof(closed_day) = 'integer'),
    leased_team INTEGER CHECK(leased_team IS NULL OR typeof(leased_team) = 'integer'),
    next_report INTEGER NOT NULL CHECK(next_report IS NULL OR typeof(next_report) = 'integer'),
    report_start INTEGER NOT NULL CHECK(report_start IS NULL OR typeof(report_start) = 'integer'),
    issue_signature TEXT NOT NULL,
    issue_message TEXT NOT NULL,
    issue_ack INTEGER NOT NULL CHECK(issue_ack IS NULL OR typeof(issue_ack) = 'integer'),
    PRIMARY KEY(id), UNIQUE(ordinal), FOREIGN KEY(colony_id) REFERENCES colonies(id), FOREIGN KEY(source_survey) REFERENCES survey_programs(id), FOREIGN KEY(requested_team) REFERENCES survey_teams(id), FOREIGN KEY(leased_team) REFERENCES survey_teams(id), FOREIGN KEY(leader_id) REFERENCES people(id)
);
CREATE TABLE analysis_fixed_inputs (
    program_id INTEGER NOT NULL CHECK(program_id IS NULL OR typeof(program_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    batch_id INTEGER NOT NULL CHECK(batch_id IS NULL OR typeof(batch_id) = 'integer'),
    PRIMARY KEY(program_id,ordinal), UNIQUE(program_id,batch_id), FOREIGN KEY(program_id) REFERENCES analysis_programs(id), FOREIGN KEY(batch_id) REFERENCES observation_batches(id)
);
CREATE TABLE analysis_jobs (
    program_id INTEGER NOT NULL CHECK(program_id IS NULL OR typeof(program_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    id INTEGER NOT NULL CHECK(id IS NULL OR typeof(id) = 'integer'),
    batch_id INTEGER NOT NULL CHECK(batch_id IS NULL OR typeof(batch_id) = 'integer'),
    started INTEGER NOT NULL CHECK(started IS NULL OR typeof(started) = 'integer'),
    ended INTEGER CHECK(ended IS NULL OR typeof(ended) = 'integer'),
    outcome INTEGER NOT NULL CHECK(outcome IS NULL OR typeof(outcome) = 'integer'),
    required REAL NOT NULL CHECK(required IS NULL OR typeof(required) IN ('integer','real')),
    PRIMARY KEY(id), UNIQUE(program_id,ordinal), UNIQUE(program_id,batch_id), FOREIGN KEY(program_id) REFERENCES analysis_programs(id), FOREIGN KEY(batch_id) REFERENCES observation_batches(id)
);
CREATE TABLE analysis_work (
    program_id INTEGER NOT NULL CHECK(program_id IS NULL OR typeof(program_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    day INTEGER NOT NULL CHECK(day IS NULL OR typeof(day) = 'integer'),
    colony_id INTEGER NOT NULL CHECK(colony_id IS NULL OR typeof(colony_id) = 'integer'),
    team_id INTEGER NOT NULL CHECK(team_id IS NULL OR typeof(team_id) = 'integer'),
    leader_id INTEGER NOT NULL CHECK(leader_id IS NULL OR typeof(leader_id) = 'integer'),
    revision INTEGER NOT NULL CHECK(revision IS NULL OR typeof(revision) = 'integer'),
    work REAL NOT NULL CHECK(work IS NULL OR typeof(work) IN ('integer','real')),
    capacity REAL NOT NULL CHECK(capacity IS NULL OR typeof(capacity) IN ('integer','real')),
    PRIMARY KEY(program_id,ordinal), FOREIGN KEY(program_id) REFERENCES analysis_programs(id), FOREIGN KEY(job_id) REFERENCES analysis_jobs(id), FOREIGN KEY(colony_id) REFERENCES colonies(id), FOREIGN KEY(team_id) REFERENCES survey_teams(id), FOREIGN KEY(leader_id) REFERENCES people(id)
);
CREATE TABLE analysis_reports (
    program_id INTEGER NOT NULL CHECK(program_id IS NULL OR typeof(program_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    start INTEGER NOT NULL CHECK(start IS NULL OR typeof(start) = 'integer'),
    end INTEGER NOT NULL CHECK(end IS NULL OR typeof(end) = 'integer'),
    review INTEGER NOT NULL CHECK(review IS NULL OR typeof(review) = 'integer'),
    revision INTEGER NOT NULL CHECK(revision IS NULL OR typeof(revision) = 'integer'),
    allowance REAL CHECK(allowance IS NULL OR typeof(allowance) IN ('integer','real')),
    work REAL NOT NULL CHECK(work IS NULL OR typeof(work) IN ('integer','real')),
    lifetime REAL NOT NULL CHECK(lifetime IS NULL OR typeof(lifetime) IN ('integer','real')),
    completed INTEGER NOT NULL CHECK(completed IS NULL OR typeof(completed) = 'integer'),
    backlog INTEGER NOT NULL CHECK(backlog IS NULL OR typeof(backlog) = 'integer'),
    source TEXT NOT NULL,
    waiting TEXT NOT NULL,
    limitations TEXT NOT NULL,
    cutoff INTEGER NOT NULL CHECK(cutoff IS NULL OR typeof(cutoff) = 'integer'),
    PRIMARY KEY(program_id,ordinal), FOREIGN KEY(program_id) REFERENCES analysis_programs(id)
);
CREATE TABLE analysis_findings (
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    batch_id INTEGER NOT NULL CHECK(batch_id IS NULL OR typeof(batch_id) = 'integer'),
    body_id INTEGER NOT NULL CHECK(body_id IS NULL OR typeof(body_id) = 'integer'),
    acquired INTEGER NOT NULL CHECK(acquired IS NULL OR typeof(acquired) = 'integer'),
    published INTEGER NOT NULL CHECK(published IS NULL OR typeof(published) = 'integer'),
    PRIMARY KEY(job_id), UNIQUE(ordinal), FOREIGN KEY(job_id) REFERENCES analysis_jobs(id), FOREIGN KEY(batch_id) REFERENCES observation_batches(id), FOREIGN KEY(body_id) REFERENCES bodies(id)
);
CREATE TABLE finding_readings (
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    mineral INTEGER NOT NULL CHECK(mineral IS NULL OR typeof(mineral) = 'integer'),
    profile INTEGER NOT NULL CHECK(profile IS NULL OR typeof(profile) = 'integer'),
    version INTEGER NOT NULL CHECK(version IS NULL OR typeof(version) = 'integer'),
    threshold REAL NOT NULL CHECK(threshold IS NULL OR typeof(threshold) IN ('integer','real')),
    access_method INTEGER NOT NULL CHECK(access_method IS NULL OR typeof(access_method) = 'integer'),
    indication INTEGER NOT NULL CHECK(indication IS NULL OR typeof(indication) = 'integer'),
    accessibility INTEGER NOT NULL CHECK(accessibility IS NULL OR typeof(accessibility) = 'integer'),
    PRIMARY KEY(job_id,ordinal), FOREIGN KEY(job_id) REFERENCES analysis_findings(job_id), FOREIGN KEY(profile) REFERENCES measurement_profiles(id)
);
CREATE TABLE assessments (
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    id INTEGER NOT NULL CHECK(id IS NULL OR typeof(id) = 'integer'),
    body_id INTEGER NOT NULL CHECK(body_id IS NULL OR typeof(body_id) = 'integer'),
    revision INTEGER NOT NULL CHECK(revision IS NULL OR typeof(revision) = 'integer'),
    version INTEGER NOT NULL CHECK(version IS NULL OR typeof(version) = 'integer'),
    published INTEGER NOT NULL CHECK(published IS NULL OR typeof(published) = 'integer'),
    trigger_job INTEGER NOT NULL CHECK(trigger_job IS NULL OR typeof(trigger_job) = 'integer'),
    previous INTEGER CHECK(previous IS NULL OR typeof(previous) = 'integer'),
    changed INTEGER NOT NULL CHECK(changed IS NULL OR typeof(changed) = 'integer'),
    repeated INTEGER NOT NULL CHECK(repeated IS NULL OR typeof(repeated) = 'integer'),
    PRIMARY KEY(id), UNIQUE(ordinal), UNIQUE(body_id,revision), FOREIGN KEY(body_id) REFERENCES bodies(id), FOREIGN KEY(trigger_job) REFERENCES analysis_findings(job_id), FOREIGN KEY(previous) REFERENCES assessments(id)
);
CREATE TABLE assessment_findings (
    assessment_id INTEGER NOT NULL CHECK(assessment_id IS NULL OR typeof(assessment_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    PRIMARY KEY(assessment_id,ordinal), UNIQUE(assessment_id,job_id), FOREIGN KEY(assessment_id) REFERENCES assessments(id), FOREIGN KEY(job_id) REFERENCES analysis_findings(job_id)
);
CREATE TABLE assessment_claims (
    assessment_id INTEGER NOT NULL CHECK(assessment_id IS NULL OR typeof(assessment_id) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    mineral INTEGER NOT NULL CHECK(mineral IS NULL OR typeof(mineral) = 'integer'),
    indication INTEGER NOT NULL CHECK(indication IS NULL OR typeof(indication) = 'integer'),
    accessibility INTEGER NOT NULL CHECK(accessibility IS NULL OR typeof(accessibility) = 'integer'),
    quantity INTEGER NOT NULL CHECK(quantity IS NULL OR typeof(quantity) = 'integer'),
    site INTEGER NOT NULL CHECK(site IS NULL OR typeof(site) = 'integer'),
    indication_day INTEGER CHECK(indication_day IS NULL OR typeof(indication_day) = 'integer'),
    access_day INTEGER CHECK(access_day IS NULL OR typeof(access_day) = 'integer'),
    earlier INTEGER NOT NULL CHECK(earlier IS NULL OR typeof(earlier) = 'integer'),
    PRIMARY KEY(assessment_id,ordinal), UNIQUE(assessment_id,mineral), FOREIGN KEY(assessment_id) REFERENCES assessments(id)
);
CREATE TABLE claim_indication_inputs (
    assessment_id INTEGER NOT NULL CHECK(assessment_id IS NULL OR typeof(assessment_id) = 'integer'),
    claim INTEGER NOT NULL CHECK(claim IS NULL OR typeof(claim) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    PRIMARY KEY(assessment_id,claim,ordinal), UNIQUE(assessment_id,claim,job_id), FOREIGN KEY(assessment_id,claim) REFERENCES assessment_claims(assessment_id,ordinal), FOREIGN KEY(job_id) REFERENCES analysis_findings(job_id)
);
CREATE TABLE claim_accessibility_inputs (
    assessment_id INTEGER NOT NULL CHECK(assessment_id IS NULL OR typeof(assessment_id) = 'integer'),
    claim INTEGER NOT NULL CHECK(claim IS NULL OR typeof(claim) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    job_id INTEGER NOT NULL CHECK(job_id IS NULL OR typeof(job_id) = 'integer'),
    PRIMARY KEY(assessment_id,claim,ordinal), UNIQUE(assessment_id,claim,job_id), FOREIGN KEY(assessment_id,claim) REFERENCES assessment_claims(assessment_id,ordinal), FOREIGN KEY(job_id) REFERENCES analysis_findings(job_id)
);
CREATE TABLE claim_alternatives (
    assessment_id INTEGER NOT NULL CHECK(assessment_id IS NULL OR typeof(assessment_id) = 'integer'),
    claim INTEGER NOT NULL CHECK(claim IS NULL OR typeof(claim) = 'integer'),
    ordinal INTEGER NOT NULL CHECK(ordinal IS NULL OR typeof(ordinal) = 'integer'),
    mineral INTEGER NOT NULL CHECK(mineral IS NULL OR typeof(mineral) = 'integer'),
    profile INTEGER NOT NULL CHECK(profile IS NULL OR typeof(profile) = 'integer'),
    version INTEGER NOT NULL CHECK(version IS NULL OR typeof(version) = 'integer'),
    threshold REAL NOT NULL CHECK(threshold IS NULL OR typeof(threshold) IN ('integer','real')),
    access_method INTEGER NOT NULL CHECK(access_method IS NULL OR typeof(access_method) = 'integer'),
    indication INTEGER NOT NULL CHECK(indication IS NULL OR typeof(indication) = 'integer'),
    accessibility INTEGER NOT NULL CHECK(accessibility IS NULL OR typeof(accessibility) = 'integer'),
    PRIMARY KEY(assessment_id,claim,ordinal), FOREIGN KEY(assessment_id,claim) REFERENCES assessment_claims(assessment_id,ordinal), FOREIGN KEY(profile) REFERENCES measurement_profiles(id)
);
)sql");
}
void clearScienceState(Database& db) {
    db.execute("DELETE FROM claim_alternatives;");
    db.execute("DELETE FROM claim_accessibility_inputs;");
    db.execute("DELETE FROM claim_indication_inputs;");
    db.execute("DELETE FROM assessment_claims;");
    db.execute("DELETE FROM assessment_findings;");
    db.execute("DELETE FROM assessments;");
    db.execute("DELETE FROM finding_readings;");
    db.execute("DELETE FROM analysis_findings;");
    db.execute("DELETE FROM analysis_reports;");
    db.execute("DELETE FROM analysis_work;");
    db.execute("DELETE FROM analysis_jobs;");
    db.execute("DELETE FROM analysis_fixed_inputs;");
    db.execute("DELETE FROM analysis_programs;");
    db.execute("DELETE FROM survey_observation_links;");
    db.execute("DELETE FROM observation_channels;");
    db.execute("DELETE FROM field_exposure_dates;");
    db.execute("DELETE FROM field_instruments;");
    db.execute("DELETE FROM field_work_dates;");
    db.execute("DELETE FROM observation_exposure_dates;");
    db.execute("DELETE FROM observation_instruments;");
    db.execute("DELETE FROM observation_work_dates;");
    db.execute("DELETE FROM observation_batches;");
    db.execute("DELETE FROM component_measurements;");
    db.execute("DELETE FROM colony_laboratories;");
    db.execute("DELETE FROM measurement_channels;");
    db.execute("DELETE FROM measurement_profiles;");
}
void saveScienceState(Database& db, const GameState& s) {
    {
        Statement q(db, "INSERT INTO measurement_profiles(ordinal,id,name,version,threshold,access) VALUES "
                        "(?,?,?,?,?,?);");
        for (std::size_t i = 0; i < s.measurementProfiles.size(); ++i)
            if (const auto& r = s.measurementProfiles[i]; true) {
                q.bindInt64(1, i);
                q.bindInt64(2, int64(r.id));
                q.bindText(3, r.name);
                q.bindInt64(4, int64(r.methodVersion));
                q.bindDouble(5, r.detectionThreshold);
                q.bindInt64(6, int64(r.measuresAccessibility));
                q.execute();
                reuse(q);
            }
    }
    {
        Statement q(db, "INSERT INTO measurement_channels(profile_id,ordinal,mineral) VALUES (?,?,?);");
        for (const auto& p : s.measurementProfiles)
            for (std::size_t i = 0; i < p.channels.size(); ++i)
                if (auto r = p.channels[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO colony_laboratories(colony_id,capacity) VALUES (?,?);");
        for (const auto& r : s.colonies) {
            q.bindInt64(1, int64(r.id));
            q.bindDouble(2, r.analysisCapacity);
            q.execute();
            reuse(q);
        }
    }
    {
        Statement q(db, "INSERT INTO component_measurements(component_id,profile_id) VALUES (?,?);");
        for (const auto& r : s.shipComponents) {
            q.bindInt64(1, int64(r.id));
            optional(q, 2, r.measurementProfileId);
            q.execute();
            reuse(q);
        }
    }
    {
        Statement q(db, "INSERT INTO "
                        "observation_batches(ordinal,id,body_id,fleet_id,origin,survey_program_id,team_id,"
                        "pass,first_day,acquired_day,available_day) VALUES (?,?,?,?,?,?,?,?,?,?,?);");
        for (std::size_t i = 0; i < s.observations.size(); ++i)
            if (const auto& r = s.observations[i]; true) {
                q.bindInt64(1, i);
                q.bindInt64(2, int64(r.id));
                q.bindInt64(3, int64(r.bodyId));
                q.bindInt64(4, int64(r.fleetId));
                q.bindInt64(5, int64(r.origin));
                optional(q, 6, r.surveyProgramId);
                optional(q, 7, r.teamId);
                q.bindInt64(8, int64(r.passNumber));
                q.bindInt64(9, r.firstWorkDay);
                q.bindInt64(10, r.acquiredDay);
                q.bindInt64(11, r.availableDay);
                q.execute();
                reuse(q);
            }
    }
    {
        Statement q(db, "INSERT INTO observation_work_dates(parent_id,ordinal,day) VALUES (?,?,?);");
        for (const auto& p : s.observations)
            for (std::size_t i = 0; i < p.workDates.size(); ++i)
                if (auto r = p.workDates[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, r);
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "observation_instruments(parent_id,ordinal,ship_id,class_id,component_id,profile_id,"
                        "workdays) VALUES (?,?,?,?,?,?,?);");
        for (const auto& p : s.observations)
            for (std::size_t i = 0; i < p.instruments.size(); ++i)
                if (const auto& r = p.instruments[i].exposure; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.shipId));
                    q.bindInt64(4, int64(r.classId));
                    q.bindInt64(5, int64(r.componentId));
                    q.bindInt64(6, int64(r.profileId));
                    q.bindInt64(7, int64(r.workdays));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(
            db, "INSERT INTO observation_exposure_dates(parent_id,instrument,ordinal,day) VALUES (?,?,?,?);");
        for (const auto& p : s.observations)
            for (std::size_t j = 0; j < p.instruments.size(); ++j)
                for (std::size_t i = 0; i < p.instruments[j].exposure.dates.size(); ++i)
                    if (auto r = p.instruments[j].exposure.dates[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, r);
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(db, "INSERT INTO field_work_dates(parent_id,ordinal,day) VALUES (?,?,?);");
        for (const auto& p : s.surveyPrograms)
            for (std::size_t i = 0; i < p.workDates.size(); ++i)
                if (auto r = p.workDates[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, r);
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "field_instruments(parent_id,ordinal,ship_id,class_id,component_id,profile_id,"
                        "workdays) VALUES (?,?,?,?,?,?,?);");
        for (const auto& p : s.surveyPrograms)
            for (std::size_t i = 0; i < p.exposures.size(); ++i)
                if (const auto& r = p.exposures[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.shipId));
                    q.bindInt64(4, int64(r.classId));
                    q.bindInt64(5, int64(r.componentId));
                    q.bindInt64(6, int64(r.profileId));
                    q.bindInt64(7, int64(r.workdays));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db,
                    "INSERT INTO field_exposure_dates(parent_id,instrument,ordinal,day) VALUES (?,?,?,?);");
        for (const auto& p : s.surveyPrograms)
            for (std::size_t j = 0; j < p.exposures.size(); ++j)
                for (std::size_t i = 0; i < p.exposures[j].dates.size(); ++i)
                    if (auto r = p.exposures[j].dates[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, r);
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(
            db,
            "INSERT INTO observation_channels(batch_id,instrument,ordinal,mineral,indication,accessibility) "
            "VALUES (?,?,?,?,?,?);");
        for (const auto& p : s.observations)
            for (std::size_t j = 0; j < p.instruments.size(); ++j)
                for (std::size_t i = 0; i < p.instruments[j].channels.size(); ++i)
                    if (const auto& r = p.instruments[j].channels[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, int64(r.mineral));
                        q.bindInt64(5, int64(r.indication));
                        q.bindInt64(6, int64(r.accessibility));
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(db, "INSERT INTO survey_observation_links(program_id,ordinal,batch_id) VALUES (?,?,?);");
        for (const auto& p : s.surveyPrograms)
            for (std::size_t i = 0; i < p.receipts.size(); ++i)
                if (const auto& r = p.receipts[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.observationBatchId));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "analysis_programs(ordinal,id,name,colony_id,source_kind,source_survey,requested_"
                        "team,leader_id,allowance,created_day,revision,lifecycle,closure,closed_day,leased_"
                        "team,next_report,report_start,issue_signature,issue_message,issue_ack) VALUES "
                        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        for (std::size_t i = 0; i < s.analysisPrograms.size(); ++i)
            if (const auto& r = s.analysisPrograms[i]; true) {
                q.bindInt64(1, i);
                q.bindInt64(2, int64(r.id));
                q.bindText(3, r.charter.name);
                q.bindInt64(4, int64(r.charter.colonyId));
                q.bindInt64(5, r.charter.source.index());
                optional(q, 6, sourceSurvey(r));
                optional(q, 7, r.charter.requestedTeamId);
                optional(q, 8, r.charter.requestedLeaderId);
                optional(q, 9, r.charter.workAllowance);
                q.bindInt64(10, r.createdDay);
                q.bindInt64(11, int64(r.charterRevision));
                q.bindInt64(12, int64(r.lifecycle));
                q.bindInt64(13, int64(r.closure));
                optional(q, 14, r.closedDay);
                optional(q, 15, r.leasedTeamId);
                q.bindInt64(16, r.nextReportDay);
                q.bindInt64(17, r.reportStartDay);
                q.bindText(18, r.issue.signature);
                q.bindText(19, r.issue.message);
                q.bindInt64(20, int64(r.issue.acknowledged));
                q.execute();
                reuse(q);
            }
    }
    {
        Statement q(db, "INSERT INTO analysis_fixed_inputs(program_id,ordinal,batch_id) VALUES (?,?,?);");
        for (const auto& p : s.analysisPrograms)
            if (const auto* fixed = std::get_if<FixedBatchInput>(&p.charter.source))
                for (std::size_t i = 0; i < fixed->batches.size(); ++i)
                    if (auto r = fixed->batches[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, i);
                        q.bindInt64(3, int64(r));
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(
            db, "INSERT INTO analysis_jobs(program_id,ordinal,id,batch_id,started,ended,outcome,required) "
                "VALUES (?,?,?,?,?,?,?,?);");
        for (const auto& p : s.analysisPrograms)
            for (std::size_t i = 0; i < p.jobs.size(); ++i)
                if (const auto& r = p.jobs[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.id));
                    q.bindInt64(4, int64(r.batchId));
                    q.bindInt64(5, r.startedDay);
                    optional(q, 6, r.endedDay);
                    q.bindInt64(7, int64(r.outcome));
                    q.bindDouble(8, r.requiredWork);
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "analysis_work(program_id,ordinal,job_id,day,colony_id,team_id,leader_id,revision,"
                        "work,capacity) VALUES (?,?,?,?,?,?,?,?,?,?);");
        for (const auto& p : s.analysisPrograms)
            for (std::size_t i = 0; i < p.receipts.size(); ++i)
                if (const auto& r = p.receipts[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.jobId));
                    q.bindInt64(4, r.day);
                    q.bindInt64(5, int64(r.colonyId));
                    q.bindInt64(6, int64(r.teamId));
                    q.bindInt64(7, int64(r.leaderId));
                    q.bindInt64(8, int64(r.charterRevision));
                    q.bindDouble(9, r.work);
                    q.bindDouble(10, r.labCapacity);
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(
            db,
            "INSERT INTO "
            "analysis_reports(program_id,ordinal,start,end,review,revision,allowance,work,lifetime,completed,"
            "backlog,source,waiting,limitations,cutoff) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        for (const auto& p : s.analysisPrograms)
            for (std::size_t i = 0; i < p.reports.size(); ++i)
                if (const auto& r = p.reports[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, r.startDay);
                    q.bindInt64(4, r.endDay);
                    q.bindInt64(5, int64(r.isNinetyDayReview));
                    q.bindInt64(6, int64(r.charterRevision));
                    optional(q, 7, r.workAllowance);
                    q.bindDouble(8, r.workPerformed);
                    q.bindDouble(9, r.lifetimeWork);
                    q.bindInt64(10, int64(r.jobsCompleted));
                    q.bindInt64(11, int64(r.inputBacklog));
                    q.bindText(12, r.sourceStatus);
                    q.bindText(13, r.waitingReason);
                    q.bindText(14, r.limitations);
                    q.bindInt64(15, r.auditThroughId);
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO analysis_findings(ordinal,job_id,batch_id,body_id,acquired,published) "
                        "VALUES (?,?,?,?,?,?);");
        for (std::size_t i = 0; i < s.analysisFindings.size(); ++i)
            if (const auto& r = s.analysisFindings[i]; true) {
                q.bindInt64(1, i);
                q.bindInt64(2, int64(r.jobId));
                q.bindInt64(3, int64(r.batchId));
                q.bindInt64(4, int64(r.bodyId));
                q.bindInt64(5, r.acquiredDay);
                q.bindInt64(6, r.publishedDay);
                q.execute();
                reuse(q);
            }
    }
    {
        Statement q(db, "INSERT INTO "
                        "finding_readings(job_id,ordinal,mineral,profile,version,threshold,access_method,"
                        "indication,accessibility) VALUES (?,?,?,?,?,?,?,?,?);");
        for (const auto& p : s.analysisFindings)
            for (std::size_t i = 0; i < p.readings.size(); ++i)
                if (const auto& r = p.readings[i]; true) {
                    q.bindInt64(1, int64(p.jobId));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.mineral));
                    q.bindInt64(4, int64(r.profileId));
                    q.bindInt64(5, int64(r.methodVersion));
                    q.bindDouble(6, r.threshold);
                    q.bindInt64(7, int64(r.measuresAccessibility));
                    q.bindInt64(8, int64(r.indication));
                    q.bindInt64(9, int64(r.accessibility));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "assessments(ordinal,id,body_id,revision,version,published,trigger_job,previous,"
                        "changed,repeated) VALUES (?,?,?,?,?,?,?,?,?,?);");
        for (std::size_t i = 0; i < s.assessments.size(); ++i)
            if (const auto& r = s.assessments[i]; true) {
                q.bindInt64(1, i);
                q.bindInt64(2, int64(r.id));
                q.bindInt64(3, int64(r.bodyId));
                q.bindInt64(4, int64(r.revision));
                q.bindInt64(5, int64(r.methodVersion));
                q.bindInt64(6, r.publishedDay);
                q.bindInt64(7, int64(r.triggeringJobId));
                optional(q, 8, r.previousId);
                q.bindInt64(9, int64(r.contentChanged));
                q.bindInt64(10, int64(r.repeatedEvidence));
                q.execute();
                reuse(q);
            }
    }
    {
        Statement q(db, "INSERT INTO assessment_findings(assessment_id,ordinal,job_id) VALUES (?,?,?);");
        for (const auto& p : s.assessments)
            for (std::size_t i = 0; i < p.findingIds.size(); ++i)
                if (auto r = p.findingIds[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(db, "INSERT INTO "
                        "assessment_claims(assessment_id,ordinal,mineral,indication,accessibility,quantity,"
                        "site,indication_day,access_day,earlier) VALUES (?,?,?,?,?,?,?,?,?,?);");
        for (const auto& p : s.assessments)
            for (std::size_t i = 0; i < p.claims.size(); ++i)
                if (const auto& r = p.claims[i]; true) {
                    q.bindInt64(1, int64(p.id));
                    q.bindInt64(2, i);
                    q.bindInt64(3, int64(r.mineral));
                    q.bindInt64(4, int64(r.indication));
                    q.bindInt64(5, int64(r.accessibility));
                    q.bindInt64(6, int64(r.quantity));
                    q.bindInt64(7, int64(r.site));
                    optional(q, 8, r.indicationDay);
                    optional(q, 9, r.accessibilityDay);
                    q.bindInt64(10, int64(r.earlierIndication));
                    q.execute();
                    reuse(q);
                }
    }
    {
        Statement q(
            db, "INSERT INTO claim_indication_inputs(assessment_id,claim,ordinal,job_id) VALUES (?,?,?,?);");
        for (const auto& p : s.assessments)
            for (std::size_t j = 0; j < p.claims.size(); ++j)
                for (std::size_t i = 0; i < p.claims[j].indicationInputs.size(); ++i)
                    if (auto r = p.claims[j].indicationInputs[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, int64(r));
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(
            db,
            "INSERT INTO claim_accessibility_inputs(assessment_id,claim,ordinal,job_id) VALUES (?,?,?,?);");
        for (const auto& p : s.assessments)
            for (std::size_t j = 0; j < p.claims.size(); ++j)
                for (std::size_t i = 0; i < p.claims[j].accessibilityInputs.size(); ++i)
                    if (auto r = p.claims[j].accessibilityInputs[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, int64(r));
                        q.execute();
                        reuse(q);
                    }
    }
    {
        Statement q(db, "INSERT INTO "
                        "claim_alternatives(assessment_id,claim,ordinal,mineral,profile,version,threshold,"
                        "access_method,indication,accessibility) VALUES (?,?,?,?,?,?,?,?,?,?);");
        for (const auto& p : s.assessments)
            for (std::size_t j = 0; j < p.claims.size(); ++j)
                for (std::size_t i = 0; i < p.claims[j].alternatives.size(); ++i)
                    if (const auto& r = p.claims[j].alternatives[i]; true) {
                        q.bindInt64(1, int64(p.id));
                        q.bindInt64(2, j);
                        q.bindInt64(3, i);
                        q.bindInt64(4, int64(r.mineral));
                        q.bindInt64(5, int64(r.profileId));
                        q.bindInt64(6, int64(r.methodVersion));
                        q.bindDouble(7, r.threshold);
                        q.bindInt64(8, int64(r.measuresAccessibility));
                        q.bindInt64(9, int64(r.indication));
                        q.bindInt64(10, int64(r.accessibility));
                        q.execute();
                        reuse(q);
                    }
    }
}
void loadScienceState(Database& db, GameState& s) {
    {
        Statement q(
            db,
            "SELECT ordinal,id,name,version,threshold,access FROM measurement_profiles ORDER BY ordinal;");
        while (q.step()) {
            next(q, 0, s.measurementProfiles.size());
            MeasurementProfile r{};
            r.id = MeasurementProfileId{q.columnInt64Strict(1)};
            r.name = q.columnText(2);
            r.methodVersion = integer(q, 3);
            r.detectionThreshold = q.columnDoubleStrict(4);
            r.measuresAccessibility = boolean(q, 5);
            s.measurementProfiles.push_back(std::move(r));
        }
    }
    {
        Statement q(db, "SELECT profile_id,ordinal,mineral FROM measurement_channels ORDER BY "
                        "profile_id,ordinal,mineral;");
        while (q.step()) {
            auto& p = byId(s.measurementProfiles, MeasurementProfileId{q.columnInt64Strict(0)});
            next(q, 1, p.channels.size());
            p.channels.push_back(static_cast<Mineral>(integer(q, 2)));
        }
    }
    {
        Statement q(db, "SELECT colony_id,capacity FROM colony_laboratories ORDER BY colony_id;");
        while (q.step()) {
            auto& colony = byId(s.colonies, ColonyId{q.columnInt64Strict(0)});
            Colony r{};
            r.analysisCapacity = q.columnDoubleStrict(1);
            colony.analysisCapacity = r.analysisCapacity;
        }
    }
    {
        Statement q(db, "SELECT component_id,profile_id FROM component_measurements ORDER BY component_id;");
        while (q.step()) {
            auto& component = byId(s.shipComponents, ShipComponentId{q.columnInt64Strict(0)});
            ShipComponentDefinition r{};
            r.measurementProfileId =
                (q.columnIsNull(1) ? std::nullopt
                                   : std::optional{MeasurementProfileId{q.columnInt64Strict(1)}});
            component.measurementProfileId = r.measurementProfileId;
        }
    }
    {
        Statement q(db, "SELECT "
                        "ordinal,id,body_id,fleet_id,origin,survey_program_id,team_id,pass,first_day,"
                        "acquired_day,available_day FROM observation_batches ORDER BY ordinal;");
        while (q.step()) {
            next(q, 0, s.observations.size());
            ObservationBatch r{};
            r.id = ObservationBatchId{q.columnInt64Strict(1)};
            r.bodyId = BodyId{q.columnInt64Strict(2)};
            r.fleetId = FleetId{q.columnInt64Strict(3)};
            r.origin = static_cast<ObservationOrigin>(integer(q, 4));
            r.surveyProgramId =
                (q.columnIsNull(5) ? std::nullopt : std::optional{SurveyProgramId{q.columnInt64Strict(5)}});
            r.teamId =
                (q.columnIsNull(6) ? std::nullopt : std::optional{SurveyTeamId{q.columnInt64Strict(6)}});
            r.passNumber = integer(q, 7);
            r.firstWorkDay = q.columnInt64Strict(8);
            r.acquiredDay = q.columnInt64Strict(9);
            r.availableDay = q.columnInt64Strict(10);
            s.observations.push_back(std::move(r));
        }
    }
    {
        Statement q(
            db, "SELECT parent_id,ordinal,day FROM observation_work_dates ORDER BY parent_id,ordinal,day;");
        while (q.step()) {
            auto& p = byId(s.observations, ObservationBatchId{q.columnInt64Strict(0)});
            next(q, 1, p.workDates.size());
            p.workDates.push_back(q.columnInt64Strict(2));
        }
    }
    {
        Statement q(db, "SELECT parent_id,ordinal,ship_id,class_id,component_id,profile_id,workdays FROM "
                        "observation_instruments ORDER BY parent_id,ordinal;");
        while (q.step()) {
            auto& p = byId(s.observations, ObservationBatchId{q.columnInt64Strict(0)});
            next(q, 1, p.instruments.size());
            InstrumentExposure r{};
            r.shipId = ShipId{q.columnInt64Strict(2)};
            r.classId = ShipClassId{q.columnInt64Strict(3)};
            r.componentId = ShipComponentId{q.columnInt64Strict(4)};
            r.profileId = MeasurementProfileId{q.columnInt64Strict(5)};
            r.workdays = integer(q, 6);
            p.instruments.push_back({r, byId(s.measurementProfiles, r.profileId), {}});
        }
    }
    {
        Statement q(db, "SELECT parent_id,instrument,ordinal,day FROM observation_exposure_dates ORDER BY "
                        "parent_id,instrument,ordinal,day;");
        while (q.step()) {
            auto& p = byId(s.observations, ObservationBatchId{q.columnInt64Strict(0)});
            auto& dates = child(p.instruments, q, 1).exposure.dates;
            next(q, 2, dates.size());
            dates.push_back(q.columnInt64Strict(3));
        }
    }
    {
        Statement q(db, "SELECT parent_id,ordinal,day FROM field_work_dates ORDER BY parent_id,ordinal,day;");
        while (q.step()) {
            auto& p = byId(s.surveyPrograms, SurveyProgramId{q.columnInt64Strict(0)});
            next(q, 1, p.workDates.size());
            p.workDates.push_back(q.columnInt64Strict(2));
        }
    }
    {
        Statement q(db, "SELECT parent_id,ordinal,ship_id,class_id,component_id,profile_id,workdays FROM "
                        "field_instruments ORDER BY parent_id,ordinal;");
        while (q.step()) {
            auto& p = byId(s.surveyPrograms, SurveyProgramId{q.columnInt64Strict(0)});
            next(q, 1, p.exposures.size());
            InstrumentExposure r{};
            r.shipId = ShipId{q.columnInt64Strict(2)};
            r.classId = ShipClassId{q.columnInt64Strict(3)};
            r.componentId = ShipComponentId{q.columnInt64Strict(4)};
            r.profileId = MeasurementProfileId{q.columnInt64Strict(5)};
            r.workdays = integer(q, 6);
            p.exposures.push_back(r);
        }
    }
    {
        Statement q(db, "SELECT parent_id,instrument,ordinal,day FROM field_exposure_dates ORDER BY "
                        "parent_id,instrument,ordinal,day;");
        while (q.step()) {
            auto& p = byId(s.surveyPrograms, SurveyProgramId{q.columnInt64Strict(0)});
            auto& dates = child(p.exposures, q, 1).dates;
            next(q, 2, dates.size());
            dates.push_back(q.columnInt64Strict(3));
        }
    }
    {
        Statement q(db, "SELECT batch_id,instrument,ordinal,mineral,indication,accessibility FROM "
                        "observation_channels ORDER BY batch_id,instrument,ordinal;");
        while (q.step()) {
            auto& rows =
                child(byId(s.observations, ObservationBatchId{q.columnInt64Strict(0)}).instruments, q, 1)
                    .channels;
            next(q, 2, rows.size());
            ObservationChannel r{};
            r.mineral = static_cast<Mineral>(integer(q, 3));
            r.indication = static_cast<ResourceIndication>(integer(q, 4));
            r.accessibility = static_cast<AccessibilityReading>(integer(q, 5));
            rows.push_back(r);
        }
    }
    {
        Statement q(
            db,
            "SELECT program_id,ordinal,batch_id FROM survey_observation_links ORDER BY program_id,ordinal;");
        while (q.step()) {
            auto& receipt =
                child(byId(s.surveyPrograms, SurveyProgramId{q.columnInt64Strict(0)}).receipts, q, 1);
            SurveyVisitReceipt r{};
            r.observationBatchId = ObservationBatchId{q.columnInt64Strict(2)};
            receipt.observationBatchId = r.observationBatchId;
        }
    }
    {
        Statement q(
            db,
            "SELECT "
            "ordinal,id,name,colony_id,source_kind,source_survey,requested_team,leader_id,allowance,created_"
            "day,revision,lifecycle,closure,closed_day,leased_team,next_report,report_start,issue_signature,"
            "issue_message,issue_ack FROM analysis_programs ORDER BY ordinal,source_kind,source_survey;");
        while (q.step()) {
            next(q, 0, s.analysisPrograms.size());
            AnalysisProgram r{};
            r.id = AnalysisProgramId{q.columnInt64Strict(1)};
            r.charter.name = q.columnText(2);
            r.charter.colonyId = ColonyId{q.columnInt64Strict(3)};
            r.charter.requestedTeamId =
                (q.columnIsNull(6) ? std::nullopt : std::optional{SurveyTeamId{q.columnInt64Strict(6)}});
            r.charter.requestedLeaderId =
                (q.columnIsNull(7) ? std::nullopt : std::optional{PersonId{q.columnInt64Strict(7)}});
            r.charter.workAllowance =
                (q.columnIsNull(8) ? std::nullopt : std::optional{q.columnDoubleStrict(8)});
            r.createdDay = q.columnInt64Strict(9);
            r.charterRevision = integer(q, 10);
            r.lifecycle = static_cast<AnalysisLifecycle>(integer(q, 11));
            r.closure = static_cast<AnalysisClosure>(integer(q, 12));
            r.closedDay = (q.columnIsNull(13) ? std::nullopt : std::optional{q.columnInt64Strict(13)});
            r.leasedTeamId =
                (q.columnIsNull(14) ? std::nullopt : std::optional{SurveyTeamId{q.columnInt64Strict(14)}});
            r.nextReportDay = q.columnInt64Strict(15);
            r.reportStartDay = q.columnInt64Strict(16);
            r.issue.signature = q.columnText(17);
            r.issue.message = q.columnText(18);
            r.issue.acknowledged = boolean(q, 19);
            check(integer(q, 4) == 0 || integer(q, 4) == 1, "Invalid analysis source tag");
            if (integer(q, 4) == 0) {
                check(!q.columnIsNull(5), "Missing follower source");
                r.charter.source = FollowSurveyInput{SurveyProgramId{q.columnInt64Strict(5)}};
            } else {
                check(q.columnIsNull(5), "Fixed source has survey ID");
                r.charter.source = FixedBatchInput{};
            }
            s.analysisPrograms.push_back(std::move(r));
        }
    }
    {
        Statement q(db, "SELECT program_id,ordinal,batch_id FROM analysis_fixed_inputs ORDER BY "
                        "program_id,ordinal,batch_id;");
        while (q.step()) {
            auto& p = byId(s.analysisPrograms, AnalysisProgramId{q.columnInt64Strict(0)});
            check(std::holds_alternative<FixedBatchInput>(p.charter.source), "Fixed inputs on follower");
            auto& rows = std::get<FixedBatchInput>(p.charter.source).batches;
            next(q, 1, rows.size());
            rows.push_back(ObservationBatchId{q.columnInt64Strict(2)});
        }
    }
    {
        Statement q(db, "SELECT program_id,ordinal,id,batch_id,started,ended,outcome,required FROM "
                        "analysis_jobs ORDER BY program_id,ordinal;");
        while (q.step()) {
            auto& rows = byId(s.analysisPrograms, AnalysisProgramId{q.columnInt64Strict(0)}).jobs;
            next(q, 1, rows.size());
            AnalysisJob r{};
            r.id = AnalysisJobId{q.columnInt64Strict(2)};
            r.batchId = ObservationBatchId{q.columnInt64Strict(3)};
            r.startedDay = q.columnInt64Strict(4);
            r.endedDay = (q.columnIsNull(5) ? std::nullopt : std::optional{q.columnInt64Strict(5)});
            r.outcome = static_cast<AnalysisJobOutcome>(integer(q, 6));
            r.requiredWork = q.columnDoubleStrict(7);
            rows.push_back(r);
        }
    }
    {
        Statement q(db,
                    "SELECT program_id,ordinal,job_id,day,colony_id,team_id,leader_id,revision,work,capacity "
                    "FROM analysis_work ORDER BY program_id,ordinal;");
        while (q.step()) {
            auto& rows = byId(s.analysisPrograms, AnalysisProgramId{q.columnInt64Strict(0)}).receipts;
            next(q, 1, rows.size());
            AnalysisWorkReceipt r{};
            r.jobId = AnalysisJobId{q.columnInt64Strict(2)};
            r.day = q.columnInt64Strict(3);
            r.colonyId = ColonyId{q.columnInt64Strict(4)};
            r.teamId = SurveyTeamId{q.columnInt64Strict(5)};
            r.leaderId = PersonId{q.columnInt64Strict(6)};
            r.charterRevision = integer(q, 7);
            r.work = q.columnDoubleStrict(8);
            r.labCapacity = q.columnDoubleStrict(9);
            rows.push_back(r);
        }
    }
    {
        Statement q(db,
                    "SELECT "
                    "program_id,ordinal,start,end,review,revision,allowance,work,lifetime,completed,backlog,"
                    "source,waiting,limitations,cutoff FROM analysis_reports ORDER BY program_id,ordinal;");
        while (q.step()) {
            auto& rows = byId(s.analysisPrograms, AnalysisProgramId{q.columnInt64Strict(0)}).reports;
            next(q, 1, rows.size());
            AnalysisReport r{};
            r.startDay = q.columnInt64Strict(2);
            r.endDay = q.columnInt64Strict(3);
            r.isNinetyDayReview = boolean(q, 4);
            r.charterRevision = integer(q, 5);
            r.workAllowance = (q.columnIsNull(6) ? std::nullopt : std::optional{q.columnDoubleStrict(6)});
            r.workPerformed = q.columnDoubleStrict(7);
            r.lifetimeWork = q.columnDoubleStrict(8);
            r.jobsCompleted = integer(q, 9);
            r.inputBacklog = integer(q, 10);
            r.sourceStatus = q.columnText(11);
            r.waitingReason = q.columnText(12);
            r.limitations = q.columnText(13);
            r.auditThroughId = q.columnInt64Strict(14);
            rows.push_back(std::move(r));
        }
    }
    {
        Statement q(db, "SELECT ordinal,job_id,batch_id,body_id,acquired,published FROM analysis_findings "
                        "ORDER BY ordinal;");
        while (q.step()) {
            next(q, 0, s.analysisFindings.size());
            AnalysisFinding r{};
            r.jobId = AnalysisJobId{q.columnInt64Strict(1)};
            r.batchId = ObservationBatchId{q.columnInt64Strict(2)};
            r.bodyId = BodyId{q.columnInt64Strict(3)};
            r.acquiredDay = q.columnInt64Strict(4);
            r.publishedDay = q.columnInt64Strict(5);
            s.analysisFindings.push_back(std::move(r));
        }
    }
    {
        Statement q(
            db,
            "SELECT job_id,ordinal,mineral,profile,version,threshold,access_method,indication,accessibility "
            "FROM finding_readings ORDER BY job_id,ordinal;");
        while (q.step()) {
            auto& rows = finding(s, AnalysisJobId{q.columnInt64Strict(0)}).readings;
            next(q, 1, rows.size());
            AnalyzedReading r{};
            r.mineral = static_cast<Mineral>(integer(q, 2));
            r.profileId = MeasurementProfileId{q.columnInt64Strict(3)};
            r.methodVersion = integer(q, 4);
            r.threshold = q.columnDoubleStrict(5);
            r.measuresAccessibility = boolean(q, 6);
            r.indication = static_cast<ResourceIndication>(integer(q, 7));
            r.accessibility = static_cast<AccessibilityReading>(integer(q, 8));
            rows.push_back(r);
        }
    }
    {
        Statement q(
            db, "SELECT ordinal,id,body_id,revision,version,published,trigger_job,previous,changed,repeated "
                "FROM assessments ORDER BY ordinal;");
        while (q.step()) {
            next(q, 0, s.assessments.size());
            AssessmentRevision r{};
            r.id = AssessmentId{q.columnInt64Strict(1)};
            r.bodyId = BodyId{q.columnInt64Strict(2)};
            r.revision = integer(q, 3);
            r.methodVersion = integer(q, 4);
            r.publishedDay = q.columnInt64Strict(5);
            r.triggeringJobId = AnalysisJobId{q.columnInt64Strict(6)};
            r.previousId =
                (q.columnIsNull(7) ? std::nullopt : std::optional{AssessmentId{q.columnInt64Strict(7)}});
            r.contentChanged = boolean(q, 8);
            r.repeatedEvidence = boolean(q, 9);
            s.assessments.push_back(std::move(r));
        }
    }
    {
        Statement q(db, "SELECT assessment_id,ordinal,job_id FROM assessment_findings ORDER BY "
                        "assessment_id,ordinal,job_id;");
        while (q.step()) {
            auto& rows = byId(s.assessments, AssessmentId{q.columnInt64Strict(0)}).findingIds;
            next(q, 1, rows.size());
            rows.push_back(AnalysisJobId{q.columnInt64Strict(2)});
        }
    }
    {
        Statement q(db, "SELECT "
                        "assessment_id,ordinal,mineral,indication,accessibility,quantity,site,indication_day,"
                        "access_day,earlier FROM assessment_claims ORDER BY assessment_id,ordinal;");
        while (q.step()) {
            auto& rows = byId(s.assessments, AssessmentId{q.columnInt64Strict(0)}).claims;
            next(q, 1, rows.size());
            AssessedClaim r{};
            r.mineral = static_cast<Mineral>(integer(q, 2));
            r.indication = static_cast<IndicationAssessment>(integer(q, 3));
            r.accessibility = static_cast<AccessibilityAssessment>(integer(q, 4));
            r.quantity = static_cast<QuantityAssessment>(integer(q, 5));
            r.site = static_cast<SiteAssessment>(integer(q, 6));
            r.indicationDay = (q.columnIsNull(7) ? std::nullopt : std::optional{q.columnInt64Strict(7)});
            r.accessibilityDay = (q.columnIsNull(8) ? std::nullopt : std::optional{q.columnInt64Strict(8)});
            r.earlierIndication = boolean(q, 9);
            rows.push_back(std::move(r));
        }
    }
    {
        Statement q(db, "SELECT assessment_id,claim,ordinal,job_id FROM claim_indication_inputs ORDER BY "
                        "assessment_id,claim,ordinal,job_id;");
        while (q.step()) {
            auto& rows = child(byId(s.assessments, AssessmentId{q.columnInt64Strict(0)}).claims, q, 1)
                             .indicationInputs;
            next(q, 2, rows.size());
            rows.push_back(AnalysisJobId{q.columnInt64Strict(3)});
        }
    }
    {
        Statement q(db, "SELECT assessment_id,claim,ordinal,job_id FROM claim_accessibility_inputs ORDER BY "
                        "assessment_id,claim,ordinal,job_id;");
        while (q.step()) {
            auto& rows = child(byId(s.assessments, AssessmentId{q.columnInt64Strict(0)}).claims, q, 1)
                             .accessibilityInputs;
            next(q, 2, rows.size());
            rows.push_back(AnalysisJobId{q.columnInt64Strict(3)});
        }
    }
    {
        Statement q(db,
                    "SELECT "
                    "assessment_id,claim,ordinal,mineral,profile,version,threshold,access_method,indication,"
                    "accessibility FROM claim_alternatives ORDER BY assessment_id,claim,ordinal;");
        while (q.step()) {
            auto& rows =
                child(byId(s.assessments, AssessmentId{q.columnInt64Strict(0)}).claims, q, 1).alternatives;
            next(q, 2, rows.size());
            AnalyzedReading r{};
            r.mineral = static_cast<Mineral>(integer(q, 3));
            r.profileId = MeasurementProfileId{q.columnInt64Strict(4)};
            r.methodVersion = integer(q, 5);
            r.threshold = q.columnDoubleStrict(6);
            r.measuresAccessibility = boolean(q, 7);
            r.indication = static_cast<ResourceIndication>(integer(q, 8));
            r.accessibility = static_cast<AccessibilityReading>(integer(q, 9));
            rows.push_back(r);
        }
    }
    // A missing zero-capacity or null-profile row must not silently default.
    {
        Statement q(db, "SELECT (SELECT count(*) FROM colony_laboratories),(SELECT count(*) FROM "
                        "component_measurements);");
        check(q.step() && q.columnInt64Strict(0) == static_cast<std::int64_t>(s.colonies.size()) &&
                  q.columnInt64Strict(1) == static_cast<std::int64_t>(s.shipComponents.size()),
              "Missing laboratory or component measurement rows");
    }
}
} // namespace deep::save
