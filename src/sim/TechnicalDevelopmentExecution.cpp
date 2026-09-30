// Performs paid technical work and converts only completed stages into durable
// designs, prototypes, test evidence, local processes, or team qualification.
#include "sim/TechnicalDevelopmentExecution.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace deep {
namespace {
template <class Rows, class Id> auto* find(Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
void audit(const TechnicalDevelopmentProgram& program, TechnicalDevelopmentAuditKind kind, double amount,
           std::string detail, const TechnicalDevelopmentExecutionHooks& hooks) {
    hooks.emit(EventSeverity::Info,
               TechnicalDevelopmentAuditEvent{program.id, kind, program.charter.opportunityId, program.stage,
                                              program.charterRevision, amount, std::move(detail)});
}
bool represents(double before, double after, double expected) {
    if (!std::isfinite(before) || !std::isfinite(after) || after < 0.0 || expected < 0.0)
        return false;
    const double actual = std::abs(after - before);
    return expected == 0.0
               ? actual == 0.0
               : actual > 0.0 && std::abs(actual - expected) <= 1e-9 * std::max({1.0, actual, expected});
}
const PrototypeDesignRecord* designFor(const GameState& state, TechnologyOpportunityId opportunity) {
    const auto it = std::find_if(state.prototypeDesigns.begin(), state.prototypeDesigns.end(),
                                 [&](const auto& row) { return row.opportunityId == opportunity; });
    return it == state.prototypeDesigns.end() ? nullptr : &*it;
}
PrototypeComponentUnit* prototypeFor(GameState& state, TechnologyOpportunityId opportunity) {
    const auto it = std::find_if(state.prototypeComponentUnits.begin(), state.prototypeComponentUnits.end(),
                                 [&](const auto& row) { return row.opportunityId == opportunity; });
    return it == state.prototypeComponentUnits.end() ? nullptr : &*it;
}
const TechnologyCandidateTruth* truthFor(const GameState& state, TechnologyOpportunityId opportunity) {
    const auto it =
        std::find_if(state.technologyCandidateTruths.begin(), state.technologyCandidateTruths.end(),
                     [&](const auto& row) { return row.opportunityId == opportunity; });
    return it == state.technologyCandidateTruths.end() ? nullptr : &*it;
}
void publishDesign(GameState& state, TechnicalDevelopmentProgram& program,
                   const TechnicalDevelopmentExecutionHooks& hooks) {
    const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId);
    const auto* baseline =
        opportunity ? find(state.shipComponents, opportunity->baselineComponentId) : nullptr;
    if (!baseline || !baseline->serviceProfile)
        throw std::logic_error("Technical opportunity lost its established service baseline");
    PrototypeDesignRecord record;
    record.id = PrototypeDesignId{state.ids.nextPrototypeDesignId++};
    record.opportunityId = opportunity->id;
    record.programId = program.id;
    record.createdDay = state.date.day;
    record.name = "Precision Characterization Array Mk I";
    record.serialBuildCost.set(ProcessedMaterial::Electronics, 60.0);
    record.serialBuildCost.set(ProcessedMaterial::IndustrialComposites, 10.0);
    record.serviceProfile = EquipmentServiceProfile{baseline->serviceProfile->familyId, 90.0, 0.25, {}};
    record.serviceProfile.materialsPerDuty.set(ProcessedMaterial::Electronics, 0.75);
    record.serviceProfile.materialsPerDuty.set(ProcessedMaterial::IndustrialComposites, 0.75);
    state.prototypeDesigns.push_back(std::move(record));
    audit(program, TechnicalDevelopmentAuditKind::DesignCompleted, 0.0,
          "Immutable prototype mechanical/electrical design completed; sensitivity remains untested", hooks);
}
void publishPrototype(GameState& state, TechnicalDevelopmentProgram& program,
                      const TechnicalDevelopmentExecutionHooks& hooks) {
    const auto* design = designFor(state, program.charter.opportunityId);
    if (!design)
        throw std::logic_error("Prototype fabrication completed without a design");
    state.prototypeComponentUnits.push_back(
        PrototypeComponentUnit{.id = PrototypeComponentUnitId{state.ids.nextPrototypeComponentUnitId++},
                               .opportunityId = program.charter.opportunityId,
                               .designId = design->id,
                               .programId = program.id,
                               .colonyId = program.charter.developmentColonyId,
                               .fabricationDay = state.date.day,
                               .state = PrototypeComponentState::Available,
                               .componentId = std::nullopt,
                               .availableDay = std::nullopt,
                               .reservedOrderId = std::nullopt,
                               .reservedHullNumber = std::nullopt,
                               .consumedShipId = std::nullopt});
    audit(program, TechnicalDevelopmentAuditKind::PrototypeFabricated, 0.0,
          "One physical local prototype component fabricated; no serial process created", hooks);
}
void publishTestAndMaybeDemonstration(GameState& state, TechnicalDevelopmentProgram& program,
                                      const TechnicalDevelopmentExecutionHooks& hooks) {
    auto* prototype = prototypeFor(state, program.charter.opportunityId);
    const auto* truth = truthFor(state, program.charter.opportunityId);
    const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId);
    const auto* design = designFor(state, program.charter.opportunityId);
    const auto* facility = program.charter.requestedFacilityId
                               ? find(state.technicalFacilities, *program.charter.requestedFacilityId)
                               : nullptr;
    if (!prototype || !truth || !opportunity || !design || !facility || !program.charter.requestedTeamId ||
        !program.charter.requestedLeaderId)
        throw std::logic_error("Prototype test lost its physical or authoritative provenance");
    int sequence = 1;
    for (const auto& row : state.technicalTestRecords)
        if (row.prototypeId == prototype->id)
            ++sequence;
    const TechnicalTestId testId{state.ids.nextTechnicalTestId++};
    state.technicalTestRecords.push_back(TechnicalTestRecord{
        .id = testId,
        .opportunityId = opportunity->id,
        .prototypeId = prototype->id,
        .programId = program.id,
        .facilityId = facility->id,
        .teamId = *program.charter.requestedTeamId,
        .leaderId = *program.charter.requestedLeaderId,
        .sequence = sequence,
        .day = state.date.day,
        .measuredDetectionThreshold = truth->achievedDetectionThreshold,
        .targetDetectionThreshold = opportunity->targetDetectionThreshold,
        .meetsTarget = truth->achievedDetectionThreshold <= opportunity->targetDetectionThreshold,
        .limitation = "Deterministic prototype method test; three workdays establish repeatability"});
    audit(program, TechnicalDevelopmentAuditKind::TestCompleted, truth->achievedDetectionThreshold,
          "Prototype test recorded measured detection threshold", hooks);
    if (sequence < 3)
        return;

    const auto* baseline = find(state.shipComponents, opportunity->baselineComponentId);
    const auto* baselineProfile = baseline && baseline->measurementProfileId
                                      ? find(state.measurementProfiles, *baseline->measurementProfileId)
                                      : nullptr;
    if (!baseline || !baselineProfile)
        throw std::logic_error("Demonstration lost its established measurement comparator");
    const MeasurementProfileId profileId{state.ids.nextMeasurementProfileId++};
    state.measurementProfiles.push_back(
        MeasurementProfile{.id = profileId,
                           .name = "Precision Characterization",
                           .methodVersion = 1,
                           .detectionThreshold = truth->achievedDetectionThreshold,
                           .measuresAccessibility = true,
                           .channels = baselineProfile->channels});
    const ShipComponentId componentId{state.ids.nextShipComponentId++};
    state.shipComponents.push_back(ShipComponentDefinition{.id = componentId,
                                                           .name = design->name,
                                                           .kind = ShipComponentKind::SurveySensor,
                                                           .mass = design->mass,
                                                           .volume = design->volume,
                                                           .powerDemand = design->powerDemand,
                                                           .surveyCapability = design->surveyCapability,
                                                           .buildCost = design->serialBuildCost,
                                                           .buildPoints = design->componentBuildPoints,
                                                           .serviceProfile = design->serviceProfile,
                                                           .measurementProfileId = profileId});
    std::vector<TechnicalTestId> tests;
    for (const auto& row : state.technicalTestRecords)
        if (row.prototypeId == prototype->id)
            tests.push_back(row.id);
    state.developedComponentRevisions.push_back(DevelopedComponentRevision{
        .id = DevelopedComponentRevisionId{state.ids.nextDevelopedComponentRevisionId++},
        .opportunityId = opportunity->id,
        .designId = design->id,
        .prototypeId = prototype->id,
        .testIds = std::move(tests),
        .measurementProfileId = profileId,
        .componentId = componentId,
        .demonstratedDay = state.date.day,
        .availableDay = state.date.day + 1});
    prototype->componentId = componentId;
    prototype->availableDay = state.date.day + 1;
    audit(program, TechnicalDevelopmentAuditKind::ComponentDemonstrated, truth->achievedDetectionThreshold,
          "Test-derived immutable component and measurement profile demonstrated", hooks);
    if (truth->achievedDetectionThreshold > opportunity->targetDetectionThreshold) {
        program.issue.signature = "technical/performance-miss";
        program.issue.message = "Prototype performance missed the authorized target; achieved threshold " +
                                std::to_string(truth->achievedDetectionThreshold) + " versus target " +
                                std::to_string(opportunity->targetDetectionThreshold) + ".";
        program.issue.acknowledged = false;
        audit(program, TechnicalDevelopmentAuditKind::IssueRaised, truth->achievedDetectionThreshold,
              program.issue.message, hooks);
    }
}
void publishProductionCapability(GameState& state, TechnicalDevelopmentProgram& program,
                                 const TechnicalDevelopmentExecutionHooks& hooks) {
    const auto* developed = developedRevisionForOpportunity(state, program.charter.opportunityId);
    if (!developed || !program.charter.requestedFacilityId)
        throw std::logic_error("Production qualification lost demonstrated design or facility");
    state.componentProductionCapabilities.push_back(ComponentProductionCapability{
        .id = ComponentProductionCapabilityId{state.ids.nextComponentProductionCapabilityId++},
        .componentId = developed->componentId,
        .opportunityId = developed->opportunityId,
        .colonyId = program.charter.developmentColonyId,
        .facilityId = *program.charter.requestedFacilityId,
        .qualifyingProgramId = program.id,
        .qualifiedDay = state.date.day,
        .availableDay = state.date.day + 1});
    audit(program, TechnicalDevelopmentAuditKind::ProductionQualified, 0.0,
          "Local serial component process qualified; effective next opening", hooks);
}
void publishSupportQualification(GameState& state, TechnicalDevelopmentProgram& program,
                                 const TechnicalDevelopmentExecutionHooks& hooks) {
    const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId);
    const auto* baseline =
        opportunity ? find(state.shipComponents, opportunity->baselineComponentId) : nullptr;
    if (!baseline || !baseline->serviceProfile || !program.charter.requestedTeamId)
        throw std::logic_error("Support qualification lost team or specialist family");
    state.supportQualificationRecords.push_back(
        SupportQualificationRecord{.id = SupportQualificationId{state.ids.nextSupportQualificationId++},
                                   .opportunityId = opportunity->id,
                                   .teamId = *program.charter.requestedTeamId,
                                   .familyId = baseline->serviceProfile->familyId,
                                   .programId = program.id,
                                   .qualifiedDay = state.date.day,
                                   .availableDay = state.date.day + 1});
    audit(program, TechnicalDevelopmentAuditKind::SupportQualified, 0.0,
          "Exact engineering team completed specialist support qualification; effective next opening", hooks);
}
} // namespace

void runTechnicalDevelopmentOpeningDay(GameState& state, TechnicalDevelopmentProgram& program,
                                       OpeningProgramContext& opening,
                                       const TechnicalDevelopmentExecutionHooks& hooks) {
    if (program.lifecycle != TechnicalDevelopmentLifecycle::Authorized)
        return;
    if (program.stage == TechnicalDevelopmentStage::Complete) {
        if (!program.issue.acknowledged)
            return;
        if (hooks.prepareEvents)
            hooks.prepareEvents(1);
        program.lifecycle = TechnicalDevelopmentLifecycle::Closed;
        program.closure = TechnicalDevelopmentClosure::Completed;
        program.closedDay = state.date.day;
        program.leasedTeamId.reset();
        audit(program, TechnicalDevelopmentAuditKind::Closed, 0.0,
              "Authorized technical-development scope completed", hooks);
        return;
    }
    const auto readiness = technicalDevelopmentReadiness(state, program, &opening);
    if (!readiness.canWork) {
        program.leasedTeamId.reset();
        const std::string signature = "technical/" + std::to_string(static_cast<int>(readiness.cause));
        if (program.issue.signature != signature) {
            program.issue.signature = signature;
            program.issue.message = readiness.explanation;
            program.issue.acknowledged = program.receipts.empty() ||
                                         program.receipts.back().charterRevision != program.charterRevision;
            if (!program.issue.acknowledged)
                audit(program, TechnicalDevelopmentAuditKind::IssueRaised, 0.0, readiness.explanation, hooks);
        }
        return;
    }
    if (!readiness.teamId || !readiness.facilityId)
        throw std::logic_error("Ready technical work lacks physical participants");
    const bool finalStep =
        program.stageWork + readiness.work + 1e-9 >= technicalStageRequiredWork(program.stage);
    bool identitiesAvailable = state.ids.nextEventId < std::numeric_limits<std::int64_t>::max();
    if (program.stage == TechnicalDevelopmentStage::ConceptEngineering && finalStep)
        identitiesAvailable &= state.ids.nextPrototypeDesignId < std::numeric_limits<std::int64_t>::max();
    if (program.stage == TechnicalDevelopmentStage::PrototypeFabrication && finalStep)
        identitiesAvailable &=
            state.ids.nextPrototypeComponentUnitId < std::numeric_limits<std::int64_t>::max();
    if (program.stage == TechnicalDevelopmentStage::PrototypeTesting) {
        identitiesAvailable &= state.ids.nextTechnicalTestId < std::numeric_limits<std::int64_t>::max();
        if (finalStep)
            identitiesAvailable &=
                state.ids.nextMeasurementProfileId < std::numeric_limits<std::int64_t>::max() &&
                state.ids.nextShipComponentId < std::numeric_limits<std::int64_t>::max() &&
                state.ids.nextDevelopedComponentRevisionId < std::numeric_limits<std::int64_t>::max() &&
                state.date.day < std::numeric_limits<std::int64_t>::max();
    }
    if (program.stage == TechnicalDevelopmentStage::ProductionQualification && finalStep)
        identitiesAvailable &=
            state.ids.nextComponentProductionCapabilityId < std::numeric_limits<std::int64_t>::max() &&
            state.date.day < std::numeric_limits<std::int64_t>::max();
    if (program.stage == TechnicalDevelopmentStage::SupportQualification && finalStep)
        identitiesAvailable &=
            state.ids.nextSupportQualificationId < std::numeric_limits<std::int64_t>::max() &&
            state.date.day < std::numeric_limits<std::int64_t>::max();
    if (!identitiesAvailable) {
        program.leasedTeamId.reset();
        program.issue = {"technical/numeric", "Technical-development identity/date limit reached", false};
        if (hooks.prepareEvents)
            hooks.prepareEvents(1);
        audit(program, TechnicalDevelopmentAuditKind::IssueRaised, 0.0, program.issue.message, hooks);
        return;
    }
    if (!program.leasedTeamId) {
        if (opening.occupiedMaintenanceTeams.contains(readiness.teamId->value))
            return;
        program.leasedTeamId = *readiness.teamId;
        opening.occupiedMaintenanceTeams.insert(readiness.teamId->value);
        audit(program, TechnicalDevelopmentAuditKind::LeaseAcquired, 0.0,
              "Engineering team acquired for executable technical work", hooks);
    }
    const auto* colony = find(state.colonies, program.charter.developmentColonyId);
    if (!colony)
        throw std::logic_error("Technical-development colony disappeared");
    auto* mutableColony = find(state.colonies, program.charter.developmentColonyId);
    for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
        const double amount = readiness.consumed.amount[index];
        if (!represents(mutableColony->processedStockpile.amount[index],
                        mutableColony->processedStockpile.amount[index] - amount, amount) ||
            !represents(program.stageConsumed.amount[index], program.stageConsumed.amount[index] + amount,
                        amount))
            throw std::runtime_error("Technical-development material delta is not representable");
    }
    if (!represents(program.stageWork, program.stageWork + readiness.work, readiness.work))
        throw std::runtime_error("Technical-development work delta is not representable");

    state.eventLog.reserve(state.eventLog.size() + 8);
    state.prototypeDesigns.reserve(state.prototypeDesigns.size() + 1);
    state.prototypeComponentUnits.reserve(state.prototypeComponentUnits.size() + 1);
    state.technicalTestRecords.reserve(state.technicalTestRecords.size() + 1);
    state.measurementProfiles.reserve(state.measurementProfiles.size() + 1);
    state.shipComponents.reserve(state.shipComponents.size() + 1);
    state.developedComponentRevisions.reserve(state.developedComponentRevisions.size() + 1);
    state.componentProductionCapabilities.reserve(state.componentProductionCapabilities.size() + 1);
    state.supportQualificationRecords.reserve(state.supportQualificationRecords.size() + 1);
    if (program.stage == TechnicalDevelopmentStage::SupportQualification && finalStep)
        if (auto* team = find(state.maintenanceTeams, *readiness.teamId))
            team->qualifiedFamilies.reserve(team->qualifiedFamilies.size() + 1);
    program.receipts.reserve(program.receipts.size() + 1);
    if (hooks.prepareEvents)
        hooks.prepareEvents(8);
    for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
        const double amount = readiness.consumed.amount[index];
        mutableColony->processedStockpile.amount[index] -= amount;
        program.stageConsumed.amount[index] += amount;
        opening.debit(program.charter.developmentColonyId, static_cast<ProcessedMaterial>(index), amount);
    }
    const double priorWork = program.stageWork;
    program.stageWork += readiness.work;
    opening.debitTechnicalFacility(*readiness.facilityId, readiness.work);
    program.receipts.push_back(TechnicalWorkReceipt{.sequence = static_cast<int>(program.receipts.size() + 1),
                                                    .day = state.date.day,
                                                    .charterRevision = program.charterRevision,
                                                    .stage = program.stage,
                                                    .facilityId = *readiness.facilityId,
                                                    .teamId = *readiness.teamId,
                                                    .leaderId = *program.charter.requestedLeaderId,
                                                    .work = readiness.work,
                                                    .consumed = readiness.consumed});
    program.issue = {};
    audit(program, TechnicalDevelopmentAuditKind::WorkPerformed, readiness.work,
          "Actual engineering/facility work consumed proportional development material", hooks);

    if (program.stage == TechnicalDevelopmentStage::PrototypeTesting) {
        const int oldTests = static_cast<int>(std::floor(priorWork + 1e-9));
        const int newTests = static_cast<int>(std::floor(program.stageWork + 1e-9));
        for (int test = oldTests; test < newTests; ++test)
            publishTestAndMaybeDemonstration(state, program, hooks);
    }
    const double required = technicalStageRequiredWork(program.stage);
    if (program.stageWork + 1e-9 < required)
        return;
    switch (program.stage) {
    case TechnicalDevelopmentStage::ConceptEngineering:
        publishDesign(state, program, hooks);
        break;
    case TechnicalDevelopmentStage::PrototypeFabrication:
        publishPrototype(state, program, hooks);
        break;
    case TechnicalDevelopmentStage::PrototypeTesting:
        break;
    case TechnicalDevelopmentStage::ProductionQualification:
        publishProductionCapability(state, program, hooks);
        break;
    case TechnicalDevelopmentStage::SupportQualification:
        publishSupportQualification(state, program, hooks);
        break;
    case TechnicalDevelopmentStage::Complete:
        break;
    }
    program.stage =
        firstMissingTechnicalStage(state, program.charter.opportunityId, program.charter.developmentColonyId,
                                   program.charter.requestedTeamId, program.charter.scope);
    program.stageWork = 0.0;
    program.stageConsumed = {};
    program.leasedTeamId.reset();
}

void finishTechnicalDevelopmentDay(GameState& state, const TechnicalDevelopmentExecutionHooks& hooks) {
    // Support records are created during the opening but become actual service
    // qualifications only after every program had its opening action.
    for (const auto& record : state.supportQualificationRecords) {
        if (record.qualifiedDay != state.date.day)
            continue;
        auto* team = find(state.maintenanceTeams, record.teamId);
        if (team && std::find(team->qualifiedFamilies.begin(), team->qualifiedFamilies.end(),
                              record.familyId) == team->qualifiedFamilies.end())
            team->qualifiedFamilies.push_back(record.familyId);
    }
    for (auto& program : state.technicalDevelopmentPrograms) {
        if (program.lifecycle == TechnicalDevelopmentLifecycle::Closed ||
            program.nextReportDay != state.date.day)
            continue;
        TechnicalDevelopmentReport report;
        report.startDay = program.reportStartDay;
        report.endDay = state.date.day;
        report.isNinetyDayReview = state.date.day % 90 == 0;
        report.charterRevision = program.charterRevision;
        report.scope = program.charter.scope;
        report.stage = program.stage;
        report.facilityId = program.charter.requestedFacilityId;
        report.teamId = program.leasedTeamId ? program.leasedTeamId : program.charter.requestedTeamId;
        report.leaderId = program.charter.requestedLeaderId;
        report.waitingReason = technicalDevelopmentCondition(state, program);
        for (const auto& receipt : program.receipts) {
            report.lifetimeWork += receipt.work;
            report.lifetimeConsumed.addSet(receipt.consumed);
            if (receipt.day > report.startDay && receipt.day <= report.endDay) {
                report.periodWork += receipt.work;
                report.periodConsumed.addSet(receipt.consumed);
            }
        }
        if (const auto* prototype = prototypeFor(state, program.charter.opportunityId))
            report.prototypeId = prototype->id;
        for (const auto& test : state.technicalTestRecords)
            if (test.opportunityId == program.charter.opportunityId) {
                ++report.testCount;
                report.demonstratedThreshold = test.measuredDetectionThreshold;
            }
        if (const auto* developed = developedRevisionForOpportunity(state, program.charter.opportunityId)) {
            report.componentId = developed->componentId;
            report.localProductionReady = serialProductionAvailable(
                state, developed->componentId, program.charter.developmentColonyId, state.date.day);
        }
        const auto* opportunity = find(state.technologyOpportunities, program.charter.opportunityId);
        const auto* baseline =
            opportunity ? find(state.shipComponents, opportunity->baselineComponentId) : nullptr;
        if (program.charter.requestedTeamId && baseline && baseline->serviceProfile) {
            report.supportQualified = teamHasEffectiveSupportQualification(
                state, *program.charter.requestedTeamId, baseline->serviceProfile->familyId, state.date.day);
        }
        report.auditThroughId = state.ids.nextEventId - 1;
        program.reports.reserve(program.reports.size() + 1);
        if (hooks.prepareEvents)
            hooks.prepareEvents(1);
        program.reports.push_back(std::move(report));
        program.reportStartDay = state.date.day;
        program.nextReportDay += 30;
        audit(program, TechnicalDevelopmentAuditKind::ReportPublished, 0.0,
              "Technical-development report published", hooks);
    }
}

} // namespace deep
