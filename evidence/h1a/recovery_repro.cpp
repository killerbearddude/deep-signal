#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include <iostream>
#include <utility>

int main() {
    auto state = deep::createHomeSystemScenario();
    auto& colony = state.colonies.at(2); // Ceres: no shipyard production.
    colony.mines = 0.0;
    colony.processorCapacity = 60.0;
    colony.stockpile.amount.fill(1'000'000.0);
    colony.processedStockpile.amount.fill(1'000'000'000'000.0);
    colony.processingPolicy = deep::ProcessingPolicy::StockpileRecovery;
    deep::Simulation sim{std::move(state)};
    const double before = sim.state().colonies.at(2).processedStockpile.amount.at(0);
    sim.advanceDays(1);
    const double after = sim.state().colonies.at(2).processedStockpile.amount.at(0);
    std::cout << "alloys_delta=" << after-before << '\n';
}
