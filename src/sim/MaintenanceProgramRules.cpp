// Stationary service readiness uses public condition, ownership, qualifications
// and real colony supply. No hidden deposit or future cargo credit is consulted.
#include "sim/MaintenanceProgramRules.h"
#include "sim/ProgramControl.h"
#include "sim/SurveyProgramRules.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace deep {
namespace {
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
bool stationary(const Fleet& fleet) {
    return fleet.activeOrder.type == FleetOrderType::None && !fleet.destinationBodyId &&
           fleet.queuedOrders.empty();
}
bool validSet(const ProcessedMaterialSet& set) {
    return std::all_of(set.amount.begin(), set.amount.end(),
                       [](double x) { return std::isfinite(x) && x >= 0.0; });
}
const EquipmentCondition* row(const Ship& ship, ShipComponentId id) {
    const auto it = std::find_if(ship.equipmentCondition.begin(), ship.equipmentCondition.end(),
                                 [=](const auto& c) { return c.componentId == id; });
    return it == ship.equipmentCondition.end() ? nullptr : &*it;
}
} // namespace
const ServiceJob* activeServiceJob(const MaintenanceProgram& p) {
    return !p.jobs.empty() && p.jobs.back().outcome == ServiceJobOutcome::Active ? &p.jobs.back() : nullptr;
}
ServiceJob* activeServiceJob(MaintenanceProgram& p) {
    return !p.jobs.empty() && p.jobs.back().outcome == ServiceJobOutcome::Active ? &p.jobs.back() : nullptr;
}
const ServiceJob* clientServiceJob(const GameState& state, SurveyProgramId client) {
    for (const auto& p : state.maintenancePrograms) {
        const auto* job = activeServiceJob(p);
        if (job && job->surveyProgramId == client)
            return job;
    }
    return nullptr;
}
std::optional<std::string> validateMaintenanceCharter(const GameState& state,
                                                      const MaintenanceProgramCharter& c) {
    if (c.name.find_first_not_of(" \t\r\n") == std::string::npos)
        return "Maintenance name must be nonempty";
    if (!find(state.colonies, c.serviceColonyId))
        return "Service colony does not exist";
    if (c.requestedTenderId && !find(state.fleets, *c.requestedTenderId))
        return "Requested tender does not exist";
    if (c.requestedTeamId && !find(state.maintenanceTeams, *c.requestedTeamId))
        return "Requested engineering team does not exist";
    if (c.requestedLeaderId && !find(state.people, *c.requestedLeaderId))
        return "Requested maintenance leader does not exist";
    std::unordered_set<std::int64_t> clients;
    for (FleetId id : c.clients) {
        if (!find(state.fleets, id) || !clients.insert(id.value).second)
            return "Authorized clients must be unique existing fleets";
    }
    if (!validSet(c.policy.floors) ||
        (c.policy.lifetimeAllowances && !validSet(*c.policy.lifetimeAllowances))) {
        return "Maintenance floors/allowances must be finite and nonnegative";
    }
    return std::nullopt;
}
MaintenanceProgramAmendment maintenanceAmendmentFromCharter(const MaintenanceProgramCharter& c) {
    return {c.name, c.requestedTenderId, c.requestedTeamId, c.requestedLeaderId, c.clients, c.policy};
}
void applyMaintenanceAmendment(MaintenanceProgramCharter& c, const MaintenanceProgramAmendment& a) {
    c.name = a.name;
    c.requestedTenderId = a.requestedTenderId;
    c.requestedTeamId = a.requestedTeamId;
    c.requestedLeaderId = a.requestedLeaderId;
    c.clients = a.clients;
    c.policy = a.policy;
}

SurveyServiceRequest surveyServiceRequest(const GameState& state, const SurveyProgram& p) {
    SurveyServiceRequest result;
    if (!p.charter.policy.maintenanceProgramId || p.lifecycle != SurveyProgramLifecycle::Authorized ||
        surveyCharterFinished(p))
        return result;
    const auto fleetId =
        p.leasedFleetId ? p.leasedFleetId : (p.taskFleetId ? p.taskFleetId : p.charter.requestedFleetId);
    const auto* fleet = fleetId ? find(state.fleets, *fleetId) : nullptr;
    if (!fleet)
        return result;
    const double demand =
        p.workDaysCompleted > 0 ? kP3ASurveyWorkDaysPerPass - p.workDaysCompleted : kP3ASurveyWorkDaysPerPass;
    bool threshold = false;
    for (ShipId id : fleet->shipIds) {
        const auto* ship = find(state.ships, id);
        const auto* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (!cls)
            continue;
        for (const auto& install : cls->components) {
            const auto* component = find(state.shipComponents, install.componentId);
            if (!component || !component->serviceProfile || component->surveyCapability <= 0.0)
                continue;
            const auto* condition = row(*ship, component->id);
            if (!condition || condition->usedDuty <= 0.0)
                continue;
            const double remaining = 1.0 - condition->usedDuty / component->serviceProfile->dutyCapacity;
            threshold = threshold || remaining <= p.charter.policy.remainingDutyTrigger;
            result.targets.push_back({ship->id, component->id, condition->usedDuty});
        }
    }
    const auto capability = prepareSurveyDuty(state, *fleet, demand);
    result.requested = clientServiceJob(state, p.id) || threshold || capability.usableCapability <= 0.0;
    if (!result.requested)
        result.targets.clear();
    else if (result.targets.empty()) {
        result.condition = capability.poweredCapability <= 0.0
                               ? capability.condition
                               : "Design duty envelope cannot sustain the required pass; fresh instruments "
                                 "have no service debt";
    } else
        result.condition = "Preventive policy requests full base service before further survey work";
    return result;
}
bool eligibleServiceClient(const GameState& state, const SurveyProgram& p) {
    if (p.lifecycle != SurveyProgramLifecycle::Authorized || !p.leasedFleetId || !p.leasedTeamId ||
        p.maintenanceReturn)
        return false;
    const auto* fleet = find(state.fleets, *p.leasedFleetId);
    const auto* home = find(state.colonies, p.charter.homeColonyId);
    return fleet && home && stationary(*fleet) && fleet->currentBodyId == home->bodyId &&
           controllingProgram(state, fleet->id) == ProgramController{p.id};
}

std::string surveySupportCondition(const GameState& state, const SurveyProgram& p) {
    const auto request = surveyServiceRequest(state, p);
    if (!request.requested)
        return {};
    const auto fleetId = p.leasedFleetId ? p.leasedFleetId : p.charter.requestedFleetId;
    if (fleetId)
        if (const auto owner = controllingProgram(state, *fleetId);
            owner && *owner != ProgramController{p.id}) {
            return "Waiting: requested client fleet is controlled by " +
                   programControllerLabel(state, *owner);
        }
    const auto* provider = find(state.maintenancePrograms, *p.charter.policy.maintenanceProgramId);
    if (!provider)
        return "Waiting for selected maintenance provider";
    if (provider->lifecycle == MaintenanceProgramLifecycle::Closed)
        return "Waiting: selected maintenance provider is closed";
    if (provider->lifecycle == MaintenanceProgramLifecycle::Suspended)
        return "Waiting: selected maintenance provider is suspended";
    if (!fleetId || std::find(provider->charter.clients.begin(), provider->charter.clients.end(), *fleetId) ==
                        provider->charter.clients.end()) {
        return "Waiting: selected provider does not authorize this client fleet";
    }
    const auto* colony = find(state.colonies, provider->charter.serviceColonyId);
    const auto* home = find(state.colonies, p.charter.homeColonyId);
    if (!colony || !home || colony->bodyId != home->bodyId)
        return "Waiting: selected service colony is not at the survey home body";
    if (provider->charter.requestedTenderId == fleetId)
        return "Waiting: this executor needs a separate tender; client already has survey control";
    if (request.targets.empty())
        return request.condition;
    if (const auto* job = clientServiceJob(state, p.id)) {
        const auto work = planMaintenanceWork(state, *provider, *job);
        return work.ready ? "Service in progress; full job holds client until a later opening"
                          : "Waiting for service: " + work.condition;
    }
    return "Waiting for base service: " + maintenanceExecutionCondition(state, *provider);
}

MaintenanceWorkPlan planMaintenanceWork(const GameState& state, const MaintenanceProgram& p,
                                        const ServiceJob& job, const OpeningProgramContext* opening) {
    MaintenanceWorkPlan result;
    const auto* colony = find(state.colonies, p.charter.serviceColonyId);
    const auto* fleet = find(state.fleets, job.tenderFleetId);
    const auto* client = find(state.fleets, job.clientFleetId);
    const auto* team = find(state.maintenanceTeams, job.teamId);
    if (!colony || !fleet || !client || !team || !stationary(*fleet) || !stationary(*client) ||
        fleet->currentBodyId != colony->bodyId || client->currentBodyId != colony->bodyId ||
        team->location != MaintenanceTeamLocation::Fleet || team->fleetId != fleet->id) {
        result.condition =
            "Tender, client and engineering team must be stationary and co-located at service colony";
        return result;
    }
    ProcessedMaterialSet available;
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        auto material = static_cast<ProcessedMaterial>(j);
        available.amount[j] =
            opening ? opening->available(state, colony->id, material, p.charter.policy.floors.amount[j])
                    : std::max(0.0, colony->processedStockpile.amount[j] - p.charter.policy.floors.amount[j]);
        if (p.charter.policy.lifetimeAllowances)
            available.amount[j] =
                std::min(available.amount[j], std::max(0.0, p.charter.policy.lifetimeAllowances->amount[j] -
                                                                p.consumed.amount[j]));
    }
    result.condition = "No unfinished service target has compatible powered workshop and qualified team";
    for (std::size_t i = 0; i < job.targets.size(); ++i) {
        if (job.activeTarget && static_cast<int>(i) != *job.activeTarget)
            continue;
        const auto& target = job.targets[i];
        const auto* ship = find(state.ships, target.shipId);
        const auto* component = find(state.shipComponents, target.componentId);
        const auto* condition = ship ? row(*ship, target.componentId) : nullptr;
        if (!condition || condition->usedDuty <= 0.0 || !component || !component->serviceProfile)
            continue;
        const auto family = component->serviceProfile->familyId;
        const auto* familyDefinition = find(state.equipmentFamilies, family);
        const auto familyName =
            familyDefinition ? familyDefinition->name : "family #" + std::to_string(family.value);
        if (!maintenanceTeamQualified(*team, family) || team->workdaysPerDay <= 0.0) {
            result.condition = !maintenanceTeamQualified(*team, family)
                                   ? "Engineering team lacks qualification for " + familyName
                                   : "Engineering team has zero workdays-per-day capacity";
            if (job.activeTarget)
                return result;
            continue;
        }
        const Ship* workshop = nullptr;
        if (job.workshopShipId)
            workshop = find(state.ships, *job.workshopShipId);
        else
            for (ShipId id : fleet->shipIds) {
                const auto* candidate = find(state.ships, id);
                if (candidate && operationalWorkshopRate(state, *candidate, family) > 0.0) {
                    workshop = candidate;
                    break;
                }
            }
        const double rate = workshop ? operationalWorkshopRate(state, *workshop, family) : 0.0;
        if (rate <= 0.0) {
            result.condition = "No compatible powered workshop hull for " + familyName;
            if (job.activeTarget)
                return result;
            continue;
        }
        // Preserve truthful compatible capacity even when materials are the
        // immediate work blocker; reports must not imply the workshop vanished.
        result.workshopRate = rate;
        result.teamRate = team->workdaysPerDay;
        auto plan = planEquipmentService(state, target.shipId, target.componentId, rate, team->workdaysPerDay,
                                         available);
        result.condition = plan.condition;
        if (!plan.ready) {
            for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
                if (component->serviceProfile->materialsPerDuty.amount[j] <= 0.0 || available.amount[j] > 0.0)
                    continue;
                const auto material = std::string(toString(static_cast<ProcessedMaterial>(j)));
                if (p.charter.policy.lifetimeAllowances &&
                    p.charter.policy.lifetimeAllowances->amount[j] <= p.consumed.amount[j])
                    result.condition = "Lifetime maintenance material allowance exhausted for " + material;
                else
                    result.condition =
                        "Waiting for service supplies: " + material + " above the reserve floor";
                break;
            }
            if (job.activeTarget)
                return result;
            continue;
        }
        const auto representable = [](double before, double after, double delta) {
            return std::isfinite(after) && after >= 0.0 &&
                   (delta == 0.0 ||
                    (before != after && equipmentNearlyEqual(std::abs(after - before), delta)));
        };
        bool conservative =
            representable(p.restoredDuty, p.restoredDuty + plan.restoredDuty * plan.quantity,
                          plan.restoredDuty * plan.quantity) &&
            representable(p.teamWorkdays, p.teamWorkdays + plan.teamWorkdays, plan.teamWorkdays);
        for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
            conservative = conservative &&
                           representable(colony->processedStockpile.amount[j],
                                         colony->processedStockpile.amount[j] - plan.consumed.amount[j],
                                         plan.consumed.amount[j]) &&
                           representable(p.consumed.amount[j], p.consumed.amount[j] + plan.consumed.amount[j],
                                         plan.consumed.amount[j]);
        }
        if (!conservative) {
            result.condition = "Maintenance stock/restoration change cannot be represented conservatively";
            if (job.activeTarget)
                return result;
            continue;
        }
        result.ready = true;
        result.targetIndex = static_cast<int>(i);
        result.workshopShipId = workshop->id;
        result.workshopRate = rate;
        result.teamRate = team->workdaysPerDay;
        result.service = std::move(plan);
        return result;
    }
    return result;
}

std::string maintenanceExecutionCondition(const GameState& state, const MaintenanceProgram& p) {
    if (p.lifecycle == MaintenanceProgramLifecycle::Closed)
        return "Maintenance provider closed";
    if (p.lifecycle == MaintenanceProgramLifecycle::Suspended)
        return "Maintenance provider suspended";
    if (!p.issue.signature.empty() && !p.issue.acknowledged)
        return "Decision needed: " + p.issue.message;
    if (!p.charter.requestedLeaderId)
        return "Waiting for responsible maintenance leader";
    if (!p.leasedTenderId && !p.charter.requestedTenderId)
        return "Waiting for requested tender fleet";
    if (!p.leasedTeamId && !p.charter.requestedTeamId)
        return "Waiting for finite engineering team";
    const auto tenderId = p.leasedTenderId ? p.leasedTenderId : p.charter.requestedTenderId;
    const auto teamId = p.leasedTeamId ? p.leasedTeamId : p.charter.requestedTeamId;
    const auto* tender = tenderId ? find(state.fleets, *tenderId) : nullptr;
    const auto* team = teamId ? find(state.maintenanceTeams, *teamId) : nullptr;
    const auto* colony = find(state.colonies, p.charter.serviceColonyId);
    if (!tender || !team || !colony)
        return "Waiting for actual tender/team/service colony";
    if (const auto owner = controllingProgram(state, tender->id);
        owner && *owner != ProgramController{p.id}) {
        return "Waiting: requested tender is controlled by " + programControllerLabel(state, *owner);
    }
    if (const auto owner=controllingEngineeringTeam(state,team->id); owner && *owner!=ProgramController{p.id})
        return "Waiting: engineering team is controlled by "+programControllerLabel(state,*owner);
    if (!stationary(*tender))
        return "Waiting: tender has existing transit or queued movement";
    if (tender->currentBodyId != colony->bodyId)
        return "Waiting: tender must be physically prepositioned at the service colony";
    if (!((team->location == MaintenanceTeamLocation::Colony && team->colonyId == colony->id) ||
          (team->location == MaintenanceTeamLocation::Fleet && team->fleetId == tender->id)))
        return "Waiting: engineering team is not co-located with tender";
    if (const auto* job = activeServiceJob(p)) {
        const auto plan = planMaintenanceWork(state, p, *job);
        return plan.ready ? "Servicing one installation group; client held until full service"
                          : plan.condition;
    }
    return "Standing support ready to select an eligible client in charter order";
}
} // namespace deep
