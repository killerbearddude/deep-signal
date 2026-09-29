// Current-snapshot science integrity. Recomputed interpretation is safe here:
// it uses immutable recorded outputs, never current physical mineral deposits.
#include "sim/ScienceValidation.h"
#include "sim/AnalysisProgramRules.h"
#include "sim/ObservationRules.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
namespace deep {
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class T, class ID> const T* find(const std::vector<T>& rows, ID id) {
    const auto i = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.id == id; });
    return i == rows.end() ? nullptr : &*i;
}
template <class T> void identities(const std::vector<T>& rows, std::int64_t counter) {
    check(counter > 0, "Invalid science counter");
    std::set<std::int64_t> ids;
    for (const auto& row : rows)
        check(row.id.value > 0 && row.id.value < counter && ids.insert(row.id.value).second,
              "Invalid or duplicated science identity");
}
void exposureReferences(const GameState& s, const InstrumentExposure& e) {
    const auto* ship = find(s.ships, e.shipId);
    const auto* cls = find(s.shipClasses, e.classId);
    const auto* component = find(s.shipComponents, e.componentId);
    check(ship && cls && component && ship->shipClassId == e.classId &&
              component->measurementProfileId == e.profileId,
          "Instrument provenance references incompatible hull or profile");
    check(std::any_of(cls->components.begin(), cls->components.end(),
                      [&](const auto& i) { return i.componentId == e.componentId; }),
          "Observation instrument not installed on exact class");
}
} // namespace
void validateScienceState(const GameState& s) {
    identities(s.measurementProfiles, s.ids.nextMeasurementProfileId);
    identities(s.observations, s.ids.nextObservationBatchId);
    identities(s.analysisPrograms, s.ids.nextAnalysisProgramId);
    identities(s.assessments, s.ids.nextAssessmentId);
    check(s.ids.nextAnalysisJobId > 0, "Invalid analysis job counter");
    for (const auto& p : s.measurementProfiles)
        validateMeasurementProfile(p);
    for (const auto& c : s.shipComponents) {
        check((c.surveyCapability > 0) == c.measurementProfileId.has_value(),
              "Survey capability must have explicit measurement profile");
        if (c.measurementProfileId)
            check(find(s.measurementProfiles, *c.measurementProfileId), "Unknown measurement profile");
    }
    for (const auto& c : s.colonies)
        check(std::isfinite(c.analysisCapacity) && c.analysisCapacity >= 0, "Invalid laboratory capacity");
    std::set<std::pair<std::int64_t, std::int64_t>> fieldTeamDays;
    std::set<std::int64_t> linkedBatches;
    std::int64_t previousAcquisition = -1;
    std::int64_t previousBatchId = 0;
    for (const auto& b : s.observations) {
        validateObservationBatch(b);
        check(b.acquiredDay <= s.date.day && b.acquiredDay >= previousAcquisition,
              "Invalid acquisition ordering");
        check(b.id.value > previousBatchId, "Observation identities are not in allocation order");
        previousBatchId = b.id.value;
        previousAcquisition = b.acquiredDay;
        const auto* fleet = find(s.fleets, b.fleetId);
        check(find(s.bodies, b.bodyId) && fleet, "Missing acquisition body or fleet");
        for (const auto& i : b.instruments) {
            exposureReferences(s, i.exposure);
            const auto* p = find(s.measurementProfiles, i.profile.id);
            check(p && *p == i.profile, "Observation method snapshot differs from immutable catalog");
            check(std::find(fleet->shipIds.begin(), fleet->shipIds.end(), i.exposure.shipId) !=
                      fleet->shipIds.end(),
                  "Observation hull not in source fleet");
        }
        if (b.teamId) {
            check(find(s.surveyTeams, *b.teamId), "Missing fieldwork scientist");
            for (auto day : b.workDates)
                check(fieldTeamDays.emplace(b.teamId->value, day).second,
                      "Scientific team fieldwork double booked");
        }
        if (b.surveyProgramId) {
            const auto* p = find(s.surveyPrograms, *b.surveyProgramId);
            check(p, "Observation source survey missing");
            check(std::count_if(p->receipts.begin(), p->receipts.end(),
                                [&](const auto& r) {
                                    return r.observationBatchId == b.id && r.bodyId == b.bodyId &&
                                           r.fleetId == b.fleetId && b.teamId == r.teamId &&
                                           r.passNumber == b.passNumber && r.firstWorkDay == b.firstWorkDay &&
                                           r.completedDay == b.acquiredDay;
                                }) == 1,
                  "Observation does not match its single completed pass");
        }
        check(std::count_if(s.eventLog.begin(), s.eventLog.end(),
                            [&](const auto& event) {
                                const auto* result =
                                    std::get_if<ResourceSurveyCompletedEvent>(&event.payload);
                                return result && result->observationBatchId == b.id &&
                                       result->bodyId == b.bodyId && result->fleetId == b.fleetId &&
                                       event.day == b.acquiredDay;
                            }) == 1,
              "Observation needs exactly one publication audit");
    }
    for (const auto& p : s.surveyPrograms) {
        for (const auto& r : p.receipts) {
            check(find(s.observations, r.observationBatchId) &&
                      linkedBatches.insert(r.observationBatchId.value).second,
                  "Completed pass lacks unique acquired batch");
        }
        check(p.workDates.size() == static_cast<std::size_t>(p.workDaysCompleted),
              "Partial field dates differ from completed work");
        check((p.workDaysCompleted == 0) == p.exposures.empty(),
              "Partial contributor provenance missing or extra");
        if (p.workDaysCompleted > 0) {
            check(p.taskTeamId && p.workDates.front() == p.firstWorkDay && p.workDates.back() <= s.date.day &&
                      std::adjacent_find(p.workDates.begin(), p.workDates.end(), std::greater_equal<>()) ==
                          p.workDates.end(),
                  "Invalid partial field dates");
            for (auto day : p.workDates)
                check(fieldTeamDays.emplace(p.taskTeamId->value, day).second,
                      "Partial scientist fieldwork double booked");
            std::set<std::pair<std::int64_t, std::int64_t>> groups;
            for (const auto& e : p.exposures) {
                exposureReferences(s, e);
                check(e.workdays > 0 && e.workdays <= p.workDaysCompleted &&
                          e.dates.size() == static_cast<std::size_t>(e.workdays) &&
                          groups.emplace(e.shipId.value, e.componentId.value).second,
                      "Invalid partial contributor work");
                check(std::adjacent_find(e.dates.begin(), e.dates.end(), std::greater_equal<>()) ==
                          e.dates.end(),
                      "Invalid partial exposure ordering");
                for (auto day : e.dates)
                    check(std::find(p.workDates.begin(), p.workDates.end(), day) != p.workDates.end(),
                          "Exposure outside partial work");
            }
        }
    }
    std::set<std::int64_t> owners, jobIds;
    for (const auto& p : s.surveyPrograms)
        if (p.leasedTeamId)
            owners.insert(p.leasedTeamId->value);
    std::map<std::pair<std::int64_t, std::int64_t>, double> teamWork, labWork, labCap;
    std::vector<AnalysisFinding> expectedFindings;
    for (const auto& p : s.analysisPrograms) {
        check(!validateAnalysisCharter(s, p.charter), "Invalid analysis charter");
        check(p.createdDay >= 0 && p.createdDay <= s.date.day && p.charterRevision > 0 &&
                  p.lifecycle >= AnalysisLifecycle::Authorized && p.lifecycle <= AnalysisLifecycle::Closed &&
                  p.closure >= AnalysisClosure::None && p.closure <= AnalysisClosure::Cancelled,
              "Invalid analysis lifecycle or date");
        check((p.lifecycle == AnalysisLifecycle::Closed) == p.closedDay.has_value() &&
                  (p.lifecycle == AnalysisLifecycle::Closed) == (p.closure != AnalysisClosure::None),
              "Invalid analysis closure");
        if (p.closedDay)
            check(*p.closedDay >= p.createdDay && *p.closedDay <= s.date.day, "Invalid analysis close date");
        if (p.leasedTeamId) {
            const auto* t = find(s.surveyTeams, *p.leasedTeamId);
            check(p.lifecycle == AnalysisLifecycle::Authorized && activeAnalysisJob(p) && t &&
                      p.charter.requestedTeamId == p.leasedTeamId && p.charter.requestedLeaderId &&
                      t->locationKind == SurveyTeamLocationKind::Colony &&
                      t->colonyId == p.charter.colonyId && owners.insert(p.leasedTeamId->value).second,
                  "Analysis lease duplicates or relocates scientific team");
        }
        check(p.issue.signature.empty()
                  ? p.issue.message.empty() && p.issue.acknowledged
                  : p.issue.signature == "analysis-work-allowance" && !p.issue.message.empty(),
              "Invalid analysis issue identity");
        std::set<std::int64_t> inputs;
        const auto source = analysisSourceBatches(s, p);
        if (p.closure == AnalysisClosure::Completed)
            check(analysisSourceEnded(s, p) && p.jobs.size() == source.size() &&
                      std::all_of(p.jobs.begin(), p.jobs.end(),
                                  [](const auto& j) { return j.outcome == AnalysisJobOutcome::Completed; }),
                  "Analysis completed before source end or backlog completion");
        if (p.lifecycle == AnalysisLifecycle::Closed)
            check(!activeAnalysisJob(p), "Closed analysis retains active job");
        std::int64_t lastWorkDay = p.createdDay;
        for (const auto& r : p.receipts) {
            check(r.day > lastWorkDay && r.day <= s.date.day && (!p.closedDay || r.day <= *p.closedDay) &&
                      r.colonyId == p.charter.colonyId && find(s.surveyTeams, r.teamId) &&
                      find(s.people, r.leaderId) && r.charterRevision > 0 &&
                      r.charterRevision <= p.charterRevision && std::isfinite(r.work) && r.work > 0 &&
                      r.work <= 1 && std::isfinite(r.labCapacity) && r.labCapacity > 0,
                  "Invalid analysis labor receipt or multiple jobs in one opening");
            lastWorkDay = r.day;
            check(find(p.jobs, r.jobId), "Analysis work references absent job");
            check(!fieldTeamDays.contains({r.teamId.value, r.day}),
                  "Scientific team field/lab work overlaps");
            const auto tk = std::pair{r.teamId.value, r.day}, lk = std::pair{r.colonyId.value, r.day};
            teamWork[tk] += r.work;
            labWork[lk] += r.work;
            if (labCap.contains(lk))
                check(labCap[lk] == r.labCapacity, "Inconsistent same-opening laboratory capacity");
            labCap[lk] = r.labCapacity;
        }
        for (std::size_t i = 0; i < p.jobs.size(); ++i) {
            const auto& j = p.jobs[i];
            const auto* b = find(s.observations, j.batchId);
            check(j.id.value > 0 && j.id.value < s.ids.nextAnalysisJobId &&
                      jobIds.insert(j.id.value).second && b && inputs.insert(j.batchId.value).second &&
                      i < source.size() && source[i] == j.batchId && j.startedDay >= b->availableDay &&
                      j.startedDay > p.createdDay && j.startedDay <= s.date.day &&
                      j.requiredWork == kAnalysisJobWorkdays,
                  "Invalid analysis job identity/input/order/demand");
            check(j.outcome >= AnalysisJobOutcome::Active && j.outcome <= AnalysisJobOutcome::Cancelled &&
                      (j.outcome == AnalysisJobOutcome::Active) == !j.endedDay &&
                      (j.outcome != AnalysisJobOutcome::Active || i + 1 == p.jobs.size()),
                  "Invalid analysis job outcome");
            if (j.outcome == AnalysisJobOutcome::Cancelled)
                check(p.closure == AnalysisClosure::Cancelled, "Cancelled job without program cancellation");
            const double total = analysisWork(p, j.id);
            check(std::isfinite(total) && total > 0 && total <= j.requiredWork &&
                      (j.outcome == AnalysisJobOutcome::Completed) == (total == j.requiredWork),
                  "Analysis work does not reconcile to completion");
            std::int64_t first = -1, last = -1;
            for (const auto& r : p.receipts)
                if (r.jobId == j.id) {
                    if (first < 0)
                        first = r.day;
                    last = r.day;
                    check(r.day >= j.startedDay && (!j.endedDay || r.day <= *j.endedDay),
                          "Labor outside job dates");
                }
            check(first == j.startedDay &&
                      (!j.endedDay || (*j.endedDay >= last && *j.endedDay <= s.date.day)),
                  "Job dates differ from work");
            if (j.outcome == AnalysisJobOutcome::Completed) {
                check(j.endedDay == last, "Completed job date differs from last actual work");
                expectedFindings.push_back(interpretObservationBatch(*b, j.id, *j.endedDay));
            }
        }
        const auto through = p.closedDay.value_or(s.date.day);
        check(p.reports.size() == static_cast<std::size_t>(through / 30 - p.createdDay / 30),
              "Missing or extra analysis reports");
        std::int64_t begin = p.createdDay;
        for (const auto& r : p.reports) {
            check(r.startDay == begin && r.endDay == nextGlobalSurveyBoundary(begin, 30) &&
                      r.isNinetyDayReview == (r.endDay % 90 == 0) && r.charterRevision > 0 &&
                      r.charterRevision <= p.charterRevision && r.auditThroughId >= 0 &&
                      r.auditThroughId < s.ids.nextEventId && !r.limitations.empty(),
                  "Invalid analysis report boundary");
            double period = 0, lifetime = 0;
            int completed = 0;
            for (const auto& work : p.receipts)
                if (work.day <= r.endDay) {
                    lifetime += work.work;
                    if (work.day > r.startDay)
                        period += work.work;
                }
            for (const auto& j : p.jobs)
                if (j.outcome == AnalysisJobOutcome::Completed && j.endedDay && *j.endedDay > r.startDay &&
                    *j.endedDay <= r.endDay)
                    ++completed;
            check(equipmentNearlyEqual(r.workPerformed, period) &&
                      equipmentNearlyEqual(r.lifetimeWork, lifetime) && r.jobsCompleted == completed &&
                      r.inputBacklog >= 0 &&
                      (!r.workAllowance || (std::isfinite(*r.workAllowance) && *r.workAllowance >= 0)),
                  "Analysis report work/policy differs from history");
            begin = r.endDay;
        }
        check(p.reportStartDay == begin && p.nextReportDay == nextGlobalSurveyBoundary(begin, 30),
              "Analysis report cursor invalid");
    }
    for (const auto& [key, work] : teamWork)
        check(work <= 1, "Scientific team/day exceeds one workday");
    for (const auto& [key, work] : labWork)
        check(work <= labCap[key] || equipmentNearlyEqual(work, labCap[key]),
              "Laboratory opening capacity exceeded");
    check(s.analysisFindings.size() == expectedFindings.size(), "Missing or extra completed findings");
    for (const auto& expected : expectedFindings)
        check(std::count(s.analysisFindings.begin(), s.analysisFindings.end(), expected) == 1,
              "Completed job requires exactly one finding");
    std::vector<AnalysisFinding> prior;
    std::map<std::int64_t, const AssessmentRevision*> latest;
    check(s.assessments.size() == s.analysisFindings.size(), "Completed findings and revisions differ");
    for (std::size_t i = 0; i < s.analysisFindings.size(); ++i) {
        const auto& f = s.analysisFindings[i];
        check(std::count(expectedFindings.begin(), expectedFindings.end(), f) == 1,
              "Finding differs from its completed job/record");
        check(i == 0 || s.analysisFindings[i - 1].publishedDay <= f.publishedDay,
              "Finding publication order invalid");
        prior.push_back(f);
        const auto& a = s.assessments[i];
        check(i == 0 || s.assessments[i - 1].id.value < a.id.value,
              "Assessment identities are not in allocation order");
        const auto expected = assembleAssessmentRevision(prior, f.bodyId, a.id, f.jobId, f.publishedDay,
                                                         latest[f.bodyId.value]);
        check(a == expected, "Assessment differs from its recorded completed inputs");
        latest[f.bodyId.value] = &a;
    }
}
} // namespace deep
