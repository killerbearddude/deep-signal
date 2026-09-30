// Computes P5 work envelopes from public artifacts, real people/facilities,
// opening stock, and charter authority. Hidden achieved performance is read only
// by the test executor after actual test work.
#include "sim/TechnicalDevelopmentRules.h"
#include "sim/ProgramControl.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace deep {
namespace {
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
bool validSet(const ProcessedMaterialSet& set) {
    return std::all_of(set.amount.begin(), set.amount.end(),
                       [](double value) { return std::isfinite(value) && value >= 0.0; });
}
ProcessedMaterialSet lifetimeConsumed(const TechnicalDevelopmentProgram& program) {
    ProcessedMaterialSet result;
    for (const auto& receipt : program.receipts)
        result.addSet(receipt.consumed);
    return result;
}
const EquipmentServiceProfile* baselineService(const GameState& state, TechnologyOpportunityId id) {
    const auto* opportunity = find(state.technologyOpportunities, id);
    const auto* component =
        opportunity ? find(state.shipComponents, opportunity->baselineComponentId) : nullptr;
    return component && component->serviceProfile ? &*component->serviceProfile : nullptr;
}
} // namespace

ProcessedMaterialSet technicalStageCost(TechnicalDevelopmentStage stage) {
    ProcessedMaterialSet cost;
    switch (stage) {
    case TechnicalDevelopmentStage::ConceptEngineering:
        cost.set(ProcessedMaterial::Electronics, 10.0);
        cost.set(ProcessedMaterial::IndustrialComposites, 5.0);
        break;
    case TechnicalDevelopmentStage::PrototypeFabrication:
        cost.set(ProcessedMaterial::Electronics, 80.0);
        cost.set(ProcessedMaterial::IndustrialComposites, 20.0);
        break;
    case TechnicalDevelopmentStage::PrototypeTesting:
        cost.set(ProcessedMaterial::Electronics, 15.0);
        cost.set(ProcessedMaterial::IndustrialComposites, 6.0);
        break;
    case TechnicalDevelopmentStage::ProductionQualification:
        cost.set(ProcessedMaterial::StructuralAlloys, 40.0);
        cost.set(ProcessedMaterial::Electronics, 30.0);
        cost.set(ProcessedMaterial::IndustrialComposites, 20.0);
        break;
    case TechnicalDevelopmentStage::SupportQualification:
        cost.set(ProcessedMaterial::Electronics, 10.0);
        cost.set(ProcessedMaterial::IndustrialComposites, 10.0);
        break;
    case TechnicalDevelopmentStage::Complete:
        break;
    }
    return cost;
}

double technicalStageRequiredWork(TechnicalDevelopmentStage stage) noexcept {
    switch (stage) {
    case TechnicalDevelopmentStage::ConceptEngineering:
        return kConceptEngineeringWork;
    case TechnicalDevelopmentStage::PrototypeFabrication:
        return kPrototypeFabricationWork;
    case TechnicalDevelopmentStage::PrototypeTesting:
        return kPrototypeTestingWork;
    case TechnicalDevelopmentStage::ProductionQualification:
        return kProductionQualificationWork;
    case TechnicalDevelopmentStage::SupportQualification:
        return kSupportQualificationWork;
    case TechnicalDevelopmentStage::Complete:
        return 0.0;
    }
    return 0.0;
}

int inheritedTechnicalTestCount(const GameState& state, const TechnicalDevelopmentProgram& program) {
    const auto prototype =
        std::find_if(state.prototypeComponentUnits.begin(), state.prototypeComponentUnits.end(),
                     [&](const auto& unit) { return unit.opportunityId == program.charter.opportunityId; });
    if (prototype == state.prototypeComponentUnits.end())
        return 0;
    return static_cast<int>(std::count_if(
        state.technicalTestRecords.begin(), state.technicalTestRecords.end(), [&](const auto& test) {
            // Later programs must not change the work owed by a historical
            // cancelled program. Same-day tests already existed at authorization.
            return test.opportunityId == program.charter.opportunityId && test.prototypeId == prototype->id &&
                   test.programId != program.id && test.day <= program.createdDay;
        }));
}

double technicalProgramStageRequiredWork(const GameState& state, const TechnicalDevelopmentProgram& program) {
    if (program.stage == TechnicalDevelopmentStage::PrototypeTesting)
        return std::max(0.0, kPrototypeTestingWork - inheritedTechnicalTestCount(state, program));
    return technicalStageRequiredWork(program.stage);
}

std::optional<TechnicalFacilityId> technicalWorkFacility(const TechnicalDevelopmentProgram& program) {
    if (program.stage == TechnicalDevelopmentStage::ProductionQualification)
        for (const auto& receipt : program.receipts)
            if (receipt.stage == program.stage && receipt.work > 0.0)
                return receipt.facilityId;
    return program.charter.requestedFacilityId;
}

std::optional<MaintenanceTeamId> technicalWorkTeam(const TechnicalDevelopmentProgram& program) {
    if (program.stage == TechnicalDevelopmentStage::SupportQualification)
        for (const auto& receipt : program.receipts)
            if (receipt.stage == program.stage && receipt.work > 0.0)
                return receipt.teamId;
    return program.charter.requestedTeamId;
}

bool teamHasEngineeringQualification(const MaintenanceTeam& team,
                                     EngineeringQualification qualification) noexcept {
    return std::find(team.engineeringQualifications.begin(), team.engineeringQualifications.end(),
                     qualification) != team.engineeringQualifications.end();
}

const DevelopedComponentRevision* developedRevisionForComponent(const GameState& state,
                                                                ShipComponentId component) noexcept {
    const auto it =
        std::find_if(state.developedComponentRevisions.begin(), state.developedComponentRevisions.end(),
                     [&](const auto& row) { return row.componentId == component; });
    return it == state.developedComponentRevisions.end() ? nullptr : &*it;
}
const DevelopedComponentRevision*
developedRevisionForOpportunity(const GameState& state, TechnologyOpportunityId opportunity) noexcept {
    const auto it =
        std::find_if(state.developedComponentRevisions.begin(), state.developedComponentRevisions.end(),
                     [&](const auto& row) { return row.opportunityId == opportunity; });
    return it == state.developedComponentRevisions.end() ? nullptr : &*it;
}
bool serialProductionAvailable(const GameState& state, ShipComponentId component, ColonyId colony,
                               std::int64_t day) noexcept {
    return std::any_of(state.componentProductionCapabilities.begin(),
                       state.componentProductionCapabilities.end(), [&](const auto& row) {
                           return row.componentId == component && row.colonyId == colony &&
                                  row.availableDay <= day;
                       });
}
std::vector<PrototypeComponentUnitId> availablePrototypeUnits(const GameState& state,
                                                              ShipComponentId component, ColonyId colony,
                                                              std::int64_t day) {
    std::vector<PrototypeComponentUnitId> result;
    for (const auto& row : state.prototypeComponentUnits)
        if (row.componentId == component && row.colonyId == colony && row.availableDay &&
            *row.availableDay <= day && row.state == PrototypeComponentState::Available)
            result.push_back(row.id);
    return result;
}
bool teamHasEffectiveSupportQualification(const GameState& state, MaintenanceTeamId team,
                                          EquipmentFamilyId family, std::int64_t day) noexcept {
    const auto record =
        std::find_if(state.supportQualificationRecords.begin(), state.supportQualificationRecords.end(),
                     [&](const auto& row) { return row.teamId == team && row.familyId == family; });
    if (record != state.supportQualificationRecords.end())
        return record->availableDay <= day;
    const auto* selected = find(state.maintenanceTeams, team);
    return selected && std::find(selected->qualifiedFamilies.begin(), selected->qualifiedFamilies.end(),
                                 family) != selected->qualifiedFamilies.end();
}

TechnicalDevelopmentStage firstMissingTechnicalStage(const GameState& state,
                                                     TechnologyOpportunityId opportunity, ColonyId colony,
                                                     std::optional<MaintenanceTeamId> team,
                                                     TechnicalDevelopmentScope scope) {
    const bool design = std::any_of(state.prototypeDesigns.begin(), state.prototypeDesigns.end(),
                                    [&](const auto& row) { return row.opportunityId == opportunity; });
    if (!design)
        return TechnicalDevelopmentStage::ConceptEngineering;
    const bool prototype =
        std::any_of(state.prototypeComponentUnits.begin(), state.prototypeComponentUnits.end(),
                    [&](const auto& row) { return row.opportunityId == opportunity; });
    if (!prototype)
        return TechnicalDevelopmentStage::PrototypeFabrication;
    const auto* demonstrated = developedRevisionForOpportunity(state, opportunity);
    if (!demonstrated)
        return TechnicalDevelopmentStage::PrototypeTesting;
    if (scope == TechnicalDevelopmentScope::DemonstratePrototype)
        return TechnicalDevelopmentStage::Complete;
    const bool process = std::any_of(
        state.componentProductionCapabilities.begin(), state.componentProductionCapabilities.end(),
        [&](const auto& row) { return row.opportunityId == opportunity && row.colonyId == colony; });
    if (!process)
        return TechnicalDevelopmentStage::ProductionQualification;
    if (scope == TechnicalDevelopmentScope::ProductionReady)
        return TechnicalDevelopmentStage::Complete;
    const auto* service = baselineService(state, opportunity);
    if (team && service) {
        const auto* selected = find(state.maintenanceTeams, *team);
        const bool established =
            selected && std::find(selected->qualifiedFamilies.begin(), selected->qualifiedFamilies.end(),
                                  service->familyId) != selected->qualifiedFamilies.end();
        if (established ||
            teamHasEffectiveSupportQualification(state, *team, service->familyId,
                                                 state.date.day == std::numeric_limits<std::int64_t>::max()
                                                     ? state.date.day
                                                     : state.date.day + 1))
            return TechnicalDevelopmentStage::Complete;
    }
    return TechnicalDevelopmentStage::SupportQualification;
}

std::optional<std::string> validateTechnicalDevelopmentCharter(const GameState& state,
                                                               const TechnicalDevelopmentCharter& charter,
                                                               bool amendment) {
    if (charter.name.find_first_not_of(" \t\r\n") == std::string::npos)
        return "Technical-development name is required";
    if (!find(state.technologyOpportunities, charter.opportunityId))
        return "Technology opportunity does not exist";
    if (!find(state.colonies, charter.developmentColonyId))
        return "Development colony does not exist";
    if (charter.requestedFacilityId) {
        const auto* facility = find(state.technicalFacilities, *charter.requestedFacilityId);
        if (!facility || facility->colonyId != charter.developmentColonyId)
            return "Requested technical facility does not exist at the development colony";
    }
    if (charter.requestedTeamId && !find(state.maintenanceTeams, *charter.requestedTeamId))
        return "Requested engineering team does not exist";
    if (charter.requestedLeaderId && !find(state.people, *charter.requestedLeaderId))
        return "Responsible technical leader does not exist";
    if (charter.scope < TechnicalDevelopmentScope::DemonstratePrototype ||
        charter.scope > TechnicalDevelopmentScope::ProductionAndSupportReady)
        return "Technical-development scope is invalid";
    if (!validSet(charter.policy.floors) ||
        (charter.policy.lifetimeAllowances && !validSet(*charter.policy.lifetimeAllowances)))
        return "Technical-development material limits must be finite and nonnegative";
    if (!amendment && std::any_of(state.technicalDevelopmentPrograms.begin(),
                                  state.technicalDevelopmentPrograms.end(), [&](const auto& program) {
                                      return program.charter.opportunityId == charter.opportunityId &&
                                             program.lifecycle != TechnicalDevelopmentLifecycle::Closed;
                                  }))
        return "An active technical-development program already owns this opportunity";
    return std::nullopt;
}

TechnicalDevelopmentStage technicalStageUnderCurrentAuthority(const GameState& state,
                                                              const TechnicalDevelopmentProgram& program) {
    auto supportTeam = program.charter.requestedTeamId;
    for (const auto& receipt : program.receipts)
        if (receipt.stage == TechnicalDevelopmentStage::SupportQualification && receipt.work > 0.0) {
            // A completed or temporarily out-of-scope course still belongs to
            // its original team when future authority is amended.
            supportTeam = receipt.teamId;
            break;
        }
    return firstMissingTechnicalStage(state, program.charter.opportunityId,
                                      program.charter.developmentColonyId, supportTeam,
                                      program.charter.scope);
}

void reconcileTechnicalStageAfterAmendment(const GameState& state, TechnicalDevelopmentProgram& program) {
    const auto nextStage = technicalStageUnderCurrentAuthority(state, program);
    if (nextStage == program.stage)
        return;
    program.stage = nextStage;
    program.stageWork = 0.0;
    program.stageConsumed = {};
    // Scope may be narrowed and expanded before closure. Restore only this
    // program's own paid stage work, never a cancelled predecessor's fraction.
    for (const auto& receipt : program.receipts)
        if (receipt.stage == nextStage) {
            program.stageWork += receipt.work;
            program.stageConsumed.addSet(receipt.consumed);
        }
}

TechnicalReadiness technicalDevelopmentReadiness(const GameState& state,
                                                 const TechnicalDevelopmentProgram& program,
                                                 const OpeningProgramContext* opening) {
    TechnicalReadiness result;
    if (program.lifecycle == TechnicalDevelopmentLifecycle::Suspended) {
        result.cause = TechnicalWaitCause::Suspended;
        result.explanation = "Technical development is suspended";
        return result;
    }
    if (program.lifecycle == TechnicalDevelopmentLifecycle::Closed ||
        program.stage == TechnicalDevelopmentStage::Complete) {
        result.cause = TechnicalWaitCause::Complete;
        result.explanation = "Authorized technical-development scope is complete";
        return result;
    }
    const auto& charter = program.charter;
    if (!charter.requestedLeaderId) {
        result.cause = TechnicalWaitCause::NoLeader;
        result.explanation = "Waiting for a responsible technical leader";
        return result;
    }
    const auto teamId = technicalWorkTeam(program);
    const auto* team = teamId ? find(state.maintenanceTeams, *teamId) : nullptr;
    if (!team) {
        result.cause = TechnicalWaitCause::NoEngineeringTeam;
        result.explanation = "Waiting for an actual engineering team";
        return result;
    }
    result.teamId = team->id;
    if (const auto owner = controllingEngineeringTeam(state, team->id);
        owner && *owner != ProgramController{program.id}) {
        result.cause = TechnicalWaitCause::TeamControlled;
        result.explanation =
            "Waiting: engineering team is controlled by " + programControllerLabel(state, *owner);
        return result;
    }
    if (!teamHasEngineeringQualification(*team, EngineeringQualification::PrototypeInstrumentation)) {
        result.cause = TechnicalWaitCause::MissingEngineeringQualification;
        result.explanation = "Waiting for Prototype Instrumentation engineering qualification";
        return result;
    }
    if (team->location != MaintenanceTeamLocation::Colony || team->colonyId != charter.developmentColonyId) {
        result.cause = TechnicalWaitCause::TeamLocation;
        result.explanation = "Waiting for the engineering team at the development colony";
        return result;
    }
    const auto facilityId = technicalWorkFacility(program);
    const auto* facility = facilityId ? find(state.technicalFacilities, *facilityId) : nullptr;
    if (!facility || facility->colonyId != charter.developmentColonyId ||
        facility->capability != TechnicalFacilityCapability::PrototypeInstrumentation) {
        result.cause = TechnicalWaitCause::NoFacility;
        result.explanation = "Waiting for a Prototype Instrumentation technical facility";
        return result;
    }
    result.facilityId = facility->id;
    if (program.stage == TechnicalDevelopmentStage::PrototypeTesting) {
        const auto prototype =
            std::find_if(state.prototypeComponentUnits.begin(), state.prototypeComponentUnits.end(),
                         [&](const auto& unit) { return unit.opportunityId == charter.opportunityId; });
        if (prototype == state.prototypeComponentUnits.end() ||
            prototype->colonyId != charter.developmentColonyId) {
            result.cause = TechnicalWaitCause::NoFacility;
            result.explanation = "Waiting for the physical prototype at the development colony; "
                                 "prototype transport is unavailable";
            return result;
        }
    }
    double facilityRate = facility->engineeringWorkdaysPerDay;
    if (opening)
        facilityRate = opening->availableTechnicalFacility(facility->id);
    if (!(facilityRate > 0.0)) {
        result.cause = TechnicalWaitCause::FacilityCapacity;
        result.explanation = "Waiting for finite technical-facility throughput";
        return result;
    }
    const double required = technicalProgramStageRequiredWork(state, program);
    // The material rate remains the full stage's rate. Inherited tests reduce
    // paid work, not the per-test bill of five Electronics and two Composites.
    const double fullStageWork = technicalStageRequiredWork(program.stage);
    const double remaining = std::max(0.0, required - program.stageWork);
    double work = std::min({remaining, team->workdaysPerDay, facilityRate});
    if (!(team->workdaysPerDay > 0.0)) {
        result.cause = TechnicalWaitCause::NoEngineeringTeam;
        result.explanation = "Waiting for positive engineering-team throughput";
        return result;
    }
    const auto cost = technicalStageCost(program.stage);
    const auto spent = lifetimeConsumed(program);
    const auto* colony = find(state.colonies, charter.developmentColonyId);
    bool floorLimited = false, allowanceLimited = false, stockLimited = false;
    for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
        const double coefficient = fullStageWork > 0.0 ? cost.amount[index] / fullStageWork : 0.0;
        if (coefficient <= 0.0)
            continue;
        const auto material = static_cast<ProcessedMaterial>(index);
        const double available = opening ? opening->available(state, charter.developmentColonyId, material,
                                                              charter.policy.floors.amount[index])
                                         : std::max(0.0, colony->processedStockpile.amount[index] -
                                                             charter.policy.floors.amount[index]);
        const double stock = colony->processedStockpile.amount[index];
        floorLimited |= available <= 1e-12 && stock > 1e-12 && charter.policy.floors.amount[index] > 0.0;
        stockLimited |= available <= 1e-12 && stock <= 1e-12;
        work = std::min(work, available / coefficient);
        if (charter.policy.lifetimeAllowances) {
            const double remainingAuthority =
                std::max(0.0, charter.policy.lifetimeAllowances->amount[index] - spent.amount[index]);
            allowanceLimited |= remainingAuthority <= 1e-12;
            work = std::min(work, remainingAuthority / coefficient);
        }
    }
    if (!(work > 1e-12) || !std::isfinite(work)) {
        if (allowanceLimited) {
            result.cause = TechnicalWaitCause::MaterialAllowance;
            result.explanation = "Waiting: lifetime development-material authority is exhausted";
        } else if (floorLimited) {
            result.cause = TechnicalWaitCause::MaterialFloor;
            result.explanation = "Waiting: protected development-colony stock floor blocks work";
        } else {
            result.cause = TechnicalWaitCause::MaterialStock;
            result.explanation = stockLimited ? "Waiting for actual development material stock"
                                              : "Waiting for sufficient authorized development materials";
        }
        return result;
    }
    // Normalize only a genuinely positive final step; this absorbs accumulated
    // binary residue without granting work from zero stock.
    if (remaining - work <=
        32.0 * std::numeric_limits<double>::epsilon() * std::max({1.0, required, remaining}))
        work = remaining;
    result.work = work;
    for (std::size_t index = 0; index < processedMaterialCount(); ++index)
        result.consumed.amount[index] = cost.amount[index] * work / fullStageWork;
    result.canWork = true;
    result.cause = TechnicalWaitCause::None;
    result.explanation = "Ready for finite technical-development work";
    return result;
}

std::string technicalDevelopmentCondition(const GameState& state,
                                          const TechnicalDevelopmentProgram& program) {
    return technicalDevelopmentReadiness(state, program).explanation;
}

} // namespace deep
