// Standing site authority is independent from construction closeout. Amendments
// never erase paid duty, stock, installed groups or observed operating outcomes.
#include "sim/Simulation.h"
#include "sim/SiteOperationRules.h"
#include <algorithm>
#include <limits>
namespace deep {
namespace {
ResourceSite* site(GameState& s, SiteId id) {
    auto it = std::find_if(s.resourceSites.begin(), s.resourceSites.end(),
                           [&](const auto& p) { return p.id == id; });
    return it == s.resourceSites.end() ? nullptr : &*it;
}
} // namespace
CommandResult Simulation::amendSiteOperatingPolicy(const AmendSiteOperatingPolicyCommand& command) {
    if (state_.ids.nextEventId > std::numeric_limits<std::int64_t>::max() - 3)
        return CommandResult::failure("Site audit identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 3);
    auto* at = site(state_, command.siteId);
    std::optional<std::string> error;
    if (!at)
        error = "Site does not exist";
    else if (at->operatingRevision == std::numeric_limits<int>::max())
        error = "Site operating revision limit reached";
    else
        error = validateSiteOperatingPolicy(state_, command.policy);
    if (error) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{*error});
        return CommandResult::failure(*error);
    }
    // Responding with revised authority also acknowledges the disclosed old
    // episode; the next opening still re-evaluates the physical constraint.
    if (!at->issue.acknowledged) {
        const auto result =
            acknowledgeSiteOperatingIssue({at->id, at->issue.cause, at->issue.episodeStartedDay});
        if (!result.ok)
            return result;
    }
    at->operatingPolicy = command.policy;
    ++at->operatingRevision;
    appendEvent(EventSeverity::Info,
                SiteOperatingAuditEvent{at->id, SiteOperatingAuditKind::PolicyAmended, at->operatingRevision,
                                        SiteOperatingIssueCause::None, 0, 0,
                                        "Future operating authority amended; paid history retained"});
    if (!at->dutyReceipts.empty() && at->operatingPolicy.enabled) {
        const auto next = state_.date.day == std::numeric_limits<std::int64_t>::max() ? state_.date.day
                                                                                      : state_.date.day + 1;
        const auto known = siteDutyPreview(state_, *at, next);
        if (!known.canWork &&
            (known.cause == SiteWorkCause::ReactorFuel || known.cause == SiteWorkCause::Composites ||
             known.cause == SiteWorkCause::DutyAllowance)) {
            const auto cause = known.cause == SiteWorkCause::DutyAllowance
                                   ? SiteOperatingIssueCause::DutyAllowance
                                   : SiteOperatingIssueCause::SupplyLost;
            at->issue = {cause, at->dutyReceipts.back().day, known.explanation, true};
            appendEvent(EventSeverity::Info,
                        SiteOperatingAuditEvent{at->id, SiteOperatingAuditKind::IssueAcknowledged,
                                                at->operatingRevision, cause, at->issue.episodeStartedDay, 0,
                                                "Explicitly authorized operating limit is a known wait"});
        }
    }
    return CommandResult::success("Site operating authority amended");
}
CommandResult Simulation::setSiteOperationEnabled(SiteId id, bool enabled) {
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Site audit identity limit reached");
    auto* at = site(state_, id);
    if (!at) {
        const std::string why = "Site does not exist";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    auto policy = at->operatingPolicy;
    policy.enabled = enabled;
    return amendSiteOperatingPolicy({id, policy});
}
CommandResult Simulation::acknowledgeSiteOperatingIssue(const AcknowledgeSiteOperatingIssueCommand& command) {
    if (state_.ids.nextEventId == std::numeric_limits<std::int64_t>::max())
        return CommandResult::failure("Site audit identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size() + 1);
    auto* at = site(state_, command.siteId);
    if (!at || at->issue.acknowledged || at->issue.cause == SiteOperatingIssueCause::None ||
        at->issue.cause != command.cause || at->issue.episodeStartedDay != command.episodeStartedDay) {
        const std::string why = "Site operating issue identity is not pending";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{why});
        return CommandResult::failure(why);
    }
    at->issue.acknowledged = true;
    appendEvent(EventSeverity::Info,
                SiteOperatingAuditEvent{at->id, SiteOperatingAuditKind::IssueAcknowledged,
                                        at->operatingRevision, at->issue.cause, at->issue.episodeStartedDay,
                                        0, "Operating issue acknowledged; operation and evidence unchanged"});
    return CommandResult::success("Site operating issue acknowledged");
}
} // namespace deep
