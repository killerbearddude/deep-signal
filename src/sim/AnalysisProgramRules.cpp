// Analysis readiness is independent of hidden mineral presence and output value.
#include "sim/AnalysisProgramRules.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <limits>
#include <sstream>
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
namespace {
AnalysisReadiness wait(AnalysisWaitCause cause, std::string text) {
    return {false, cause, std::move(text), std::nullopt, 0.0};
}
AnalysisReadiness readinessAtDay(const GameState& s, const AnalysisProgram& p, std::int64_t day) {
    if (p.lifecycle == AnalysisLifecycle::Closed)
        return wait(AnalysisWaitCause::Closed, "Closed: " + analysisSourceStatus(s, p));
    if (p.lifecycle == AnalysisLifecycle::Suspended)
        return wait(AnalysisWaitCause::Suspended, "Suspended; documented work retained");
    std::optional<ObservationBatchId> input;
    if (const auto* job = activeAnalysisJob(p))
        input = job->batchId;
    else
        for (auto id : analysisSourceBatches(s, p)) {
            if (std::any_of(p.jobs.begin(), p.jobs.end(), [&](const auto& j) { return j.batchId == id; }))
                continue;
            const auto* batch = find(s.observations, id);
            if (batch && batch->availableDay <= day)
                input = id;
            break; // Fixed source order never skips an undelivered selected input.
        }
    if (!input)
        return wait(AnalysisWaitCause::NoInput,
                    "Waiting: no eligible observation input; " + analysisSourceStatus(s, p));
    if (!p.charter.requestedLeaderId)
        return wait(AnalysisWaitCause::NoLeader, "Waiting: no responsible analysis leader");
    if (!p.charter.requestedTeamId)
        return wait(AnalysisWaitCause::NoTeam, "Waiting: no scientist team requested");
    if (const auto owner = controllingScientificTeam(s, *p.charter.requestedTeamId);
        owner && *owner != ProgramController{p.id})
        return wait(AnalysisWaitCause::TeamControlled,
                    "Waiting: scientist team is controlled by " + programControllerLabel(s, *owner));
    const auto* team = find(s.surveyTeams, *p.charter.requestedTeamId);
    if (!team || team->locationKind != SurveyTeamLocationKind::Colony || team->colonyId != p.charter.colonyId)
        return wait(AnalysisWaitCause::TeamLocation,
                    "Waiting: scientist team is not physically at the laboratory colony");
    const auto* colony = find(s.colonies, p.charter.colonyId);
    if (!colony || colony->analysisCapacity <= 0)
        return wait(AnalysisWaitCause::NoLaboratory, "Waiting: no laboratory throughput");
    if (p.charter.workAllowance && analysisWork(p) >= *p.charter.workAllowance)
        return wait(AnalysisWaitCause::WorkAllowance, "Waiting: analyst-work allowance exhausted");
    return {true, AnalysisWaitCause::None, "Ready for finite laboratory work", input, 0.0};
}
} // namespace
AnalysisReadiness analysisReadiness(const GameState& s, const AnalysisProgram& p) {
    return readinessAtDay(s, p, s.date.day);
}
AnalysisReadiness analysisOpeningReadiness(const GameState& s, const AnalysisProgram& p,
                                           const OpeningProgramContext& opening, std::int64_t day) {
    auto result = readinessAtDay(s, p, day);
    if (!result.canAttemptWork)
        return result;
    if (!p.leasedTeamId && opening.occupiedTeams.contains(p.charter.requestedTeamId->value))
        return wait(AnalysisWaitCause::TeamOccupied, "Waiting: scientist team is occupied for this opening");
    if (!opening.availableObservations.contains(result.batchId->value))
        return wait(AnalysisWaitCause::InputUnavailable,
                    "Waiting: observation input is not available in this opening snapshot");
    const auto budget = std::find_if(opening.analysisThroughput.begin(), opening.analysisThroughput.end(),
                                     [&](const auto& value) { return value.first == p.charter.colonyId; });
    if (budget == opening.analysisThroughput.end() || budget->second <= 0)
        return wait(AnalysisWaitCause::LaboratoryContention,
                    "Waiting: laboratory throughput is committed to earlier analysis work this opening.");
    const auto* active = activeAnalysisJob(p);
    const double completed = active ? analysisWork(p, active->id) : 0.0;
    const double remaining = (active ? active->requiredWork : kAnalysisJobWorkdays) - completed;
    const double spent = analysisWork(p);
    const double authority =
        p.charter.workAllowance ? std::max(0.0, *p.charter.workAllowance - spent) : remaining;
    const double work = std::min({remaining, 1.0, budget->second, authority});
    if (!std::isfinite(work) || work <= 0 || completed + work == completed || spent + work == spent ||
        budget->second - work == budget->second)
        return wait(AnalysisWaitCause::NumericLimit,
                    "Waiting: analyst work cannot be represented within current numerical limits");
    result.workThisOpening = work;
    const double uncontended =
        std::min({remaining, 1.0, find(s.colonies, p.charter.colonyId)->analysisCapacity, authority});
    if (work < uncontended) {
        result.cause = AnalysisWaitCause::PartialLaboratoryShare;
        std::ostringstream out;
        out << "Executable with partial laboratory throughput: " << work
            << " team-workdays available after earlier analysis work this opening.";
        result.explanation = out.str();
    }
    return result;
}
AnalysisReadiness analysisNextOpeningReadiness(const GameState& s, const AnalysisProgram& p) {
    // Drafts have no dispatch position. Their preview remains mechanical; live
    // programs project only one pass over existing heads, never a future plan.
    if (p.lifecycle != AnalysisLifecycle::Authorized || !find(s.analysisPrograms, p.id))
        return analysisReadiness(s, p);
    if (s.date.day == std::numeric_limits<std::int64_t>::max())
        return wait(AnalysisWaitCause::NumericLimit, "Waiting: next opening date cannot be represented");
    const auto day = s.date.day + 1;
    OpeningProgramContext opening(s);
    for (const auto& batch : s.observations)
        if (batch.availableDay <= day)
            opening.availableObservations.insert(batch.id.value);
    for (const auto& owner : programOpeningOrder(s)) {
        const auto* id = std::get_if<AnalysisProgramId>(&owner);
        if (!id)
            continue;
        const auto& candidate = *find(s.analysisPrograms, *id);
        auto readiness = analysisOpeningReadiness(s, candidate, opening, day);
        if (candidate.id == p.id)
            return readiness;
        if (!readiness.canAttemptWork)
            continue;
        opening.occupiedTeams.insert(candidate.charter.requestedTeamId->value);
        for (auto& budget : opening.analysisThroughput)
            if (budget.first == candidate.charter.colonyId)
                budget.second -= readiness.workThisOpening;
    }
    return analysisReadiness(s, p);
}
std::string analysisExecutionCondition(const GameState& s, const AnalysisProgram& p) {
    return analysisNextOpeningReadiness(s, p).explanation;
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
