// Five real years from an earned mature industrial site. Yearly checkpoints
// validate/save/load/continue and record growth without a hardware-speed gate.
#include "app/P6ProvingFixture.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"
#include "sim/Simulation.h"
#include "support/P6StateFingerprint.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>

namespace {
using namespace deep;
struct FuelRecoveryWitness {
    std::optional<FreightProgramId> waitingProgram;
    double loadedAtWait = 0;
    double grossAtWait = 0;
    std::int64_t waitDay = 0;
    std::int64_t recoveredDay = 0;
    std::map<std::pair<std::int64_t, std::string>, std::pair<std::int64_t, double>> acknowledgedLimitations;
};
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void oneDay(Simulation& sim, FuelRecoveryWitness* witness = nullptr) {
    const double producedBeforeOpening =
        sim.state().colonies.back().processedProductionTotals.get(ProcessedMaterial::Propellant);
    const auto result = sim.advanceDaysDetailed(1);
    if (result.advancedDays != 1)
        throw std::runtime_error("P6 five-year standing run missed a day: " + result.stopReason);
    if (result.interrupted) {
        bool acceptedFuelLimit = false;
        for (const auto& program : sim.state().freightPrograms)
            if (!program.issue.acknowledged &&
                program.issue.message.find("base operating Propellant above the protected floor") !=
                    std::string::npos) {
                const auto response =
                    sim.execute(AcknowledgeFreightProgramIssueCommand{program.id, program.issue.signature});
                require(response.ok, "declared standing-freight fuel limitation is acknowledgeable");
                acceptedFuelLimit = true;
                if (witness) {
                    const auto key = std::pair{program.id.value, program.issue.signature};
                    const auto prior = witness->acknowledgedLimitations.find(key);
                    if (prior != witness->acknowledgedLimitations.end())
                        require(program.fuelLoaded > prior->second.second,
                                "accepted fuel limitation cannot reinterrupt without new physical work");
                    witness->acknowledgedLimitations[key] = {sim.state().date.day, program.fuelLoaded};
                }
                if (witness && !witness->waitingProgram) {
                    witness->waitingProgram = program.id;
                    witness->loadedAtWait = program.fuelLoaded;
                    witness->grossAtWait = sim.state().colonies.back().processedProductionTotals.get(
                        ProcessedMaterial::Propellant);
                    witness->waitDay = sim.state().date.day;
                }
            }
        if (!acceptedFuelLimit)
            throw std::runtime_error("P6 five-year standing run needs a decision at day " +
                                     std::to_string(sim.state().date.day) + ": " + result.stopReason);
    }
    if (witness && witness->waitingProgram && witness->recoveredDay == 0)
        for (const auto& program : sim.state().freightPrograms)
            if (program.id == *witness->waitingProgram && program.fuelLoaded > witness->loadedAtWait &&
                producedBeforeOpening > witness->grossAtWait)
                witness->recoveredDay = sim.state().date.day;
}
void five_year_standing_operation() {
    p6test::TemporaryDirectory temp;
    Simulation sim(earnP6EstablishedLoop().mature);
    const auto startingDay = sim.state().date.day;
    const auto oneTimeDesigns = sim.state().developedComponentRevisions.size();
    const auto originalShipCount = sim.state().ships.size();
    const auto installedGroups = sim.state().resourceSites.front().installed.size();
    const auto originalAssessments = sim.state().assessments.size();
    const auto initialClosedSurveyReports = sim.state().surveyPrograms.front().reports.size();
    std::size_t priorEvents = sim.state().eventLog.size();
    std::size_t priorExtraction = sim.state().resourceSites.front().extractionReceipts.size();
    std::size_t priorFreight = 0;
    FuelRecoveryWitness recovery;
    for (const auto& program : sim.state().freightPrograms)
        priorFreight += program.receipts.size();

    for (int year = 1; year <= 5; ++year) {
        const auto started = std::chrono::steady_clock::now();
        for (int day = 0; day < 365; ++day)
            oneDay(sim, &recovery);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto& state = sim.state();
        require(state.date.day == startingDay + 365 * year, "yearly integer-day boundary is exact");
        validateGameState(state);
        require(state.ships.size() == originalShipCount &&
                    state.resourceSites.front().installed.size() == installedGroups &&
                    state.assessments.size() == originalAssessments &&
                    state.developedComponentRevisions.size() == oneTimeDesigns &&
                    state.prototypeIntegrationReceipts.empty(),
                "one-time assets and technical artifacts do not duplicate with elapsed years");
        require(state.surveyPrograms.front().lifecycle == SurveyProgramLifecycle::Closed &&
                    state.surveyPrograms.front().reports.size() == initialClosedSurveyReports,
                "closed survey stays closed and creates no later periodic reports");
        for (const auto& colony : state.colonies)
            for (double quantity : colony.processedStockpile.amount)
                require(std::isfinite(quantity) && quantity >= 0,
                        "standing industry has no nonfinite or negative processed stock");
        const auto& site = state.resourceSites.front();
        require(site.extractionReceipts.size() >= priorExtraction && state.eventLog.size() >= priorEvents,
                "cumulative physical histories never shrink");
        std::size_t freightRows = 0;
        for (const auto& program : state.freightPrograms) {
            freightRows += program.receipts.size();
            double onboard = 0;
            for (const auto& ship : state.ships)
                if (ship.cargo && ship.cargo->programId == program.id)
                    onboard += ship.cargo->quantity;
            require(std::abs(program.cargoLoaded - program.cargoDelivered - program.cargoReturned - onboard) <
                        1e-7,
                    "standing freight keeps exact custody through yearly boundary");
        }
        require(freightRows >= priorFreight, "freight history remains cumulative");
        const auto saved = temp.path / "yearly.sqlite";
        save::SaveGameRepository::save(saved, state);
        Simulation loaded(save::SaveGameRepository::load(saved));
        require(p6test::durableSnapshot(state, temp.path / "original.sqlite") ==
                    p6test::durableSnapshot(loaded.state(), temp.path / "loaded.sqlite"),
                "yearly loaded world matches all logical v18 columns");
        Simulation direct(state);
        for (int sample = 0; sample < 7; ++sample) {
            oneDay(direct);
            oneDay(loaded);
        }
        require(p6test::durableSnapshot(direct.state(), temp.path / "sample-original.sqlite") ==
                    p6test::durableSnapshot(loaded.state(), temp.path / "sample-loaded.sqlite"),
                "yearly Save/Load continues identically across another week");

        std::cout << "P6 year " << year << " day=" << state.date.day << " seconds=" << seconds
                  << " events=" << state.eventLog.size()
                  << " event_delta=" << state.eventLog.size() - priorEvents
                  << " extraction=" << site.extractionReceipts.size()
                  << " extraction_delta=" << site.extractionReceipts.size() - priorExtraction
                  << " freight=" << freightRows << " freight_delta=" << freightRows - priorFreight
                  << " site_reports=" << site.reports.size()
                  << " survey_reports=" << state.surveyPrograms.front().reports.size()
                  << " assessments=" << state.assessments.size()
                  << " technical=" << state.developedComponentRevisions.size()
                  << " ships=" << state.ships.size() << '\n';
        priorEvents = state.eventLog.size();
        priorExtraction = site.extractionReceipts.size();
        priorFreight = freightRows;
    }
    require(recovery.waitingProgram.has_value() && recovery.recoveredDay > recovery.waitDay,
            "same waiting freight intent later loads Propellant produced before its opening");
    std::cout << "P6 fuel recovery: wait_day=" << recovery.waitDay
              << " recovered_day=" << recovery.recoveredDay << " program=" << recovery.waitingProgram->value
              << '\n';
}

void completed_technical_artifacts_survive_five_years() {
    Simulation sim(earnTechnicalDevelopmentFixture(6.0));
    const auto starting = sim.state().date.day;
    require(sim.state().developedComponentRevisions.size() == 1 &&
                sim.state().prototypeIntegrationReceipts.size() == 1 &&
                sim.state().supportQualificationRecords.size() == 1,
            "supplemental long-session branch starts from earned P5 artifacts");
    for (int day = 0; day < 1'825; ++day)
        oneDay(sim);
    require(sim.state().date.day == starting + 1'825 && sim.state().developedComponentRevisions.size() == 1 &&
                sim.state().prototypeIntegrationReceipts.size() == 1 &&
                sim.state().componentProductionCapabilities.size() == 1 &&
                sim.state().supportQualificationRecords.size() == 1,
            "technical demonstration, prototype use, process and support never republish with elapsed years");
    validateGameState(sim.state());
}
} // namespace

int main() {
    try {
        five_year_standing_operation();
        completed_technical_artifacts_survive_five_years();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 soak proof failed: " << error.what() << '\n';
        return 1;
    }
}
