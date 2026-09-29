#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/FreightProgramRules.h"
#include "sim/ScenarioFactory.h"

// P3B-01/03/15/19/24/27/36: owned app projections expose actual hull custody and
// mutable authority without turning previews into transfers or admission gates.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(const bool value, const std::string_view message) {
    if (!value) throw std::runtime_error{std::string{message}};
}

deep::FreightProgramCharter charterFor(const deep::GameState& state) {
    deep::FreightProgramCharter charter;
    charter.name = "App delivery";
    charter.sourceColonyId = state.colonies.at(state.colonies.size() - 2).id;
    charter.destinationColonyId = state.colonies.back().id;
    charter.material = deep::ProcessedMaterial::StructuralAlloys;
    charter.totalQuantity = 500.0;
    charter.requestedFleetId = state.fleets.back().id;
    charter.requestedLeaderId = state.people.front().id;
    return charter;
}

void ownedPreviewsAndUnreadyAuthoring() {
    deep::SimulationService service{deep::createDelegatedFreightScenario()};
    deep::SimulationQueries queries{service};
    const auto catalog = queries.shipComponents();
    require(catalog.at(5).cargoCapacity == 100.0 && catalog.at(5).cargoHandlingPerDay == 25.0,
            "catalog DTO exposes authoritative cargo bay units and rate");
    const auto classes = queries.shipClasses();
    require(classes.front().design.cargoCapacity == 0.0 && classes.front().design.buildPoints == 500.0,
            "Survey Cutter totals remain unchanged");
    const auto design = queries.previewShipDesign(classes.back().components).design;
    require(design.cargoCapacity == 200.0 && design.cargoHandlingPerDay == 50.0 && design.buildPoints == 550.0,
            "reference design preview has independently expected cargo/rate/work totals");
    auto charter = charterFor(service.state());
    const auto before = queries.colonies();
    const auto preview = queries.previewFreightProgramCharter(charter);
    require(preview.structurallyValid && preview.shipmentReady && preview.plannedQuantity == 200.0 &&
            preview.loadingDays == 4 && preview.unloadingDays == 4,
            "shared plan previews one bounded 200-unit shipment with four handling days each way");
    // The query runs between days: day 1 refills, days 2-5 load, then day 6
    // departs. This catches an execution-day planner reused one day too early.
    require(preview.projectedDepartureDay == 6, "candidate starts at the next opening boundary");
    require(preview.hulls.size() == 1 && preview.hulls.front().cargoQuantity == 0.0 &&
            preview.hulls.front().plannedQuantity == 200.0 && service.state().freightPrograms.empty(),
            "preview distinguishes tentative quantity from actual cargo and authorizes nothing");
    require(queries.colonies().at(before.size() - 2).totalProcessedStockpile == before.at(before.size() - 2).totalProcessedStockpile,
            "preview does not debit inventory");
    charter.requestedFleetId.reset();
    charter.requestedLeaderId.reset();
    const auto unready = queries.previewFreightProgramCharter(charter);
    require(unready.structurallyValid && unready.waitingReasons.size() == 2,
            "missing optional assignments remain valid and explained");
    require(service.execute(deep::CreateFreightProgramCommand{charter}).ok, "unready intent authorizes");
    const auto oldRows = queries.freightPrograms();
    require(oldRows.size() == 1 && oldRows.front().hulls.empty() && oldRows.front().canAmend && oldRows.front().canSuspend,
            "unready intention is visible and actionable");
    auto amended = deep::freightAmendmentFromCharter(charter);
    amended.name = "Revised delivery";
    amended.totalQuantity = 0.0;
    auto revisedCharter = charter;
    deep::applyFreightAmendment(revisedCharter, amended);
    require(queries.previewFreightProgramCharter(revisedCharter, oldRows.front().id).structurallyValid,
            "amendment can reduce future demand to zero");
    require(service.execute(deep::AmendFreightProgramCommand{oldRows.front().id, amended}).ok, "valid amendment accepted");
    require(queries.freightPrograms().front().charter.name == amended.name && oldRows.front().charter.name == charter.name,
            "returned DTO owns charter data across mutation");
    revisedCharter.destinationColonyId = revisedCharter.sourceColonyId;
    require(!queries.previewFreightProgramCharter(revisedCharter, oldRows.front().id).structurallyValid,
            "amendment preview rejects a changed fixed contract route");
}

void physicalManifestAndLifecycleProjection() {
    deep::SimulationService service{deep::createDelegatedFreightScenario()};
    deep::SimulationQueries queries{service};
    auto charter = charterFor(service.state());
    require(service.execute(deep::CreateFreightProgramCommand{charter}).ok, "ready delivery accepted");
    const auto id = queries.freightPrograms().front().id;
    for (int i = 0; i < 12 && queries.freightPrograms().front().cargoAboard == 0.0; ++i) {
        require(service.advanceDaysDetailed(1).advancedDays == 1, "ready delivery advances to partial loading");
    }
    auto row = queries.freightPrograms().front();
    require(row.cargoAboard == 50.0 && row.hulls.size() == 1 && row.hulls.front().cargoQuantity == 50.0,
            "manifest shows real first daily per-hull load");
    require(row.hulls.front().cargoProgramId == id && row.hulls.front().shipmentNumber == 1,
            "actual cargo carries typed program and shipment identity");
    const auto fleet = queries.fleet(*charter.requestedFleetId);
    require(fleet && fleet->controllingProgram == deep::ProgramController{id} &&
            fleet->controllingProgramLabel.find("freight program") != std::string::npos,
            "fleet reverse ownership exposes the freight kind");
    require(service.execute(deep::SuspendFreightProgramCommand{id}).ok, "loaded program suspends");
    row = queries.freightPrograms().front();
    require(row.canResume && row.canAmend && row.canCancel && !row.canSuspend && row.leasedFleetId,
            "paused cargo custody remains leased and all useful decisions remain exposed");
    auto amendment = deep::freightAmendmentFromCharter(charter);
    amendment.totalQuantity = 0.0;
    amendment.requestedFleetId.reset();
    require(service.execute(deep::AmendFreightProgramCommand{id, amendment}).ok, "paused commitment can amend future demand");
    row = queries.freightPrograms().front();
    require(row.pendingFleetChange && row.commitmentAboveTarget > 0.0 && row.canResume,
            "pending asset change and existing commitment above reduced demand are explicit");
    require(service.advanceDaysDetailed(3).advancedDays == 3 && queries.freightPrograms().front().cargoAboard == 50.0,
            "UI projection preserves exact cargo during suspended advancement");
    require(service.execute(deep::CancelFreightProgramCommand{id}).ok, "cancel authorizes bounded source disposition");
    for (int i = 0; i < 8 && queries.freightPrograms().front().lifecycle != deep::FreightProgramLifecycle::Closed; ++i) {
        require(service.advanceDaysDetailed(1).advancedDays == 1, "source disposition advances");
    }
    row = queries.freightPrograms().front();
    require(row.lifecycle == deep::FreightProgramLifecycle::Closed && row.cargoReturned == 50.0 && row.cargoDelivered == 0.0,
            "source return remains distinct from cumulative delivered quantity");
    require(!row.canAmend && !row.canSuspend && !row.canResume && !row.canCancel && !row.nextReportDay && row.closedDay,
            "only physically closed programs become terminal and stop reports");
    require(!row.receipts.empty() && queries.recentEvents(1).back().eventType == "freight_program",
            "receipt and typed audit projections remain inspectable");
}

void sharedAdvanceInterruption() {
    deep::SimulationService setup{deep::createDelegatedFreightScenario()};
    auto charter = charterFor(setup.state());
    charter.requestedFleetId.reset();
    require(setup.execute(deep::CreateFreightProgramCommand{charter}).ok, "issue fixture charter accepted");
    auto state = setup.state();
    const auto id = state.freightPrograms.front().id;
    state.freightPrograms.front().issue = {"app:freight:decision", "Freight decision pending", false};
    deep::SimulationService service{std::move(state)};
    const auto before = service.state().date.day;
    const auto result = service.advanceDaysDetailed(30);
    require(result.interrupted && result.advancedDays == 0 && result.issueProgramId == deep::ProgramController{id},
            "detailed service path returns typed freight issue");
    require(service.advanceDays(30).empty() && service.state().date.day == before,
            "event-only service path cannot bypass freight interruption");
    const auto command = service.execute(deep::AdvanceDaysCommand{30});
    require(command.ok && command.message.find("Advanced 0 day(s); stopped") != std::string::npos,
            "UI/CLI command path preserves actual elapsed day count");
    require(service.execute(deep::AcknowledgeFreightProgramIssueCommand{id, "app:freight:decision"}).ok,
            "typed acknowledgement crosses command boundary");
}

void historicalReportPoliciesRemainOwnedSnapshots() {
    // P3B-29/36: report limits describe the historical boundary. A mutable
    // charter and even the absence of a lifetime cap cannot rewrite that view.
    deep::SimulationService service{deep::createDelegatedFreightScenario()};
    deep::SimulationQueries queries{service};
    auto charter = charterFor(service.state());
    charter.requestedFleetId.reset();
    charter.policy.sourceCargoFloor = 4.0;
    charter.policy.sourcePropellantFloor = 8.0;
    charter.policy.maxAdditionalPropellant = 30.0;
    charter.policy.returnContingencyFraction = 0.15;
    require(service.execute(deep::CreateFreightProgramCommand{charter}).ok, "historical policy fixture accepted");
    require(service.advanceDaysDetailed(30).advancedDays == 30, "waiting program reaches first report boundary");
    const auto oldRows = queries.freightPrograms();
    require(oldRows.front().reports.size() == 1, "first report is projected");
    const auto originalPolicy = oldRows.front().reports.front().report.policy;
    require(originalPolicy.sourceCargoFloor == 4.0 && originalPolicy.sourcePropellantFloor == 8.0 &&
            originalPolicy.maxAdditionalPropellant == 30.0 && originalPolicy.returnContingencyFraction == 0.15,
            "report captures all four policy values at its own boundary");
    auto amendment = deep::freightAmendmentFromCharter(charter);
    amendment.policy.sourceCargoFloor = 7.0;
    amendment.policy.sourcePropellantFloor = 11.0;
    amendment.policy.maxAdditionalPropellant.reset();
    amendment.policy.returnContingencyFraction = 0.25;
    require(service.execute(deep::AmendFreightProgramCommand{oldRows.front().id, amendment}).ok, "later limits may amend");
    require(service.advanceDaysDetailed(30).advancedDays == 30, "later policy reaches another report boundary");
    const auto current = queries.freightPrograms().front();
    require(current.reports.size() == 2, "both historical reports remain visible");
    const auto& first = current.reports.front().report.policy;
    const auto& second = current.reports.back().report.policy;
    require(first.sourceCargoFloor == 4.0 && first.sourcePropellantFloor == 8.0 &&
            first.maxAdditionalPropellant == 30.0 && first.returnContingencyFraction == 0.15,
            "later charter amendment leaves the first report's limits intact");
    require(second.sourceCargoFloor == 7.0 && second.sourcePropellantFloor == 11.0 &&
            !second.maxAdditionalPropellant && second.returnContingencyFraction == 0.25,
            "new report snapshots changed limits including unlimited allowance");
    require(oldRows.front().reports.size() == 1 && oldRows.front().charterRevision == 1 &&
            oldRows.front().reports.front().report.policy.maxAdditionalPropellant == 30.0,
            "previously returned DTO remains owned across amendment and report append");
}
} // namespace

int main() {
    try {
        ownedPreviewsAndUnreadyAuthoring();
        physicalManifestAndLifecycleProjection();
        sharedAdvanceInterruption();
        historicalReportPoliciesRemainOwnedSnapshots();
        std::cout << "Freight program app: 4 scenarios passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Freight program app failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
