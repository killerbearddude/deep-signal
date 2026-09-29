// Real laboratory labor over immutable acquired inputs. Publication is prepared
// before applying the last work step, avoiding free assessments after a retry.
#include "sim/AnalysisProgramExecution.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace deep {
namespace {
template <class T, class ID> const T* find(const std::vector<T>& rows, ID id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
void audit(AnalysisProgram& p, AnalysisAuditKind kind, AnalysisJobId job, const std::string& text,
           const AnalysisExecutionHooks& hooks) {
    hooks.emit(EventSeverity::Info, AnalysisProgramAuditEvent{p.id, kind, job, text});
}
bool allDone(const GameState& s, const AnalysisProgram& p) {
    if (activeAnalysisJob(p))
        return false;
    for (auto batch : analysisSourceBatches(s, p))
        if (std::none_of(p.jobs.begin(), p.jobs.end(), [&](const auto& j) {
                return j.batchId == batch && j.outcome == AnalysisJobOutcome::Completed;
            }))
            return false;
    return true;
}
void closeIfDone(GameState& s, AnalysisProgram& p, const AnalysisExecutionHooks& hooks) {
    if (p.lifecycle != AnalysisLifecycle::Authorized || !analysisSourceEnded(s, p) || !allDone(s, p))
        return;
    p.leasedTeamId.reset();
    p.lifecycle = AnalysisLifecycle::Closed;
    p.closure = AnalysisClosure::Completed;
    p.closedDay = s.date.day;
    p.issue = {};
    audit(p, AnalysisAuditKind::Completed, {}, analysisSourceStatus(s, p), hooks);
}
} // namespace
void runAnalysisOpeningDay(GameState& s, AnalysisProgram& p, OpeningProgramContext& opening,
                           const AnalysisExecutionHooks& hooks) {
    if (p.lifecycle != AnalysisLifecycle::Authorized)
        return;
    closeIfDone(s, p, hooks);
    if (p.lifecycle == AnalysisLifecycle::Closed)
        return;
    const auto readiness = analysisOpeningReadiness(s, p, opening, s.date.day);
    if (!readiness.canAttemptWork)
        return;
    const auto team = *p.charter.requestedTeamId;
    auto budget = std::find_if(opening.analysisThroughput.begin(), opening.analysisThroughput.end(),
                               [&](const auto& value) { return value.first == p.charter.colonyId; });
    const AnalysisJob* active = activeAnalysisJob(p);
    std::optional<AnalysisJob> newJob;
    if (!active) {
        if (s.ids.nextAnalysisJobId <= 0 ||
            s.ids.nextAnalysisJobId == std::numeric_limits<std::int64_t>::max())
            throw std::runtime_error("Analysis job identity limit reached");
        newJob = AnalysisJob{AnalysisJobId{s.ids.nextAnalysisJobId},
                             *readiness.batchId,
                             s.date.day,
                             std::nullopt,
                             AnalysisJobOutcome::Active,
                             kAnalysisJobWorkdays};
        active = &*newJob;
    }
    const double remaining = active->requiredWork - analysisWork(p, active->id);
    const double work = readiness.workThisOpening;
    // Equality uses the actual remaining delta. Never round a tiny shortage up
    // into a free completion; a partial receipt remains partial.
    const bool completes = work == remaining;
    std::optional<AnalysisFinding> finding;
    std::optional<AssessmentRevision> assessment;
    if (completes) {
        const auto* batch = find(s.observations, active->batchId);
        if (!batch)
            throw std::runtime_error("Analysis input disappeared");
        finding = interpretObservationBatch(*batch, active->id, s.date.day);
        const AssessmentRevision* previous = nullptr;
        for (const auto& revision : s.assessments)
            if (revision.bodyId == batch->bodyId)
                previous = &revision;
        if (s.ids.nextAssessmentId <= 0 || s.ids.nextAssessmentId == std::numeric_limits<std::int64_t>::max())
            throw std::runtime_error("Assessment identity limit reached");
        auto completedFindings = s.analysisFindings;
        completedFindings.push_back(*finding);
        assessment =
            assembleAssessmentRevision(completedFindings, batch->bodyId, AssessmentId{s.ids.nextAssessmentId},
                                       active->id, s.date.day, previous);
        s.analysisFindings.reserve(s.analysisFindings.size() + 1);
        s.assessments.reserve(s.assessments.size() + 1);
    }
    const auto jobId = active->id;
    p.jobs.reserve(p.jobs.size() + (newJob ? 1U : 0U));
    p.receipts.reserve(p.receipts.size() + 1);
    if (hooks.prepareEvents)
        hooks.prepareEvents(5);
    s.eventLog.reserve(s.eventLog.size() + 5);
    // Occupancy insertion can allocate; it precedes authoritative job creation.
    opening.occupiedTeams.insert(team.value);
    if (newJob) {
        p.jobs.push_back(std::move(*newJob));
        ++s.ids.nextAnalysisJobId;
    }
    const bool acquired = !p.leasedTeamId;
    p.leasedTeamId = team;
    p.receipts.push_back({jobId, s.date.day, p.charter.colonyId, team, *p.charter.requestedLeaderId,
                          p.charterRevision, work, find(s.colonies, p.charter.colonyId)->analysisCapacity});
    budget->second -= work;
    if (completes) {
        auto& job = p.jobs.back();
        job.outcome = AnalysisJobOutcome::Completed;
        job.endedDay = s.date.day;
        s.analysisFindings.push_back(std::move(*finding));
        s.assessments.push_back(std::move(*assessment));
        ++s.ids.nextAssessmentId;
        p.leasedTeamId.reset(); // Opening occupancy deliberately remains set.
    }
    if (acquired)
        audit(p, AnalysisAuditKind::LeaseAcquired, jobId, "Scientific team acquired at actual laboratory",
              hooks);
    if (newJob)
        audit(p, AnalysisAuditKind::JobStarted, jobId,
              "Fixed three team-workday batch interpretation started", hooks);
    audit(p, AnalysisAuditKind::WorkPerformed, jobId, "Dated scientific team-work receipt recorded", hooks);
    if (completes)
        audit(p, AnalysisAuditKind::JobCompleted, jobId,
              "Immutable assessment published from recorded inputs", hooks);
    closeIfDone(s, p, hooks);
}
void finishAnalysisDay(GameState& s, const AnalysisExecutionHooks& hooks) {
    for (auto& p : s.analysisPrograms) {
        closeIfDone(s, p, hooks);
        const bool backlog = !allDone(s, p);
        if (p.lifecycle == AnalysisLifecycle::Authorized && backlog && p.charter.workAllowance &&
            analysisWork(p) > 0 && analysisWork(p) >= *p.charter.workAllowance && p.issue.signature.empty()) {
            p.issue = {"analysis-work-allowance", "Analyst-work allowance exhausted", false};
            audit(p, AnalysisAuditKind::IssueRaised, {}, p.issue.message, hooks);
        }
        if (p.nextReportDay != s.date.day || (p.closedDay && *p.closedDay < s.date.day))
            continue;
        AnalysisReport r;
        r.startDay = p.reportStartDay;
        r.endDay = s.date.day;
        r.isNinetyDayReview = s.date.day % 90 == 0;
        r.charterRevision = p.charterRevision;
        r.workAllowance = p.charter.workAllowance;
        r.lifetimeWork = analysisWork(p);
        for (const auto& receipt : p.receipts)
            if (receipt.day > r.startDay && receipt.day <= r.endDay)
                r.workPerformed += receipt.work;
        for (const auto& job : p.jobs)
            if (job.outcome == AnalysisJobOutcome::Completed && job.endedDay && *job.endedDay > r.startDay &&
                *job.endedDay <= r.endDay)
                ++r.jobsCompleted;
        for (auto batch : analysisSourceBatches(s, p))
            if (std::none_of(p.jobs.begin(), p.jobs.end(), [&](const auto& j) {
                    return j.batchId == batch && j.outcome == AnalysisJobOutcome::Completed;
                }))
                ++r.inputBacklog;
        r.sourceStatus = analysisSourceStatus(s, p);
        r.waitingReason = analysisExecutionCondition(s, p);
        r.limitations = "Reserve quantity unmeasured; site suitability unassessed. Repetition adds history, "
                        "not certainty.";
        r.auditThroughId = s.ids.nextEventId - 1;
        const auto next = nextGlobalSurveyBoundary(s.date.day, 30);
        p.reports.push_back(std::move(r));
        p.reportStartDay = s.date.day;
        p.nextReportDay = next;
        audit(p, AnalysisAuditKind::ReportPublished, {},
              "Analysis report published at fixed policy and audit boundary", hooks);
    }
}
} // namespace deep
