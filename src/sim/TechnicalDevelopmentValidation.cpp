// Reconstructs P5 results from paid work and test evidence. Validation rejects
// silent repairs, duplicated prototypes, globalized processes, and inconsistent
// shipyard prototype reservations.
#include "sim/TechnicalDevelopmentValidation.h"
#include "sim/TechnicalDevelopmentRules.h"
#include "sim/TechnicalShipyardRules.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace deep {
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool finiteNonnegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}
bool near(double a, double b) {
    return equipmentNearlyEqual(a, b);
}
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
void validMaterials(const ProcessedMaterialSet& set, const char* message) {
    for (double value : set.amount)
        require(finiteNonnegative(value), message);
}
const TechnologyOpportunity* opportunity(const GameState& state, TechnologyOpportunityId id) {
    return find(state.technologyOpportunities, id);
}
double workForStage(const TechnicalDevelopmentProgram& program, TechnicalDevelopmentStage stage) {
    double result = 0.0;
    for (const auto& receipt : program.receipts)
        if (receipt.stage == stage)
            result += receipt.work;
    return result;
}
const TechnicalWorkReceipt* firstStageReceipt(const TechnicalDevelopmentProgram& program,
                                              TechnicalDevelopmentStage stage) {
    const auto it = std::find_if(program.receipts.begin(), program.receipts.end(), [=](const auto& receipt) {
        return receipt.stage == stage && receipt.work > 0.0;
    });
    return it == program.receipts.end() ? nullptr : &*it;
}
const TechnicalWorkReceipt* lastStageReceipt(const TechnicalDevelopmentProgram& program,
                                             TechnicalDevelopmentStage stage) {
    const auto it =
        std::find_if(program.receipts.rbegin(), program.receipts.rend(),
                     [=](const auto& receipt) { return receipt.stage == stage && receipt.work > 0.0; });
    return it == program.receipts.rend() ? nullptr : &*it;
}
} // namespace

void validateTechnicalDevelopmentState(const GameState& state) {
    std::unordered_set<std::int64_t> opportunities;
    for (const auto& row : state.technologyOpportunities) {
        const auto* baseline = find(state.shipComponents, row.baselineComponentId);
        require(row.id.value > 0 && opportunities.insert(row.id.value).second,
                "Technology opportunity identity is invalid or duplicate");
        require(!row.name.empty() && !row.objective.empty() && !row.knownTradeoff.empty(),
                "Technology opportunity public text is incomplete");
        require(baseline, "Technology opportunity baseline component is missing");
        require(std::isfinite(row.targetDetectionThreshold) && row.targetDetectionThreshold > 0.0,
                "Technology opportunity target must be finite and positive");
        const auto* profile = baseline && baseline->measurementProfileId
                                  ? find(state.measurementProfiles, *baseline->measurementProfileId)
                                  : nullptr;
        require(baseline && profile && baseline->kind == ShipComponentKind::SurveySensor &&
                    baseline->serviceProfile && baseline->measurementProfileId == profile->id,
                "Technology opportunity comparator lost its established capability records");
    }
    std::unordered_set<std::int64_t> truths;
    for (const auto& row : state.technologyCandidateTruths) {
        require(opportunity(state, row.opportunityId) && truths.insert(row.opportunityId.value).second,
                "Candidate truth must map one-to-one to a public opportunity");
        require(std::isfinite(row.achievedDetectionThreshold) && row.achievedDetectionThreshold > 0.0,
                "Candidate truth threshold must be finite and positive");
    }
    require(truths.size() == state.technologyOpportunities.size(),
            "Every technology opportunity requires exactly one candidate truth");

    for (const auto& facility : state.technicalFacilities) {
        require(facility.id.value > 0 && find(state.colonies, facility.colonyId) && !facility.name.empty(),
                "Technical facility identity, colony, or name is invalid");
        require(facility.capability == TechnicalFacilityCapability::PrototypeInstrumentation &&
                    finiteNonnegative(facility.engineeringWorkdaysPerDay),
                "Technical facility capability/rate is invalid");
    }
    for (const auto& team : state.maintenanceTeams) {
        std::set<EngineeringQualification> unique;
        for (auto qualification : team.engineeringQualifications)
            require(qualification == EngineeringQualification::PrototypeInstrumentation &&
                        unique.insert(qualification).second,
                    "Engineering qualification is invalid or duplicate");
    }

    std::unordered_set<std::int64_t> activeOpportunities;
    // A complete test is one integer crossing of THIS program's paid testing
    // work. Reused records reduce the later program's obligation, but a canceled
    // fractional remainder can never create a crossing in another program.
    std::map<std::int64_t, std::vector<const TechnicalWorkReceipt*>> testWorkBoundaries;
    for (const auto& program : state.technicalDevelopmentPrograms) {
        require(program.id.value > 0 && !validateTechnicalDevelopmentCharter(state, program.charter, true),
                "Technical-development program charter is invalid");
        require(program.createdDay >= 0 && program.createdDay <= state.date.day &&
                    program.charterRevision > 0,
                "Technical-development creation/revision is invalid");
        require(program.lifecycle >= TechnicalDevelopmentLifecycle::Authorized &&
                    program.lifecycle <= TechnicalDevelopmentLifecycle::Closed &&
                    program.closure >= TechnicalDevelopmentClosure::None &&
                    program.closure <= TechnicalDevelopmentClosure::Cancelled &&
                    program.stage >= TechnicalDevelopmentStage::ConceptEngineering &&
                    program.stage <= TechnicalDevelopmentStage::Complete,
                "Technical-development lifecycle/stage tag is invalid");
        if (program.lifecycle != TechnicalDevelopmentLifecycle::Closed)
            require(activeOpportunities.insert(program.charter.opportunityId.value).second,
                    "Two active technical programs own one opportunity");
        require((program.lifecycle == TechnicalDevelopmentLifecycle::Closed) == program.closedDay.has_value(),
                "Technical-development closed date disagrees with lifecycle");
        if (program.closedDay)
            require(*program.closedDay >= program.createdDay && *program.closedDay <= state.date.day &&
                        program.closure != TechnicalDevelopmentClosure::None && !program.leasedTeamId,
                    "Closed technical development retains lease or invalid closure");
        if (program.leasedTeamId)
            require(find(state.maintenanceTeams, *program.leasedTeamId) &&
                        program.lifecycle == TechnicalDevelopmentLifecycle::Authorized,
                    "Technical-development team lease is invalid");
        require(finiteNonnegative(program.stageWork) &&
                    program.stageWork <= technicalProgramStageRequiredWork(state, program) + 1e-9,
                "Technical-development current-stage work is invalid");
        validMaterials(program.stageConsumed, "Technical-development current-stage material is invalid");
        std::map<TechnicalDevelopmentStage, double> work;
        std::map<TechnicalDevelopmentStage, ProcessedMaterialSet> consumed;
        int sequence = 1;
        std::int64_t priorDay = program.createdDay;
        TechnicalDevelopmentStage priorStage = TechnicalDevelopmentStage::ConceptEngineering;
        std::optional<TechnicalFacilityId> productionFacility;
        std::optional<MaintenanceTeamId> supportTeam;
        const int inheritedTests = inheritedTechnicalTestCount(state, program);
        require(inheritedTests >= 0 && inheritedTests <= 3,
                "Technical program inherited invalid completed test evidence");
        for (const auto& receipt : program.receipts) {
            require(receipt.sequence == sequence++ && receipt.day > priorDay && receipt.day <= state.date.day &&
                        (!program.closedDay || receipt.day <= *program.closedDay),
                    "Technical work receipt sequence/date is invalid");
            priorDay = receipt.day;
            require(receipt.charterRevision > 0 && receipt.charterRevision <= program.charterRevision &&
                        receipt.stage >= TechnicalDevelopmentStage::ConceptEngineering &&
                        receipt.stage < TechnicalDevelopmentStage::Complete && receipt.work > 0.0 &&
                        std::isfinite(receipt.work),
                    "Technical work receipt stage/work/revision is invalid");
            require(receipt.stage >= priorStage,
                    "Technical work receipt stages must be nondecreasing within one program");
            priorStage = receipt.stage;
            const auto* facility = find(state.technicalFacilities, receipt.facilityId);
            require(facility && facility->colonyId == program.charter.developmentColonyId &&
                        find(state.maintenanceTeams, receipt.teamId) && find(state.people, receipt.leaderId),
                    "Technical work receipt participant is missing or outside its fixed colony");
            // Only the qualification-specific participant is pinned. Future
            // requested IDs and leaders may change without rewriting evidence.
            if (receipt.stage == TechnicalDevelopmentStage::ProductionQualification) {
                if (!productionFacility)
                    productionFacility = receipt.facilityId;
                require(*productionFacility == receipt.facilityId,
                        "Production qualification combines work from different facilities");
            }
            if (receipt.stage == TechnicalDevelopmentStage::SupportQualification) {
                if (!supportTeam)
                    supportTeam = receipt.teamId;
                require(*supportTeam == receipt.teamId,
                        "Support qualification combines work from different physical teams");
            }
            validMaterials(receipt.consumed, "Technical work receipt material is invalid");
            const auto expectedCost = technicalStageCost(receipt.stage);
            const double expectedWork = technicalStageRequiredWork(receipt.stage);
            for (std::size_t material = 0; material < processedMaterialCount(); ++material)
                require(near(receipt.consumed.amount[material],
                             expectedCost.amount[material] * receipt.work / expectedWork),
                        "Technical work receipt material is not proportional to positive work");
            const double before = work[receipt.stage];
            work[receipt.stage] += receipt.work;
            const double required = receipt.stage == TechnicalDevelopmentStage::PrototypeTesting
                                        ? kPrototypeTestingWork - inheritedTests
                                        : expectedWork;
            require(finiteNonnegative(work[receipt.stage]) && work[receipt.stage] <= required + 1e-9,
                    "Technical work receipts exceed this program's stage obligation");
            if (receipt.stage == TechnicalDevelopmentStage::PrototypeTesting) {
                // The bounded sums above make conversion safe. A high-throughput
                // opening may earn several tests from the same real receipt.
                const int oldCount = static_cast<int>(std::floor(before + 1e-9));
                const int newCount = static_cast<int>(std::floor(work[receipt.stage] + 1e-9));
                for (int count = oldCount; count < newCount; ++count)
                    testWorkBoundaries[program.id.value].push_back(&receipt);
            }
            consumed[receipt.stage].addSet(receipt.consumed);
        }
        if (program.stage == TechnicalDevelopmentStage::SupportQualification && program.leasedTeamId &&
            supportTeam)
            require(program.leasedTeamId == supportTeam,
                    "Support qualification lease differs from its pinned physical team");
        if (program.stage != TechnicalDevelopmentStage::Complete) {
            require(near(work[program.stage], program.stageWork),
                    "Technical current-stage work does not reconcile to receipts");
            for (std::size_t index = 0; index < processedMaterialCount(); ++index)
                require(near(consumed[program.stage].amount[index], program.stageConsumed.amount[index]),
                        "Technical current-stage material does not reconcile to receipts");
        }
        require(program.reportStartDay >= program.createdDay && program.reportStartDay <= state.date.day &&
                    (program.lifecycle == TechnicalDevelopmentLifecycle::Closed ||
                     program.nextReportDay > program.reportStartDay),
                "Technical-development report cursor is invalid");
        std::int64_t reportBoundary = program.createdDay;
        std::map<int, TechnicalDevelopmentScope> scopeByRevision;
        for (const auto& report : program.reports) {
            require(report.startDay == reportBoundary && report.endDay > report.startDay &&
                        report.endDay <= state.date.day && report.charterRevision > 0 &&
                        report.charterRevision <= program.charterRevision &&
                        report.scope >= TechnicalDevelopmentScope::DemonstratePrototype &&
                        report.scope <= TechnicalDevelopmentScope::ProductionAndSupportReady &&
                        report.stage >= TechnicalDevelopmentStage::ConceptEngineering &&
                        report.stage <= TechnicalDevelopmentStage::Complete &&
                        finiteNonnegative(report.periodWork) && finiteNonnegative(report.lifetimeWork) &&
                        report.periodWork <= report.lifetimeWork + 1e-9 && report.testCount >= 0 &&
                        report.auditThroughId >= 0 && report.auditThroughId < state.ids.nextEventId,
                    "Technical-development report chronology/numbers are invalid");
            reportBoundary = report.endDay;
            validMaterials(report.periodConsumed, "Technical report period material is invalid");
            validMaterials(report.lifetimeConsumed, "Technical report lifetime material is invalid");
            if (report.facilityId)
                require(find(state.technicalFacilities, *report.facilityId),
                        "Technical report facility is missing");
            if (report.teamId)
                require(find(state.maintenanceTeams, *report.teamId), "Technical report team is missing");
            if (report.leaderId)
                require(find(state.people, *report.leaderId), "Technical report leader is missing");
            if (report.prototypeId)
                require(find(state.prototypeComponentUnits, *report.prototypeId),
                        "Technical report prototype is missing");
            if (report.componentId)
                require(find(state.shipComponents, *report.componentId),
                        "Technical report component is missing");
            auto [scope, inserted] = scopeByRevision.emplace(report.charterRevision, report.scope);
            require(inserted || scope->second == report.scope,
                    "Technical reports disagree on scope for one charter revision");
            double periodWork = 0.0, lifetimeWork = 0.0;
            ProcessedMaterialSet periodMaterials, lifetimeMaterials;
            for (const auto& receipt : program.receipts) {
                if (receipt.day <= report.endDay) {
                    lifetimeWork += receipt.work;
                    lifetimeMaterials.addSet(receipt.consumed);
                }
                if (receipt.day > report.startDay && receipt.day <= report.endDay) {
                    periodWork += receipt.work;
                    periodMaterials.addSet(receipt.consumed);
                }
            }
            require(near(report.periodWork, periodWork) && near(report.lifetimeWork, lifetimeWork),
                    "Technical report work does not reconcile to dated receipts");
            for (std::size_t material = 0; material < processedMaterialCount(); ++material)
                require(
                    near(report.periodConsumed.amount[material], periodMaterials.amount[material]) &&
                        near(report.lifetimeConsumed.amount[material], lifetimeMaterials.amount[material]),
                    "Technical report material does not reconcile to dated receipts");
        }
        require(reportBoundary == program.reportStartDay,
                "Technical-development report boundary disagrees with report history");
    }

    std::unordered_set<std::int64_t> designOpportunities;
    for (const auto& design : state.prototypeDesigns) {
        const auto* designProgram = find(state.technicalDevelopmentPrograms, design.programId);
        require(design.id.value > 0 && opportunity(state, design.opportunityId) && designProgram &&
                    designOpportunities.insert(design.opportunityId.value).second,
                "Prototype design provenance is invalid or duplicate");
        require(designProgram->charter.opportunityId == design.opportunityId,
                "Prototype design program/opportunity provenance disagrees");
        require(design.createdDay >= 0 && design.createdDay <= state.date.day && !design.name.empty() &&
                    near(design.mass, 35.0) && near(design.volume, 100.0) && near(design.powerDemand, 55.0) &&
                    near(design.surveyCapability, 1.0) && near(design.componentBuildPoints, 90.0),
                "Prototype design differs from the completed concept result");
        require(near(workForStage(*designProgram, TechnicalDevelopmentStage::ConceptEngineering),
                     kConceptEngineeringWork),
                "Prototype design was not earned by complete concept work");
        validMaterials(design.serialBuildCost, "Prototype design build cost is invalid");
        require(find(state.equipmentFamilies, design.serviceProfile.familyId) &&
                    std::isfinite(design.serviceProfile.dutyCapacity) &&
                    design.serviceProfile.dutyCapacity > 0.0 &&
                    std::isfinite(design.serviceProfile.teamWorkdaysPerDuty) &&
                    design.serviceProfile.teamWorkdaysPerDuty > 0.0,
                "Prototype design service profile is invalid");
        validMaterials(design.serviceProfile.materialsPerDuty,
                       "Prototype design service material is invalid");
    }

    std::unordered_set<std::int64_t> prototypeOpportunities;
    for (const auto& unit : state.prototypeComponentUnits) {
        const auto* fabricationProgram = find(state.technicalDevelopmentPrograms, unit.programId);
        require(unit.id.value > 0 && opportunity(state, unit.opportunityId) &&
                    find(state.prototypeDesigns, unit.designId) && fabricationProgram &&
                    find(state.colonies, unit.colonyId) &&
                    prototypeOpportunities.insert(unit.opportunityId.value).second,
                "Prototype unit provenance is invalid or duplicated");
        require(fabricationProgram->charter.opportunityId == unit.opportunityId &&
                    fabricationProgram->charter.developmentColonyId == unit.colonyId,
                "Prototype fabrication program/opportunity/location disagrees");
        require(unit.fabricationDay >= 0 && unit.fabricationDay <= state.date.day &&
                    unit.state >= PrototypeComponentState::Available &&
                    unit.state <= PrototypeComponentState::Consumed,
                "Prototype unit date/state is invalid");
        require(near(workForStage(*fabricationProgram, TechnicalDevelopmentStage::PrototypeFabrication),
                     kPrototypeFabricationWork),
                "Prototype unit was not earned by complete fabrication work");
        require(unit.componentId.has_value() == unit.availableDay.has_value(),
                "Prototype demonstration identity/date are incomplete");
        if (unit.availableDay)
            require(*unit.availableDay > unit.fabricationDay,
                    "Prototype cannot become usable on its fabrication day");
        if (unit.state == PrototypeComponentState::Available)
            require(!unit.reservedOrderId && !unit.reservedHullNumber && !unit.consumedShipId,
                    "Available prototype retains reservation/consumption identity");
        if (unit.state == PrototypeComponentState::ReservedForShipyard)
            require(unit.reservedOrderId && unit.reservedHullNumber && !unit.consumedShipId,
                    "Reserved prototype identity is incomplete");
        if (unit.state == PrototypeComponentState::Consumed)
            require(!unit.reservedOrderId && !unit.reservedHullNumber && unit.consumedShipId &&
                        find(state.ships, *unit.consumedShipId),
                    "Consumed prototype lacks its produced ship");
    }

    std::map<std::int64_t, std::vector<const TechnicalTestRecord*>> testsByPrototype;
    std::map<std::int64_t, std::size_t> testsByProgram;
    for (const auto& test : state.technicalTestRecords) {
        const auto* unit = find(state.prototypeComponentUnits, test.prototypeId);
        require(test.id.value > 0 && unit && test.opportunityId == unit->opportunityId &&
                    find(state.technicalDevelopmentPrograms, test.programId) &&
                    find(state.technicalFacilities, test.facilityId) &&
                    find(state.maintenanceTeams, test.teamId) && find(state.people, test.leaderId),
                "Technical test provenance is invalid");
        const auto* testingProgram = find(state.technicalDevelopmentPrograms, test.programId);
        require(testingProgram && testingProgram->charter.opportunityId == test.opportunityId &&
                    testingProgram->charter.developmentColonyId ==
                        find(state.technicalFacilities, test.facilityId)->colonyId &&
                    testingProgram->charter.developmentColonyId == unit->colonyId,
                "Technical test opportunity or physical prototype locality is inconsistent");
        require(test.sequence > 0 && test.sequence <= 3 && test.day > unit->fabricationDay &&
                    test.day <= state.date.day && std::isfinite(test.measuredDetectionThreshold) &&
                    test.measuredDetectionThreshold > 0.0 &&
                    near(test.targetDetectionThreshold,
                         opportunity(state, test.opportunityId)->targetDetectionThreshold) &&
                    test.meetsTarget == (test.measuredDetectionThreshold <= test.targetDetectionThreshold),
                "Technical test result/date/target is invalid");
        const int localSequence = test.sequence - inheritedTechnicalTestCount(state, *testingProgram);
        const auto& boundaries = testWorkBoundaries[testingProgram->id.value];
        require(localSequence > 0 && static_cast<std::size_t>(localSequence) <= boundaries.size(),
                "Technical test has no complete newly paid test-work boundary");
        const auto* receipt = boundaries[static_cast<std::size_t>(localSequence - 1)];
        require(receipt->day == test.day && receipt->facilityId == test.facilityId &&
                    receipt->teamId == test.teamId && receipt->leaderId == test.leaderId,
                "Technical test differs from its historical complete-work receipt");
        ++testsByProgram[test.programId.value];
        testsByPrototype[test.prototypeId.value].push_back(&test);
    }
    for (const auto& [programId, boundaries] : testWorkBoundaries)
        require(testsByProgram[programId] == boundaries.size(),
                "Completed technical test work lost or duplicated its acquired evidence");
    for (auto& [prototype, tests] : testsByPrototype) {
        std::sort(tests.begin(), tests.end(),
                  [](const auto* a, const auto* b) { return a->sequence < b->sequence; });
        for (std::size_t index = 0; index < tests.size(); ++index)
            require(tests[index]->sequence == static_cast<int>(index + 1) &&
                        (index == 0 || tests[index]->day >= tests[index - 1]->day),
                    "Technical test sequence or physical chronology is not contiguous");
        require(tests.size() <= 3, "Prototype has more than three reference tests");
        for (const auto* test : tests)
            require(near(test->measuredDetectionThreshold, tests.front()->measuredDetectionThreshold),
                    "Repeated technical tests changed deterministic achieved performance");
    }

    std::unordered_set<std::int64_t> developedOpportunities;
    for (const auto& developed : state.developedComponentRevisions) {
        const auto* unit = find(state.prototypeComponentUnits, developed.prototypeId);
        const auto* design = find(state.prototypeDesigns, developed.designId);
        const auto* profile = find(state.measurementProfiles, developed.measurementProfileId);
        const auto* component = find(state.shipComponents, developed.componentId);
        require(developed.id.value > 0 && unit && design && profile && component &&
                    developed.opportunityId == unit->opportunityId &&
                    developed.opportunityId == design->opportunityId && unit->designId == design->id &&
                    developedOpportunities.insert(developed.opportunityId.value).second,
                "Developed component provenance is invalid or duplicate");
        require(developed.testIds.size() == 3 && testsByPrototype[developed.prototypeId.value].size() == 3 &&
                    developed.demonstratedDay == testsByPrototype[developed.prototypeId.value].back()->day &&
                    developed.demonstratedDay < std::numeric_limits<std::int64_t>::max() &&
                    developed.availableDay == developed.demonstratedDay + 1,
                "Developed component test/date evidence is incomplete");
        // Exactly three independently paid boundaries establish one prototype.
        // Their programs may differ because cancellation preserves full tests.
        for (std::size_t index = 0; index < developed.testIds.size(); ++index)
            require(developed.testIds[index] == testsByPrototype[developed.prototypeId.value][index]->id,
                    "Developed component test-ID provenance is incomplete or reordered");
        const double achieved =
            testsByPrototype[developed.prototypeId.value].front()->measuredDetectionThreshold;
        const auto* publicOpportunity = opportunity(state, developed.opportunityId);
        const auto* baselineComponent =
            publicOpportunity ? find(state.shipComponents, publicOpportunity->baselineComponentId) : nullptr;
        const auto* baselineProfile =
            baselineComponent && baselineComponent->measurementProfileId
                ? find(state.measurementProfiles, *baselineComponent->measurementProfileId)
                : nullptr;
        require(profile->name == "Precision Characterization" && profile->methodVersion == 1 &&
                    near(profile->detectionThreshold, achieved) && profile->measuresAccessibility &&
                    baselineProfile && profile->channels == baselineProfile->channels,
                "Developed measurement profile differs from test evidence");
        require(component->measurementProfileId == profile->id &&
                    component->kind == ShipComponentKind::SurveySensor &&
                    near(component->mass, design->mass) && near(component->volume, design->volume) &&
                    near(component->powerDemand, design->powerDemand) &&
                    near(component->surveyCapability, design->surveyCapability) &&
                    near(component->buildPoints, design->componentBuildPoints),
                "Developed component differs from frozen design/test evidence");
        require(unit->componentId == component->id && unit->availableDay == developed.availableDay,
                "Prototype unit was not assigned the demonstrated component/date");
    }
    for (const auto& [prototypeId, tests] : testsByPrototype)
        if (tests.size() == 3)
            require(std::any_of(
                        state.developedComponentRevisions.begin(), state.developedComponentRevisions.end(),
                        [=](const auto& developed) { return developed.prototypeId.value == prototypeId; }),
                    "Three completed prototype tests lack their single demonstrated revision");

    std::set<std::pair<std::int64_t, std::int64_t>> processLocations;
    for (const auto& process : state.componentProductionCapabilities) {
        const auto* developed = developedRevisionForComponent(state, process.componentId);
        const auto* facility = find(state.technicalFacilities, process.facilityId);
        const auto* qualifyingProgram = find(state.technicalDevelopmentPrograms, process.qualifyingProgramId);
        require(process.id.value > 0 && developed && process.opportunityId == developed->opportunityId &&
                    facility && facility->colonyId == process.colonyId && qualifyingProgram &&
                    processLocations.emplace(process.componentId.value, process.colonyId.value).second &&
                    process.qualifiedDay >= developed->demonstratedDay &&
                    process.qualifiedDay <= state.date.day &&
                    process.qualifiedDay < std::numeric_limits<std::int64_t>::max() &&
                    process.availableDay == process.qualifiedDay + 1,
                "Local component-production capability is invalid or duplicate");
        require(qualifyingProgram->charter.opportunityId == process.opportunityId &&
                    qualifyingProgram->charter.developmentColonyId == process.colonyId,
                "Production capability differs from its qualifying local charter");
        const auto* first =
            firstStageReceipt(*qualifyingProgram, TechnicalDevelopmentStage::ProductionQualification);
        const auto* last =
            lastStageReceipt(*qualifyingProgram, TechnicalDevelopmentStage::ProductionQualification);
        require(first && last && first->facilityId == process.facilityId && last->day == process.qualifiedDay,
                "Production capability differs from its pinned facility or final paid-work date");
        require(near(workForStage(*qualifyingProgram, TechnicalDevelopmentStage::ProductionQualification),
                     kProductionQualificationWork),
                "Production capability was not earned by complete local process work");
    }
    std::set<std::pair<std::int64_t, std::int64_t>> supportPairs;
    for (const auto& support : state.supportQualificationRecords) {
        const auto* team = find(state.maintenanceTeams, support.teamId);
        const auto* qualifyingProgram = find(state.technicalDevelopmentPrograms, support.programId);
        const auto* publicOpportunity = opportunity(state, support.opportunityId);
        const auto* baseline =
            publicOpportunity ? find(state.shipComponents, publicOpportunity->baselineComponentId) : nullptr;
        require(support.id.value > 0 && opportunity(state, support.opportunityId) && team &&
                    find(state.equipmentFamilies, support.familyId) && qualifyingProgram &&
                    supportPairs.emplace(support.teamId.value, support.familyId.value).second &&
                    support.qualifiedDay >= 0 && support.qualifiedDay <= state.date.day &&
                    support.qualifiedDay < std::numeric_limits<std::int64_t>::max() &&
                    support.availableDay == support.qualifiedDay + 1,
                "Support qualification record is invalid or duplicate");
        require(qualifyingProgram->charter.opportunityId == support.opportunityId,
                "Support qualification differs from its program opportunity");
        const auto* first =
            firstStageReceipt(*qualifyingProgram, TechnicalDevelopmentStage::SupportQualification);
        const auto* last =
            lastStageReceipt(*qualifyingProgram, TechnicalDevelopmentStage::SupportQualification);
        require(first && last && first->teamId == support.teamId && last->day == support.qualifiedDay,
                "Support qualification differs from its exact trained team or final paid-work date");
        require(baseline && baseline->serviceProfile &&
                    support.familyId == baseline->serviceProfile->familyId,
                "Support qualification does not match the opportunity's specialist service family");
        require(near(workForStage(*qualifyingProgram, TechnicalDevelopmentStage::SupportQualification),
                     kSupportQualificationWork),
                "Support qualification was not earned by complete work");
        require(std::find(team->qualifiedFamilies.begin(), team->qualifiedFamilies.end(), support.familyId) !=
                    team->qualifiedFamilies.end(),
                "Support qualification does not agree with current team capability");
    }

    std::unordered_set<std::int64_t> reservedUnits;
    for (const auto& order : state.shipyardOrders) {
        const auto* orderClass = find(state.shipClasses, order.shipClassId);
        if (orderClass && classRequiresDevelopedComponent(state, *orderClass) &&
            order.status == ShipyardOrderStatus::Active && order.accumulatedBuildPoints > 0.0)
            require(order.currentHullSupplyPlan.has_value(),
                    "Developed-component hull progress lacks its frozen supply plan");
        if (!order.currentHullSupplyPlan)
            continue;
        const auto& plan = *order.currentHullSupplyPlan;
        const auto* shipClass = find(state.shipClasses, order.shipClassId);
        require(order.status == ShipyardOrderStatus::Active && shipClass &&
                    plan.shipClassId == order.shipClassId && plan.hullNumber == order.quantityCompleted + 1 &&
                    plan.boundDay <= state.date.day && plan.effectiveBuildPoints > 0.0 &&
                    std::isfinite(plan.effectiveBuildPoints),
                "Current-hull developed-component supply plan identity is invalid");
        const auto recomputed = evaluateShipDesign(state.shipComponents, shipClass->components);
        require(order.accumulatedBuildPoints <= plan.effectiveBuildPoints + 1e-9,
                "Shipyard progress exceeds frozen effective build work");
        ProcessedMaterialSet expectedCost = recomputed.buildCost;
        double expectedBuildPoints = recomputed.buildPoints;
        std::unordered_set<std::int64_t> suppliedComponents;
        for (const auto& supply : plan.developedComponents) {
            require(supply.quantity > 0 && developedRevisionForComponent(state, supply.componentId),
                    "Supply plan references a non-developed component");
            const auto install =
                std::find_if(shipClass->components.begin(), shipClass->components.end(),
                             [&](const auto& row) { return row.componentId == supply.componentId; });
            require(install != shipClass->components.end() && install->quantity == supply.quantity &&
                        suppliedComponents.insert(supply.componentId.value).second,
                    "Supply plan component/quantity differs from immutable class");
            if (supply.kind == DevelopedComponentSupplyKind::PrototypeUnit) {
                require(supply.prototypeUnits.size() == static_cast<std::size_t>(supply.quantity),
                        "Prototype supply-plan quantity is incomplete");
                for (auto id : supply.prototypeUnits) {
                    const auto* unit = find(state.prototypeComponentUnits, id);
                    require(unit && unit->state == PrototypeComponentState::ReservedForShipyard &&
                                unit->reservedOrderId == order.id &&
                                unit->reservedHullNumber == plan.hullNumber &&
                                reservedUnits.insert(id.value).second,
                            "Prototype is not uniquely reserved to its current hull");
                }
                const auto* component = find(state.shipComponents, supply.componentId);
                expectedBuildPoints -= component->buildPoints * supply.quantity;
                for (std::size_t material = 0; material < processedMaterialCount(); ++material)
                    expectedCost.amount[material] -= component->buildCost.amount[material] * supply.quantity;
            } else {
                require(serialProductionAvailable(state, supply.componentId, order.colonyId, plan.boundDay),
                        "Serial supply plan bound before local process availability");
            }
        }
        const auto developedInstalls = std::count_if(
            shipClass->components.begin(), shipClass->components.end(),
            [&](const auto& install) { return developedRevisionForComponent(state, install.componentId); });
        require(plan.developedComponents.size() == static_cast<std::size_t>(developedInstalls) &&
                    near(plan.effectiveBuildPoints, expectedBuildPoints),
                "Supply plan does not cover every developed component or has wrong effective BP");
        for (std::size_t material = 0; material < processedMaterialCount(); ++material)
            require(near(plan.effectiveBuildCost.amount[material], expectedCost.amount[material]),
                    "Supply plan effective material cost does not match prototype credit");
        require(recomputed.constructible, "Supply plan belongs to a nonconstructible class");
    }
    for (const auto& unit : state.prototypeComponentUnits)
        if (unit.state == PrototypeComponentState::ReservedForShipyard)
            require(reservedUnits.contains(unit.id.value),
                    "Reserved prototype is not present in an active current-hull plan");
    std::unordered_set<std::int64_t> integratedUnits;
    for (const auto& receipt : state.prototypeIntegrationReceipts) {
        const auto* unit = find(state.prototypeComponentUnits, receipt.prototypeId);
        const auto* order = find(state.shipyardOrders, receipt.orderId);
        const auto* ship = find(state.ships, receipt.shipId);
        const auto* shipClass = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        require(
            unit && unit->state == PrototypeComponentState::Consumed &&
                unit->consumedShipId == receipt.shipId && order && ship && shipClass &&
                order->shipClassId == ship->shipClassId && unit->componentId &&
                std::any_of(shipClass->components.begin(), shipClass->components.end(),
                            [&](const auto& install) { return install.componentId == *unit->componentId; }) &&
                integratedUnits.insert(unit->id.value).second && receipt.hullNumber > 0 &&
                receipt.hullNumber <= order->quantityCompleted && receipt.day >= 0 &&
                receipt.day <= state.date.day,
            "Prototype integration receipt is invalid");
    }
    for (const auto& unit : state.prototypeComponentUnits)
        if (unit.state == PrototypeComponentState::Consumed)
            require(integratedUnits.contains(unit.id.value),
                    "Consumed prototype lacks one exact ship integration receipt");
}

} // namespace deep
