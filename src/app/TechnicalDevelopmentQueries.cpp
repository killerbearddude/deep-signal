// Owned P5 projections for opportunity, engineering intent, acquired technical
// evidence, local production, prototype availability, and support readiness.
#include "app/SimulationQueries.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>

namespace deep {
namespace {
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
std::string statusName(TechnologyOpportunityStatus status) {
    switch (status) {
    case TechnologyOpportunityStatus::NotPursued:
        return "Not pursued";
    case TechnologyOpportunityStatus::InDevelopment:
        return "In development";
    case TechnologyOpportunityStatus::PrototypeDemonstrated:
        return "Prototype demonstrated";
    case TechnologyOpportunityStatus::LocalProductionReady:
        return "Local production ready";
    case TechnologyOpportunityStatus::Supported:
        return "Production and support ready";
    }
    return "Invalid";
}
} // namespace

std::vector<TechnologyOpportunitySummary> SimulationQueries::technologyOpportunities() const {
    const auto& state = service_.state();
    std::vector<TechnologyOpportunitySummary> rows;
    for (const auto& opportunity : state.technologyOpportunities) {
        TechnologyOpportunitySummary row;
        row.opportunity = opportunity;
        if (const auto* baseline = find(state.shipComponents, opportunity.baselineComponentId))
            row.baselineComponentName = baseline->name;
        const auto* developed = developedRevisionForOpportunity(state, opportunity.id);
        const bool active = std::any_of(state.technicalDevelopmentPrograms.begin(),
                                        state.technicalDevelopmentPrograms.end(), [&](const auto& program) {
                                            return program.charter.opportunityId == opportunity.id &&
                                                   program.lifecycle != TechnicalDevelopmentLifecycle::Closed;
                                        });
        row.status =
            active ? TechnologyOpportunityStatus::InDevelopment : TechnologyOpportunityStatus::NotPursued;
        if (developed) {
            row.status = TechnologyOpportunityStatus::PrototypeDemonstrated;
            if (const auto* profile = find(state.measurementProfiles, developed->measurementProfileId))
                row.demonstratedThreshold = profile->detectionThreshold;
            for (const auto& process : state.componentProductionCapabilities)
                if (process.opportunityId == opportunity.id && process.availableDay <= state.date.day)
                    row.productionColonies.push_back(process.colonyId);
            if (!row.productionColonies.empty())
                row.status = TechnologyOpportunityStatus::LocalProductionReady;
            for (const auto& support : state.supportQualificationRecords)
                if (support.opportunityId == opportunity.id && support.availableDay <= state.date.day)
                    row.supportTeams.push_back(support.teamId);
            const auto* baseline = find(state.shipComponents, opportunity.baselineComponentId);
            if (baseline && baseline->serviceProfile)
                for (const auto& team : state.maintenanceTeams)
                    if (teamHasEffectiveSupportQualification(
                            state, team.id, baseline->serviceProfile->familyId, state.date.day) &&
                        std::find(row.supportTeams.begin(), row.supportTeams.end(), team.id) ==
                            row.supportTeams.end())
                        row.supportTeams.push_back(team.id);
            if (!row.productionColonies.empty() && !row.supportTeams.empty())
                row.status = TechnologyOpportunityStatus::Supported;
        }
        row.statusName = statusName(row.status);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<TechnicalFacility> SimulationQueries::technicalFacilities() const {
    return service_.state().technicalFacilities;
}

std::vector<TechnicalDevelopmentSummary> SimulationQueries::technicalDevelopments() const {
    const auto& state = service_.state();
    std::vector<TechnicalDevelopmentSummary> rows;
    for (const auto& program : state.technicalDevelopmentPrograms) {
        TechnicalDevelopmentSummary row;
        row.program = program;
        row.condition = technicalDevelopmentCondition(state, program);
        if (const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId))
            row.opportunityName = opportunity->name;
        if (const auto* colony = find(state.colonies, program.charter.developmentColonyId))
            row.colonyName = colony->name;
        if (program.charter.requestedFacilityId)
            if (const auto* facility =
                    find(state.technicalFacilities, *program.charter.requestedFacilityId)) {
                row.facilityName = facility->name;
                row.facilityRate = facility->engineeringWorkdaysPerDay;
            }
        if (program.charter.requestedTeamId)
            if (const auto* team = find(state.maintenanceTeams, *program.charter.requestedTeamId)) {
                row.teamName = team->name;
                if (team->colonyId)
                    if (const auto* colony = find(state.colonies, *team->colonyId))
                        row.teamLocation = colony->name;
                if (team->fleetId)
                    if (const auto* fleet = find(state.fleets, *team->fleetId))
                        row.teamLocation = fleet->name;
                if (const auto owner = controllingEngineeringTeam(state, team->id))
                    row.teamOwner = programControllerLabel(state, *owner);
            }
        row.requiredStageWork = technicalStageRequiredWork(program.stage);
        row.requiredStageMaterials = technicalStageCost(program.stage);
        const auto prototype = std::find_if(
            state.prototypeComponentUnits.begin(), state.prototypeComponentUnits.end(),
            [&](const auto& value) { return value.opportunityId == program.charter.opportunityId; });
        if (prototype != state.prototypeComponentUnits.end())
            row.prototype = *prototype;
        for (const auto& test : state.technicalTestRecords)
            if (test.opportunityId == program.charter.opportunityId)
                row.tests.push_back(test);
        if (const auto* developed = developedRevisionForOpportunity(state, program.charter.opportunityId)) {
            row.developed = *developed;
            row.localProductionReady = serialProductionAvailable(
                state, developed->componentId, program.charter.developmentColonyId, state.date.day);
        }
        const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId);
        const auto* baseline =
            opportunity ? find(state.shipComponents, opportunity->baselineComponentId) : nullptr;
        if (program.charter.requestedTeamId && baseline && baseline->serviceProfile) {
            row.supportQualified = teamHasEffectiveSupportQualification(
                state, *program.charter.requestedTeamId, baseline->serviceProfile->familyId, state.date.day);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

TechnicalDevelopmentPreview
SimulationQueries::previewTechnicalDevelopment(const TechnicalDevelopmentCharter& charter,
                                               std::optional<TechnicalDevelopmentProgramId> amending) const {
    const auto& state = service_.state();
    TechnicalDevelopmentPreview preview;
    if (amending) {
        const auto* existing = find(state.technicalDevelopmentPrograms, *amending);
        if (!existing || existing->lifecycle == TechnicalDevelopmentLifecycle::Closed) {
            preview.validationMessage = "No editable technical-development program with that identity";
            return preview;
        }
        if (existing->charter.opportunityId != charter.opportunityId ||
            existing->charter.developmentColonyId != charter.developmentColonyId) {
            preview.validationMessage = "Opportunity and development colony are immutable program identity";
            return preview;
        }
    }
    if (const auto error = validateTechnicalDevelopmentCharter(state, charter, amending.has_value())) {
        preview.validationMessage = *error;
        return preview;
    }
    preview.structurallyValid = true;
    preview.startingStage = firstMissingTechnicalStage(
        state, charter.opportunityId, charter.developmentColonyId, charter.requestedTeamId, charter.scope);
    preview.requiredWork = technicalStageRequiredWork(preview.startingStage);
    preview.requiredMaterials = technicalStageCost(preview.startingStage);
    TechnicalDevelopmentProgram provisional;
    provisional.charter = charter;
    provisional.stage = preview.startingStage;
    preview.condition = technicalDevelopmentCondition(state, provisional);
    preview.validationMessage = "Valid intent; authorization creates no prototype, process, or qualification";
    return preview;
}

} // namespace deep
