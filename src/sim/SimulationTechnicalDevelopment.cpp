// Command admission for durable P5 intent. Commands validate and revise
// authority only; daily execution owns all engineering work and artifacts.
#include "sim/Simulation.h"
#include "sim/TechnicalDevelopmentRules.h"
#include "sim/SurveyProgramRules.h"

#include <algorithm>
#include <limits>

namespace deep {
namespace {
template <class Rows, class Id> auto* find(Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
TechnicalDevelopmentAuditEvent event(const TechnicalDevelopmentProgram& program,
                                     TechnicalDevelopmentAuditKind kind, std::string detail) {
    return {
        program.id,       kind, program.charter.opportunityId, program.stage, program.charterRevision, 0.0,
        std::move(detail)};
}
} // namespace

CommandResult Simulation::createTechnicalDevelopment(const CreateTechnicalDevelopmentCommand& command) {
    const auto reject = [&](std::string why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(std::move(why));
    };
    if (const auto error = validateTechnicalDevelopmentCharter(state_, command.charter))
        return reject(*error);
    if (state_.ids.nextTechnicalDevelopmentProgramId == std::numeric_limits<std::int64_t>::max())
        return reject("Technical-development identity limit reached");
    if (state_.ids.nextEventId > std::numeric_limits<std::int64_t>::max() - 2)
        return CommandResult::failure("Technical-development audit identity limit reached");
    state_.technicalDevelopmentPrograms.reserve(state_.technicalDevelopmentPrograms.size() + 1);
    state_.eventLog.reserve(state_.eventLog.size() + 2);
    TechnicalDevelopmentProgram program;
    program.id = TechnicalDevelopmentProgramId{state_.ids.nextTechnicalDevelopmentProgramId++};
    program.charter = command.charter;
    program.createdDay = state_.date.day;
    program.reportStartDay = state_.date.day;
    program.nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30);
    program.stage =
        firstMissingTechnicalStage(state_, program.charter.opportunityId, program.charter.developmentColonyId,
                                   program.charter.requestedTeamId, program.charter.scope);
    if (program.stage == TechnicalDevelopmentStage::Complete) {
        program.lifecycle = TechnicalDevelopmentLifecycle::Closed;
        program.closure = TechnicalDevelopmentClosure::Completed;
        program.closedDay = state_.date.day;
    }
    state_.technicalDevelopmentPrograms.push_back(std::move(program));
    auto& stored = state_.technicalDevelopmentPrograms.back();
    appendEvent(EventSeverity::Info,
                event(stored, TechnicalDevelopmentAuditKind::Authorized,
                      "Technical-development intent authorized; no capability or inventory created"));
    if (stored.lifecycle == TechnicalDevelopmentLifecycle::Closed)
        appendEvent(EventSeverity::Info, event(stored, TechnicalDevelopmentAuditKind::Closed,
                                               "Requested scope was already satisfied by durable artifacts"));
    return CommandResult::success("Technical-development intent authorized");
}

CommandResult Simulation::amendTechnicalDevelopment(const AmendTechnicalDevelopmentCommand& command) {
    auto* program = find(state_.technicalDevelopmentPrograms, command.programId);
    const auto reject = [&](std::string why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(std::move(why));
    };
    if (!program || program->lifecycle == TechnicalDevelopmentLifecycle::Closed)
        return reject("Technical-development program is missing or closed");
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Technical-development audit identity limit reached");
    if (command.charter.opportunityId != program->charter.opportunityId ||
        command.charter.developmentColonyId != program->charter.developmentColonyId)
        return reject("Opportunity and development colony are immutable program identity");
    if (program->charterRevision == std::numeric_limits<int>::max())
        return reject("Technical-development revision limit reached");
    if (const auto error = validateTechnicalDevelopmentCharter(state_, command.charter, true))
        return reject(*error);
    program->charter = command.charter;
    ++program->charterRevision;
    program->leasedTeamId.reset();
    program->issue.acknowledged = true;
    reconcileTechnicalStageAfterAmendment(state_, *program);
    appendEvent(EventSeverity::Info,
                event(*program, TechnicalDevelopmentAuditKind::Amended,
                      "Future technical authority amended; completed artifacts and sunk work retained"));
    return CommandResult::success("Technical-development authority amended");
}

CommandResult Simulation::setTechnicalDevelopmentLifecycle(TechnicalDevelopmentProgramId id,
                                                           TechnicalDevelopmentLifecycle target) {
    auto* program = find(state_.technicalDevelopmentPrograms, id);
    const auto reject = [&](std::string why) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(std::move(why));
    };
    if (!program || program->lifecycle == TechnicalDevelopmentLifecycle::Closed)
        return reject("Technical-development lifecycle action is unavailable");
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Technical-development audit identity limit reached");
    if (target == TechnicalDevelopmentLifecycle::Suspended) {
        if (program->lifecycle == target)
            return reject("Technical development is already suspended");
        program->lifecycle = target;
        program->leasedTeamId.reset();
        program->issue.acknowledged = true;
        appendEvent(EventSeverity::Info,
                    event(*program, TechnicalDevelopmentAuditKind::Suspended,
                          "Technical work suspended; partial work and artifacts retained"));
        return CommandResult::success("Technical development suspended");
    }
    if (target == TechnicalDevelopmentLifecycle::Authorized) {
        if (program->lifecycle != TechnicalDevelopmentLifecycle::Suspended)
            return reject("Only suspended technical development can resume");
        program->lifecycle = target;
        appendEvent(EventSeverity::Info, event(*program, TechnicalDevelopmentAuditKind::Resumed,
                                               "Technical-development intent resumed"));
        return CommandResult::success("Technical development resumed");
    }
    program->lifecycle = TechnicalDevelopmentLifecycle::Closed;
    program->closure = TechnicalDevelopmentClosure::Cancelled;
    program->closedDay = state_.date.day;
    program->leasedTeamId.reset();
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info,
                event(*program, TechnicalDevelopmentAuditKind::Cancelled,
                      "Future work cancelled; consumed material and completed artifacts retained"));
    return CommandResult::success("Technical development cancelled");
}

CommandResult
Simulation::acknowledgeTechnicalDevelopmentIssue(const AcknowledgeTechnicalDevelopmentIssueCommand& command) {
    auto* program = find(state_.technicalDevelopmentPrograms, command.programId);
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Technical-development audit identity limit reached");
    if (!program || program->issue.signature.empty() || program->issue.acknowledged ||
        program->issue.signature != command.signature) {
        const std::string why = "Technical-development issue identity is not pending";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info,
                event(*program, TechnicalDevelopmentAuditKind::IssueAcknowledged,
                      "Technical issue acknowledged; evidence and physical limits unchanged"));
    return CommandResult::success("Technical-development issue acknowledged");
}

} // namespace deep
