// Serialized charter/lifecycle commands. Documented work is transferable between
// analysts; changing requested identity releases staff without moving anyone.
#include "sim/Simulation.h"
#include "sim/AnalysisProgramRules.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <limits>
namespace deep {
namespace {
AnalysisProgram* find(GameState& s, AnalysisProgramId id) {
    const auto i = std::find_if(s.analysisPrograms.begin(), s.analysisPrograms.end(),
                                [&](const auto& p) { return p.id == id; });
    return i == s.analysisPrograms.end() ? nullptr : &*i;
}
} // namespace
CommandResult Simulation::createAnalysisProgram(const CreateAnalysisProgramCommand& c) {
    const auto reject = [&](const std::string& why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    };
    if (auto error = validateAnalysisCharter(state_, c.charter))
        return reject(*error);
    if (state_.ids.nextAnalysisProgramId == std::numeric_limits<std::int64_t>::max())
        return reject("Analysis identity limit reached");
    AnalysisProgram prepared;
    prepared.id = AnalysisProgramId{state_.ids.nextAnalysisProgramId};
    prepared.charter = c.charter;
    prepared.createdDay = state_.date.day;
    prepared.reportStartDay = state_.date.day;
    try {
        prepared.nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30);
    } catch (const std::exception&) {
        return reject("Analysis reporting date limit reached");
    }
    acknowledgeKnownAnalysisLimit(state_, prepared);
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    state_.analysisPrograms.push_back(std::move(prepared));
    ++state_.ids.nextAnalysisProgramId;
    appendEvent(EventSeverity::Info,
                AnalysisProgramAuditEvent{state_.analysisPrograms.back().id,
                                          AnalysisAuditKind::Authorized,
                                          {},
                                          "Analysis intent authorized; no work or staff reserved"});
    return CommandResult::success("Analysis authorized; " +
                                  analysisExecutionCondition(state_, state_.analysisPrograms.back()));
}
CommandResult Simulation::amendAnalysisProgram(const AmendAnalysisProgramCommand& c) {
    const auto reject = [&](const std::string& why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    };
    auto* p = find(state_, c.programId);
    if (!p || p->lifecycle == AnalysisLifecycle::Closed)
        return reject("Analysis program missing or closed");
    if (p->charterRevision == std::numeric_limits<int>::max())
        return reject("Analysis charter revision limit reached");
    auto prepared = p->charter;
    prepared.name = c.amendment.name;
    prepared.requestedTeamId = c.amendment.requestedTeamId;
    prepared.requestedLeaderId = c.amendment.requestedLeaderId;
    prepared.workAllowance = c.amendment.workAllowance;
    if (auto error = validateAnalysisCharter(state_, prepared))
        return reject(*error);
    const bool release = prepared.requestedTeamId != p->charter.requestedTeamId ||
                         prepared.requestedLeaderId != p->charter.requestedLeaderId;
    if (release)
        p->leasedTeamId.reset();
    std::swap(p->charter, prepared);
    ++p->charterRevision;
    acknowledgeKnownAnalysisLimit(state_, *p);
    appendEvent(EventSeverity::Info,
                AnalysisProgramAuditEvent{
                    p->id,
                    AnalysisAuditKind::Amended,
                    {},
                    "Analysis assignments/authority amended; dated work and fixed source retained"});
    return CommandResult::success("Analysis charter amended");
}
CommandResult Simulation::setAnalysisLifecycle(AnalysisProgramId id, AnalysisLifecycle target) {
    auto* p = find(state_, id);
    if (!p || p->lifecycle == AnalysisLifecycle::Closed || p->lifecycle == target) {
        const std::string why = "Analysis lifecycle action unavailable";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    p->lifecycle = target;
    p->issue.acknowledged = true;
    if (target != AnalysisLifecycle::Authorized)
        p->leasedTeamId.reset();
    if (target == AnalysisLifecycle::Closed) {
        p->closure = AnalysisClosure::Cancelled;
        p->closedDay = state_.date.day;
        if (activeAnalysisJob(*p)) {
            p->jobs.back().outcome = AnalysisJobOutcome::Cancelled;
            p->jobs.back().endedDay = state_.date.day;
        }
    }
    const auto kind = target == AnalysisLifecycle::Closed      ? AnalysisAuditKind::Cancelled
                      : target == AnalysisLifecycle::Suspended ? AnalysisAuditKind::Suspended
                                                               : AnalysisAuditKind::Resumed;
    appendEvent(EventSeverity::Info,
                AnalysisProgramAuditEvent{
                    id,
                    kind,
                    {},
                    "Analysis disposition changed; prior observations, receipts and assessments retained"});
    return CommandResult::success(analysisExecutionCondition(state_, *p));
}
CommandResult Simulation::acknowledgeAnalysisIssue(const AcknowledgeAnalysisIssueCommand& c) {
    auto* p = find(state_, c.programId);
    if (!p || c.signature.empty() || p->issue.signature != c.signature) {
        const std::string why = "Analysis issue is missing or changed";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    p->issue.acknowledged = true;
    appendEvent(EventSeverity::Info,
                AnalysisProgramAuditEvent{p->id, AnalysisAuditKind::IssueAcknowledged, {}, c.signature});
    return CommandResult::success("Analysis issue acknowledged");
}
} // namespace deep
