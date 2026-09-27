#include "sim/Commands.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

void check(const char* name, const std::vector<deep::ProcessingAllocation>& rows) {
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId id = sim.state().colonies.front().id;
    const double beforeElectronics = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Electronics);
    const double beforeAlloys = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys);
    const auto result = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = id, .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = rows});
    std::cout << name << " command_ok=" << result.ok;
    if (result.ok) {
        try {
            deep::validateGameState(sim.state());
            std::cout << " validator=accepted";
        } catch (const std::exception& error) {
            std::cout << " validator=rejected(" << error.what() << ')';
        }
        sim.advanceDays(1);
        const auto& stockpile = sim.state().colonies.front().processedStockpile;
        std::cout << " electronics_before=" << beforeElectronics
                  << " electronics_after=" << stockpile.get(deep::ProcessedMaterial::Electronics)
                  << " alloys_before=" << beforeAlloys
                  << " alloys_after=" << stockpile.get(deep::ProcessedMaterial::StructuralAlloys)
                  << " electronics_finite=" << std::isfinite(stockpile.get(deep::ProcessedMaterial::Electronics));
    }
    std::cout << '\n';
}

int main() {
    const double max = std::numeric_limits<double>::max();
    check("same_material", {{deep::ProcessedMaterial::Electronics, max},
                            {deep::ProcessedMaterial::Electronics, max}});
    check("cross_material", {{deep::ProcessedMaterial::StructuralAlloys, max},
                             {deep::ProcessedMaterial::Electronics, max}});
    check("single_near_max", {{deep::ProcessedMaterial::Electronics, max}});
    deep::GameState dormant = deep::createHomeSystemScenario();
    dormant.colonies.front().processingPolicy = deep::ProcessingPolicy::Balanced;
    dormant.colonies.front().manualProcessingAllocations = {
        {deep::ProcessedMaterial::Electronics, max},
        {deep::ProcessedMaterial::Electronics, max}};
    try {
        deep::validateGameState(dormant);
        std::cout << "dormant_preset_overflow validator=accepted\n";
    } catch (const std::exception& error) {
        std::cout << "dormant_preset_overflow validator=rejected(" << error.what() << ")\n";
    }
}
