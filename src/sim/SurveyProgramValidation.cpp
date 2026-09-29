#include "sim/SurveyProgramValidation.h"

#include "sim/SurveyProgramRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace deep {
namespace {

void check(const bool okay, const char* reason) {
    if (!okay) throw std::runtime_error{reason};
}

template <typename T, typename IdT>
[[nodiscard]] const T* find(const std::vector<T>& rows, const IdT id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [id](const T& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

[[nodiscard]] bool finiteNonnegative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

void validateSurveyProgramState(const GameState& state) {
    for (const SurveyTeam& team : state.surveyTeams) {
        check(!team.name.empty(), "survey team name must be non-empty");
        check(team.locationKind >= SurveyTeamLocationKind::Colony &&
              team.locationKind <= SurveyTeamLocationKind::Fleet,
              "survey team location kind must be valid");
        if (team.locationKind == SurveyTeamLocationKind::Colony) {
            check(team.colonyId.has_value() && !team.fleetId.has_value() &&
                  find(state.colonies, *team.colonyId) != nullptr,
                  "survey team must occupy exactly one existing colony");
        } else {
            check(team.fleetId.has_value() && !team.colonyId.has_value() &&
                  find(state.fleets, *team.fleetId) != nullptr,
                  "embarked survey team must occupy exactly one existing fleet");
        }
    }

    std::unordered_set<std::int64_t> leasedFleetIds;
    std::unordered_set<std::int64_t> leasedTeamIds;
    for (const SurveyProgram& program : state.surveyPrograms) {
        check(!validateSurveyProgramCharter(state, program.charter).has_value(),
              "survey program charter is malformed");
        if (program.pendingHomeColonyId) {
            check(find(state.colonies, *program.pendingHomeColonyId) != nullptr &&
                  *program.pendingHomeColonyId != program.charter.homeColonyId &&
                  program.lifecycle != SurveyProgramLifecycle::Closed,
                  "pending survey home must be a distinct existing colony on an open program");
        }
        check(program.createdDay >= 0 && program.createdDay <= state.date.day && program.charterRevision > 0,
              "survey program date or charter revision is invalid");
        check(program.lifecycle >= SurveyProgramLifecycle::Authorized &&
              program.lifecycle <= SurveyProgramLifecycle::Closed &&
              program.closure >= SurveyProgramClosure::None &&
              program.closure <= SurveyProgramClosure::Cancelled,
              "survey program lifecycle is invalid");
        if (program.lifecycle == SurveyProgramLifecycle::Closed) {
            check(program.closure != SurveyProgramClosure::None &&
                  !program.leasedFleetId.has_value() && !program.leasedTeamId.has_value(),
                  "closed survey program cannot retain an execution lease");
            if (program.closure == SurveyProgramClosure::Completed) {
                check(surveyCharterFinished(program) && program.task == SurveyProgramTask::None,
                      "completed survey program must finish visits and clear its return task");
            }
        } else if (program.lifecycle == SurveyProgramLifecycle::Closing) {
            check(program.closure != SurveyProgramClosure::None,
                  "closing survey program needs an outcome");
            if (program.closure == SurveyProgramClosure::Completed) {
                check(surveyCharterFinished(program), "completion return needs all charter visits");
            }
        } else {
            check(program.closure == SurveyProgramClosure::None,
                  "active or suspended survey program cannot have a closure outcome");
        }

        check(program.leasedFleetId.has_value() == program.leasedTeamId.has_value(),
              "survey program fleet/team lease must be acquired together");
        if (program.leasedFleetId.has_value()) {
            const SurveyTeam* team = find(state.surveyTeams, *program.leasedTeamId);
            check(find(state.fleets, *program.leasedFleetId) != nullptr && team != nullptr &&
                  team->locationKind == SurveyTeamLocationKind::Fleet &&
                  team->fleetId == program.leasedFleetId,
                  "survey program lease requires a co-located embarked team");
            check(leasedFleetIds.insert(program.leasedFleetId->value).second &&
                  leasedTeamIds.insert(program.leasedTeamId->value).second,
                  "fleet or survey team has multiple active program leases");
        }

        check(program.task >= SurveyProgramTask::None && program.task <= SurveyProgramTask::Return &&
              program.workDaysCompleted >= 0 && program.workDaysCompleted <= kP3ASurveyWorkDaysPerPass &&
              program.taskPassNumber >= 0,
              "survey program task or partial work is invalid");
        if (program.task == SurveyProgramTask::None) {
            check(!program.maintenanceReturn,"Idle survey cannot retain a maintenance detour");
            check(!program.taskBodyId.has_value() && !program.taskFleetId.has_value() &&
                  !program.taskTeamId.has_value() && !program.taskLeaderId.has_value() &&
                  program.taskPassNumber == 0 && program.workDaysCompleted == 0 &&
                  program.firstWorkDay == 0,
                  "idle survey program cannot retain active task state");
        } else {
            if(program.maintenanceReturn) check(program.task==SurveyProgramTask::Survey || program.task==SurveyProgramTask::Outbound,
                                               "Maintenance detour must preserve an incomplete survey visit");
            check(program.taskBodyId.has_value() && find(state.bodies, *program.taskBodyId) != nullptr &&
                  program.taskFleetId.has_value() && find(state.fleets, *program.taskFleetId) != nullptr &&
                  program.taskTeamId.has_value() && find(state.surveyTeams, *program.taskTeamId) != nullptr &&
                  program.taskLeaderId.has_value() && find(state.people, *program.taskLeaderId) != nullptr &&
                  (program.taskPassNumber > 0 ||
                   (program.task == SurveyProgramTask::Return && program.taskPassNumber == 0)),
                  "active survey task needs valid body and physical asset identities");
            check(program.taskApproach >= SurveyPlanningApproach::CoverageFirst &&
                  program.taskApproach <= SurveyPlanningApproach::PriorityFirst,
                  "survey task approach is invalid");
            if (program.leasedFleetId) {
                check(program.leasedFleetId == program.taskFleetId &&
                      program.leasedTeamId == program.taskTeamId,
                      "active survey task cannot change leased assets");
            }
            check(program.firstWorkDay >= 0 && program.firstWorkDay <= state.date.day,
                  "survey task first work day is invalid");
            check((program.workDaysCompleted == 0 && program.firstWorkDay == 0) ||
                  (program.workDaysCompleted > 0 && program.firstWorkDay > 0),
                  "partial survey work needs its real first work day");
            if (program.task == SurveyProgramTask::Survey || program.task == SurveyProgramTask::Outbound) {
                check(program.workDaysCompleted < kP3ASurveyWorkDaysPerPass &&
                      program.taskPassNumber == completedSurveyPasses(program, *program.taskBodyId) + 1,
                      "active survey pass must follow completed receipts");
            } else if (program.taskPassNumber > 0) {
                check(program.workDaysCompleted == 0 &&
                      program.taskPassNumber == completedSurveyPasses(program, *program.taskBodyId),
                      "return task must follow its completed visit");
            }
        }

        check(finiteNonnegative(program.fuelLoaded) && finiteNonnegative(program.fuelBurned) &&
              finiteNonnegative(program.reportedFuelLoaded) && finiteNonnegative(program.reportedFuelBurned) &&
              program.totalWorkDays >= 0 && program.reportedWorkDays >= 0 &&
              program.reportedVisits >= 0,
              "survey program fuel or work accounting is invalid");
        check(program.reportedFuelLoaded <= program.fuelLoaded + 1.0e-8 &&
              program.reportedFuelBurned <= program.fuelBurned + 1.0e-8 &&
              program.reportedWorkDays <= program.totalWorkDays &&
              static_cast<std::size_t>(program.reportedVisits) <= program.receipts.size(),
              "survey report cursor exceeds actual program work");
        check(program.nextReportDay == nextGlobalSurveyBoundary(state.date.day, 30) &&
              program.reportStartDay >= program.createdDay && program.reportStartDay < program.nextReportDay,
              "survey program report boundary is invalid");
        if (program.issue.signature.empty()) {
            check(program.issue.message.empty() && program.issue.acknowledged,
                  "empty survey issue must be acknowledged without message");
        } else {
            check(!program.issue.message.empty(), "survey issue needs a known cause");
        }

        check(program.receipts.size() <=
                  static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() /
                                           kP3ASurveyWorkDaysPerPass) &&
              program.totalWorkDays ==
                  static_cast<std::int64_t>(program.receipts.size()) * kP3ASurveyWorkDaysPerPass +
                      program.workDaysCompleted,
              "survey work total must equal completed receipts plus current partial work");
        std::unordered_map<std::int64_t, int> completedByBody;
        std::int64_t previousReceiptDay = 0;
        for (const SurveyVisitReceipt& receipt : program.receipts) {
            check(find(state.bodies, receipt.bodyId) != nullptr &&
                  find(state.fleets, receipt.fleetId) != nullptr &&
                  find(state.surveyTeams, receipt.teamId) != nullptr &&
                  (!receipt.leaderId || find(state.people, *receipt.leaderId) != nullptr),
                  "survey receipt references missing body or participant");
            check(receipt.passNumber == ++completedByBody[receipt.bodyId.value] &&
                  receipt.workDays == kP3ASurveyWorkDaysPerPass &&
                  receipt.firstWorkDay > previousReceiptDay &&
                  receipt.completedDay >= receipt.firstWorkDay &&
                  receipt.completedDay - receipt.firstWorkDay + 1 >= kP3ASurveyWorkDaysPerPass &&
                  receipt.completedDay >= previousReceiptDay && receipt.completedDay <= state.date.day,
                  "survey receipt pass identity or work interval is invalid");
            previousReceiptDay = receipt.completedDay;
            check(receipt.approach >= SurveyPlanningApproach::CoverageFirst &&
                  receipt.approach <= SurveyPlanningApproach::PriorityFirst && bool(receipt.observationBatchId),
                  "survey receipt needs valid approach and acquired batch");
        }
        if (program.workDaysCompleted > 0) {
            check(program.firstWorkDay > previousReceiptDay,
                  "partial visit cannot overlap a completed receipt");
        }

        const std::int64_t dueReports = state.date.day / 30 - program.createdDay / 30;
        check(dueReports >= 0 && static_cast<std::size_t>(dueReports) == program.reports.size(),
              "survey program has missing or duplicate due reports");
        std::int64_t expectedReportStart = program.createdDay;
        std::int64_t expectedReportEnd = nextGlobalSurveyBoundary(program.createdDay, 30);
        int reportedVisitsSum = 0;
        std::int64_t reportedWorkSum = 0;
        double reportedFuelLoadedSum = 0.0;
        double reportedFuelBurnedSum = 0.0;
        for (const SurveyProgramReport& report : program.reports) {
            check(report.startDay == expectedReportStart && report.endDay == expectedReportEnd &&
                  report.startDay <= report.endDay && report.endDay <= state.date.day &&
                  report.isNinetyDayReview == (report.endDay % 90 == 0) &&
                  report.charterRevision > 0 && report.charterRevision <= program.charterRevision,
                  "survey report interval or charter snapshot is invalid");
            expectedReportStart = report.endDay + 1;
            if (expectedReportEnd < state.date.day) {
                expectedReportEnd = nextGlobalSurveyBoundary(expectedReportEnd, 30);
            }
            check((!report.leaderId || find(state.people, *report.leaderId) != nullptr) &&
                  (!report.fleetId || find(state.fleets, *report.fleetId) != nullptr) &&
                  (!report.teamId || find(state.surveyTeams, *report.teamId) != nullptr) &&
                  (!report.fleetBodyId || find(state.bodies, *report.fleetBodyId) != nullptr) &&
                  report.approach >= SurveyPlanningApproach::CoverageFirst &&
                  report.approach <= SurveyPlanningApproach::PriorityFirst &&
                  report.visitsCompleted >= 0 && report.workDays >= 0 &&
                  finiteNonnegative(report.fuelLoaded) && finiteNonnegative(report.fuelBurned),
                  "survey report references or accounting are invalid");
            reportedVisitsSum += report.visitsCompleted;
            reportedWorkSum += report.workDays;
            reportedFuelLoadedSum += report.fuelLoaded;
            reportedFuelBurnedSum += report.fuelBurned;
        }
        check(program.reportStartDay == expectedReportStart &&
              program.reportedVisits == reportedVisitsSum &&
              program.reportedWorkDays == reportedWorkSum &&
              std::abs(program.reportedFuelLoaded - reportedFuelLoadedSum) <=
                  1.0e-8 * std::max(1.0, program.reportedFuelLoaded) &&
              std::abs(program.reportedFuelBurned - reportedFuelBurnedSum) <=
                  1.0e-8 * std::max(1.0, program.reportedFuelBurned),
              "survey report cursor must equal its completed report rows");
        const int receiptsThroughReport = static_cast<int>(std::count_if(
            program.receipts.begin(), program.receipts.end(),
            [&program](const SurveyVisitReceipt& receipt) {
                return receipt.completedDay < program.reportStartDay;
            }));
        check(program.reportedVisits == receiptsThroughReport,
              "survey report visit count must match dated receipts");
    }
}

} // namespace deep
