// Focused P4B operation proofs use detached commissioned hardware to isolate
// phase accounting. Earned construction and campaign movement are tested by
// the integrated site scenario, not implied by these arithmetic fixtures.
#include "sim/SiteOperationExecution.h"
#include "sim/SiteOperationRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/FreightProgramExecution.h"
#include "sim/FreightProgramRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 1e-8, message);
}
GameState fixture(double accessibility = 1, double remaining = 1000) {
    auto state = createDelegatedFreightScenario();
    state.date.day = 1;
    state.siteModuleCatalog = referenceSiteModuleCatalog();
    ResourceSite site;
    site.id = {1};
    site.bodyId = state.fleets.back().currentBodyId;
    site.name = "Detached operation proof";
    site.operatingPolicy.leaderId = state.people.front().id;
    site.processedStock.set(ProcessedMaterial::ReactorFuel, 100);
    site.processedStock.set(ProcessedMaterial::IndustrialComposites, 100);
    for (const auto& row : referenceSitePackage())
        site.installed.push_back(
            {{1}, static_cast<int>(site.installed.size()), 1, row.kind, row.quantity, 0});
    state.resourceSites.push_back(site);
    std::erase_if(state.mineralDeposits,
                  [&](const auto& d) { return d.bodyId == site.bodyId && d.mineral == Mineral::WaterIce; });
    state.mineralDeposits.push_back({site.bodyId, Mineral::WaterIce, remaining, accessibility});
    return state;
}
SiteOperationHooks hooks(GameState& state) {
    return {[&state](EventSeverity severity, SimEventPayload payload) {
                state.eventLog.push_back(
                    {EventId{state.ids.nextEventId++}, state.date.day, severity, std::move(payload)});
            },
            [&state](std::size_t count) { state.eventLog.reserve(state.eventLog.size() + count); }};
}
void operate(GameState& state) {
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    runSitesExtractionDay(state, opening, h);
    finishSitesDay(state, h);
}
void duty_authority_and_phase_stock() {
    auto state = fixture();
    auto& site = state.resourceSites.front();
    site.processedStock.set(ProcessedMaterial::ReactorFuel, 2);
    site.processedStock.set(ProcessedMaterial::IndustrialComposites, 5);
    site.operatingPolicy.reactorFuelFloor = 1.5;
    site.operatingPolicy.compositesFloor = 4;
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    near(site.dutyReceipts.back().duty, .5, "reactor floor bounds partial paid duty");
    near(site.processedStock.get(ProcessedMaterial::ReactorFuel), 1.5,
         "partial duty preserves reactor floor");
    near(site.processedStock.get(ProcessedMaterial::IndustrialComposites), 4.5,
         "support costs scale proportionally");
    near(opening.availableSiteRawHandling(site.id), 25, "half duty funds one shared25-unit handler");
    runSitesExtractionDay(state, opening, h);
    near(site.rawStock.get(Mineral::WaterIce), 5, "half duty bounds nominal extraction before geology");
    ++state.date.day;
    site.operatingPolicy.reactorFuelFloor = 0;
    site.operatingPolicy.compositesFloor = 0;
    site.operatingPolicy.lifetimeDutyAllowance = .75;
    operate(state);
    near(site.dutyReceipts.back().duty, .25, "lifetime authority accounts for previous actual duty");
    const auto count = site.dutyReceipts.size();
    ++state.date.day;
    operate(state);
    require(site.dutyReceipts.size() == count, "zero remaining authority incurs no support debit");
    auto incoming = fixture();
    incoming.resourceSites.front().processedStock = {};
    OpeningProgramContext beforeDelivery{incoming};
    incoming.resourceSites.front().processedStock.set(ProcessedMaterial::ReactorFuel, 20);
    incoming.resourceSites.front().processedStock.set(ProcessedMaterial::IndustrialComposites, 20);
    auto inboundHooks = hooks(incoming);
    runSitesOpeningDay(incoming, beforeDelivery, inboundHooks);
    require(incoming.resourceSites.front().dutyReceipts.empty(),
            "same-opening inbound support cannot fund duty");
    ++incoming.date.day;
    operate(incoming);
    near(incoming.resourceSites.front().dutyReceipts.back().duty, 1,
         "actual inbound support becomes spendable next opening");
}
void actual_geology_and_non_attempts() {
    auto useful = fixture(.5), absent = fixture(.5), inaccessible = fixture(0);
    absent.mineralDeposits.clear();
    require(siteOperatingCondition(useful, useful.resourceSites.front()) ==
                    siteOperatingCondition(absent, absent.resourceSites.front()) &&
                siteOperatingCondition(useful, useful.resourceSites.front()) ==
                    siteOperatingCondition(inaccessible, inaccessible.resourceSites.front()),
            "pre-interaction readiness does not reveal physical deposits");
    operate(useful);
    operate(absent);
    operate(inaccessible);
    near(useful.resourceSites.front().rawStock.get(Mineral::WaterIce), 5, "A=.5 produces half nominal ice");
    for (const auto* state : {&absent, &inaccessible}) {
        const auto& site = state->resourceSites.front();
        near(site.dutyReceipts.front().reactorFuel, 1, "zero recovery still pays positive supported duty");
        require(site.extractionReceipts.size() == 1 && site.extractionReceipts.front().recoveredIce == 0,
                "absent and inaccessible sites record genuine measured zeros");
    }
    auto saturated = fixture(2, 3);
    operate(saturated);
    near(saturated.resourceSites.front().rawStock.get(Mineral::WaterIce), 3,
         "accessibility saturates and remaining reserve bounds actual debit");
    near(saturated.mineralDeposits.back().remaining, 0, "recovery never overdraws reserve");
    auto full = fixture();
    full.resourceSites.front().rawStock.set(Mineral::Iron, 200);
    operate(full);
    require(full.resourceSites.front().extractionReceipts.empty() &&
                full.resourceSites.front().dutyReceipts.size() == 1,
            "full shared bin pays duty but is not a geological attempt");
    auto zero = fixture();
    zero.resourceSites.front().operatingPolicy.requestedIcePerDay = 0;
    operate(zero);
    require(zero.resourceSites.front().extractionReceipts.empty() &&
                zero.resourceSites.front().dutyReceipts.size() == 1,
            "zero extraction target retains paid handler without false evidence");
    const auto measured = useful.resourceSites.front().extractionReceipts.front();
    useful.mineralDeposits.back().remaining = 0;
    useful.mineralDeposits.back().accessibility = 0;
    require(useful.resourceSites.front().extractionReceipts.front() == measured,
            "later hidden geology changes never rewrite stored operating evidence");
}
void nominal_duty_precedes_recovery_caps() {
    // Independent §9.2 arithmetic distinguishes attempted throughput from the
    // physical recovered amount; storage/handling must not discount geology twice.
    auto partial = fixture();
    partial.resourceSites.front().operatingPolicy.requestedIcePerDay = 5;
    partial.resourceSites.front().operatingPolicy.lifetimeDutyAllowance = .5;
    operate(partial);
    near(partial.resourceSites.front().extractionReceipts.back().nominalAttempt, 2.5,
         "half duty times min rated10 target5 attempts2.5");
    near(partial.resourceSites.front().rawStock.get(Mineral::WaterIce), 2.5,
         "partial target and duty scale together");
    auto room = fixture(.5);
    room.resourceSites.front().rawStock.set(Mineral::Iron, 195);
    operate(room);
    near(room.resourceSites.front().extractionReceipts.back().nominalAttempt, 10,
         "remaining5 room does not pre-cap nominal10 attempt");
    near(room.resourceSites.front().rawStock.get(Mineral::WaterIce), 5,
         "nominal10 times accessibility.5 yields5 into remaining5 room");
    auto handling = fixture(.5);
    OpeningProgramContext opening{handling};
    auto h = hooks(handling);
    runSitesOpeningDay(handling, opening, h);
    opening.debitSiteRawHandling(handling.resourceSites.front().id, 45);
    runSitesExtractionDay(handling, opening, h);
    near(handling.resourceSites.front().extractionReceipts.back().nominalAttempt, 10,
         "remaining5 handler does not pre-cap nominal10 attempt");
    near(handling.resourceSites.front().rawStock.get(Mineral::WaterIce), 5,
         "accessibility applies before recovered output handler cap");
}
void commissioning_and_shared_raw_pool() {
    auto fresh = fixture();
    for (auto& group : fresh.resourceSites.front().installed)
        group.commissionedDay = fresh.date.day;
    operate(fresh);
    require(fresh.resourceSites.front().dutyReceipts.empty(),
            "today's commissioned package cannot operate at same opening");
    ++fresh.date.day;
    operate(fresh);
    near(fresh.resourceSites.front().rawStock.get(Mineral::WaterIce), 10,
         "commissioned package first operates next opening");
    auto state = fixture();
    auto& site = state.resourceSites.front();
    site.rawStock.set(Mineral::WaterIce, 200);
    FreightProgram freight;
    freight.id = {1};
    freight.charter.name = "Shared raw export";
    freight.charter.source = site.id;
    freight.charter.destination = state.colonies.at(state.colonies.size() - 2).id;
    freight.charter.operatingBaseColonyId = std::get<ColonyId>(freight.charter.destination);
    freight.charter.commodity = Mineral::WaterIce;
    freight.charter.totalQuantity = 45;
    freight.charter.requestedLeaderId = state.people.front().id;
    freight.charter.requestedFleetId = state.fleets.back().id;
    freight.leasedFleetId = state.fleets.back().id;
    freight.taskFleetId = freight.leasedFleetId;
    freight.task = FreightProgramTask::Loading;
    freight.nextShipmentNumber = 2;
    freight.shipment = FreightShipment{1,
                                       1,
                                       0,
                                       *freight.leasedFleetId,
                                       *freight.charter.requestedLeaderId,
                                       freight.charter.source,
                                       freight.charter.destination,
                                       freight.charter.operatingBaseColonyId,
                                       freight.charter.commodity,
                                       {{state.ships.back().id, 45}}};
    state.freightPrograms.push_back(freight);
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    FreightProgramExecutionHooks freightHooks{
        [](FreightProgramId, FleetId, BodyId, double&) { return false; }, h.emit};
    runFreightProgramOpeningDay(state, state.freightPrograms.back(), opening, freightHooks);
    near(freightCargoAboard(state, freight.id), 45, "actual freight export spends45 of shared50 handling");
    near(opening.availableSiteRawRoom(site.id), 0,
         "outgoing raw load never credits opening freight receiving room");
    runSitesExtractionDay(state, opening, h);
    near(site.extractionReceipts.back().recoveredIce, 5,
         "later extraction sees actual room but only remaining5 handler");
    near(site.rawStock.get(Mineral::WaterIce), 160, "export45 plus extraction5 conserves site bin");
    near(opening.availableSiteRawHandling(site.id), 0,
         "freight and extraction together consume one50-unit site pool");
}
void physical_precision_and_missing_capability() {
    // A positive site credit must have a representable matching deposit debit.
    // Support paid at opening is sunk even if extraction itself cannot execute.
    auto state = fixture(1, 1e300);
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    runSitesExtractionDay(state, opening, h);
    near(state.resourceSites.front().rawStock.get(Mineral::WaterIce), 0,
         "failed deposit preflight creates no ice");
    require(state.mineralDeposits.back().remaining == 1e300 &&
                state.resourceSites.front().extractionReceipts.empty(),
            "failed deposit preflight retains physical reserve and evidence history");
    auto terminal = fixture();
    terminal.date.day = std::numeric_limits<std::int64_t>::max();
    require(!siteOperatingCondition(terminal, terminal.resourceSites.front()).empty(),
            "terminal-date readiness does not overflow next-opening arithmetic");
    auto terminalHooks = hooks(terminal);
    finishSitesDay(terminal, terminalHooks);
    for (int missing = 0; missing < 3; ++missing) {
        auto cold = fixture();
        auto& site = cold.resourceSites.front();
        if (missing == 0)
            site.operatingPolicy.leaderId.reset();
        else
            std::erase_if(site.installed, [&](const auto& group) {
                return group.kind ==
                       (missing == 1 ? SiteModuleKind::Power : SiteModuleKind::AutomationSupport);
            });
        operate(cold);
        require(site.dutyReceipts.empty() && site.extractionReceipts.empty(),
                "missing leader power or support incurs no duty and no geological attempt");
        near(site.processedStock.get(ProcessedMaterial::ReactorFuel), 100,
             "non-operation preserves actual support stock");
    }
}
void actual_zero_episode_and_shared_reserve() {
    auto state = fixture(0);
    auto h = hooks(state);
    for (int attempt = 0; attempt < 4; ++attempt) {
        operate(state);
        ++state.date.day;
    }
    require(state.resourceSites.front().issue.cause == SiteOperatingIssueCause::None,
            "four genuine zero attempts do not trigger five-attempt episode");
    state.resourceSites.front().operatingPolicy.enabled = false;
    operate(state);
    ++state.date.day;
    require(state.resourceSites.front().extractionReceipts.size() == 4,
            "disabled calendar day is not a negative attempt");
    state.resourceSites.front().operatingPolicy.enabled = true;
    operate(state);
    auto& site = state.resourceSites.front();
    require(site.issue.cause == SiteOperatingIssueCause::ZeroRecovery && !site.issue.acknowledged,
            "fifth actual zero creates dated interruption");
    const auto episode = site.issue.episodeStartedDay;
    site.issue.acknowledged = true;
    h.emit(EventSeverity::Info,
           SiteOperatingAuditEvent{site.id, SiteOperatingAuditKind::IssueAcknowledged, site.operatingRevision,
                                   site.issue.cause, episode, 0, "Acknowledged measured zero episode"});
    site.processedStock = {};
    ++state.date.day;
    operate(state);
    require(site.issue.cause == SiteOperatingIssueCause::SupplyLost && !site.issue.acknowledged,
            "new support loss interrupts even when prior zero episode was acknowledged");
    site.processedStock.set(ProcessedMaterial::ReactorFuel, 10);
    site.processedStock.set(ProcessedMaterial::IndustrialComposites, 10);
    ++state.date.day;
    operate(state);
    require(site.issue.cause == SiteOperatingIssueCause::ZeroRecovery && site.issue.acknowledged &&
                site.issue.episodeStartedDay == episode,
            "resupply restores prior acknowledged zero episode without a duplicate interruption");
    site.operatingPolicy.lifetimeDutyAllowance = siteDutySpent(site);
    ++state.date.day;
    operate(state);
    require(site.issue.cause == SiteOperatingIssueCause::DutyAllowance && !site.issue.acknowledged,
            "new lifetime duty limit remains actionable after acknowledged zero episode");
    site.operatingPolicy.lifetimeDutyAllowance.reset();
    ++state.date.day;
    operate(state);
    require(site.issue.cause == SiteOperatingIssueCause::ZeroRecovery && site.issue.acknowledged &&
                site.issue.episodeStartedDay == episode,
            "restored duty allowance retains original acknowledged evidence episode");
    auto shared = fixture(1, 13);
    auto second = shared.resourceSites.front();
    second.id = {2};
    second.name = "Second shared-deposit site";
    shared.resourceSites.push_back(second);
    // The colony phase has already removed3 units; sites consume the remaining
    // shared physical record in stored order, never private copied reserves.
    shared.mineralDeposits.back().remaining -= 3;
    operate(shared);
    near(shared.resourceSites[0].rawStock.get(Mineral::WaterIce), 10,
         "first stored site consumes remaining shared reserve");
    near(shared.resourceSites[1].rawStock.get(Mineral::WaterIce), 0,
         "second site cannot duplicate first site's recovery");
    near(shared.mineralDeposits.back().remaining, 0, "prior colony3 plus actual site10 conserves reserve13");
}
// Minimal committed records let these tests exercise the shared executor without
// pretending detached hardware was earned by a construction command.
FreightProgram rawCommit(const GameState& state, FreightProgramId id, const Fleet& fleet, double quantity) {
    FreightProgram p;
    p.id = id;
    p.charter.name = "Committed collector";
    p.charter.source = state.resourceSites.front().id;
    p.charter.destination = state.colonies.at(state.colonies.size() - 2).id;
    p.charter.operatingBaseColonyId = std::get<ColonyId>(p.charter.destination);
    p.charter.commodity = Mineral::WaterIce;
    p.charter.totalQuantity = quantity;
    p.charter.requestedLeaderId = state.people.front().id;
    p.charter.requestedFleetId = fleet.id;
    p.leasedFleetId = fleet.id;
    p.taskFleetId = fleet.id;
    p.task = FreightProgramTask::Loading;
    p.nextShipmentNumber = 2;
    p.shipment = FreightShipment{1,
                                 1,
                                 0,
                                 fleet.id,
                                 *p.charter.requestedLeaderId,
                                 p.charter.source,
                                 p.charter.destination,
                                 p.charter.operatingBaseColonyId,
                                 p.charter.commodity,
                                 {{fleet.shipIds.front(), quantity}}};
    return p;
}
void competing_collectors_and_real_support_delivery() {
    auto state = fixture();
    auto& site = state.resourceSites.front();
    site.rawStock.set(Mineral::WaterIce, 40);
    auto fleet = state.fleets.back();
    fleet.id = {901};
    auto ship = state.ships.back();
    ship.id = {901};
    ship.fleetId = fleet.id;
    fleet.shipIds = {ship.id};
    state.ships.push_back(ship);
    state.fleets.push_back(fleet);
    auto first = rawCommit(state, {1}, state.fleets[state.fleets.size() - 2], 40);
    auto second = rawCommit(state, {2}, state.fleets.back(), 40);
    // Both plans observe the same real40 stock. Planning neither spends it nor
    // reserves it, so physical roster order must settle the later competition.
    require(planFreightShipment(state, first, state.fleets[state.fleets.size() - 2]).ready &&
                planFreightShipment(state, second, state.fleets.back()).ready,
            "two collectors may conditionally plan from the same known finite source");
    near(site.rawStock.get(Mineral::WaterIce), 40, "plans create no speculative reservation or spending");
    state.freightPrograms = {first, second};
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    FreightProgramExecutionHooks freightHooks{
        [](FreightProgramId, FleetId, BodyId, double&) { return false; }, h.emit};
    runFreightProgramOpeningDay(state, state.freightPrograms[0], opening, freightHooks);
    runFreightProgramOpeningDay(state, state.freightPrograms[1], opening, freightHooks);
    near(freightCargoAboard(state, {1}), 40, "first actual collector takes finite source stock once");
    near(freightCargoAboard(state, {2}), 0, "later collector cannot spend source stock twice");
    near(freightShipmentPlannedQuantity(*state.freightPrograms[1].shipment), 40,
         "later collector keeps its committed requirement");
    near(opening.availableSiteRawHandling(site.id), 10,
         "both fleets share one handler rather than receiving independent50 pools");
    site.rawStock.set(Mineral::WaterIce, 40);
    ++state.date.day;
    OpeningProgramContext next{state};
    runSitesOpeningDay(state, next, h);
    runFreightProgramOpeningDay(state, state.freightPrograms[1], next, freightHooks);
    near(freightCargoAboard(state, {2}), 40, "same committed collector resumes after actual stock arrives");

    auto cold = fixture();
    auto& coldSite = cold.resourceSites.front();
    coldSite.rawStock.set(Mineral::WaterIce, 40);
    coldSite.processedStock.set(ProcessedMaterial::ReactorFuel, 0);
    auto supplyFleet = cold.fleets.back();
    supplyFleet.id = {902};
    auto supplyShip = cold.ships.back();
    supplyShip.id = {902};
    supplyShip.fleetId = supplyFleet.id;
    supplyFleet.shipIds = {supplyShip.id};
    cold.ships.push_back(supplyShip);
    cold.fleets.push_back(supplyFleet);
    auto collector = rawCommit(cold, {1}, cold.fleets[cold.fleets.size() - 2], 40);
    auto supply = rawCommit(cold, {2}, cold.fleets.back(), 10);
    std::swap(supply.charter.source, supply.charter.destination);
    supply.charter.commodity = ProcessedMaterial::ReactorFuel;
    supply.shipment->source = supply.charter.source;
    supply.shipment->destination = supply.charter.destination;
    supply.shipment->commodity = supply.charter.commodity;
    supply.task = FreightProgramTask::Unloading;
    supply.cargoLoaded = 10;
    cold.ships.back().cargo = ShipCargo{supply.id, 1, supply.charter.commodity, 10};
    cold.freightPrograms = {collector, supply};
    OpeningProgramContext unsupported{cold};
    auto coldHooks = hooks(cold);
    runSitesOpeningDay(cold, unsupported, coldHooks);
    FreightProgramExecutionHooks deliveries{[](FreightProgramId, FleetId, BodyId, double&) { return false; },
                                            coldHooks.emit};
    runFreightProgramOpeningDay(cold, cold.freightPrograms[0], unsupported, deliveries);
    near(freightCargoAboard(cold, {1}), 0,
         "raw cargo at unsupported site waits without a packaged-store bypass");
    runFreightProgramOpeningDay(cold, cold.freightPrograms[1], unsupported, deliveries);
    near(coldSite.processedStock.get(ProcessedMaterial::ReactorFuel), 10,
         "real ship-handled support cargo can land at cold site");
    runFreightProgramOpeningDay(cold, cold.freightPrograms[0], unsupported, deliveries);
    near(freightCargoAboard(cold, {1}), 0,
         "same-opening support delivery cannot retroactively fund site handler");
    ++cold.date.day;
    OpeningProgramContext supported{cold};
    runSitesOpeningDay(cold, supported, coldHooks);
    runFreightProgramOpeningDay(cold, cold.freightPrograms[0], supported, deliveries);
    near(freightCargoAboard(cold, {1}), 40,
         "standing raw collection resumes next opening after actual support delivery");
}
void incoming_raw_channels_share_one_bin() {
    auto state = fixture();
    auto& site = state.resourceSites.front();
    site.rawStock.set(Mineral::Iron, 160);
    auto fleet = state.fleets.back();
    fleet.id = {903};
    auto ship = state.ships.back();
    ship.id = {903};
    ship.fleetId = fleet.id;
    fleet.shipIds = {ship.id};
    state.ships.push_back(ship);
    state.fleets.push_back(fleet);
    for (int i = 0; i < 2; ++i) {
        auto p = rawCommit(state, FreightProgramId{i + 1},
                           state.fleets[state.fleets.size() - 2 + static_cast<std::size_t>(i)], 30);
        std::swap(p.charter.source, p.charter.destination);
        p.charter.commodity = i == 0 ? Commodity{Mineral::Iron} : Commodity{Mineral::WaterIce};
        p.shipment->source = p.charter.source;
        p.shipment->destination = p.charter.destination;
        p.shipment->commodity = p.charter.commodity;
        p.task = FreightProgramTask::Unloading;
        p.cargoLoaded = 30;
        auto carrier = std::find_if(state.ships.begin(), state.ships.end(), [&](const auto& row) {
            return row.id == p.shipment->manifest.front().shipId;
        });
        carrier->cargo = ShipCargo{p.id, 1, p.charter.commodity, 30};
        state.freightPrograms.push_back(p);
    }
    OpeningProgramContext opening{state};
    auto h = hooks(state);
    runSitesOpeningDay(state, opening, h);
    FreightProgramExecutionHooks freightHooks{
        [](FreightProgramId, FleetId, BodyId, double&) { return false; }, h.emit};
    for (auto& p : state.freightPrograms)
        runFreightProgramOpeningDay(state, p, opening, freightHooks);
    near(site.rawStock.get(Mineral::Iron), 190, "first raw commodity takes30 of shared40 receiving room");
    near(site.rawStock.get(Mineral::WaterIce), 10, "second commodity receives only actual shared remainder");
    near(siteRawOccupied(site), 200, "raw channels do not each receive a private200-unit bin");
    near(freightCargoAboard(state, {2}), 20, "unreceived second payload remains aboard its original hull");
    near(opening.availableSiteRawHandling(site.id), 10, "multiple raw fleets share one50-unit daily handler");
    near(opening.availableSiteRawRoom(site.id), 0,
         "incoming raw transfers debit one opening receiving-space pool");
}
void remote_payloads_never_refill_engines() {
    for (const Commodity commodity :
         {Commodity{Mineral::WaterIce}, Commodity{Mineral::Volatiles},
          Commodity{ProcessedMaterial::ReactorFuel}, Commodity{ProcessedMaterial::Propellant}}) {
        auto state = fixture();
        auto& site = state.resourceSites.front();
        site.bodyId = state.colonies.back().bodyId;
        site.processedStock.set(ProcessedMaterial::Propellant, 100);
        state.fleets.back().currentBodyId = site.bodyId;
        state.ships.back().fuel = 0;
        auto p = rawCommit(state, {1}, state.fleets.back(), 25);
        p.charter.commodity = commodity;
        p.shipment->commodity = commodity;
        p.cargoLoaded = 25;
        state.ships.back().cargo = ShipCargo{p.id, 1, commodity, 25};
        state.freightPrograms.push_back(p);
        OpeningProgramContext opening{state};
        auto h = hooks(state);
        runSitesOpeningDay(state, opening, h);
        int departures = 0;
        FreightProgramExecutionHooks freightHooks{[&](FreightProgramId, FleetId, BodyId, double&) {
                                                      ++departures;
                                                      return false;
                                                  },
                                                  h.emit};
        runFreightProgramOpeningDay(state, state.freightPrograms.front(), opening, freightHooks);
        near(state.ships.back().fuel, 0, "remote inventory and payload never become engine fuel");
        near(freightCargoAboard(state, p.id), 25, "unsupported loaded home leg retains exact typed payload");
        near(site.processedStock.get(ProcessedMaterial::Propellant), 100,
             "remote site Propellant is not a fueling depot");
        require(departures == 0 && state.freightPrograms.front().fuelLoaded == 0,
                "collection cannot launch or record phantom base fuel transfer while remote");
    }
}
} // namespace
int main() {
    try {
        duty_authority_and_phase_stock();
        actual_geology_and_non_attempts();
        nominal_duty_precedes_recovery_caps();
        commissioning_and_shared_raw_pool();
        physical_precision_and_missing_capability();
        actual_zero_episode_and_shared_reserve();
        competing_collectors_and_real_support_delivery();
        remote_payloads_never_refill_engines();
        incoming_raw_channels_share_one_bin();
        std::cout << "site operation tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
