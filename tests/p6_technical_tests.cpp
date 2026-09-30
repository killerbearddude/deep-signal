// Optional P5 success and disappointment remain causal field consequences.
// The disappointing branch stops at demonstrated evidence by player choice.
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/GameStateValidation.h"
#include "sim/ObservationRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
const MeasurementProfile& profile(const GameState& state, MeasurementProfileId id) {
    const auto row = std::find_if(state.measurementProfiles.begin(), state.measurementProfiles.end(),
                                  [&](const auto& value) { return value.id == id; });
    if (row == state.measurementProfiles.end())
        throw std::runtime_error("P6 demonstrated measurement profile is missing");
    return *row;
}
ResourceIndication ice(const std::vector<ObservationChannel>& channels) {
    const auto row = std::find_if(channels.begin(), channels.end(),
                                  [](const auto& value) { return value.mineral == Mineral::WaterIce; });
    if (row == channels.end())
        throw std::runtime_error("P6 declared Ice measurement channel is missing");
    return row->indication;
}

void optional_precision_success() {
    const auto state = earnTechnicalDevelopmentFixture(6.0);
    validateGameState(state);
    require(state.technicalTestRecords.size() == 3 && state.developedComponentRevisions.size() == 1 &&
                state.prototypeIntegrationReceipts.size() == 1 &&
                state.componentProductionCapabilities.size() == 1 &&
                state.supportQualificationRecords.size() == 1,
            "real test evidence, prototype use, local process and exact team support are separate");
    const auto& developed = state.developedComponentRevisions.front();
    const auto& measured = profile(state, developed.measurementProfileId);
    require(measured.detectionThreshold == 6.0 && developed.testIds.size() == 3 &&
                state.componentProductionCapabilities.front().availableDay ==
                    state.componentProductionCapabilities.front().qualifiedDay + 1 &&
                state.supportQualificationRecords.front().availableDay ==
                    state.supportQualificationRecords.front().qualifiedDay + 1,
            "measured capability and distinct process/support first-use dates survive completion");
    bool establishedMiss = false, precisionDetect = false;
    for (const auto& batch : state.observations)
        for (const auto& instrument : batch.instruments) {
            if (instrument.profile.detectionThreshold == 10.0)
                establishedMiss |= ice(instrument.channels) == ResourceIndication::NotDetectedWithinLimit;
            if (instrument.profile.detectionThreshold == 6.0)
                precisionDetect |= ice(instrument.channels) == ResourceIndication::Detected;
        }
    require(establishedMiss && precisionDetect,
            "normal field acquisition misses signal7 with established threshold10 and detects with earned6");
    require(!state.maintenancePrograms.empty() && !state.maintenancePrograms.front().receipts.empty() &&
                state.maintenancePrograms.front().receipts.front().componentId == developed.componentId,
            "earned advanced instrument receives ordinary physical specialist service");
}

void optional_precision_miss_and_decline() {
    auto state = createHomeSystemScenario();
    state.technologyCandidateTruths.front().achievedDetectionThreshold = 9.0;
    const auto facility = state.technicalFacilities.front();
    auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                               [&](const auto& value) { return value.id == facility.colonyId; });
    colony->processedStockpile.amount.fill(10'000);
    colony->processorCapacity = 0;
    const auto team =
        std::find_if(state.maintenanceTeams.begin(), state.maintenanceTeams.end(),
                     [](const auto& value) { return value.name == "Prototype Engineering Team"; });
    TechnicalDevelopmentCharter charter{"P6 target-miss demonstration",
                                        state.technologyOpportunities.front().id,
                                        facility.colonyId,
                                        facility.id,
                                        team->id,
                                        state.people.front().id,
                                        TechnicalDevelopmentScope::DemonstratePrototype,
                                        {}};
    Simulation sim(state);
    require(sim.execute(CreateTechnicalDevelopmentCommand{charter}).ok,
            "optional target-miss development authorizes before its outcome is known");
    const auto stopped = sim.advanceDaysDetailed(30);
    require(stopped.interrupted && stopped.advancedDays == 12 &&
                sim.state().technicalTestRecords.size() == 3 &&
                sim.state().developedComponentRevisions.size() == 1,
            "third real test raises an observed consequential threshold miss");
    const auto& developed = sim.state().developedComponentRevisions.front();
    require(profile(sim.state(), developed.measurementProfileId).detectionThreshold == 9.0 &&
                !sim.state().technicalDevelopmentPrograms.front().issue.acknowledged,
            "public target7 does not replace acquired threshold9 or hide its issue");
    const auto issue = sim.state().technicalDevelopmentPrograms.front().issue;
    require(sim.execute(AcknowledgeTechnicalDevelopmentIssueCommand{
                            sim.state().technicalDevelopmentPrograms.front().id, issue.signature})
                .ok,
            "player accepts the measured limitation without reroll or a new program");
    require(sim.advanceDaysDetailed(1).advancedDays == 1, "accepted result allows ordinary time to continue");
    require(sim.state().developedComponentRevisions.size() == 1 &&
                sim.state().componentProductionCapabilities.empty() &&
                sim.state().supportQualificationRecords.empty(),
            "player can decline later process and support maturity without losing evidence");
    const BodyId target{999};
    const std::vector<MineralDeposit> signal{{target, Mineral::WaterIce, 7.0, 1.0}};
    require(ice(sampleObservationChannels(signal, target,
                                          profile(sim.state(), developed.measurementProfileId), 5)) ==
                ResourceIndication::NotDetectedWithinLimit,
            "threshold9 instrument cannot detect fixed signal7 by a target-based bonus");
    require(std::any_of(sim.state().shipComponents.begin(), sim.state().shipComponents.end(),
                        [](const auto& value) { return value.name == "Specialist Survey Array"; }),
            "established equipment remains available after optional disappointment");
    validateGameState(sim.state());
}
} // namespace

int main() {
    try {
        optional_precision_success();
        optional_precision_miss_and_decline();
        std::cout << "P6 optional technical outcomes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 technical scenario failed: " << error.what() << '\n';
        return 1;
    }
}
