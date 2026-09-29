// Command boundary for frozen construction intent. Commands never consume work,
// relocate engineering teams, revise installed assets, or inspect site geology.
#include "sim/Simulation.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/SiteEvents.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace deep {
namespace {
template <class T, class I> T* find(std::vector<T>& v, I id) {
    auto it = std::find_if(v.begin(), v.end(), [&](const T& x) { return x.id == id; });
    return it == v.end() ? nullptr : &*it;
}
SiteDevelopmentAuditEvent event(const SiteDevelopmentProgram& p, SiteDevelopmentAuditKind kind,
                                std::string detail) {
    return {p.id,
            kind,
            p.charter.siteId,
            p.taskBuilderId,
            p.taskTeamId,
            {},
            p.charter.assignments.leaderId,
            p.charterRevision,
            -1,
            0,
            std::move(detail)};
}
bool nonnegative(double x) {
    return std::isfinite(x) && x >= 0;
}
} // namespace
CommandResult Simulation::createSiteDevelopment(const CreateSiteDevelopmentCommand& command) {
    // No accepted mutation may precede a representable audit identity.
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Event identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    const auto reject = [&](std::string reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    if (state_.ids.nextSiteDevelopmentProgramId == std::numeric_limits<std::int64_t>::max() ||
        (command.newSite && state_.ids.nextSiteId == std::numeric_limits<std::int64_t>::max()))
        return reject("Site/development identity limit reached");
    ResourceSite site;
    auto charter = command.charter;
    if (command.newSite) {
        const auto& n = *command.newSite;
        const auto& policy = n.operatingPolicy;
        if (charter.siteId.value != 0)
            return reject("New site registration requires an unassigned site ID");
        if (!find(state_.bodies, n.bodyId) || n.name.find_first_not_of(" \t\r\n") == std::string::npos)
            return reject("New site requires an existing public body and nonempty name");
        if (n.institutionId && !find(state_.institutions, *n.institutionId))
            return reject("Site institution does not exist");
        if ((policy.leaderId && !find(state_.people, *policy.leaderId)) ||
            !nonnegative(policy.requestedIcePerDay) || !nonnegative(policy.reactorFuelFloor) ||
            !nonnegative(policy.compositesFloor) ||
            (policy.lifetimeDutyAllowance && !nonnegative(*policy.lifetimeDutyAllowance)))
            return reject("Site operating policy is malformed");
        site.id = SiteId{state_.ids.nextSiteId};
        site.bodyId = n.bodyId;
        site.name = n.name;
        site.institutionId = n.institutionId;
        site.createdDay = state_.date.day;
        site.operatingPolicy = policy;
        site.reportStartDay = state_.date.day;
        charter.siteId = site.id;
    }
    if (const auto error = validateSiteDevelopmentCharter(state_, charter, command.newSite.has_value()))
        return reject(*error);
    SiteDevelopmentProgram p;
    p.id = SiteDevelopmentProgramId{state_.ids.nextSiteDevelopmentProgramId};
    p.charter = std::move(charter);
    p.createdDay = state_.date.day;
    p.reportStartDay = state_.date.day;
    p.rows.resize(p.charter.package.size());
    try {
        p.nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30);
        site.nextReportDay = p.nextReportDay;
    } catch (const std::exception&) {
        return reject("Development report date limit reached");
    }
    // Prepare both owning vectors before registering either record, preserving
    // the atomic new-site/project boundary on malformed data and allocation failure.
    state_.siteDevelopmentPrograms.reserve(state_.siteDevelopmentPrograms.size() + 1);
    if (command.newSite)
        state_.resourceSites.reserve(state_.resourceSites.size() + 1);
    if (command.newSite) {
        state_.resourceSites.push_back(std::move(site));
        ++state_.ids.nextSiteId;
    }
    state_.siteDevelopmentPrograms.push_back(std::move(p));
    ++state_.ids.nextSiteDevelopmentProgramId;
    appendEvent(EventSeverity::Info,
                event(state_.siteDevelopmentPrograms.back(), SiteDevelopmentAuditKind::Authorized,
                      "Construction authorized; no physical resources or installed capacity created"));
    return CommandResult::success("Site development authorized");
}
CommandResult Simulation::amendSiteDevelopment(const AmendSiteDevelopmentCommand& command) {
    // No accepted mutation may precede a representable audit identity.
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Event identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    const auto reject = [&](std::string reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    auto* p = find(state_.siteDevelopmentPrograms, command.programId);
    if (!p || p->lifecycle == SiteDevelopmentLifecycle::Closed)
        return reject("Development is missing or physically closed");
    if (p->charterRevision == std::numeric_limits<int>::max())
        return reject("Development revision limit reached");
    auto prepared = p->charter;
    prepared.assignments = command.assignments;
    if (const auto error = validateSiteDevelopmentCharter(state_, prepared))
        return reject(*error);
    std::swap(p->charter, prepared);
    ++p->charterRevision;
    p->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, event(*p, SiteDevelopmentAuditKind::Amended,
                                           "Future construction authority amended; route, package, "
                                           "expenditure, and actual participants retained"));
    return CommandResult::success("Development authority amended");
}
CommandResult Simulation::setSiteDevelopmentLifecycle(SiteDevelopmentProgramId id,
                                                      SiteDevelopmentLifecycle target) {
    // No accepted mutation may precede a representable audit identity.
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Event identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    auto* p = find(state_.siteDevelopmentPrograms, id);
    if (!p || p->lifecycle == SiteDevelopmentLifecycle::Closed ||
        (p->lifecycle == target && target != SiteDevelopmentLifecycle::Closing &&
         target != SiteDevelopmentLifecycle::Closed) ||
        (target == SiteDevelopmentLifecycle::Authorized &&
         p->lifecycle != SiteDevelopmentLifecycle::Suspended)) {
        const std::string why = "Development lifecycle action unavailable";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    auto kind = SiteDevelopmentAuditKind::Resumed;
    if (target == SiteDevelopmentLifecycle::Suspended)
        kind = SiteDevelopmentAuditKind::Suspended;
    if (target == SiteDevelopmentLifecycle::Closing || target == SiteDevelopmentLifecycle::Closed) {
        // Cancellation stops future work; earned equipment and its Completed
        // disposition remain intact while the original builders return.
        if (p->closure == SiteDevelopmentClosure::None)
            p->closure = SiteDevelopmentClosure::Cancelled;
        target = SiteDevelopmentLifecycle::Closing;
        kind = SiteDevelopmentAuditKind::Cancelled;
        if (p->taskBuilderId)
            p->task = SiteDevelopmentTask::Return;
    }
    if (target == SiteDevelopmentLifecycle::Authorized && p->closure != SiteDevelopmentClosure::None)
        target = SiteDevelopmentLifecycle::Closing;
    p->lifecycle = target;
    p->issue.acknowledged = true;
    appendEvent(
        EventSeverity::Info,
        event(*p, kind, "Development intention changed; paid movement and physical custody retained"));
    return CommandResult::success("Development intention changed");
}
CommandResult
Simulation::acknowledgeSiteDevelopmentIssue(const AcknowledgeSiteDevelopmentIssueCommand& command) {
    // No accepted mutation may precede a representable audit identity.
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Event identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    auto* p = find(state_.siteDevelopmentPrograms, command.programId);
    if (!p || p->issue.acknowledged || p->issue.signature.empty() ||
        p->issue.signature != command.signature) {
        const std::string why = "Development issue identity is not pending";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    p->issue.acknowledged = true;
    appendEvent(EventSeverity::Info,
                event(*p, SiteDevelopmentAuditKind::IssueAcknowledged, command.signature));
    return CommandResult::success("Development issue acknowledged; physical constraint unchanged");
}
} // namespace deep
