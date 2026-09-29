// Analysis readiness is independent of hidden mineral presence and output value.
#include "sim/AnalysisProgramRules.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace deep {
namespace {
template <class T, class ID> const T* find(const std::vector<T>& rows, ID id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
} // namespace
std::optional<std::string> validateAnalysisCharter(const GameState& s, const AnalysisCharter& c) {
    if (c.name.empty())
        return "Analysis name must not be empty";
    if (!find(s.colonies, c.colonyId))
        return "Analysis laboratory colony does not exist";
    if (c.requestedTeamId && !find(s.surveyTeams, *c.requestedTeamId))
        return "Analysis scientist team does not exist";
    if (c.requestedLeaderId && !find(s.people, *c.requestedLeaderId))
        return "Analysis leader does not exist";
    if (c.workAllowance && (!std::isfinite(*c.workAllowance) || *c.workAllowance < 0))
        return "Analysis work allowance must be finite and nonnegative";
    if (c.source.valueless_by_exception())
        return "Analysis input source is malformed";
    if (const auto* source = std::get_if<FollowSurveyInput>(&c.source)) {
        if (!find(s.surveyPrograms, source->programId))
            return "Source survey program does not exist";
    } else {
        const auto& ids = std::get<FixedBatchInput>(c.source).batches;
        if (ids.empty())
            return "Fixed analysis requires at least one acquired observation";
        std::set<std::int64_t> seen;
        for (auto id : ids)
            if (!find(s.observations, id) || !seen.insert(id.value).second)
                return "Fixed analysis input is missing or duplicated";
    }
    return std::nullopt;
}
std::vector<ObservationBatchId> analysisSourceBatches(const GameState& s, const AnalysisProgram& p) {
    if (const auto* fixed = std::get_if<FixedBatchInput>(&p.charter.source))
        return fixed->batches;
    const auto id = std::get<FollowSurveyInput>(p.charter.source).programId;
    std::vector<ObservationBatchId> result;
    // Acquisition vector order is authoritative; do not reorder by body or ID.
    for (const auto& b : s.observations)
        if (b.surveyProgramId == id)
            result.push_back(b.id);
    return result;
}
double analysisWork(const AnalysisProgram& p, std::optional<AnalysisJobId> job) {
    double work = 0;
    for (const auto& r : p.receipts)
        if (!job || r.jobId == job)
            work += r.work;
    return work;
}
const AnalysisJob* activeAnalysisJob(const AnalysisProgram& p) {
    return !p.jobs.empty() && p.jobs.back().outcome == AnalysisJobOutcome::Active ? &p.jobs.back() : nullptr;
}
bool analysisSourceEnded(const GameState& s, const AnalysisProgram& p) {
    if (std::holds_alternative<FixedBatchInput>(p.charter.source))
        return true;
    const auto* source = find(s.surveyPrograms, std::get<FollowSurveyInput>(p.charter.source).programId);
    return source && source->lifecycle == SurveyProgramLifecycle::Closed;
}
std::string analysisSourceStatus(const GameState& s, const AnalysisProgram& p) {
    if (std::holds_alternative<FixedBatchInput>(p.charter.source))
        return "Fixed acquired batches";
    const auto* source = find(s.surveyPrograms, std::get<FollowSurveyInput>(p.charter.source).programId);
    if (!source)
        return "Source missing";
    if (source->lifecycle == SurveyProgramLifecycle::Closed)
        return analysisSourceBatches(s, p).empty() ? "Source ended; no observation batches" : "Source ended";
    if (source->lifecycle == SurveyProgramLifecycle::Suspended)
        return "Source suspended";
    if (source->lifecycle == SurveyProgramLifecycle::Closing)
        return "Source returning; not yet closed";
    return "Following survey acquisition";
}
std::string analysisExecutionCondition(const GameState& s, const AnalysisProgram& p) {
    if (p.lifecycle == AnalysisLifecycle::Closed)
        return "Closed: " + analysisSourceStatus(s, p);
    if (p.lifecycle == AnalysisLifecycle::Suspended)
        return "Suspended; documented work retained";
    bool hasInput = activeAnalysisJob(p) != nullptr;
    for (auto id : analysisSourceBatches(s, p)) {
        if (std::any_of(p.jobs.begin(), p.jobs.end(), [&](const auto& j) { return j.batchId == id; }))
            continue;
        const auto* b = find(s.observations, id);
        if (b && b->availableDay <= s.date.day)
            hasInput = true;
        break; // Fixed source order never skips an undelivered selected input.
    }
    if (!hasInput)
        return "Waiting: no eligible observation input; " + analysisSourceStatus(s, p);
    if (!p.charter.requestedLeaderId)
        return "Waiting: no responsible analysis leader";
    if (!p.charter.requestedTeamId)
        return "Waiting: no scientist team requested";
    if (const auto owner = controllingScientificTeam(s, *p.charter.requestedTeamId);
        owner && *owner != ProgramController{p.id})
        return "Waiting: scientist team is controlled by " + programControllerLabel(s, *owner);
    const auto* team = find(s.surveyTeams, *p.charter.requestedTeamId);
    if (!team || team->locationKind != SurveyTeamLocationKind::Colony || team->colonyId != p.charter.colonyId)
        return "Waiting: scientist team is not physically at the laboratory colony";
    const auto* colony = find(s.colonies, p.charter.colonyId);
    if (!colony || colony->analysisCapacity <= 0)
        return "Waiting: no laboratory throughput";
    if (p.charter.workAllowance && analysisWork(p) >= *p.charter.workAllowance)
        return "Waiting: analyst-work allowance exhausted";
    return "Ready for finite laboratory work";
}
void acknowledgeKnownAnalysisLimit(const GameState& s, AnalysisProgram& p) {
    // An explicitly inadequate allowance is accepted intent. Remember it before
    // its inevitable exhaustion so a known limit does not become a surprise.
    const double remaining =
        p.charter.workAllowance ? *p.charter.workAllowance - analysisWork(p) : kAnalysisJobWorkdays;
    const auto* job = activeAnalysisJob(p);
    double need = job ? job->requiredWork - analysisWork(p, job->id) : 0;
    for (auto batch : analysisSourceBatches(s, p))
        if (std::none_of(p.jobs.begin(), p.jobs.end(), [&](const auto& j) { return j.batchId == batch; }))
            need += kAnalysisJobWorkdays;
    if (need == 0)
        need = kAnalysisJobWorkdays;
    if (remaining < need)
        p.issue = {"analysis-work-allowance", "Analyst-work allowance exhausted", true};
    else
        p.issue = {};
}
} // namespace deep
