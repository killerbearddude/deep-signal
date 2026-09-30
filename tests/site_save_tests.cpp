// P4B-16/38/39/40/41/42: earned construction and real freight checkpoints test
// every durable field independently of the mapper, then continue identical
// worlds. Unique RAII directories keep these tests isolated from other suites.
#include "app/SimulationService.h"
#include "save/Database.h"
#include "save/EventJson.h"
#include "save/SaveGameRepository.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/SiteDevelopmentCommands.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() / "deep-site-save-XXXXXX").string();
        char* created = mkdtemp(pattern.data());
        if (!created)
            throw std::runtime_error("Cannot create unique site test directory");
        path = created;
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
// Direct value fingerprint, not a re-serialization through SaveGameRepository.
// Every field of the new records is included; sequences retain stored order.
struct Values {
    std::ostringstream out;
    Values() {
        out << std::hexfloat;
    }
    template <class T> void q(const T& value) {
        if constexpr (std::is_enum_v<T>)
            out << static_cast<std::int64_t>(value) << '|';
        else if constexpr (requires { value.value; })
            out << value.value << '|';
        else if constexpr (requires { value.amount; }) {
            for (const auto& v : value.amount)
                q(v);
        } else if constexpr (std::is_same_v<T, std::string>)
            out << std::quoted(value) << '|';
        else
            out << value << '|';
    }
    template <class T> void q(const std::optional<T>& value) {
        q(value.has_value());
        if (value)
            q(*value);
    }
    template <class... T> void q(const std::variant<T...>& value) {
        q(value.index());
        std::visit([&](const auto& v) { q(v); }, value);
    }
    template <class T> void q(const std::vector<T>& value) {
        q(value.size());
        for (const auto& v : value)
            q(v);
    }
    template <class... T> void fields(const T&... value) {
        (q(value), ...);
    }
    void policy(const SiteOperatingPolicy& p) {
        fields(p.leaderId, p.enabled, p.requestedIcePerDay, p.reactorFuelFloor, p.compositesFloor,
               p.lifetimeDutyAllowance);
    }
    void policy(const SiteDevelopmentPolicy& p) {
        fields(p.homePropellantFloor, p.additionalPropellantAllowance, p.returnContingencyFraction,
               p.constructionFloors, p.materialAllowances);
    }
};
std::string fingerprint(const GameState& s) {
    Values v;
    v.fields(s.date.day, s.ids.nextSiteId, s.ids.nextSiteDevelopmentProgramId, s.siteConstructionFamilyId,
             s.siteModuleCatalog.size());
    for (const auto& c : s.siteModuleCatalog)
        v.fields(c.kind, c.version, c.cost, c.assemblyWorkdays, c.powerGeneration, c.powerDemand,
                 c.extractionPerDay, c.handlingPerDay, c.rawStorage, c.supportedExtractors,
                 c.reactorFuelPerDuty, c.compositesPerDuty);
    v.q(s.resourceSites.size());
    for (const auto& site : s.resourceSites) {
        v.fields(site.id, site.bodyId, site.name, site.createdDay, site.institutionId, site.processedStock,
                 site.rawStock, site.installed.size());
        for (const auto& g : site.installed)
            v.fields(g.programId, g.packageRow, g.catalogVersion, g.kind, g.quantity, g.commissionedDay);
        v.policy(site.operatingPolicy);
        v.fields(site.operatingRevision, site.reportStartDay, site.nextReportDay, site.issue.cause,
                 site.issue.episodeStartedDay, site.issue.message, site.issue.acknowledged,
                 site.dutyReceipts.size());
        for (const auto& r : site.dutyReceipts) {
            v.fields(r.day, r.operatingRevision, r.leaderId, r.installedGroupCutoff, r.duty, r.reactorFuel,
                     r.composites, r.rawHandling);
            v.policy(r.policy);
        }
        v.q(site.extractionReceipts.size());
        for (const auto& r : site.extractionReceipts)
            v.fields(r.day, r.operatingRevision, r.leaderId, r.installedGroupCutoff, r.nominalAttempt,
                     r.recoveredIce, r.freeRawRoom, r.availableHandling, r.observation);
        v.q(site.reports.size());
        for (const auto& r : site.reports) {
            v.fields(r.startDay, r.endDay, r.isNinetyDayReview, r.operatingRevision);
            v.policy(r.policy);
            v.fields(r.installedGroupCutoff, r.supportedDuty, r.reactorFuel, r.composites, r.attempts,
                     r.recoveredIce, r.rawOccupancy, r.rawCapacity, r.exportedIce, r.deliveredIce,
                     r.waitingReason, r.auditThroughId);
        }
    }
    v.q(s.siteDevelopmentPrograms.size());
    for (const auto& p : s.siteDevelopmentPrograms) {
        const auto& a = p.charter.assignments;
        v.fields(p.id, p.charter.siteId, p.charter.supportColonyId, p.charter.catalogVersion,
                 p.charter.package.size());
        for (const auto& row : p.charter.package)
            v.fields(row.kind, row.quantity);
        v.fields(a.name, a.builderId, a.teamId, a.leaderId);
        v.policy(a.policy);
        v.fields(p.createdDay, p.charterRevision, p.lifecycle, p.closure, p.closedDay, p.leasedBuilderId,
                 p.leasedTeamId, p.holdsSiteConstruction, p.task, p.taskBuilderId, p.taskTeamId,
                 p.rows.size());
        for (const auto& row : p.rows)
            v.fields(row.workCompleted, row.consumed, row.workshopShipId);
        v.fields(p.commissioningWork, p.commissioningWorkshopId, p.commissionedDay, p.workReceipts.size());
        for (const auto& r : p.workReceipts)
            v.fields(r.day, r.charterRevision, r.kind, r.packageRow, r.builderId, r.workshopShipId, r.teamId,
                     r.leaderId, r.work, r.consumed);
        v.fields(p.fuelLoaded, p.fuelBurned, p.reportStartDay, p.nextReportDay, p.issue.signature,
                 p.issue.message, p.issue.acknowledged, p.reports.size());
        for (const auto& r : p.reports) {
            v.fields(r.startDay, r.endDay, r.isNinetyDayReview, r.charterRevision);
            v.policy(r.policy);
            v.fields(r.assemblyWork, r.commissioningWork, r.consumed, r.fuelLoaded, r.fuelBurned, r.builderId,
                     r.teamId, r.bodyId, r.commissioned, r.waitingReason, r.auditThroughId);
        }
    }
    // These existing records are the real other ends of site stock and labor.
    for (const auto& c : s.colonies)
        v.fields(c.id, c.stockpile, c.processedStockpile, c.processedProductionTotals);
    for (const auto& d : s.mineralDeposits)
        v.fields(d.bodyId, d.mineral, d.remaining, d.accessibility);
    for (const auto& team : s.maintenanceTeams)
        v.fields(team.id, team.location, team.colonyId, team.fleetId, team.qualifiedFamilies,
                 team.workdaysPerDay);
    for (const auto& ship : s.ships) {
        v.fields(ship.id, ship.shipClassId, ship.fleetId, ship.fuel, ship.cargo.has_value());
        if (ship.cargo)
            v.fields(ship.cargo->programId, ship.cargo->shipmentNumber, ship.cargo->commodity,
                     ship.cargo->quantity);
    }
    for (const auto& fleet : s.fleets) {
        const auto& o = fleet.activeOrder;
        v.fields(fleet.id, fleet.currentBodyId, fleet.destinationBodyId, fleet.shipIds, o.type,
                 o.targetBodyId, o.daysRemaining, o.departureBodyId, o.departureDay, o.arrivalDay,
                 o.departurePosition.x, o.departurePosition.y, o.projectedArrivalPosition.x,
                 o.projectedArrivalPosition.y, o.transitDistanceKm, o.burnAccelerationG,
                 o.routeCurveControlPoint.x, o.routeCurveControlPoint.y);
    }
    for (const auto& p : s.freightPrograms) {
        v.fields(p.id, p.charter.source, p.charter.destination, p.charter.operatingBaseColonyId,
                 p.charter.commodity, p.charter.totalQuantity, p.lifecycle, p.closure, p.closedDay,
                 p.leasedFleetId, p.task, p.taskFleetId, p.cargoLoaded, p.cargoDelivered, p.cargoReturned,
                 p.fuelLoaded, p.fuelBurned, p.issue.signature, p.issue.message, p.issue.acknowledged,
                 p.receipts.size());
        for (const auto& r : p.receipts)
            v.fields(r.sequence, r.shipmentNumber, r.day, r.fleetId, r.leaderId, r.location, r.commodity,
                     r.kind, r.amount);
    }
    v.q(s.eventLog.size());
    for (const auto& e : s.eventLog)
        v.fields(e.id, e.day, e.severity, save::eventTypeName(e.payload),
                 save::eventPayloadToJson(e.payload));
    return v.out.str();
}

GameState fixture() {
    auto s = createDelegatedFreightScenario();
    s.siteModuleCatalog = referenceSiteModuleCatalog();
    const EquipmentFamilyId family{s.ids.nextEquipmentFamilyId++};
    s.equipmentFamilies.push_back({family, "Persistence construction family"});
    s.siteConstructionFamilyId = family;
    ShipComponentDefinition workshop;
    workshop.id = ShipComponentId{s.ids.nextShipComponentId++};
    workshop.name = "Persistence workshop";
    workshop.kind = ShipComponentKind::Workshop;
    workshop.mass = 100;
    workshop.volume = 150;
    workshop.powerDemand = 30;
    workshop.buildPoints = 100;
    workshop.workshopRates = {{family, 1}};
    s.shipComponents.push_back(workshop);
    auto builder = s.shipClasses.front();
    builder.id = ShipClassId{s.ids.nextShipClassId++};
    builder.name = "Persistence builder";
    builder.role = ShipRole::Builder;
    builder.basedOnClassId.reset();
    builder.revision = 1;
    std::erase_if(builder.components, [&](const auto& install) {
        return std::any_of(s.shipComponents.begin(), s.shipComponents.end(), [&](const auto& c) {
            return c.id == install.componentId && c.kind == ShipComponentKind::SurveySensor;
        });
    });
    builder.components.push_back({workshop.id, 1});
    s.shipClasses.push_back(builder);
    const FleetId fleet{s.ids.nextFleetId++};
    const ShipId ship{s.ids.nextShipId++};
    const auto base = s.colonies.at(s.colonies.size() - 2).id;
    const auto body = s.colonies.at(s.colonies.size() - 2).bodyId;
    s.ships.push_back(Ship{
        .id = ship, .shipClassId = builder.id, .name = "Finite field builder", .fleetId = fleet, .fuel = 0});
    initializeShipEquipmentCondition(s, s.ships.back());
    s.fleets.push_back(Fleet{.id = fleet,
                             .name = "Finite construction fleet",
                             .currentBodyId = body,
                             .destinationBodyId = std::nullopt,
                             .shipIds = {ship},
                             .activeOrder = {},
                             .queuedOrders = {},
                             .ownerInstitutionId = std::nullopt});
    MaintenanceTeam team;
    team.id = MaintenanceTeamId{s.ids.nextMaintenanceTeamId++};
    team.name = "Finite field engineers";
    team.qualifiedFamilies = {family};
    team.colonyId = base;
    s.maintenanceTeams.push_back(team);
    auto& colony = s.colonies.at(s.colonies.size() - 2);
    for (double& amount : colony.processedStockpile.amount)
        amount = 1000;
    colony.stockpile.set(Mineral::Volatiles, 50);
    colony.processorCapacity = 10;
    colony.processingPolicy = ProcessingPolicy::Manual;
    colony.manualProcessingAllocations = {{ProcessedMaterial::Propellant, 1}};
    s.mineralDeposits.push_back({s.bodies.back().id, Mineral::WaterIce, 10000, 1});
    return s;
}
void acknowledge(Simulation& sim) {
    // Exercise typed command acknowledgment without silently changing limits.
    for (const auto& p : sim.state().siteDevelopmentPrograms)
        if (!p.issue.acknowledged)
            require(sim.execute(AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature}).ok,
                    "acknowledge construction decision");
    for (const auto& p : sim.state().freightPrograms)
        if (!p.issue.acknowledged)
            require(sim.execute(AcknowledgeFreightProgramIssueCommand{p.id, p.issue.signature}).ok,
                    "acknowledge freight decision");
    for (const auto& site : sim.state().resourceSites)
        if (!site.issue.acknowledged)
            require(sim.execute(AcknowledgeSiteOperatingIssueCommand{site.id, site.issue.cause,
                                                                     site.issue.episodeStartedDay})
                        .ok,
                    "acknowledge operating decision");
}
void checkpoint(const GameState& state, const std::filesystem::path& path) {
    save::SaveGameRepository::save(path, state);
    auto restored = save::SaveGameRepository::load(path);
    require(fingerprint(state) == fingerprint(restored),
            "direct site/cargo/work/history field round trip differs");
    Simulation left(state), right(restored);
    acknowledge(left);
    acknowledge(right);
    const auto a = left.advanceDaysDetailed(3), b = right.advanceDaysDetailed(3);
    require(a.advancedDays == b.advancedDays && a.interrupted == b.interrupted,
            "post-load decision boundary differs");
    require(fingerprint(left.state()) == fingerprint(right.state()), "same-input site continuation differs");
}
GameState earnedCheckpoints(const std::filesystem::path& path) {
    Simulation sim(fixture());
    const auto& initial = sim.state();
    SiteDevelopmentCharter c;
    c.supportColonyId = initial.colonies.at(initial.colonies.size() - 2).id;
    c.package = referenceSitePackage();
    c.assignments.name = "Earned saved development";
    c.assignments.builderId = initial.fleets.back().id;
    c.assignments.teamId = initial.maintenanceTeams.back().id;
    c.assignments.leaderId = initial.people.front().id;
    c.assignments.policy.additionalPropellantAllowance = 20;
    c.assignments.policy.materialAllowances = ProcessedMaterialSet{};
    for (double& limit : c.assignments.policy.materialAllowances->amount)
        limit = 1000;
    NewResourceSite site;
    site.bodyId = initial.bodies.back().id;
    site.name = "Uninvestigated save site";
    site.operatingPolicy.leaderId = initial.people.front().id;
    site.operatingPolicy.lifetimeDutyAllowance = 100;
    require(sim.execute(CreateSiteDevelopmentCommand{c, site}).ok, "atomic cold registration accepted");
    checkpoint(sim.state(), path);
    const auto siteId = sim.state().resourceSites.back().id;
    const auto freightFleet = initial.fleets.at(initial.fleets.size() - 2).id;
    for (const auto& [material, amount] : {std::pair{ProcessedMaterial::StructuralAlloys, 240.0},
                                           std::pair{ProcessedMaterial::Electronics, 70.0},
                                           std::pair{ProcessedMaterial::IndustrialComposites, 100.0},
                                           std::pair{ProcessedMaterial::ReactorFuel, 40.0}}) {
        FreightProgramCharter cargo;
        cargo.name = "Real construction/support shipment";
        cargo.source = c.supportColonyId;
        cargo.destination = siteId;
        cargo.operatingBaseColonyId = c.supportColonyId;
        cargo.commodity = material;
        cargo.totalQuantity = amount;
        cargo.requestedFleetId = freightFleet;
        cargo.requestedLeaderId = initial.people.front().id;
        require(sim.execute(CreateFreightProgramCommand{cargo}).ok, "actual material delivery authorized");
    }
    FreightProgramCharter collect;
    collect.name = "Collect actually recovered Ice";
    collect.source = siteId;
    collect.destination = c.supportColonyId;
    collect.operatingBaseColonyId = c.supportColonyId;
    collect.commodity = Mineral::WaterIce;
    collect.totalQuantity = 100;
    collect.requestedFleetId = freightFleet;
    collect.requestedLeaderId = initial.people.front().id;
    require(sim.execute(CreateFreightProgramCommand{collect}).ok, "waiting raw collection authorized once");
    bool partial = false, commissioning = false, commissioned = false, returned = false, operation = false;
    bool partialDuty = false;
    bool suspendedTransit = false, suspendedWork = false, suspendedCommissioning = false;
    bool collecting = false, rawLoading = false, rawOutbound = false, rawUnloading = false,
         rawSourceReturn = false;
    for (int day = 0; day < 120; ++day) {
        acknowledge(sim);
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "earned world advances one complete day");
        const auto& project = sim.state().siteDevelopmentPrograms.front();
        const auto& current = sim.state().resourceSites.front();
        const bool pauseNow =
            (!suspendedTransit && project.task == SiteDevelopmentTask::Outbound) ||
            (!suspendedWork && !project.workReceipts.empty() && project.commissioningWork == 0) ||
            (!suspendedCommissioning && project.commissioningWork > 0 && !project.commissionedDay);
        if (pauseNow) {
            Simulation paused(sim.state());
            require(paused.execute(SuspendSiteDevelopmentCommand{project.id}).ok,
                    "suspend actual builder phase");
            checkpoint(paused.state(), path);
            require(paused.execute(ResumeSiteDevelopmentCommand{project.id}).ok,
                    "resume retained builder phase");
            checkpoint(paused.state(), path);
            if (project.task == SiteDevelopmentTask::Outbound)
                suspendedTransit = true;
            else if (project.commissioningWork > 0)
                suspendedCommissioning = true;
            else {
                suspendedWork = true;
                require(paused.execute(CancelSiteDevelopmentCommand{project.id}).ok,
                        "cancel sunk partial construction");
                checkpoint(paused.state(), path);
            }
        }
        partial = partial || (!project.workReceipts.empty() && !project.commissionedDay);
        commissioning = commissioning || (project.commissioningWork > 0 && !project.commissionedDay);
        commissioned = commissioned || project.commissionedDay.has_value();
        returned = returned || project.lifecycle == SiteDevelopmentLifecycle::Closed;
        operation = operation || !current.dutyReceipts.empty();
        if (!partialDuty && !current.dutyReceipts.empty() && current.dutyReceipts.size() == 1) {
            // A real policy amendment bounds the next supported day to half
            // duty. Neither stocks nor fabricated receipts are inserted here.
            Simulation fraction(sim.state());
            auto limited = current.operatingPolicy;
            limited.lifetimeDutyAllowance = current.dutyReceipts.front().duty + 0.5;
            require(fraction.execute(AmendSiteOperatingPolicyCommand{siteId, limited}).ok,
                    "authorize finite half-duty remainder");
            acknowledge(fraction);
            require(fraction.advanceDaysDetailed(1).advancedDays == 1, "half-duty day advances");
            require(fraction.state().resourceSites.front().dutyReceipts.back().duty == 0.5,
                    "actual duty is bounded by the remaining half-day authority");
            checkpoint(fraction.state(), path);
            fraction.advanceDays(1);
            checkpoint(fraction.state(), path);
            partialDuty = true;
        }
        const auto& collector = sim.state().freightPrograms.back();
        collecting = collecting || collector.task == FreightProgramTask::Collecting;
        rawLoading = rawLoading || collector.task == FreightProgramTask::Loading;
        rawOutbound = rawOutbound || collector.task == FreightProgramTask::Outbound;
        rawUnloading = rawUnloading || collector.task == FreightProgramTask::Unloading;
        if (!rawSourceReturn && collector.task == FreightProgramTask::Loading && collector.cargoLoaded > 0) {
            // Branch a real partly loaded collection. Cancellation must persist
            // source restitution with actual raw space/handling, never disposal.
            Simulation canceled(sim.state());
            require(canceled.execute(CancelFreightProgramCommand{collector.id}).ok,
                    "cancel committed raw pickup");
            checkpoint(canceled.state(), path);
            acknowledge(canceled);
            canceled.advanceDays(1);
            checkpoint(canceled.state(), path);
            rawSourceReturn = true;
        }
        // Every early day captures embarking/refueling/flight, cargo, partial
        // rows, commissioning and first eligible operation with no fake assets.
        checkpoint(sim.state(), path);
    }
    require(partial && commissioning && commissioned && returned && operation && partialDuty &&
                suspendedTransit && suspendedWork && suspendedCommissioning,
            "earned persistence run missed a required physical phase");
    require(collecting && rawLoading && rawOutbound && rawUnloading && rawSourceReturn,
            "earned save run missed a raw collection/cancellation custody phase");
    require(sim.state().freightPrograms.back().cargoDelivered == 100 &&
                sim.state()
                        .colonies.at(sim.state().colonies.size() - 2)
                        .processedProductionTotals.get(ProcessedMaterial::Propellant) == 100,
            "real collected Ice and50 co-input produce100 cumulative Propellant without double credit");
    require(sim.state().resourceSites.front().installed.size() == 5,
            "saved commissioning installs exactly five earned groups");
    require(!sim.state().resourceSites.front().extractionReceipts.empty(),
            "operating result records really exist");
    auto policy = sim.state().resourceSites.front().operatingPolicy;
    policy.reactorFuelFloor = 1000;
    require(sim.execute(AmendSiteOperatingPolicyCommand{siteId, policy}).ok,
            "same-day later policy change accepted");
    checkpoint(sim.state(), path);
    // An earned additive expansion preserves earlier operating snapshots and
    // their installed-count cutoff instead of recasting history with new bins.
    const auto oldReport = sim.state().resourceSites.front().reports.front();
    c.siteId = siteId;
    c.package = {{SiteModuleKind::BulkStorage, 1}};
    c.assignments.name = "Saved additive storage";
    require(sim.execute(CreateSiteDevelopmentCommand{c, std::nullopt}).ok, "authorize paid additive storage");
    for (const auto& [material, amount] : {std::pair{ProcessedMaterial::StructuralAlloys, 40.0},
                                           std::pair{ProcessedMaterial::IndustrialComposites, 20.0}}) {
        auto delivery = sim.state().freightPrograms.front().charter;
        delivery.name = "Expansion inputs";
        delivery.commodity = material;
        delivery.totalQuantity = amount;
        require(sim.execute(CreateFreightProgramCommand{delivery}).ok,
                "authorize real expansion input delivery");
    }
    for (int day = 0; day < 80 && !sim.state().siteDevelopmentPrograms.back().commissionedDay; ++day) {
        acknowledge(sim);
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "expansion advances actual work and supply");
        checkpoint(sim.state(), path);
    }
    require(sim.state().resourceSites.front().installed.size() == 6,
            "storage expansion earns one additional group");
    const auto& historical = sim.state().resourceSites.front().reports.front();
    require(historical.rawCapacity == oldReport.rawCapacity &&
                historical.installedGroupCutoff == oldReport.installedGroupCutoff &&
                historical.auditThroughId == oldReport.auditThroughId &&
                historical.policy == oldReport.policy,
            "expansion and later policy cannot rewrite a published report cutoff");
    acknowledge(sim);
    sim.advanceDays(static_cast<int>(360 - sim.state().date.day));
    require(sim.state().date.day == 360, "bounded long continuation reaches day360");
    checkpoint(sim.state(), path);
    return sim.state();
}
void malformedAndOrdering(const GameState& earned, const std::filesystem::path& path,
                          const std::filesystem::path& directory) {
    // Stored catalog/record order is not replaced by enum or ID sorting.
    auto reordered = earned;
    std::reverse(reordered.siteModuleCatalog.begin(), reordered.siteModuleCatalog.end());
    checkpoint(reordered, path);
    const auto clean = fingerprint(reordered);
    const std::vector<std::string> mutations{
        "UPDATE resource_sites SET ordinal=0.5;",
        "UPDATE resource_sites SET ordinal=5;",
        "UPDATE resource_sites SET raw_0=1e99;",
        "UPDATE site_development_rows SET work_completed=work_completed+0.25 WHERE ordinal=0;",
        "UPDATE site_development_work SET consumed_0=consumed_0+1 WHERE ordinal=0;",
        "UPDATE site_installed_modules SET commissioned_day=commissioned_day+1 WHERE ordinal=0;",
        "UPDATE site_duty_receipts SET installed_cutoff=9999 WHERE ordinal=0;",
        "UPDATE site_operating_reports SET recovered_ice=recovered_ice+1 WHERE ordinal=0;",
        // Published authority remains subject to numeric validation even after
        // the current charter changes; an old report is not a validation bypass.
        "UPDATE site_development_reports SET propellant_floor=-1 WHERE ordinal=0;",
        "UPDATE site_development_reports SET material_allowance_0=-1 WHERE ordinal=0;",
        "UPDATE freight_programs SET source_kind=99;",
        "UPDATE freight_programs SET commodity_kind=99;",
        "UPDATE colony_processing_totals SET amount=-1 WHERE material=0;",
        "DELETE FROM colony_processing_totals WHERE rowid=(SELECT rowid FROM colony_processing_totals LIMIT "
        "1);"};
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto bad = directory / ("bad-" + std::to_string(index) + ".sqlite");
        std::filesystem::copy_file(path, bad);
        {
            save::Database db(bad);
            db.execute("PRAGMA ignore_check_constraints=ON;");
            db.execute(mutations[index]);
        }
        SimulationService active(reordered);
        const auto result = active.loadGame(bad);
        if (result.ok || fingerprint(active.state()) != clean)
            throw std::runtime_error("Malformed v18 case " + std::to_string(index) +
                                     " accepted or changed active game: " + mutations[index] + " / " +
                                     result.message);
        bool refused = false;
        try {
            save::SaveGameRepository::save(bad, reordered);
        } catch (const std::exception&) {
            refused = true;
        }
        require(refused, "malformed v18 destination was silently repaired by Save");
    }
    // Rejected new input is validated before opening the existing destination.
    auto invalid = reordered;
    invalid.resourceSites.front().rawStock.set(Mineral::Iron, 1e99);
    bool rejected = false;
    try {
        save::SaveGameRepository::save(path, invalid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && fingerprint(save::SaveGameRepository::load(path)) == clean,
            "invalid source replaced a valid snapshot");
}
} // namespace
int main() {
    try {
        TempDirectory temp;
        const auto path = temp.path / "current.sqlite";
        const auto earned = earnedCheckpoints(path);
        malformedAndOrdering(earned, path, temp.path);
        std::cout << "Site persistence: earned checkpoints, continuation, ordering and malformed protection "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Site persistence failure: " << error.what() << '\n';
        return 1;
    }
}
