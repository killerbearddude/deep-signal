// Owned condition/support projections. Compatibility, duty and repair amounts
// come from simulation rules; rendering has no physical mutation authority.
#include "app/SimulationQueries.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/MaintenanceProgramRules.h"
#include <algorithm>
#include <limits>

namespace deep {
namespace {
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
template <class T, class Id> std::string name(const std::vector<T>& rows, Id id) {
    const auto* value = find(rows, id);
    return value ? value->name : "Unavailable";
}
} // namespace
std::vector<EquipmentFamily> SimulationQueries::equipmentFamilies() const {
    return service_.state().equipmentFamilies;
}
std::vector<EquipmentConditionSummary> SimulationQueries::equipmentConditions(FleetId fleetId) const {
    const auto& state = service_.state();
    std::vector<EquipmentConditionSummary> result;
    const auto* fleet = find(state.fleets, fleetId);
    if (!fleet)
        return result;
    const auto workday = prepareSurveyDuty(state, *fleet, 1.0);
    const auto manual = prepareSurveyDuty(state, *fleet, 5.0);
    ProcessedMaterialSet unbounded;
    unbounded.amount.fill(std::numeric_limits<double>::max());
    for (auto id : fleet->shipIds) {
        const auto* ship = find(state.ships, id);
        const auto* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (!cls)
            continue;
        const auto design = evaluateShipDesign(state.shipComponents, cls->components);
        for (const auto& install : cls->components) {
            const auto* component = find(state.shipComponents, install.componentId);
            if (!component || component->surveyCapability <= 0.0)
                continue;
            EquipmentConditionSummary row;
            row.shipId = ship->id;
            row.shipName = ship->name;
            row.componentId = component->id;
            row.componentName = component->name;
            row.quantity = install.quantity;
            row.nominalCapability = component->surveyCapability * install.quantity;
            row.poweredCapability = design.powerMargin >= 0.0 ? row.nominalCapability : 0.0;
            row.managed = component->serviceProfile.has_value();
            const auto contributes = [&](const SurveyDutyEvaluation& evaluation) {
                return row.poweredCapability > 0.0 &&
                       (!row.managed || std::any_of(evaluation.changes.begin(), evaluation.changes.end(),
                                                    [&](const auto& change) {
                                                        return change.shipId == ship->id &&
                                                               change.componentId == component->id;
                                                    }));
            };
            row.workdayCapability = contributes(workday) ? row.nominalCapability : 0.0;
            row.manualPassCapability = contributes(manual) ? row.nominalCapability : 0.0;
            if (component->serviceProfile) {
                const auto& profile = *component->serviceProfile;
                row.familyName = name(state.equipmentFamilies, profile.familyId);
                row.dutyCapacity = profile.dutyCapacity;
                for (const auto& condition : ship->equipmentCondition)
                    if (condition.componentId == component->id)
                        row.usedDuty = condition.usedDuty;
                row.remainingDuty = row.dutyCapacity - row.usedDuty;
                row.fullServiceNeed =
                    planEquipmentService(state, ship->id, component->id, std::numeric_limits<double>::max(),
                                         std::numeric_limits<double>::max(), unbounded);
            }
            result.push_back(std::move(row));
        }
    }
    return result;
}
std::vector<MaintenanceTeamSummary> SimulationQueries::maintenanceTeams() const {
    const auto& state = service_.state();
    std::vector<MaintenanceTeamSummary> result;
    for (const auto& team : state.maintenanceTeams) {
        MaintenanceTeamSummary row;
        row.team = team;
        row.locationName = team.colonyId ? name(state.colonies, *team.colonyId)
                                         : (team.fleetId ? name(state.fleets, *team.fleetId) : "Unavailable");
        for (auto id : team.qualifiedFamilies)
            row.qualifiedFamilyNames.push_back(name(state.equipmentFamilies, id));
        for (const auto& p : state.maintenancePrograms)
            if (p.leasedTeamId == team.id)
                row.controllingProgramId = p.id;
        result.push_back(std::move(row));
    }
    return result;
}
std::vector<MaintenanceProgramSummary> SimulationQueries::maintenancePrograms() const {
    const auto& state = service_.state();
    std::vector<MaintenanceProgramSummary> result;
    for (const auto& p : state.maintenancePrograms) {
        MaintenanceProgramSummary row;
        row.program = p;
        row.colonyName = name(state.colonies, p.charter.serviceColonyId);
        const auto tenderId = p.leasedTenderId ? p.leasedTenderId : p.charter.requestedTenderId;
        const auto teamId = p.leasedTeamId ? p.leasedTeamId : p.charter.requestedTeamId;
        row.tenderName = tenderId ? name(state.fleets, *tenderId) : "Unassigned";
        row.teamName = teamId ? name(state.maintenanceTeams, *teamId) : "Unassigned";
        row.leaderName =
            p.charter.requestedLeaderId ? name(state.people, *p.charter.requestedLeaderId) : "Unassigned";
        row.condition = maintenanceExecutionCondition(state, p);
        for (auto clientId : p.charter.clients)
            for (const auto& survey : state.surveyPrograms) {
                const auto fleet =
                    survey.leasedFleetId ? survey.leasedFleetId : survey.charter.requestedFleetId;
                if (fleet != clientId || survey.charter.policy.maintenanceProgramId != p.id ||
                    !surveyServiceRequest(state, survey).requested)
                    continue;
                row.pendingClients.push_back({survey.id, clientId, survey.charter.name,
                                              surveySupportCondition(state, survey),
                                              eligibleServiceClient(state, survey)});
            }
        if (const auto* job = activeServiceJob(p)) {
            row.nextWork = planMaintenanceWork(state, p, *job);
            row.clientEquipment = equipmentConditions(job->clientFleetId);
        }
        const auto* tender = tenderId ? find(state.fleets, *tenderId) : nullptr;
        if (tender)
            for (const auto& family : state.equipmentFamilies) {
                double installed = 0.0, operational = 0.0;
                for (auto shipId : tender->shipIds) {
                    const auto* ship = find(state.ships, shipId);
                    const auto* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
                    if (!cls)
                        continue;
                    for (const auto& rate :
                         evaluateShipDesign(state.shipComponents, cls->components).workshopRates) {
                        if (rate.familyId == family.id)
                            installed += rate.teamWorkdaysPerDay;
                    }
                    // First eligible hull matches one-team work; no pooled repair.
                    if (operational == 0.0)
                        operational = operationalWorkshopRate(state, *ship, family.id);
                }
                row.installedWorkshops.push_back({family.id, installed});
                row.operationalWorkshops.push_back({family.id, operational});
            }
        result.push_back(std::move(row));
    }
    return result;
}
MaintenanceCharterPreview
SimulationQueries::previewMaintenanceCharter(const MaintenanceProgramCharter& charter,
                                             std::optional<MaintenanceProgramId> amendingId) const {
    const auto& state = service_.state();
    MaintenanceCharterPreview result;
    MaintenanceProgram draft;
    if (amendingId) {
        const auto* existing = find(state.maintenancePrograms, *amendingId);
        if (!existing || existing->lifecycle == MaintenanceProgramLifecycle::Closed ||
            existing->charter.serviceColonyId != charter.serviceColonyId) {
            result.validationMessage = "Amendment requires an open provider and its unchanged service colony";
            return result;
        }
        draft = *existing;
    }
    if (const auto error = validateMaintenanceCharter(state, charter)) {
        result.validationMessage = *error;
        return result;
    }
    draft.charter = charter;
    result.structurallyValid = true;
    result.validationMessage = "Valid standing support intent";
    result.condition = maintenanceExecutionCondition(state, draft);
    return result;
}
} // namespace deep
