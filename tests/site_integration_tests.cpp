// Earned P4B proof: shipyard construction, four cold-site supply obligations,
// elapsed field labor, supported recovery, collection, processing and history.
#include "app/SiteDevelopmentFixture.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "sim/GameStateValidation.h"
#include "sim/SiteOperationRules.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ShipDesignRules.h"
#include "save/SaveGameRepository.h"
#include "save/EventJson.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) {
    require(std::abs(a - b) < 1e-7, why);
}
void ack(SimulationService& s) {
    for (const auto& p : s.state().siteDevelopmentPrograms)
        if (!p.issue.acknowledged)
            require(s.execute(AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature}).ok,
                    "ack development");
    for (const auto& p : s.state().freightPrograms)
        if (!p.issue.acknowledged)
            require(s.execute(AcknowledgeFreightProgramIssueCommand{p.id, p.issue.signature}).ok,
                    "ack freight");
    for (const auto& x : s.state().resourceSites)
        if (!x.issue.acknowledged)
            require(s.execute(
                         AcknowledgeSiteOperatingIssueCommand{x.id, x.issue.cause, x.issue.episodeStartedDay})
                        .ok,
                    "ack site");
}
void compare(const GameState& a, const GameState& b) {
    require(a.date.day == b.date.day && a.eventLog.size() == b.eventLog.size(),
            "continuation date/audit count");
    for (std::size_t i = 0; i < a.eventLog.size(); ++i)
        require(save::eventPayloadToJson(a.eventLog[i].payload) ==
                    save::eventPayloadToJson(b.eventLog[i].payload),
                "continuation audit content");
    require(a.resourceSites.front().dutyReceipts == b.resourceSites.front().dutyReceipts &&
                a.resourceSites.front().extractionReceipts == b.resourceSites.front().extractionReceipts,
            "continuation operating history");
    for (std::size_t i = 0; i < a.colonies.size(); ++i) {
        require(a.colonies[i].stockpile.amount == b.colonies[i].stockpile.amount &&
                    a.colonies[i].processedStockpile.amount == b.colonies[i].processedStockpile.amount &&
                    a.colonies[i].processedProductionTotals.amount ==
                        b.colonies[i].processedProductionTotals.amount,
                "continuation economy exact");
    }
    for (std::size_t i = 0; i < a.ships.size(); ++i)
        near(a.ships[i].fuel, b.ships[i].fuel, "continuation engine fuel");
}
void earned_and_continued() {
    const auto start = std::chrono::steady_clock::now();
    auto state = earnSiteDevelopmentFixture(90);
    require(state.observations.empty() && state.assessments.empty(),
            "unsurveyed investment needs no manufactured evidence");
    require(state.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed,
            "builder physically closes by90");
    const auto& site = state.resourceSites.front();
    require(site.installed.size() == 5 && !site.extractionReceipts.empty(),
            "installed package earned and operated");
    require(state.freightPrograms.back().cargoDelivered > 0,
            "standing source-absent collector recovers after actual extraction");
    require(state.colonies.back().processedProductionTotals.get(ProcessedMaterial::Propellant) > 0,
            "delivered raw Ice becomes actual gross fuel");
    require(site.reports.size() == 3 && site.reports.back().isNinetyDayReview,
            "day90 owned operating review");
    require(!state.siteDevelopmentPrograms.front().reports.empty(),
            "development reports retained after closeout");
    const auto reportCount = state.siteDevelopmentPrograms.front().reports.size();
    double usedFuel = 0, usedComposites = 0;
    for (const auto& d : site.dutyReceipts) {
        usedFuel += d.reactorFuel;
        usedComposites += d.composites;
    }
    double suppliedFuel = 0, suppliedComposites = 0;
    for (const auto& p : state.freightPrograms) {
        if (p.charter.commodity == Commodity{ProcessedMaterial::ReactorFuel})
            suppliedFuel += p.cargoDelivered;
        if (p.charter.commodity == Commodity{ProcessedMaterial::IndustrialComposites})
            suppliedComposites += p.cargoDelivered;
    }
    near(site.processedStock.get(ProcessedMaterial::ReactorFuel) + usedFuel, suppliedFuel,
         "reactor support conserved");
    near(site.processedStock.get(ProcessedMaterial::IndustrialComposites) + usedComposites + 60,
         suppliedComposites, "assembly and duty composites conserved");
    const auto file =
        std::filesystem::temp_directory_path() /
        ("deep-signal-p4b-integration-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    save::SaveGameRepository::save(file, state);
    SimulationService left(state), right(save::SaveGameRepository::load(file));
    for (int day = 90; day < 360; ++day) {
        ack(left);
        ack(right);
        const auto a = left.advanceDaysDetailed(1), b = right.advanceDaysDetailed(1);
        require(a.advancedDays == 1 && b.advancedDays == 1 && a.interrupted == b.interrupted,
                "one-day360 continuation");
    }
    compare(left.state(), right.state());
    validateGameState(left.state());
    require(left.state().siteDevelopmentPrograms.front().reports.size() == reportCount,
            "closed development stops reports");
    require(left.state().resourceSites.front().reports.size() == 12,
            "site operation continues own30day reporting");
    save::SaveGameRepository::save(file, left.state());
    compare(left.state(), save::SaveGameRepository::load(file));
    std::filesystem::remove(file);
    std::cout << "Earned90 then360-day continuation: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << "s; events=" << left.state().eventLog.size()
              << " attempts=" << left.state().resourceSites.front().extractionReceipts.size() << '\n';
}
void unsuccessful_investment() {
    const auto state = earnSiteDevelopmentFixture(90, false);
    const auto& site = state.resourceSites.front();
    require(site.installed.size() == 5 &&
                state.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed,
            "failed prospect still earned equipment and returned builders");
    require(site.extractionReceipts.size() >= 5 && site.issue.cause == SiteOperatingIssueCause::ZeroRecovery,
            "five actual negative operating attempts interrupt");
    require(std::all_of(site.extractionReceipts.begin(), site.extractionReceipts.end(),
                        [](const auto& r) { return r.recoveredIce == 0; }),
            "absence causes measured zero only");
    require(!site.dutyReceipts.empty() && site.processedStock.get(ProcessedMaterial::ReactorFuel) < 500,
            "zero yield still costs support");
    require(state.freightPrograms.back().cargoDelivered == 0, "no forecast creates nonexistent source cargo");
}
void processing_recovers_existing_intent() {
    auto state = createDelegatedFreightScenario();
    auto& source = state.colonies.at(state.colonies.size() - 2);
    source.processedStockpile = {};
    source.processedStockpile.set(ProcessedMaterial::Electronics, 10);
    source.stockpile = {};
    source.stockpile.set(Mineral::WaterIce, 100);
    source.stockpile.set(Mineral::Volatiles, 50);
    source.processorCapacity = 100;
    source.processingPolicy = ProcessingPolicy::Manual;
    source.manualProcessingAllocations = {{ProcessedMaterial::Propellant, 1}};
    const auto sourceId = source.id;
    SimulationService service(state);
    FreightProgramCharter c;
    c.name = "Previously fuel-blocked freight";
    c.source = sourceId;
    c.destination = state.colonies.back().id;
    c.operatingBaseColonyId = sourceId;
    c.commodity = ProcessedMaterial::Electronics;
    c.totalQuantity = 10;
    c.requestedFleetId = state.fleets.back().id;
    c.requestedLeaderId = state.people.front().id;
    require(service.execute(CreateFreightProgramCommand{c}).ok, "fuel shortage preserves freight intent");
    const auto id = service.state().freightPrograms.front().id;
    require(service.advanceDaysDetailed(1).advancedDays == 1, "processing day elapsed");
    const auto& first = service.state().colonies.at(state.colonies.size() - 2);
    near(first.processedProductionTotals.get(ProcessedMaterial::Propellant), 100,
         "100Ice+50Volatiles produces100 actual fuel");
    near(first.stockpile.get(Mineral::WaterIce), 0, "Ice input exactly debited");
    near(first.stockpile.get(Mineral::Volatiles), 0, "Volatiles coinput exactly debited");
    near(service.state().freightPrograms.front().fuelLoaded, 0, "same opening cannot use later processing");
    for (int i = 0; i < 12 && service.state().freightPrograms.front().fuelLoaded == 0; ++i) {
        ack(service);
        require(service.advanceDaysDetailed(1).advancedDays == 1, "future opening advances");
    }
    require(service.state().freightPrograms.front().id == id &&
                service.state().freightPrograms.front().fuelLoaded > 0,
            "same old intent takes real new fuel later");
    auto precision = state;
    precision.colonies.at(state.colonies.size() - 2)
        .processedStockpile.set(ProcessedMaterial::Propellant, 1e16);
    precision.colonies.at(state.colonies.size() - 2).processorCapacity = 3;
    SimulationService limited(precision);
    require(limited.advanceDaysDetailed(1).advancedDays == 1, "precision-limited day");
    near(limited.state().colonies.at(state.colonies.size() - 2).stockpile.get(Mineral::WaterIce), 100,
         "unrepresentable output consumes no inputs");
    near(limited.state()
             .colonies.at(state.colonies.size() - 2)
             .processedProductionTotals.get(ProcessedMaterial::Propellant),
         0, "unrepresentable production creates no history");
    auto noCoInput = state;
    noCoInput.colonies.at(state.colonies.size() - 2).stockpile.set(Mineral::Volatiles, 0);
    SimulationService dry(noCoInput);
    require(dry.advanceDaysDetailed(1).advancedDays == 1, "coinput shortage day");
    near(dry.state()
             .colonies.at(state.colonies.size() - 2)
             .processedProductionTotals.get(ProcessedMaterial::Propellant),
         0, "Ice alone cannot produce fuel");
}
void additive_expansion() {
    auto state = earnSiteDevelopmentFixture(90);
    const auto original = state.resourceSites.front();
    const auto first = state.siteDevelopmentPrograms.front();
    SimulationService service(state);
    SiteDevelopmentCharter c = first.charter;
    c.package = {{SiteModuleKind::BulkStorage, 1}};
    c.assignments.name = "Earned storage addition";
    require(service.execute(CreateSiteDevelopmentCommand{c, {}}).ok,
            "existing site additive project accepted");
    FreightProgramCharter freight = state.freightPrograms.front().charter;
    freight.name = "Expansion alloys";
    freight.totalQuantity = 40;
    require(service.execute(CreateFreightProgramCommand{freight}).ok,
            "expansion supply independently authorized");
    require(service.state().resourceSites.front().installed == original.installed,
            "authorization creates no capacity");
    for (int i = 0; i < 80 && service.state().siteDevelopmentPrograms.back().lifecycle !=
                                  SiteDevelopmentLifecycle::Closed;
         ++i) {
        ack(service);
        require(service.advanceDaysDetailed(1).advancedDays == 1, "expansion earned day");
        validateGameState(service.state());
    }
    const auto& after = service.state().resourceSites.front();
    require(service.state().siteDevelopmentPrograms.back().lifecycle == SiteDevelopmentLifecycle::Closed &&
                after.installed.size() == 6,
            "addition commissioned once and workforce returned");
    require(std::equal(original.installed.begin(), original.installed.end(), after.installed.begin()),
            "old equipment identity unchanged");
    require(after.operatingPolicy == original.operatingPolicy &&
                std::equal(original.dutyReceipts.begin(), original.dutyReceipts.end(),
                           after.dutyReceipts.begin()),
            "expansion preserves old duty and policy");
    near(siteCapabilities(service.state(), after, service.state().date.day + 1).equipment.rawStorage, 400,
         "additive storage consequence");
}
void shared_engineering_and_typed_decisions() {
    auto state = createSiteDevelopmentScenario();
    const auto home = state.colonies.back().id;
    state.maintenanceTeams.front().colonyId = home;
    state.maintenanceTeams.front().qualifiedFamilies.push_back(*state.siteConstructionFamilyId);
    const auto engineer = state.maintenanceTeams.front().id, unused = state.maintenanceTeams.back().id;
    (void)unused;
    SimulationService service(state);
    require(service
                .execute(CreateShipClassRevisionCommand{
                    "Dual-purpose tender", ShipRole::Freighter, {}, referenceTenderComponents()})
                .ok,
            "tender design admitted");
    require(service.execute(AssignShipyardBuildCommand{home, service.state().shipClasses.back().id, 1}).ok,
            "tender ordered");
    require(service.execute(AssignShipyardBuildCommand{home, state.shipClasses.at(2).id, 1}).ok,
            "builder ordered");
    require(service.advanceDaysDetailed(1).advancedDays == 1, "real workshop hulls constructed");
    MaintenanceProgramCharter maintenance;
    maintenance.name = "Earlier engineering custody";
    maintenance.serviceColonyId = home;
    maintenance.requestedTenderId = service.state().fleets.front().id;
    maintenance.requestedTeamId = engineer;
    maintenance.requestedLeaderId = state.people.front().id;
    require(service.execute(CreateMaintenanceProgramCommand{maintenance}).ok, "maintenance intent accepted");
    SiteDevelopmentCharter c;
    c.supportColonyId = home;
    c.package = referenceSitePackage();
    c.assignments.name = "Waiting same engineer";
    c.assignments.builderId = service.state().fleets.back().id;
    c.assignments.teamId = engineer;
    c.assignments.leaderId = state.people.front().id;
    NewResourceSite n;
    n.bodyId = state.bodies.back().id;
    n.name = "Competing field project";
    require(service.execute(CreateSiteDevelopmentCommand{c, n}).ok,
            "construction remains valid while team contested");
    const auto mid = service.state().maintenancePrograms.front().id;
    const auto did = service.state().siteDevelopmentPrograms.front().id;
    require(mid.value == did.value && ProgramController{mid} != ProgramController{did},
            "equal numeric purpose ids distinct");
    const auto order = programOpeningOrder(service.state());
    require(order.front() == ProgramController{mid} && order.back() == ProgramController{did},
            "fixed head tie order retains maintenance before development");
    require(service.advanceDaysDetailed(1).advancedDays == 1, "arbitration opening elapsed");
    require(controllingEngineeringTeam(service.state(), engineer) == ProgramController{mid} &&
                !service.state().siteDevelopmentPrograms.front().leasedTeamId,
            "one actual engineering owner");
    require(
        siteDevelopmentExecutionCondition(service.state(), service.state().siteDevelopmentPrograms.front())
                .find("maintenance") != std::string::npos,
        "development reports actual engineering custodian");
    auto failed = earnSiteDevelopmentFixture(90, false);
    failed.resourceSites.front().issue.acknowledged = false;
    Simulation direct(failed);
    SimulationService app(failed);
    const auto a = direct.advanceDaysDetailed(30), b = app.advanceDaysDetailed(30);
    require(a.advancedDays == 0 && b.advancedDays == 0 && a.issueSource == b.issueSource && a.issueSource &&
                std::holds_alternative<SiteId>(*a.issueSource) && !a.issueProgramId,
            "typed site issue blocks both entry points without fake program owner");
    const auto commandResult = app.execute(AdvanceDaysCommand{30});
    require(commandResult.ok &&
                commandResult.message.find("Advanced 0 day(s); stopped:") != std::string::npos &&
                app.state().date.day == failed.date.day,
            "command advancement cannot bypass site decision");
    require(app.execute(SuspendSiteOperationCommand{failed.resourceSites.front().id}).ok,
            "suspension responds to disclosed operating issue");
    require(app.advanceDaysDetailed(1).advancedDays == 1,
            "responding with suspended authority releases prior interruption");
    auto useful = earnSiteDevelopmentFixture(90);
    SimulationService policyService(useful);
    // Isolate the operating decision from the legitimate new freight-handler
    // shortage that suspending site support would otherwise cause downstream.
    for (const auto& freight : policyService.state().freightPrograms)
        if (freight.lifecycle != FreightProgramLifecycle::Closed)
            require(policyService.execute(SuspendFreightProgramCommand{freight.id}).ok,
                    "independent collectors suspended for isolated policy check");
    auto policy = useful.resourceSites.front().operatingPolicy;
    policy.lifetimeDutyAllowance = siteDutySpent(useful.resourceSites.front());
    require(
        policyService.execute(AmendSiteOperatingPolicyCommand{useful.resourceSites.front().id, policy}).ok,
        "already-spent duty can be explicitly authorized as limit");
    const auto result = policyService.advanceDaysDetailed(2);
    require(result.advancedDays == 2 && !result.interrupted,
            "explicitly inadequate authority is acknowledged wait, not fresh forced decision");
}
void earned_scientific_evidence_does_not_grant_output() {
    SimulationService service(earnSiteDevelopmentFixture(90));
    const auto base = service.state().colonies.back().id;
    const auto target = service.state().resourceSites.front().bodyId;
    require(service.execute(AssignShipyardBuildCommand{base, service.state().shipClasses.front().id, 1}).ok,
            "real survey cutter ordered for counterfactual knowledge test");
    ack(service);
    require(service.advanceDaysDetailed(1).advancedDays == 1, "cutter physically commissioned");
    const auto fleet = service.state().fleets.back().id;
    require(service.execute(MoveFleetCommand{fleet, target}).ok, "actual instrument deployed to site body");
    for (int i = 0; i < 20 && service.state().fleets.back().currentBodyId != target; ++i) {
        ack(service);
        require(service.advanceDaysDetailed(1).advancedDays == 1, "survey transit elapsed");
    }
    require(service.execute(ResourceSurveyCommand{fleet, target}).ok,
            "scientific observation genuinely acquired");
    auto informed = service.state(), uninformed = informed;
    require(!informed.observations.empty(), "real P4A evidence exists before comparison");
    // Counterfactual worlds differ only in retained organizational knowledge and
    // its audit, not physical instruments, geology, stocks, authority or dates.
    uninformed.observations.clear();
    std::erase_if(uninformed.eventLog, [](const auto& e) {
        return std::holds_alternative<ResourceSurveyCompletedEvent>(e.payload);
    });
    SimulationService a(informed), b(uninformed);
    for (int day = 0; day < 15; ++day) {
        ack(a);
        ack(b);
        require(a.advanceDaysDetailed(1).advancedDays == 1 && b.advanceDaysDetailed(1).advancedDays == 1,
                "evidence comparison advances same physical days");
    }
    require(a.state().resourceSites.front().extractionReceipts ==
                    b.state().resourceSites.front().extractionReceipts &&
                a.state().resourceSites.front().dutyReceipts ==
                    b.state().resourceSites.front().dutyReceipts &&
                a.state().resourceSites.front().rawStock.amount ==
                    b.state().resourceSites.front().rawStock.amount,
            "actual evidence cannot grant a physical extraction bonus");
}
void current_knowledge_equality() {
    auto a = createSiteDevelopmentScenario(true), b = createSiteDevelopmentScenario(false), c = a;
    c.mineralDeposits.back().accessibility = 0;
    SimulationService x(a), y(b), z(c);
    SimulationQueries qx(x), qy(y), qz(z);
    CreateSiteDevelopmentCommand cmd;
    cmd.charter.supportColonyId = a.colonies.back().id;
    cmd.charter.package = referenceSitePackage();
    cmd.charter.assignments.name = "No approval gate";
    NewResourceSite n;
    n.bodyId = a.bodies.back().id;
    n.name = "Unknown prospect";
    cmd.newSite = n;
    const auto px = qx.previewSiteDevelopment(cmd), py = qy.previewSiteDevelopment(cmd),
               pz = qz.previewSiteDevelopment(cmd);
    require(px.structurallyValid && py.structurallyValid && pz.structurallyValid &&
                px.validationMessage == py.validationMessage && px.condition == py.condition &&
                px.condition == pz.condition,
            "hidden geology cannot change admission/preview");
    auto malformed = cmd;
    malformed.charter.siteId = SiteId{99};
    require(!qx.previewSiteDevelopment(malformed).structurallyValid && !x.execute(malformed).ok,
            "preview rejects assigned id for new registration just as command");
    malformed = cmd;
    malformed.newSite->institutionId = InstitutionId{99999};
    require(!qx.previewSiteDevelopment(malformed).structurallyValid && !x.execute(malformed).ok,
            "preview rejects unknown institution just as command");
    for (auto* service : {&x, &y, &z})
        require(service->execute(cmd).ok, "unfunded unsurveyed valid intent accepted");
    const auto sx = qx.sites().front(), sy = qy.sites().front(), sz = qz.sites().front();
    require(sx.condition == sy.condition && sx.condition == sz.condition && sx.site.installed.empty() &&
                sx.rawOccupancy == 0 && sx.installed.rawStorage == 0,
            "cold site views equal and grant nothing");
}
} // namespace
int main() {
    try {
        current_knowledge_equality();
        earned_scientific_evidence_does_not_grant_output();
        shared_engineering_and_typed_decisions();
        processing_recovers_existing_intent();
        earned_and_continued();
        unsuccessful_investment();
        additive_expansion();
        std::cout << "Site integration checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
