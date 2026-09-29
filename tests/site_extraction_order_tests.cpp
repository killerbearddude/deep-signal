// P4B-31 actual-tick ordering proof. Both sites earn their hardware through
// commands, real freight and elapsed construction. Only the final finite-reserve
// accounting checkpoint is authored, isolating colony-first/stored-site order.
#include "app/SiteDevelopmentFixture.h"
#include "sim/Simulation.h"
#include "sim/GameStateValidation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double actual, double expected, const char* why) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 1e-8, why);
}
void command(Simulation& sim, const SimCommand& cmd) {
    const auto result = sim.execute(cmd);
    if (!result.ok)
        throw std::runtime_error(result.message);
}
void acknowledge(Simulation& sim) {
    for (const auto& p : sim.state().freightPrograms)
        if (!p.issue.acknowledged)
            command(sim, AcknowledgeFreightProgramIssueCommand{p.id, p.issue.signature});
    for (const auto& p : sim.state().siteDevelopmentPrograms)
        if (!p.issue.acknowledged)
            command(sim, AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature});
    for (const auto& s : sim.state().resourceSites)
        if (!s.issue.acknowledged)
            command(sim,
                    AcknowledgeSiteOperatingIssueCommand{s.id, s.issue.cause, s.issue.episodeStartedDay});
}
void step(Simulation& sim) {
    acknowledge(sim);
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "earned second-site fixture failed to advance");
}
void earned_sites_follow_actual_colony_tick() {
    Simulation construction{earnSiteDevelopmentFixture(90)};
    const auto first = construction.state().resourceSites.front().id;
    const auto body = construction.state().resourceSites.front().bodyId;
    const auto old = construction.state().siteDevelopmentPrograms.front().charter;
    std::vector<FreightProgramId> supplies;
    std::vector<FleetId> deliveryFleets;
    for (const auto& p : construction.state().freightPrograms) {
        if (p.charter.source == StockLocation{first})
            command(construction, SuspendFreightProgramCommand{p.id});
        else if (p.charter.destination == StockLocation{first}) {
            supplies.push_back(p.id);
            deliveryFleets.push_back(*p.charter.requestedFleetId);
        }
    }
    require(deliveryFleets.size() == 4, "earned fixture has four physical material suppliers");
    auto quiet = construction.state().resourceSites.front().operatingPolicy;
    quiet.requestedIcePerDay = 0;
    command(construction, AmendSiteOperatingPolicyCommand{first, quiet});
    const auto initialSettled = [&]() {
        if (construction.state().siteDevelopmentPrograms.front().lifecycle !=
            SiteDevelopmentLifecycle::Closed)
            return false;
        for (const auto id : supplies)
            for (const auto& p : construction.state().freightPrograms)
                if (p.id == id && p.lifecycle != FreightProgramLifecycle::Closed)
                    return false;
        return true;
    };
    for (int day = 0; day < 180 && !initialSettled(); ++day)
        step(construction);
    require(initialSettled(), "first builders and suppliers physically return before reuse");
    auto next = old;
    next.siteId = {};
    next.assignments.name = "Second earned same-body installation";
    NewResourceSite registration;
    registration.bodyId = body;
    registration.name = "Second shared-reserve installation";
    registration.operatingPolicy.leaderId = old.assignments.leaderId;
    registration.operatingPolicy.enabled = false;
    command(construction, CreateSiteDevelopmentCommand{next, registration});
    const auto second = construction.state().resourceSites.back().id;
    const std::pair<ProcessedMaterial, double> inputs[] = {{ProcessedMaterial::StructuralAlloys, 240},
                                                           {ProcessedMaterial::Electronics, 70},
                                                           {ProcessedMaterial::IndustrialComposites, 110},
                                                           {ProcessedMaterial::ReactorFuel, 50}};
    for (std::size_t i = 0; i < 4; ++i) {
        FreightProgramCharter freight;
        freight.name = "Second site actual input delivery";
        freight.source = old.supportColonyId;
        freight.destination = second;
        freight.operatingBaseColonyId = old.supportColonyId;
        freight.commodity = inputs[i].first;
        freight.totalQuantity = inputs[i].second;
        freight.requestedFleetId = deliveryFleets[i];
        freight.requestedLeaderId = old.assignments.leaderId;
        command(construction, CreateFreightProgramCommand{freight});
    }
    for (int day = 0; day < 240 && construction.state().siteDevelopmentPrograms.back().lifecycle !=
                                       SiteDevelopmentLifecycle::Closed;
         ++day)
        step(construction);
    require(construction.state().siteDevelopmentPrograms.back().lifecycle ==
                    SiteDevelopmentLifecycle::Closed &&
                construction.state().resourceSites.back().installed.size() == 5,
            "second package was actually assembled commissioned and builders returned");
    for (const auto& p : construction.state().freightPrograms)
        if (p.lifecycle != FreightProgramLifecycle::Closed &&
            p.lifecycle != FreightProgramLifecycle::Suspended)
            command(construction, SuspendFreightProgramCommand{p.id});
    for (const auto& site : construction.state().resourceSites) {
        auto policy = site.operatingPolicy;
        policy.enabled = true;
        policy.requestedIcePerDay = 10;
        command(construction, AmendSiteOperatingPolicyCommand{site.id, policy});
    }
    acknowledge(construction);
    auto checkpoint = construction.state();
    // Authored accounting checkpoint changes finite stock/reserve quantities,
    // not earned equipment, dates, work, logistics custody or project history.
    for (auto& site : checkpoint.resourceSites)
        site.rawStock = {};
    auto deposit =
        std::find_if(checkpoint.mineralDeposits.begin(), checkpoint.mineralDeposits.end(),
                     [&](const auto& row) { return row.bodyId == body && row.mineral == Mineral::WaterIce; });
    require(deposit != checkpoint.mineralDeposits.end(),
            "earned useful body has its shared physical deposit");
    deposit->remaining = 13;
    deposit->accessibility = 1;
    Colony miner;
    miner.id = ColonyId{checkpoint.ids.nextColonyId++};
    miner.bodyId = body;
    miner.name = "Three-unit ordering witness";
    miner.mines = 3;
    const auto mineId = miner.id;
    checkpoint.colonies.push_back(miner);
    validateGameState(checkpoint);
    Simulation sim{checkpoint};
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "actual shared-deposit simulation tick completed");
    validateGameState(sim.state());
    const auto colony = std::find_if(sim.state().colonies.begin(), sim.state().colonies.end(),
                                     [&](const auto& c) { return c.id == mineId; });
    near(colony->stockpile.get(Mineral::WaterIce), 3,
         "actual Simulation colony phase extracts3 before either site");
    near(sim.state().resourceSites[0].rawStock.get(Mineral::WaterIce), 10,
         "first stored earned site recovers remaining10");
    near(sim.state().resourceSites[1].rawStock.get(Mineral::WaterIce), 0,
         "second earned site cannot duplicate shared reserve");
    const auto physical =
        std::find_if(sim.state().mineralDeposits.begin(), sim.state().mineralDeposits.end(),
                     [&](const auto& d) { return d.bodyId == body && d.mineral == Mineral::WaterIce; });
    near(physical->remaining, 0, "colony3 plus firstsite10 conserves original13 physical reserve");
    require(sim.state().resourceSites[0].extractionReceipts.back().day == sim.state().date.day &&
                sim.state().resourceSites[1].extractionReceipts.back().day == sim.state().date.day,
            "both actual sites record same-day work in stored order");
    near(sim.state().resourceSites[1].extractionReceipts.back().recoveredIce, 0,
         "second supported attempt records measured zero after earlier depletion");
}
} // namespace
int main() {
    try {
        earned_sites_follow_actual_colony_tick();
        std::cout << "earned site extraction order test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
