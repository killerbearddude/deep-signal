// Whole-loop P5 proof plus the no-P5 comparator. Every advanced artifact and
// ship is earned through commands, elapsed work, real materials, and shipyard use.
#include "app/SiteDevelopmentFixture.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
ResourceIndication waterIce(const InstrumentObservation& instrument) {
    return std::find_if(instrument.channels.begin(), instrument.channels.end(),
                        [](const auto& row) { return row.mineral == Mineral::WaterIce; })
        ->indication;
}
void optional_path_and_whole_loop() {
    const auto established = earnSiteDevelopmentFixture(90);
    require(established.technicalDevelopmentPrograms.empty() &&
                established.developedComponentRevisions.empty() &&
                !established.resourceSites.front().extractionReceipts.empty(),
            "P4 useful path remains viable without pursuing P5");

    const auto state = earnTechnicalDevelopmentFixture();
    validateGameState(state);
    require(state.developedComponentRevisions.size() == 1 && state.prototypeComponentUnits.size() == 1 &&
                state.prototypeComponentUnits.front().state == PrototypeComponentState::Consumed &&
                state.prototypeIntegrationReceipts.size() == 1,
            "one physical prototype is consumed by exactly one experimental hull");
    require(state.componentProductionCapabilities.size() == 1 &&
                state.supportQualificationRecords.size() == 1,
            "serial process and support qualification remain separate durable results");
    const auto component = state.developedComponentRevisions.front().componentId;
    const int advancedShips =
        static_cast<int>(std::count_if(state.ships.begin(), state.ships.end(), [&](const auto& ship) {
            const auto cls = std::find_if(state.shipClasses.begin(), state.shipClasses.end(),
                                          [&](const auto& row) { return row.id == ship.shipClassId; });
            return cls != state.shipClasses.end() &&
                   std::any_of(cls->components.begin(), cls->components.end(),
                               [&](const auto& install) { return install.componentId == component; });
        }));
    require(advancedShips == 2, "fixture produces one prototype-backed and one serial advanced ship");
    bool establishedMiss = false, precisionDetection = false;
    for (const auto& batch : state.observations)
        if (batch.origin == ObservationOrigin::ImmediateManual)
            for (const auto& instrument : batch.instruments) {
                if (instrument.profile.detectionThreshold == 10.0)
                    establishedMiss |= waterIce(instrument) == ResourceIndication::NotDetectedWithinLimit;
                if (instrument.profile.detectionThreshold == 6.0)
                    precisionDetection |= waterIce(instrument) == ResourceIndication::Detected;
            }
    require(establishedMiss && precisionDetection,
            "same weak signal is missed at threshold10 and detected at demonstrated threshold6");
    require(!state.maintenancePrograms.empty() && !state.maintenancePrograms.front().receipts.empty() &&
                state.maintenancePrograms.front().receipts.front().componentId == component,
            "worn advanced sensor is serviced through normal qualified maintenance");

    const auto path =
        std::filesystem::temp_directory_path() /
        ("deep-signal-p5-loop-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    save::SaveGameRepository::save(path, state);
    const auto loaded = save::SaveGameRepository::load(path);
    require(loaded.date.day == state.date.day &&
                loaded.prototypeIntegrationReceipts.size() == state.prototypeIntegrationReceipts.size() &&
                loaded.observations == state.observations &&
                loaded.maintenancePrograms.front().receipts.size() ==
                    state.maintenancePrograms.front().receipts.size(),
            "whole-loop v18 save preserves technical/scientific/support evidence");
    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        optional_path_and_whole_loop();
        std::cout << "Technical whole-loop integration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Technical integration failure: " << error.what() << '\n';
        return 1;
    }
}
