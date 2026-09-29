// Owned science DTOs. Raw acquired data and completed assessments are separate;
// preview never samples, consumes labor, mutates leases or publishes conclusions.
#include "app/SimulationQueries.h"
#include "sim/AnalysisProgramRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace deep {
std::vector<ObservationBatch> SimulationQueries::acquiredObservations() const {
    return service_.state().observations;
}
std::vector<MeasurementProfile> SimulationQueries::measurementProfiles() const {
    return service_.state().measurementProfiles;
}
EvidenceDossier SimulationQueries::evidenceDossier(BodyId body) const {
    EvidenceDossier d;
    d.bodyId = body;
    for (const auto& b : service_.state().observations)
        if (b.bodyId == body)
            d.observations.push_back(b);
    for (const auto& f : service_.state().analysisFindings)
        if (f.bodyId == body)
            d.findings.push_back(f);
    for (const auto& a : service_.state().assessments)
        if (a.bodyId == body)
            d.assessments.push_back(a);
    return d;
}
AnalysisDraftPreview SimulationQueries::analysisDraftPreview(const AnalysisCharter& c) const {
    if (auto error = validateAnalysisCharter(service_.state(), c))
        return {false, *error, {}};
    AnalysisProgram p;
    p.charter = c;
    return {true, {}, analysisExecutionCondition(service_.state(), p)};
}
std::vector<AnalysisProgramSummary> SimulationQueries::analysisPrograms() const {
    const auto& s = service_.state();
    std::vector<AnalysisProgramSummary> rows;
    for (const auto& p : s.analysisPrograms) {
        AnalysisProgramSummary r;
        r.program = p;
        r.condition = analysisExecutionCondition(s, p);
        r.sourceStatus = analysisSourceStatus(s, p);
        r.requestedTeam = "Unassigned";
        r.actualTeam = "None";
        r.teamLocation = "Unassigned";
        r.teamOwner = "Unleased";
        for (const auto& c : s.colonies)
            if (c.id == p.charter.colonyId) {
                r.laboratory = c.name;
                r.laboratoryCapacity = c.analysisCapacity;
            }
        for (const auto& t : surveyTeams()) {
            if (p.charter.requestedTeamId == t.id) {
                r.requestedTeam = t.name;
                r.teamLocation = t.locationName;
                r.teamOwner = t.controllingProgramLabel;
            }
            if (p.leasedTeamId == t.id)
                r.actualTeam = t.name;
        }
        r.workPerformed = analysisWork(p);
        if (p.charter.workAllowance)
            r.remainingAllowance = std::max(0.0, *p.charter.workAllowance - r.workPerformed);
        if (const auto* job = activeAnalysisJob(p)) {
            r.activeWorkRemaining = std::max(0.0, job->requiredWork - analysisWork(p, job->id));
            const bool contested =
                std::any_of(s.analysisPrograms.begin(), s.analysisPrograms.end(), [&](const auto& other) {
                    return other.id != p.id && other.charter.colonyId == p.charter.colonyId &&
                           analysisExecutionCondition(s, other) == "Ready for finite laboratory work";
                });
            if (!contested && r.condition == "Ready for finite laboratory work" && r.laboratoryCapacity > 0 &&
                r.activeWorkRemaining / std::min(1.0, r.laboratoryCapacity) <=
                    static_cast<double>(std::numeric_limits<int>::max()) &&
                (!r.remainingAllowance || *r.remainingAllowance >= r.activeWorkRemaining))
                r.currentJobEta =
                    static_cast<int>(std::ceil(r.activeWorkRemaining / std::min(1.0, r.laboratoryCapacity)));
        }
        rows.push_back(std::move(r));
    }
    return rows;
}
} // namespace deep
