// Prepared-statement mapping for every authoritative P5 record. Ordered vectors
// use explicit contiguous ordinals; reverse owners and opening budgets are absent.
#include "save/TechnicalPersistence.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace deep::save {
namespace {
template <class Rows, class Id> auto& parent(Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](auto& row) { return row.id == id; });
    if (it == rows.end())
        throw std::runtime_error("Technical persistence references missing parent");
    return *it;
}
int integer(std::int64_t value) {
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Technical persistence integer is outside supported range");
    return static_cast<int>(value);
}
bool boolean(std::int64_t value) {
    if (value != 0 && value != 1)
        throw std::runtime_error("Technical persistence boolean is invalid");
    return value != 0;
}
template <class Id> std::optional<Id> optionalId(Statement& statement, int column) {
    return statement.columnIsNull(column) ? std::nullopt
                                          : std::optional<Id>{Id{statement.columnInt64Strict(column)}};
}
std::optional<std::int64_t> optionalDay(Statement& statement, int column) {
    return statement.columnIsNull(column) ? std::nullopt : std::optional{statement.columnInt64Strict(column)};
}
std::optional<double> optionalNumber(Statement& statement, int column) {
    return statement.columnIsNull(column) ? std::nullopt
                                          : std::optional{statement.columnDoubleStrict(column)};
}
void bindOptional(Statement& statement, int column, auto value) {
    if (value)
        statement.bindInt64(column, value->value);
    else
        statement.bindNull(column);
}
void bindOptionalDay(Statement& statement, int column, const std::optional<std::int64_t>& value) {
    if (value)
        statement.bindInt64(column, *value);
    else
        statement.bindNull(column);
}
void bindOptionalNumber(Statement& statement, int column, const std::optional<double>& value) {
    if (value)
        statement.bindDouble(column, *value);
    else
        statement.bindNull(column);
}
void next(Statement& statement) {
    statement.execute();
    statement.reset();
    statement.clearBindings();
}
void requireOrdinal(Statement& statement, int column, std::size_t expected, const char* label) {
    if (statement.columnInt64Strict(column) != static_cast<std::int64_t>(expected))
        throw std::runtime_error(std::string{"Invalid/gapped ordinal in "} + label);
}
void saveMaterialSet(Statement& statement, std::int64_t parentId, int category,
                     const ProcessedMaterialSet& set) {
    for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
        statement.bindInt64(1, parentId);
        statement.bindInt64(2, category);
        statement.bindInt64(3, static_cast<std::int64_t>(material));
        statement.bindDouble(4, set.amount[material]);
        next(statement);
    }
}
ProcessedMaterialSet loadMaterialSet(Database& db, const char* table, const char* parentColumn,
                                     std::int64_t parentId, int category) {
    Statement statement(db, "SELECT material,amount FROM " + std::string{table} + " WHERE " + parentColumn +
                                "=? AND category=? ORDER BY material;");
    statement.bindInt64(1, parentId);
    statement.bindInt64(2, category);
    ProcessedMaterialSet result;
    std::size_t expected = 0;
    while (statement.step()) {
        requireOrdinal(statement, 0, expected, table);
        result.amount[expected++] = statement.columnDoubleStrict(1);
    }
    if (expected != processedMaterialCount())
        throw std::runtime_error(std::string{"Missing material channels in "} + table);
    return result;
}
} // namespace

void saveTechnicalState(Database& db, const GameState& state) {
    Statement opportunity(db, "INSERT INTO technology_opportunities VALUES(?,?,?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.technologyOpportunities.size(); ++ordinal) {
        const auto& row = state.technologyOpportunities[ordinal];
        opportunity.bindInt64(1, static_cast<std::int64_t>(ordinal));
        opportunity.bindInt64(2, row.id.value);
        opportunity.bindText(3, row.name);
        opportunity.bindInt64(4, row.baselineComponentId.value);
        opportunity.bindDouble(5, row.targetDetectionThreshold);
        opportunity.bindInt64(6, row.requiresAccessibility);
        opportunity.bindText(7, row.objective);
        opportunity.bindText(8, row.knownTradeoff);
        next(opportunity);
    }
    Statement truth(db, "INSERT INTO technology_candidate_truth VALUES(?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.technologyCandidateTruths.size(); ++ordinal) {
        const auto& row = state.technologyCandidateTruths[ordinal];
        truth.bindInt64(1, static_cast<std::int64_t>(ordinal));
        truth.bindInt64(2, row.opportunityId.value);
        truth.bindDouble(3, row.achievedDetectionThreshold);
        next(truth);
    }
    Statement facility(db, "INSERT INTO technical_facilities VALUES(?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.technicalFacilities.size(); ++ordinal) {
        const auto& row = state.technicalFacilities[ordinal];
        facility.bindInt64(1, static_cast<std::int64_t>(ordinal));
        facility.bindInt64(2, row.id.value);
        facility.bindInt64(3, row.colonyId.value);
        facility.bindText(4, row.name);
        facility.bindDouble(5, row.engineeringWorkdaysPerDay);
        facility.bindInt64(6, static_cast<std::int64_t>(row.capability));
        next(facility);
    }
    Statement engineering(db, "INSERT INTO maintenance_team_engineering_qualifications VALUES(?,?,?);");
    for (const auto& team : state.maintenanceTeams)
        for (std::size_t ordinal = 0; ordinal < team.engineeringQualifications.size(); ++ordinal) {
            engineering.bindInt64(1, team.id.value);
            engineering.bindInt64(2, static_cast<std::int64_t>(ordinal));
            engineering.bindInt64(3, static_cast<std::int64_t>(team.engineeringQualifications[ordinal]));
            next(engineering);
        }

    Statement program(
        db,
        "INSERT INTO technical_development_programs VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement programMaterial(db, "INSERT INTO technical_program_materials VALUES(?,?,?,?);");
    Statement receipt(db, "INSERT INTO technical_work_receipts VALUES(?,?,?,?,?,?,?,?,?,?);");
    Statement receiptMaterial(db, "INSERT INTO technical_work_materials VALUES(?,?,?,?);");
    Statement report(
        db, "INSERT INTO technical_development_reports VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement reportMaterial(db, "INSERT INTO technical_report_materials VALUES(?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.technicalDevelopmentPrograms.size(); ++ordinal) {
        const auto& row = state.technicalDevelopmentPrograms[ordinal];
        program.bindInt64(1, static_cast<std::int64_t>(ordinal));
        program.bindInt64(2, row.id.value);
        program.bindText(3, row.charter.name);
        program.bindInt64(4, row.charter.opportunityId.value);
        program.bindInt64(5, row.charter.developmentColonyId.value);
        bindOptional(program, 6, row.charter.requestedFacilityId);
        bindOptional(program, 7, row.charter.requestedTeamId);
        bindOptional(program, 8, row.charter.requestedLeaderId);
        program.bindInt64(9, static_cast<std::int64_t>(row.charter.scope));
        program.bindInt64(10, row.createdDay);
        program.bindInt64(11, row.charterRevision);
        program.bindInt64(12, static_cast<std::int64_t>(row.lifecycle));
        program.bindInt64(13, static_cast<std::int64_t>(row.closure));
        bindOptionalDay(program, 14, row.closedDay);
        program.bindInt64(15, static_cast<std::int64_t>(row.stage));
        program.bindDouble(16, row.stageWork);
        bindOptional(program, 17, row.leasedTeamId);
        program.bindInt64(18, row.reportStartDay);
        program.bindInt64(19, row.nextReportDay);
        program.bindText(20, row.issue.signature);
        program.bindText(21, row.issue.message);
        program.bindInt64(22, row.issue.acknowledged);
        next(program);
        saveMaterialSet(programMaterial, row.id.value, 0, row.charter.policy.floors);
        if (row.charter.policy.lifetimeAllowances)
            saveMaterialSet(programMaterial, row.id.value, 1, *row.charter.policy.lifetimeAllowances);
        else
            for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
                programMaterial.bindInt64(1, row.id.value);
                programMaterial.bindInt64(2, 1);
                programMaterial.bindInt64(3, static_cast<std::int64_t>(material));
                programMaterial.bindNull(4);
                next(programMaterial);
            }
        saveMaterialSet(programMaterial, row.id.value, 2, row.stageConsumed);
        for (std::size_t index = 0; index < row.receipts.size(); ++index) {
            const auto& value = row.receipts[index];
            receipt.bindInt64(1, row.id.value);
            receipt.bindInt64(2, static_cast<std::int64_t>(index));
            receipt.bindInt64(3, value.sequence);
            receipt.bindInt64(4, value.day);
            receipt.bindInt64(5, value.charterRevision);
            receipt.bindInt64(6, static_cast<std::int64_t>(value.stage));
            receipt.bindInt64(7, value.facilityId.value);
            receipt.bindInt64(8, value.teamId.value);
            receipt.bindInt64(9, value.leaderId.value);
            receipt.bindDouble(10, value.work);
            next(receipt);
            for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
                receiptMaterial.bindInt64(1, row.id.value);
                receiptMaterial.bindInt64(2, static_cast<std::int64_t>(index));
                receiptMaterial.bindInt64(3, static_cast<std::int64_t>(material));
                receiptMaterial.bindDouble(4, value.consumed.amount[material]);
                next(receiptMaterial);
            }
        }
        for (std::size_t index = 0; index < row.reports.size(); ++index) {
            const auto& value = row.reports[index];
            report.bindInt64(1, row.id.value);
            report.bindInt64(2, static_cast<std::int64_t>(index));
            report.bindInt64(3, value.startDay);
            report.bindInt64(4, value.endDay);
            report.bindInt64(5, value.isNinetyDayReview);
            report.bindInt64(6, value.charterRevision);
            report.bindInt64(7, static_cast<std::int64_t>(value.scope));
            report.bindInt64(8, static_cast<std::int64_t>(value.stage));
            bindOptional(report, 9, value.facilityId);
            bindOptional(report, 10, value.teamId);
            bindOptional(report, 11, value.leaderId);
            report.bindDouble(12, value.periodWork);
            report.bindDouble(13, value.lifetimeWork);
            bindOptional(report, 14, value.prototypeId);
            report.bindInt64(15, value.testCount);
            bindOptionalNumber(report, 16, value.demonstratedThreshold);
            bindOptional(report, 17, value.componentId);
            report.bindInt64(18, value.localProductionReady);
            report.bindInt64(19, value.supportQualified);
            report.bindText(20, value.waitingReason);
            report.bindInt64(21, value.auditThroughId);
            next(report);
            for (int category = 0; category < 2; ++category)
                for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
                    reportMaterial.bindInt64(1, row.id.value);
                    reportMaterial.bindInt64(2, static_cast<std::int64_t>(index));
                    reportMaterial.bindInt64(3, category);
                    reportMaterial.bindInt64(4, static_cast<std::int64_t>(material));
                    reportMaterial.bindDouble(5, category == 0 ? value.periodConsumed.amount[material]
                                                               : value.lifetimeConsumed.amount[material]);
                    next(reportMaterial);
                }
        }
    }

    Statement design(db, "INSERT INTO prototype_designs VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement designMaterial(db, "INSERT INTO prototype_design_materials VALUES(?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.prototypeDesigns.size(); ++ordinal) {
        const auto& row = state.prototypeDesigns[ordinal];
        design.bindInt64(1, static_cast<std::int64_t>(ordinal));
        design.bindInt64(2, row.id.value);
        design.bindInt64(3, row.opportunityId.value);
        design.bindInt64(4, row.programId.value);
        design.bindInt64(5, row.createdDay);
        design.bindText(6, row.name);
        design.bindDouble(7, row.mass);
        design.bindDouble(8, row.volume);
        design.bindDouble(9, row.powerDemand);
        design.bindDouble(10, row.surveyCapability);
        design.bindDouble(11, row.componentBuildPoints);
        design.bindInt64(12, row.serviceProfile.familyId.value);
        design.bindDouble(13, row.serviceProfile.dutyCapacity);
        design.bindDouble(14, row.serviceProfile.teamWorkdaysPerDuty);
        next(design);
        saveMaterialSet(designMaterial, row.id.value, 0, row.serialBuildCost);
        saveMaterialSet(designMaterial, row.id.value, 1, row.serviceProfile.materialsPerDuty);
    }
    Statement prototype(db, "INSERT INTO prototype_component_units VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.prototypeComponentUnits.size(); ++ordinal) {
        const auto& row = state.prototypeComponentUnits[ordinal];
        prototype.bindInt64(1, static_cast<std::int64_t>(ordinal));
        prototype.bindInt64(2, row.id.value);
        prototype.bindInt64(3, row.opportunityId.value);
        prototype.bindInt64(4, row.designId.value);
        prototype.bindInt64(5, row.programId.value);
        prototype.bindInt64(6, row.colonyId.value);
        prototype.bindInt64(7, row.fabricationDay);
        prototype.bindInt64(8, static_cast<std::int64_t>(row.state));
        bindOptional(prototype, 9, row.componentId);
        bindOptionalDay(prototype, 10, row.availableDay);
        bindOptional(prototype, 11, row.reservedOrderId);
        if (row.reservedHullNumber)
            prototype.bindInt64(12, *row.reservedHullNumber);
        else
            prototype.bindNull(12);
        bindOptional(prototype, 13, row.consumedShipId);
        next(prototype);
    }
    Statement test(db, "INSERT INTO technical_test_records VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.technicalTestRecords.size(); ++ordinal) {
        const auto& row = state.technicalTestRecords[ordinal];
        test.bindInt64(1, static_cast<std::int64_t>(ordinal));
        test.bindInt64(2, row.id.value);
        test.bindInt64(3, row.opportunityId.value);
        test.bindInt64(4, row.prototypeId.value);
        test.bindInt64(5, row.programId.value);
        test.bindInt64(6, row.facilityId.value);
        test.bindInt64(7, row.teamId.value);
        test.bindInt64(8, row.leaderId.value);
        test.bindInt64(9, row.sequence);
        test.bindInt64(10, row.day);
        test.bindDouble(11, row.measuredDetectionThreshold);
        test.bindDouble(12, row.targetDetectionThreshold);
        test.bindInt64(13, row.meetsTarget);
        test.bindText(14, row.limitation);
        next(test);
    }
    Statement developed(db, "INSERT INTO developed_component_revisions VALUES(?,?,?,?,?,?,?,?,?);");
    Statement developedTest(db, "INSERT INTO developed_component_tests VALUES(?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.developedComponentRevisions.size(); ++ordinal) {
        const auto& row = state.developedComponentRevisions[ordinal];
        developed.bindInt64(1, static_cast<std::int64_t>(ordinal));
        developed.bindInt64(2, row.id.value);
        developed.bindInt64(3, row.opportunityId.value);
        developed.bindInt64(4, row.designId.value);
        developed.bindInt64(5, row.prototypeId.value);
        developed.bindInt64(6, row.measurementProfileId.value);
        developed.bindInt64(7, row.componentId.value);
        developed.bindInt64(8, row.demonstratedDay);
        developed.bindInt64(9, row.availableDay);
        next(developed);
        for (std::size_t index = 0; index < row.testIds.size(); ++index) {
            developedTest.bindInt64(1, row.id.value);
            developedTest.bindInt64(2, static_cast<std::int64_t>(index));
            developedTest.bindInt64(3, row.testIds[index].value);
            next(developedTest);
        }
    }
    Statement production(db, "INSERT INTO component_production_capabilities VALUES(?,?,?,?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.componentProductionCapabilities.size(); ++ordinal) {
        const auto& row = state.componentProductionCapabilities[ordinal];
        production.bindInt64(1, static_cast<std::int64_t>(ordinal));
        production.bindInt64(2, row.id.value);
        production.bindInt64(3, row.componentId.value);
        production.bindInt64(4, row.opportunityId.value);
        production.bindInt64(5, row.colonyId.value);
        production.bindInt64(6, row.facilityId.value);
        production.bindInt64(7, row.qualifyingProgramId.value);
        production.bindInt64(8, row.qualifiedDay);
        production.bindInt64(9, row.availableDay);
        next(production);
    }
    Statement support(db, "INSERT INTO support_qualifications VALUES(?,?,?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.supportQualificationRecords.size(); ++ordinal) {
        const auto& row = state.supportQualificationRecords[ordinal];
        support.bindInt64(1, static_cast<std::int64_t>(ordinal));
        support.bindInt64(2, row.id.value);
        support.bindInt64(3, row.opportunityId.value);
        support.bindInt64(4, row.teamId.value);
        support.bindInt64(5, row.familyId.value);
        support.bindInt64(6, row.programId.value);
        support.bindInt64(7, row.qualifiedDay);
        support.bindInt64(8, row.availableDay);
        next(support);
    }

    Statement plan(db, "INSERT INTO shipyard_current_supply_plans VALUES(?,?,?,?,?);");
    Statement planCost(db, "INSERT INTO shipyard_supply_plan_costs VALUES(?,?,?);");
    Statement supply(db, "INSERT INTO shipyard_developed_supplies VALUES(?,?,?,?,?);");
    Statement supplyPrototype(db, "INSERT INTO shipyard_supply_prototypes VALUES(?,?,?,?);");
    for (const auto& order : state.shipyardOrders)
        if (order.currentHullSupplyPlan) {
            const auto& value = *order.currentHullSupplyPlan;
            plan.bindInt64(1, order.id.value);
            plan.bindInt64(2, value.shipClassId.value);
            plan.bindInt64(3, value.hullNumber);
            plan.bindDouble(4, value.effectiveBuildPoints);
            plan.bindInt64(5, value.boundDay);
            next(plan);
            for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
                planCost.bindInt64(1, order.id.value);
                planCost.bindInt64(2, static_cast<std::int64_t>(material));
                planCost.bindDouble(3, value.effectiveBuildCost.amount[material]);
                next(planCost);
            }
            for (std::size_t index = 0; index < value.developedComponents.size(); ++index) {
                const auto& row = value.developedComponents[index];
                supply.bindInt64(1, order.id.value);
                supply.bindInt64(2, static_cast<std::int64_t>(index));
                supply.bindInt64(3, row.componentId.value);
                supply.bindInt64(4, row.quantity);
                supply.bindInt64(5, static_cast<std::int64_t>(row.kind));
                next(supply);
                for (std::size_t prototypeIndex = 0; prototypeIndex < row.prototypeUnits.size();
                     ++prototypeIndex) {
                    supplyPrototype.bindInt64(1, order.id.value);
                    supplyPrototype.bindInt64(2, static_cast<std::int64_t>(index));
                    supplyPrototype.bindInt64(3, static_cast<std::int64_t>(prototypeIndex));
                    supplyPrototype.bindInt64(4, row.prototypeUnits[prototypeIndex].value);
                    next(supplyPrototype);
                }
            }
        }
    Statement integration(db, "INSERT INTO prototype_integration_receipts VALUES(?,?,?,?,?,?);");
    for (std::size_t ordinal = 0; ordinal < state.prototypeIntegrationReceipts.size(); ++ordinal) {
        const auto& row = state.prototypeIntegrationReceipts[ordinal];
        integration.bindInt64(1, static_cast<std::int64_t>(ordinal));
        integration.bindInt64(2, row.prototypeId.value);
        integration.bindInt64(3, row.orderId.value);
        integration.bindInt64(4, row.hullNumber);
        integration.bindInt64(5, row.shipId.value);
        integration.bindInt64(6, row.day);
        next(integration);
    }
}

void loadTechnicalState(Database& db, GameState& state) {
    Statement opportunities(db, "SELECT * FROM technology_opportunities ORDER BY ordinal;");
    while (opportunities.step()) {
        requireOrdinal(opportunities, 0, state.technologyOpportunities.size(), "technology_opportunities");
        state.technologyOpportunities.push_back(TechnologyOpportunity{
            TechnologyOpportunityId{opportunities.columnInt64Strict(1)}, opportunities.columnText(2),
            ShipComponentId{opportunities.columnInt64Strict(3)}, opportunities.columnDoubleStrict(4),
            boolean(opportunities.columnInt64Strict(5)), opportunities.columnText(6),
            opportunities.columnText(7)});
    }
    Statement truths(db, "SELECT * FROM technology_candidate_truth ORDER BY ordinal;");
    while (truths.step()) {
        requireOrdinal(truths, 0, state.technologyCandidateTruths.size(), "technology_candidate_truth");
        state.technologyCandidateTruths.push_back(
            {TechnologyOpportunityId{truths.columnInt64Strict(1)}, truths.columnDoubleStrict(2)});
    }
    Statement facilities(db, "SELECT * FROM technical_facilities ORDER BY ordinal;");
    while (facilities.step()) {
        requireOrdinal(facilities, 0, state.technicalFacilities.size(), "technical_facilities");
        state.technicalFacilities.push_back(TechnicalFacility{
            TechnicalFacilityId{facilities.columnInt64Strict(1)}, ColonyId{facilities.columnInt64Strict(2)},
            facilities.columnText(3), facilities.columnDoubleStrict(4),
            static_cast<TechnicalFacilityCapability>(facilities.columnInt64Strict(5))});
    }
    Statement engineering(db, "SELECT team_id,ordinal,qualification FROM "
                              "maintenance_team_engineering_qualifications ORDER BY team_id,ordinal;");
    std::int64_t currentTeam = 0;
    std::size_t expected = 0;
    while (engineering.step()) {
        const auto teamId = engineering.columnInt64Strict(0);
        if (teamId != currentTeam) {
            currentTeam = teamId;
            expected = 0;
        }
        requireOrdinal(engineering, 1, expected++, "maintenance_team_engineering_qualifications");
        parent(state.maintenanceTeams, MaintenanceTeamId{teamId})
            .engineeringQualifications.push_back(
                static_cast<EngineeringQualification>(engineering.columnInt64Strict(2)));
    }
    Statement programs(db, "SELECT * FROM technical_development_programs ORDER BY ordinal;");
    while (programs.step()) {
        requireOrdinal(programs, 0, state.technicalDevelopmentPrograms.size(),
                       "technical_development_programs");
        TechnicalDevelopmentProgram row;
        row.id = TechnicalDevelopmentProgramId{programs.columnInt64Strict(1)};
        row.charter.name = programs.columnText(2);
        row.charter.opportunityId = TechnologyOpportunityId{programs.columnInt64Strict(3)};
        row.charter.developmentColonyId = ColonyId{programs.columnInt64Strict(4)};
        row.charter.requestedFacilityId = optionalId<TechnicalFacilityId>(programs, 5);
        row.charter.requestedTeamId = optionalId<MaintenanceTeamId>(programs, 6);
        row.charter.requestedLeaderId = optionalId<PersonId>(programs, 7);
        row.charter.scope = static_cast<TechnicalDevelopmentScope>(programs.columnInt64Strict(8));
        row.createdDay = programs.columnInt64Strict(9);
        row.charterRevision = integer(programs.columnInt64Strict(10));
        row.lifecycle = static_cast<TechnicalDevelopmentLifecycle>(programs.columnInt64Strict(11));
        row.closure = static_cast<TechnicalDevelopmentClosure>(programs.columnInt64Strict(12));
        row.closedDay = optionalDay(programs, 13);
        row.stage = static_cast<TechnicalDevelopmentStage>(programs.columnInt64Strict(14));
        row.stageWork = programs.columnDoubleStrict(15);
        row.leasedTeamId = optionalId<MaintenanceTeamId>(programs, 16);
        row.reportStartDay = programs.columnInt64Strict(17);
        row.nextReportDay = programs.columnInt64Strict(18);
        row.issue.signature = programs.columnText(19);
        row.issue.message = programs.columnText(20);
        row.issue.acknowledged = boolean(programs.columnInt64Strict(21));
        row.charter.policy.floors =
            loadMaterialSet(db, "technical_program_materials", "program_id", row.id.value, 0);
        Statement allowance(db, "SELECT material,amount FROM technical_program_materials WHERE program_id=? "
                                "AND category=1 ORDER BY material;");
        allowance.bindInt64(1, row.id.value);
        ProcessedMaterialSet limits;
        bool present = false;
        std::size_t material = 0;
        while (allowance.step()) {
            requireOrdinal(allowance, 0, material, "technical_program_materials allowance");
            if (!allowance.columnIsNull(1)) {
                limits.amount[material] = allowance.columnDoubleStrict(1);
                present = true;
            }
            ++material;
        }
        if (material != processedMaterialCount())
            throw std::runtime_error("Missing technical allowance channels");
        if (present)
            row.charter.policy.lifetimeAllowances = limits;
        row.stageConsumed = loadMaterialSet(db, "technical_program_materials", "program_id", row.id.value, 2);
        state.technicalDevelopmentPrograms.push_back(std::move(row));
    }
    Statement receipts(db, "SELECT * FROM technical_work_receipts ORDER BY program_id,ordinal;");
    while (receipts.step()) {
        auto& program = parent(state.technicalDevelopmentPrograms,
                               TechnicalDevelopmentProgramId{receipts.columnInt64Strict(0)});
        requireOrdinal(receipts, 1, program.receipts.size(), "technical_work_receipts");
        const auto ordinal = program.receipts.size();
        TechnicalWorkReceipt row;
        row.sequence = integer(receipts.columnInt64Strict(2));
        row.day = receipts.columnInt64Strict(3);
        row.charterRevision = integer(receipts.columnInt64Strict(4));
        row.stage = static_cast<TechnicalDevelopmentStage>(receipts.columnInt64Strict(5));
        row.facilityId = TechnicalFacilityId{receipts.columnInt64Strict(6)};
        row.teamId = MaintenanceTeamId{receipts.columnInt64Strict(7)};
        row.leaderId = PersonId{receipts.columnInt64Strict(8)};
        row.work = receipts.columnDoubleStrict(9);
        Statement materials(db, "SELECT material,amount FROM technical_work_materials WHERE program_id=? AND "
                                "receipt_ordinal=? ORDER BY material;");
        materials.bindInt64(1, program.id.value);
        materials.bindInt64(2, static_cast<std::int64_t>(ordinal));
        std::size_t m = 0;
        while (materials.step()) {
            requireOrdinal(materials, 0, m, "technical_work_materials");
            row.consumed.amount[m++] = materials.columnDoubleStrict(1);
        }
        if (m != processedMaterialCount())
            throw std::runtime_error("Missing technical work material channels");
        program.receipts.push_back(row);
    }
    Statement reports(db, "SELECT * FROM technical_development_reports ORDER BY program_id,ordinal;");
    while (reports.step()) {
        auto& program = parent(state.technicalDevelopmentPrograms,
                               TechnicalDevelopmentProgramId{reports.columnInt64Strict(0)});
        requireOrdinal(reports, 1, program.reports.size(), "technical_development_reports");
        const auto ordinal = program.reports.size();
        TechnicalDevelopmentReport row;
        row.startDay = reports.columnInt64Strict(2);
        row.endDay = reports.columnInt64Strict(3);
        row.isNinetyDayReview = boolean(reports.columnInt64Strict(4));
        row.charterRevision = integer(reports.columnInt64Strict(5));
        row.scope = static_cast<TechnicalDevelopmentScope>(reports.columnInt64Strict(6));
        row.stage = static_cast<TechnicalDevelopmentStage>(reports.columnInt64Strict(7));
        row.facilityId = optionalId<TechnicalFacilityId>(reports, 8);
        row.teamId = optionalId<MaintenanceTeamId>(reports, 9);
        row.leaderId = optionalId<PersonId>(reports, 10);
        row.periodWork = reports.columnDoubleStrict(11);
        row.lifetimeWork = reports.columnDoubleStrict(12);
        row.prototypeId = optionalId<PrototypeComponentUnitId>(reports, 13);
        row.testCount = integer(reports.columnInt64Strict(14));
        row.demonstratedThreshold = optionalNumber(reports, 15);
        row.componentId = optionalId<ShipComponentId>(reports, 16);
        row.localProductionReady = boolean(reports.columnInt64Strict(17));
        row.supportQualified = boolean(reports.columnInt64Strict(18));
        row.waitingReason = reports.columnText(19);
        row.auditThroughId = reports.columnInt64Strict(20);
        // Material rows also key report ordinal; use explicit filtered reload for multiple reports.
        for (int category = 0; category < 2; ++category) {
            Statement ms(db, "SELECT material,amount FROM technical_report_materials WHERE program_id=? AND "
                             "report_ordinal=? AND category=? ORDER BY material;");
            ms.bindInt64(1, program.id.value);
            ms.bindInt64(2, static_cast<std::int64_t>(ordinal));
            ms.bindInt64(3, category);
            ProcessedMaterialSet value;
            std::size_t m = 0;
            while (ms.step()) {
                requireOrdinal(ms, 0, m, "technical_report_materials");
                value.amount[m++] = ms.columnDoubleStrict(1);
            }
            if (m != processedMaterialCount())
                throw std::runtime_error("Missing technical report materials");
            if (category == 0)
                row.periodConsumed = value;
            else
                row.lifetimeConsumed = value;
        }
        program.reports.push_back(row);
    }
    Statement designs(db, "SELECT * FROM prototype_designs ORDER BY ordinal;");
    while (designs.step()) {
        requireOrdinal(designs, 0, state.prototypeDesigns.size(), "prototype_designs");
        PrototypeDesignRecord row;
        row.id = PrototypeDesignId{designs.columnInt64Strict(1)};
        row.opportunityId = TechnologyOpportunityId{designs.columnInt64Strict(2)};
        row.programId = TechnicalDevelopmentProgramId{designs.columnInt64Strict(3)};
        row.createdDay = designs.columnInt64Strict(4);
        row.name = designs.columnText(5);
        row.mass = designs.columnDoubleStrict(6);
        row.volume = designs.columnDoubleStrict(7);
        row.powerDemand = designs.columnDoubleStrict(8);
        row.surveyCapability = designs.columnDoubleStrict(9);
        row.componentBuildPoints = designs.columnDoubleStrict(10);
        row.serviceProfile.familyId = EquipmentFamilyId{designs.columnInt64Strict(11)};
        row.serviceProfile.dutyCapacity = designs.columnDoubleStrict(12);
        row.serviceProfile.teamWorkdaysPerDuty = designs.columnDoubleStrict(13);
        row.serialBuildCost = loadMaterialSet(db, "prototype_design_materials", "design_id", row.id.value, 0);
        row.serviceProfile.materialsPerDuty =
            loadMaterialSet(db, "prototype_design_materials", "design_id", row.id.value, 1);
        state.prototypeDesigns.push_back(row);
    }
    Statement prototypes(db, "SELECT * FROM prototype_component_units ORDER BY ordinal;");
    while (prototypes.step()) {
        requireOrdinal(prototypes, 0, state.prototypeComponentUnits.size(), "prototype_component_units");
        PrototypeComponentUnit row;
        row.id = PrototypeComponentUnitId{prototypes.columnInt64Strict(1)};
        row.opportunityId = TechnologyOpportunityId{prototypes.columnInt64Strict(2)};
        row.designId = PrototypeDesignId{prototypes.columnInt64Strict(3)};
        row.programId = TechnicalDevelopmentProgramId{prototypes.columnInt64Strict(4)};
        row.colonyId = ColonyId{prototypes.columnInt64Strict(5)};
        row.fabricationDay = prototypes.columnInt64Strict(6);
        row.state = static_cast<PrototypeComponentState>(prototypes.columnInt64Strict(7));
        row.componentId = optionalId<ShipComponentId>(prototypes, 8);
        row.availableDay = optionalDay(prototypes, 9);
        row.reservedOrderId = optionalId<ShipyardOrderId>(prototypes, 10);
        if (!prototypes.columnIsNull(11))
            row.reservedHullNumber = integer(prototypes.columnInt64Strict(11));
        row.consumedShipId = optionalId<ShipId>(prototypes, 12);
        state.prototypeComponentUnits.push_back(row);
    }
    Statement tests(db, "SELECT * FROM technical_test_records ORDER BY ordinal;");
    while (tests.step()) {
        requireOrdinal(tests, 0, state.technicalTestRecords.size(), "technical_test_records");
        state.technicalTestRecords.push_back(TechnicalTestRecord{
            TechnicalTestId{tests.columnInt64Strict(1)}, TechnologyOpportunityId{tests.columnInt64Strict(2)},
            PrototypeComponentUnitId{tests.columnInt64Strict(3)},
            TechnicalDevelopmentProgramId{tests.columnInt64Strict(4)},
            TechnicalFacilityId{tests.columnInt64Strict(5)}, MaintenanceTeamId{tests.columnInt64Strict(6)},
            PersonId{tests.columnInt64Strict(7)}, integer(tests.columnInt64Strict(8)),
            tests.columnInt64Strict(9), tests.columnDoubleStrict(10), tests.columnDoubleStrict(11),
            boolean(tests.columnInt64Strict(12)), tests.columnText(13)});
    }
    Statement developed(db, "SELECT * FROM developed_component_revisions ORDER BY ordinal;");
    while (developed.step()) {
        requireOrdinal(developed, 0, state.developedComponentRevisions.size(),
                       "developed_component_revisions");
        DevelopedComponentRevision row;
        row.id = DevelopedComponentRevisionId{developed.columnInt64Strict(1)};
        row.opportunityId = TechnologyOpportunityId{developed.columnInt64Strict(2)};
        row.designId = PrototypeDesignId{developed.columnInt64Strict(3)};
        row.prototypeId = PrototypeComponentUnitId{developed.columnInt64Strict(4)};
        row.measurementProfileId = MeasurementProfileId{developed.columnInt64Strict(5)};
        row.componentId = ShipComponentId{developed.columnInt64Strict(6)};
        row.demonstratedDay = developed.columnInt64Strict(7);
        row.availableDay = developed.columnInt64Strict(8);
        Statement links(
            db,
            "SELECT ordinal,test_id FROM developed_component_tests WHERE revision_id=? ORDER BY ordinal;");
        links.bindInt64(1, row.id.value);
        while (links.step()) {
            requireOrdinal(links, 0, row.testIds.size(), "developed_component_tests");
            row.testIds.push_back(TechnicalTestId{links.columnInt64Strict(1)});
        }
        state.developedComponentRevisions.push_back(row);
    }
    Statement production(db, "SELECT * FROM component_production_capabilities ORDER BY ordinal;");
    while (production.step()) {
        requireOrdinal(production, 0, state.componentProductionCapabilities.size(),
                       "component_production_capabilities");
        state.componentProductionCapabilities.push_back(
            {ComponentProductionCapabilityId{production.columnInt64Strict(1)},
             ShipComponentId{production.columnInt64Strict(2)},
             TechnologyOpportunityId{production.columnInt64Strict(3)},
             ColonyId{production.columnInt64Strict(4)}, TechnicalFacilityId{production.columnInt64Strict(5)},
             TechnicalDevelopmentProgramId{production.columnInt64Strict(6)}, production.columnInt64Strict(7),
             production.columnInt64Strict(8)});
    }
    Statement support(db, "SELECT * FROM support_qualifications ORDER BY ordinal;");
    while (support.step()) {
        requireOrdinal(support, 0, state.supportQualificationRecords.size(), "support_qualifications");
        state.supportQualificationRecords.push_back(
            {SupportQualificationId{support.columnInt64Strict(1)},
             TechnologyOpportunityId{support.columnInt64Strict(2)},
             MaintenanceTeamId{support.columnInt64Strict(3)}, EquipmentFamilyId{support.columnInt64Strict(4)},
             TechnicalDevelopmentProgramId{support.columnInt64Strict(5)}, support.columnInt64Strict(6),
             support.columnInt64Strict(7)});
    }
    Statement plans(db, "SELECT * FROM shipyard_current_supply_plans ORDER BY order_id;");
    while (plans.step()) {
        auto& order = parent(state.shipyardOrders, ShipyardOrderId{plans.columnInt64Strict(0)});
        ShipyardCurrentHullSupplyPlan plan;
        plan.shipClassId = ShipClassId{plans.columnInt64Strict(1)};
        plan.hullNumber = integer(plans.columnInt64Strict(2));
        plan.effectiveBuildPoints = plans.columnDoubleStrict(3);
        plan.boundDay = plans.columnInt64Strict(4);
        Statement costs(
            db, "SELECT material,amount FROM shipyard_supply_plan_costs WHERE order_id=? ORDER BY material;");
        costs.bindInt64(1, order.id.value);
        std::size_t m = 0;
        while (costs.step()) {
            requireOrdinal(costs, 0, m, "shipyard_supply_plan_costs");
            plan.effectiveBuildCost.amount[m++] = costs.columnDoubleStrict(1);
        }
        if (m != processedMaterialCount())
            throw std::runtime_error("Missing supply plan costs");
        Statement supplies(db, "SELECT ordinal,component_id,quantity,kind FROM shipyard_developed_supplies "
                               "WHERE order_id=? ORDER BY ordinal;");
        supplies.bindInt64(1, order.id.value);
        while (supplies.step()) {
            requireOrdinal(supplies, 0, plan.developedComponents.size(), "shipyard_developed_supplies");
            DevelopedComponentSupply supply;
            const auto supplyOrdinal = static_cast<std::int64_t>(plan.developedComponents.size());
            supply.componentId = ShipComponentId{supplies.columnInt64Strict(1)};
            supply.quantity = integer(supplies.columnInt64Strict(2));
            supply.kind = static_cast<DevelopedComponentSupplyKind>(supplies.columnInt64Strict(3));
            Statement units(db, "SELECT ordinal,prototype_id FROM shipyard_supply_prototypes WHERE "
                                "order_id=? AND supply_ordinal=? ORDER BY ordinal;");
            units.bindInt64(1, order.id.value);
            units.bindInt64(2, supplyOrdinal);
            while (units.step()) {
                requireOrdinal(units, 0, supply.prototypeUnits.size(), "shipyard_supply_prototypes");
                supply.prototypeUnits.push_back(PrototypeComponentUnitId{units.columnInt64Strict(1)});
            }
            plan.developedComponents.push_back(supply);
        }
        order.currentHullSupplyPlan = plan;
    }
    Statement integrations(db, "SELECT * FROM prototype_integration_receipts ORDER BY ordinal;");
    while (integrations.step()) {
        requireOrdinal(integrations, 0, state.prototypeIntegrationReceipts.size(),
                       "prototype_integration_receipts");
        state.prototypeIntegrationReceipts.push_back(
            {PrototypeComponentUnitId{integrations.columnInt64Strict(1)},
             ShipyardOrderId{integrations.columnInt64Strict(2)}, integer(integrations.columnInt64Strict(3)),
             ShipId{integrations.columnInt64Strict(4)}, integrations.columnInt64Strict(5)});
    }
}

} // namespace deep::save
