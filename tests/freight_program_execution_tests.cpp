#include "sim/Commands.h"
#include "sim/FreightProgramRules.h"
#include "sim/FreightProgramExecution.h"
#include "sim/ProgramControl.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// P3B physical-loop evidence. Tests count dated transfers independently, check
// actual inventories after every action, and exercise lifecycle commands while
// material and paid trajectories are committed.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}
void near(double a, double b, const char* message) {
    require(std::isfinite(a) && std::abs(a - b) < 1.e-7, message);
}
deep::FreightProgramCharter charter(const deep::GameState& s, double quantity = 500) {
    deep::FreightProgramCharter c;
    c.name = "Freight acceptance";
    c.source = s.colonies[s.colonies.size() - 2].id;
    c.operatingBaseColonyId = std::get<deep::ColonyId>(c.source);
    c.destination = s.colonies.back().id;
    c.totalQuantity = quantity;
    c.requestedFleetId = s.fleets.back().id;
    c.requestedLeaderId = s.people.front().id;
    return c;
}
deep::FreightProgramId authorize(deep::Simulation& sim, deep::FreightProgramCharter c) {
    require(sim.execute(deep::CreateFreightProgramCommand{c}).ok, "valid freight authorization accepted");
    return sim.state().freightPrograms.back().id;
}
const deep::FreightProgram& p(const deep::Simulation& sim) {
    return sim.state().freightPrograms.back();
}
double sourceStock(const deep::Simulation& sim,
                   deep::ProcessedMaterial m = deep::ProcessedMaterial::StructuralAlloys) {
    return sim.state().colonies[sim.state().colonies.size() - 2].processedStockpile.get(m);
}
double destStock(const deep::Simulation& sim,
                 deep::ProcessedMaterial m = deep::ProcessedMaterial::StructuralAlloys) {
    return sim.state().colonies.back().processedStockpile.get(m);
}
void day(deep::Simulation& sim) {
    const auto r = sim.advanceDaysDetailed(1);
    if(r.advancedDays!=1 || r.interrupted) throw std::runtime_error("Expected uninterrupted physical day "+std::to_string(sim.state().date.day)+": "+(sim.state().freightPrograms.empty()?std::string{}:sim.state().freightPrograms.back().issue.message));
    deep::validateGameState(sim.state());
}
template <class Predicate> void until(deep::Simulation& sim, Predicate pred) {
    for (int i = 0; i < 500 && !pred(sim.state()); ++i)
        day(sim);
    require(pred(sim.state()), "bounded expected phase reached");
}
void repeated_shipments_and_conservation() {
    // P3B-05/07/12/29: three exact batches, separate handling/departure days,
    // cumulative destination throughput, and no post-close periodic reports.
    deep::Simulation sim{deep::createDelegatedFreightScenario()};
    authorize(sim, charter(sim.state()));
    const double initialFuel = sourceStock(sim, deep::ProcessedMaterial::Propellant);
    for (int i = 0; i < 180; ++i) {
        day(sim);
        near(sourceStock(sim) + destStock(sim) + deep::freightCargoAboard(sim.state(), p(sim).id), 500,
             "cargo conservation every complete day");
        near(sourceStock(sim, deep::ProcessedMaterial::Propellant) + sim.state().ships.back().fuel +
                 p(sim).fuelBurned,
             initialFuel, "engine transfer/burn conservation every day");
    }
    require(p(sim).lifecycle == deep::FreightProgramLifecycle::Closed,
            "finite freight target closes after physical return");
    near(p(sim).cargoDelivered, 500, "500 units actually unloaded");
    near(destStock(sim), 500, "destination credited exactly500");
    near(p(sim).cargoReturned, 0, "normal deliveries not counted source-return");
    require(!p(sim).leasedFleetId && !sim.state().ships.back().cargo, "closed program releases empty hull");
    require(sim.state().fleets.back().currentBodyId ==
                sim.state().colonies[sim.state().colonies.size() - 2].bodyId,
            "normal completion requires actual source return");
    std::map<int, double> quantities;
    std::map<int, int> loadDays, unloadDays;
    std::map<int, std::int64_t> lastLoad;
    for (const auto& r : p(sim).receipts) {
        if (r.kind == deep::FreightTransferKind::Load) {
            quantities[r.shipmentNumber] += r.amount;
            ++loadDays[r.shipmentNumber];
            lastLoad[r.shipmentNumber] = r.day;
        }
        if (r.kind == deep::FreightTransferKind::Delivery)
            ++unloadDays[r.shipmentNumber];
    }
    require(quantities.size() == 3, "exactly three physical shipments");
    near(quantities[1], 200, "first batch200");
    near(quantities[2], 200, "second batch200");
    near(quantities[3], 100, "third batch100");
    require(loadDays[1] == 4 && loadDays[2] == 4 && loadDays[3] == 2, "loading workdays independently4/4/2");
    require(unloadDays[1] == 4 && unloadDays[2] == 4 && unloadDays[3] == 2,
            "unloading workdays independently4/4/2");
    for (const auto& e : sim.state().eventLog)
        if (const auto* a = std::get_if<deep::FreightProgramAuditEvent>(&e.payload);
            a && a->kind == deep::FreightProgramAuditKind::Departure && a->shipmentNumber > 0)
            require(e.day > lastLoad[a->shipmentNumber], "last loading day cannot also depart");
    for (const auto& r : p(sim).reports)
        require(r.endDay <= *p(sim).closedDay, "closed programs generate no later periodic reports");
}
void admission_waiting_and_recovery() {
    // P3B-03/04/08/35: absent readiness is durable intent, malformed commands
    // have no partial effect, and newly supplied stock resumes the same ID.
    auto state = deep::createDelegatedFreightScenario();
    auto c = charter(state);
    c.requestedFleetId.reset();
    c.requestedLeaderId.reset();
    deep::Simulation missing{state};
    const auto id = authorize(missing, c);
    const auto eventCount = missing.state().eventLog.size();
    missing.advanceDays(10);
    require(p(missing).id == id && p(missing).receipts.empty() &&
                missing.state().eventLog.size() == eventCount,
            "known initial shortage creates no daily warning spam");
    auto bad = charter(state);
    bad.totalQuantity = std::numeric_limits<double>::quiet_NaN();
    require(!missing.execute(deep::CreateFreightProgramCommand{bad}).ok &&
                missing.state().freightPrograms.size() == 1,
            "malformed create does not allocate intent");
    bad = charter(state);
    bad.requestedFleetId = deep::FleetId{999999};
    require(!missing.execute(deep::CreateFreightProgramCommand{bad}).ok, "invalid supplied ID rejected");
    for (int constraint = 0; constraint < 4; ++constraint) {
        auto unavailable = state;
        for (auto& component : unavailable.shipComponents) {
            if (component.kind == deep::ShipComponentKind::CargoBay && constraint == 0)
                component.cargoCapacity = 0.0;
            if (component.kind == deep::ShipComponentKind::CargoBay && constraint == 1)
                component.cargoHandlingPerDay = 0.0;
            if (component.kind == deep::ShipComponentKind::Reactor && constraint == 2)
                component.powerGeneration = 0.0;
            if (component.kind == deep::ShipComponentKind::PropellantTank && constraint == 3)
                component.propellantCapacity = 0.0;
        }
        if (constraint == 3)
            for (auto& ship : unavailable.ships)
                ship.fuel = 0.0;
        deep::Simulation waiting{unavailable};
        authorize(waiting, charter(waiting.state()));
        waiting.advanceDays(10);
        require(p(waiting).receipts.empty() && p(waiting).issue.acknowledged &&
                    deep::freightCargoAboard(waiting.state(), p(waiting).id) == 0.0,
                "absent capacity/rate/power/tankage keeps accepted intent without phantom physical work");
    }
    auto& source = state.colonies[state.colonies.size() - 2];
    source.processedStockpile.set(deep::ProcessedMaterial::StructuralAlloys, 75);
    deep::Simulation partial{state};
    authorize(partial, charter(partial.state(), 125));
    until(partial, [](const auto& s) {
        return s.freightPrograms.back().cargoDelivered == 75 &&
               s.freightPrograms.back().task == deep::FreightProgramTask::None;
    });
    require(p(partial).nextShipmentNumber == 2, "partial supported75 shipment proceeds without full hold");
    auto restored = partial.state();
    restored.colonies[restored.colonies.size() - 2].processedStockpile.set(
        deep::ProcessedMaterial::StructuralAlloys, 50);
    deep::Simulation continued{restored};
    until(continued, [](const auto& s) {
        return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed;
    });
    near(p(continued).cargoDelivered, 125, "later source replenishment resumes original intent");
    auto same = deep::createDelegatedFreightScenario();
    same.colonies.back().bodyId = same.colonies[same.colonies.size() - 2].bodyId;
    deep::Simulation local{same};
    authorize(local, charter(local.state(), 25));
    day(local);
    near(sourceStock(local), 475, "same-body first day loads real25");
    near(destStock(local), 0, "loading same-body does not directly credit destination");
    until(local, [](const auto& s) {
        return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed;
    });
    near(p(local).fuelBurned, 0, "same-body route has no fictitious burn");
    near(p(local).cargoDelivered, 25, "same-body distinct colonies really transfer stock");
}
void source_cancellation_and_suspension() {
    // P3B-19/21/23: cargo custody survives suspension; cancellation performs
    // rate-limited source restitution, and pausing settlement preserves intent.
    deep::Simulation sim{deep::createDelegatedFreightScenario()};
    const auto id = authorize(sim, charter(sim.state()));
    until(sim, [](const auto& s) { return deep::freightCargoAboard(s, s.freightPrograms.back().id) == 100; });
    const int shipment = p(sim).shipment->number;
    require(sim.execute(deep::SuspendFreightProgramCommand{id}).ok, "loaded program suspension accepted");
    const double stock = sourceStock(sim);
    sim.advanceDays(7);
    near(sourceStock(sim), stock, "suspension performs no cargo movement");
    near(deep::freightCargoAboard(sim.state(), id), 100, "suspended cargo stays on ship");
    require(p(sim).leasedFleetId.has_value(), "loaded suspension retains exclusive custody");
    require(sim.execute(deep::CancelFreightProgramCommand{id}).ok,
            "cancellation supersedes suspension for bounded settlement");
    day(sim);
    near(p(sim).cargoReturned, 50, "source restitution rate limited to50");
    near(p(sim).cargoDelivered, 0, "source restitution is not delivery");
    require(sim.execute(deep::SuspendFreightProgramCommand{id}).ok, "settlement may be suspended");
    sim.advanceDays(3);
    require(p(sim).closure == deep::FreightProgramClosure::Cancelled && p(sim).shipment->number == shipment,
            "paused cancellation retains original intent and shipment");
    near(deep::freightCargoAboard(sim.state(), id), 50, "paused restitution stays physically aboard");
    require(sim.execute(deep::ResumeFreightProgramCommand{id}).ok, "settlement resume accepted");
    day(sim);
    require(p(sim).lifecycle == deep::FreightProgramLifecycle::Closed &&
                p(sim).closure == deep::FreightProgramClosure::Cancelled,
            "cancellation closes only after custody settled");
    near(sourceStock(sim), 500, "all unshipped cargo really returned");
    near(p(sim).cargoLoaded, 100, "historical loading not erased by return");
}
void dispatched_cancellation_and_amendment() {
    // P3B-20/22/24: paid outbound transit survives lifecycle changes. A lower
    // amended target cannot erase committed material or replace its fleet.
    deep::Simulation sim{deep::createDelegatedFreightScenario()};
    const auto id = authorize(sim, charter(sim.state()));
    until(sim,
          [](const auto& s) { return s.freightPrograms.back().task == deep::FreightProgramTask::Outbound; });
    const auto route = sim.state().fleets.back().activeOrder;
    const double fuel = sim.state().ships.back().fuel;
    auto amendment = deep::freightAmendmentFromCharter(p(sim).charter);
    amendment.totalQuantity = 10;
    amendment.requestedFleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::AmendFreightProgramCommand{id, amendment}).ok,
            "lower target and future fleet amendment accepted");
    require(p(sim).shipment->fleetId == sim.state().fleets.back().id && p(sim).shipment->charterRevision == 1,
            "current commitment keeps original fleet and revision");
    require(sim.execute(deep::SuspendFreightProgramCommand{id}).ok, "outbound transit suspension accepted");
    require(sim.state().fleets.back().activeOrder.arrivalDay == route.arrivalDay,
            "paid trajectory untouched by suspend");
    near(sim.state().ships.back().fuel, fuel, "suspend manufactures/refunds no fuel");
    sim.advanceDays(route.daysRemaining + 1);
    near(destStock(sim), 0, "suspended arrival credits no cargo");
    near(deep::freightCargoAboard(sim.state(), id), 200, "suspended loaded cargo remains aboard at arrival");
    require(sim.execute(deep::CancelFreightProgramCommand{id}).ok,
            "cancel authorizes only loaded disposition");
    until(sim, [](const auto& s) {
        return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed;
    });
    near(p(sim).cargoDelivered, 200, "committed shipment may overfulfil amended target");
    require(sim.state().fleets.back().currentBodyId == sim.state().colonies.back().bodyId,
            "cancel after dispatch closes at destination without extra return");
    near(p(sim).cargoReturned, 0, "dispatched cargo is never magically refunded to source");
    require(p(sim).nextShipmentNumber == 2, "cancellation starts no future shipment");
}
void cargo_propellant_is_not_engine_fuel_and_issues() {
    // P3B-10/11/25/26: joint-stock conservation, cargo excluded from tanks,
    // and an actionable completion-return issue with stable acknowledgment.
    auto state = deep::createDelegatedFreightScenario();
    auto c = charter(state, 100);
    c.commodity = deep::ProcessedMaterial::Propellant;
    const double initial = state.colonies[state.colonies.size() - 2].processedStockpile.get(
        std::get<deep::ProcessedMaterial>(c.commodity));
    deep::Simulation sim{state};
    authorize(sim, c);
    until(sim,
          [](const auto& s) { return s.freightPrograms.back().task == deep::FreightProgramTask::Return; });
    near(sourceStock(sim, std::get<deep::ProcessedMaterial>(c.commodity)) +
             destStock(sim, std::get<deep::ProcessedMaterial>(c.commodity)) + sim.state().ships.back().fuel +
             p(sim).fuelBurned,
         initial, "payload Propellant and tanks conserve separate actual accounts");
    auto blocked = sim.state();
    blocked.ships.back().fuel = 0;
    deep::Simulation stalled{blocked};
    const auto result = stalled.advanceDaysDetailed(10);
    require(result.interrupted && result.advancedDays == 1 && !p(stalled).issue.acknowledged,
            "new physical return failure interrupts at complete day");
    require(p(stalled).lifecycle == deep::FreightProgramLifecycle::Closing,
            "final delivery is physical return state");
    const auto signature = p(stalled).issue.signature;
    require(stalled.execute(deep::AcknowledgeFreightProgramIssueCommand{p(stalled).id, signature}).ok,
            "return issue acknowledgment accepted");
    require(stalled.advanceDaysDetailed(10).advancedDays == 10,
            "unchanged acknowledged return shortage does not trap clock");
    auto amendment = deep::freightAmendmentFromCharter(p(stalled).charter);
    amendment.totalQuantity = 150;
    require(stalled.execute(deep::AmendFreightProgramCommand{p(stalled).id, amendment}).ok,
            "completion return amendment remains actionable");
    require(p(stalled).task == deep::FreightProgramTask::Return &&
                p(stalled).lifecycle == deep::FreightProgramLifecycle::Authorized,
            "amendment reopens future work but keeps committed return");
    require(stalled.execute(deep::SuspendFreightProgramCommand{p(stalled).id}).ok,
            "completion return can suspend");
    require(stalled.execute(deep::ResumeFreightProgramCommand{p(stalled).id}).ok,
            "completion return can resume without fuel fabrication");
    near(stalled.state().ships.back().fuel, 0, "lifecycle actions create no fuel");

    deep::Simulation loading{state};
    authorize(loading, c);
    until(loading,
          [](const auto& s) { return deep::freightCargoAboard(s, s.freightPrograms.back().id) == 100; });
    auto empty = loading.state();
    empty.ships.back().fuel = 0;
    empty.colonies[empty.colonies.size() - 2].processedStockpile.set(
        std::get<deep::ProcessedMaterial>(c.commodity), 0);
    deep::Simulation noConversion{empty};
    noConversion.advanceDays(1);
    near(noConversion.state().ships.back().fuel, 0, "cargo Propellant cannot enter engines implicitly");
    near(deep::freightCargoAboard(noConversion.state(), p(noConversion).id), 100,
         "cargo Propellant stays in hold under engine shortage");
    require(noConversion.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "positive cargo cannot buy unsupported movement");
}
void mixed_hulls_and_midload_competition() {
    // P3B-06/08: each hull pays its own rate; another hull never operates an
    // unpowered hold. A disappeared source balance sends a useful partial load.
    auto state = deep::createDelegatedFreightScenario();
    auto& fleet = state.fleets.back();
    auto second = state.ships.back();
    second.id = {state.ids.nextShipId++};
    second.name = "Second cargo hull";
    const auto firstId = state.ships.back().id;
    state.ships.push_back(second);
    fleet.shipIds.push_back(second.id);
    deep::Simulation sim{state};
    authorize(sim, charter(sim.state(), 300));
    day(sim);
    day(sim);
    near(sim.state().ships[sim.state().ships.size() - 2].cargo->quantity, 50,
         "first hull receives its own50-unit rate");
    near(sim.state().ships.back().cargo->quantity, 50,
         "second hull receives own50 rather than pooled repeat");
    auto competitive = sim.state();
    competitive.colonies[competitive.colonies.size() - 2].processedStockpile.set(
        deep::ProcessedMaterial::StructuralAlloys, 0);
    deep::Simulation partial{competitive};
    day(partial);
    require(p(partial).task == deep::FreightProgramTask::Outbound,
            "positive partial load departs when no more authorized source stock");
    near(deep::freightCargoAboard(partial.state(), p(partial).id), 100,
         "partial actual manifest is100 rather than original300");
    until(partial,
          [](const auto& s) { return s.freightPrograms.back().task == deep::FreightProgramTask::Unloading; });
    auto impaired = partial.state();
    auto damaged = *std::find_if(impaired.shipClasses.begin(),impaired.shipClasses.end(),[&](const auto& cls){return cls.id==impaired.ships.back().shipClassId;});
    damaged.id = {impaired.ids.nextShipClassId++};
    damaged.name = "Unpowered detached test fixture";
    damaged.basedOnClassId.reset();
    damaged.revision = 1;
    for (auto& row : damaged.components)
        for (const auto& component : impaired.shipComponents)
            if (component.id == row.componentId && component.kind == deep::ShipComponentKind::Reactor)
                row.quantity = 0;
    std::erase_if(damaged.components, [](const auto& row) { return row.quantity == 0; });
    impaired.shipClasses.push_back(damaged);
    for (auto& ship : impaired.ships)
        if (ship.id == firstId)
            ship.shipClassId = damaged.id;
    deep::Simulation onePowered{impaired};
    onePowered.advanceDays(1);
    near(destStock(onePowered), 50, "powered second hull unloads its own50 only");
    near(deep::freightCargoAboard(onePowered.state(), p(onePowered).id), 50,
         "unpowered hull's cargo remains physically aboard");
}

void fractional_and_unrepresentable_transfers() {
    // P3B numeric boundaries: a positive fractional target is actually moved,
    // while a finite subtraction that would round away is rejected before any
    // stock or lot mutation. This does not clamp a negative or invent a credit.
    auto fractional = deep::createDelegatedFreightScenario();
    fractional.colonies.back().bodyId = fractional.colonies[fractional.colonies.size() - 2].bodyId;
    deep::Simulation small{fractional};
    authorize(small, charter(small.state(), 0.125));
    until(small, [](const auto& s) {
        return s.freightPrograms.back().lifecycle == deep::FreightProgramLifecycle::Closed;
    });
    near(p(small).cargoDelivered, 0.125, "fractional positive cargo is never integer-truncated");
    require(p(small).receipts.size() == 2,
            "fractional local delivery has one real load and one unload receipt");

    auto extreme = deep::createDelegatedFreightScenario();
    extreme.colonies.back().bodyId = extreme.colonies[extreme.colonies.size() - 2].bodyId;
    extreme.colonies[extreme.colonies.size() - 2].processedStockpile.set(
        deep::ProcessedMaterial::StructuralAlloys, 1.0e300);
    deep::Simulation precise{extreme};
    authorize(precise, charter(precise.state(), 0.125));
    day(precise);
    require(sourceStock(precise) == 1.0e300 && p(precise).receipts.empty() &&
                !precise.state().ships.back().cargo,
            "unrepresentable debit cannot create a credited cargo lot");
    require(deep::freightProgramExecutionCondition(precise.state(), p(precise)).find("precision") !=
                std::string::npos,
            "numeric transfer limit is exposed as the real waiting reason");

    auto overflow = deep::createDelegatedFreightScenario();
    overflow.colonies.back().bodyId = overflow.colonies[overflow.colonies.size() - 2].bodyId;
    deep::Simulation loaded{overflow};
    authorize(loaded, charter(loaded.state(), 25));
    day(loaded);
    auto changed = loaded.state();
    changed.colonies.back().processedStockpile.set(deep::ProcessedMaterial::StructuralAlloys, 1.0e300);
    deep::Simulation blockedCredit{changed};
    blockedCredit.advanceDays(5);
    require(blockedCredit.state().colonies.back().processedStockpile.get(
                deep::ProcessedMaterial::StructuralAlloys) == 1.0e300,
            "unrepresentable destination credit leaves the source lot aboard");
    near(deep::freightCargoAboard(blockedCredit.state(), p(blockedCredit).id), 25,
         "rejected credit does not lose cargo");
    near(p(blockedCredit).cargoDelivered, 0, "rejected credit cannot earn delivery throughput");

    // The retained common movement path normalizes tiny fuel residuals. P3B
    // preflights its actual roster debit so neither that normalization nor a
    // rounded-away large-tank subtraction can fabricate attributed travel burn.
    deep::Simulation prepared{deep::createDelegatedFreightScenario()};
    authorize(prepared, charter(prepared.state(), 25));
    until(prepared,
          [](const auto& s) { return deep::freightCargoAboard(s, s.freightPrograms.back().id) == 25; });
    auto enormous = prepared.state();
    for (auto& component : enormous.shipComponents)
        if (component.kind == deep::ShipComponentKind::PropellantTank)
            component.propellantCapacity = 1.0e300;
    enormous.ships.back().fuel = 1.0e300;
    deep::Simulation noFreeTravel{enormous};
    const auto stopped = noFreeTravel.advanceDaysDetailed(1);
    require(stopped.interrupted && p(noFreeTravel).issue.signature == "fuel-precision",
            "unrepresentable engine debit creates an actionable physical constraint");
    require(noFreeTravel.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "a rounded-away tank debit cannot launch freight");
    require(noFreeTravel.state().ships.back().fuel == 1.0e300 && p(noFreeTravel).fuelBurned == 0.0,
            "failed movement preflight changes neither tanks nor burn accounting");
    near(deep::freightCargoAboard(noFreeTravel.state(), p(noFreeTravel).id), 25,
         "numeric movement failure retains actual cargo custody");
    auto residual = prepared.state();
    residual.ships.back().fuel = 1.0 + 1.0e-7;
    require(!deep::freightFuelDebitRepresentable(residual, residual.fleets.back(), 1.0),
            "common tiny-residual normalization cannot silently lose program engine fuel");
}
// Focused executor fixture uses detached supported hardware to isolate raw
// custody and phase pools; the campaign proof earns the installation separately.
void raw_same_body_collection_custody() {
    auto state = deep::createDelegatedFreightScenario();
    auto c = charter(state, 100);
    deep::ResourceSite site;
    site.id = {1};
    site.name = "Stock staging";
    site.bodyId = state.fleets.back().currentBodyId;
    site.rawStock.set(deep::Mineral::WaterIce, 100);
    state.resourceSites.push_back(site);
    c.source = site.id;
    c.destination = c.operatingBaseColonyId;
    c.commodity = deep::Mineral::WaterIce;
    deep::FreightProgram program;
    program.id = {1};
    program.charter = c;
    // A prepared commitment avoids giving this detached site fictional installed
    // capabilities merely to exercise the transfer executor's funded pool.
    program.leasedFleetId = state.fleets.back().id;
    program.taskFleetId = program.leasedFleetId;
    program.task = deep::FreightProgramTask::Loading;
    program.nextShipmentNumber = 2;
    program.shipment = deep::FreightShipment{1,
                                             1,
                                             0,
                                             *program.leasedFleetId,
                                             *c.requestedLeaderId,
                                             c.source,
                                             c.destination,
                                             c.operatingBaseColonyId,
                                             c.commodity,
                                             {{state.ships.back().id, 100}}};
    state.freightPrograms.push_back(program);
    deep::FreightProgramExecutionHooks hooks;
    hooks.emit = [](deep::EventSeverity, deep::SimEventPayload) {};
    hooks.startProgramMove = [](deep::FreightProgramId, deep::FleetId, deep::BodyId, double&) {
        throw std::runtime_error("same-body route invented a transit");
        return false;
    };
    auto& actual = state.freightPrograms.back();
    auto opening = [&](double handling) {
        ++state.date.day;
        deep::OpeningProgramContext context{state};
        for (auto& budget : context.sites)
            budget.remainingHandling = handling;
        deep::runFreightProgramOpeningDay(state, actual, context, hooks);
    };
    opening(25);
    near(deep::freightCargoAboard(state, actual.id), 25, "shared site rate bounds physical per-hull pickup");
    near(state.resourceSites.back().rawStock.get(deep::Mineral::WaterIce), 75,
         "raw load debits only selected site inventory");
    // Cancelling before dispatch returns to the exact source identity and must
    // retain custody when its shared receiving room or handling is unavailable.
    auto cancelled = state;
    auto& cancellation = cancelled.freightPrograms.back();
    cancellation.closure = deep::FreightProgramClosure::Cancelled;
    cancellation.lifecycle = deep::FreightProgramLifecycle::Closing;
    deep::OpeningProgramContext blocked{cancelled};
    for (auto& budget : blocked.sites) {
        budget.remainingHandling = 50;
        budget.receivingRoom = 0;
    }
    deep::runFreightProgramOpeningDay(cancelled, cancellation, blocked, hooks);
    near(deep::freightCargoAboard(cancelled, cancellation.id), 25,
         "full raw receiving room retains cancelled cargo aboard");
    require(cancellation.lifecycle != deep::FreightProgramLifecycle::Closed,
            "custody prevents premature cancellation close");
    deep::OpeningProgramContext room{cancelled};
    for (auto& budget : room.sites) {
        budget.remainingHandling = 50;
        budget.receivingRoom = 25;
    }
    deep::runFreightProgramOpeningDay(cancelled, cancellation, room, hooks);
    near(cancellation.cargoReturned, 25, "cancellation performs actual raw source return");
    near(cancelled.resourceSites.back().rawStock.get(deep::Mineral::WaterIce), 100,
         "cancelled partial collection conserves source raw cargo");
    require(cancellation.receipts.back().location == c.source,
            "same-body cancellation receipt identifies source site, not base colony");
    state.resourceSites.back().rawStock.set(deep::Mineral::WaterIce, 0);
    opening(50);
    require(actual.task == deep::FreightProgramTask::Loading &&
                deep::freightShipmentPlannedQuantity(*actual.shipment) == 100,
            "collection retains committed manifest when another consumer depletes source");
    near(deep::freightCargoAboard(state, actual.id), 25, "source shortage retains partial custody");
    state.resourceSites.back().rawStock.set(deep::Mineral::WaterIce, 75);
    opening(50);
    opening(50);
    opening(50);
    require(actual.task == deep::FreightProgramTask::Unloading,
            "same-body loaded dispatch is a distinct action");
    const double before = state.colonies[state.colonies.size() - 2].stockpile.get(deep::Mineral::WaterIce);
    opening(0);
    opening(0);
    near(actual.cargoDelivered, 100, "raw cargo delivered to destination raw store");
    near(state.colonies[state.colonies.size() - 2].stockpile.get(deep::Mineral::WaterIce) - before, 100,
         "raw delivery updates existing industry inventory");
    require(actual.lifecycle == deep::FreightProgramLifecycle::Closed && !actual.shipment,
            "collection completes at base without final empty source trip");
}
} // namespace
int main() {
    try {
        repeated_shipments_and_conservation();
        admission_waiting_and_recovery();
        source_cancellation_and_suspension();
        dispatched_cancellation_and_amendment();
        cargo_propellant_is_not_engine_fuel_and_issues();
        mixed_hulls_and_midload_competition();
        fractional_and_unrepresentable_transfers();
        raw_same_body_collection_custody();
        std::cout << "freight execution tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
