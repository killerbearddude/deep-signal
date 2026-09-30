// Generates seven command-earned checkpoints into a new directory, validates
// their current-schema storage and continuation, then publishes the directory.
#include "app/P6ReviewPack.h"
#include "app/P6ProvingFixture.h"
#include "app/SiteDevelopmentFixture.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "save/EventJson.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace deep {
namespace {
void verifyForeignKeys(const std::filesystem::path& path) {
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (raw)
            sqlite3_close(raw);
        throw std::runtime_error("P6 checkpoint cannot open read-only for FK inspection");
    }
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database.get(), "PRAGMA foreign_key_check", -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error("P6 checkpoint cannot inspect SQLite foreign keys");
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> check(statement, sqlite3_finalize);
    if (sqlite3_step(check.get()) != SQLITE_DONE)
        throw std::runtime_error("P6 checkpoint has a foreign-key violation");
}

void compareOneDay(GameState state, const GameState& loaded) {
    Simulation left(std::move(state)), right(loaded);
    // A review checkpoint may intentionally stop at a modeled decision. Both
    // continuations make the same existing typed acknowledgment before ticking.
    for (const auto& site : left.state().resourceSites)
        if (!site.issue.acknowledged) {
            const AcknowledgeSiteOperatingIssueCommand response{site.id, site.issue.cause,
                                                                site.issue.episodeStartedDay};
            if (!left.execute(response).ok || !right.execute(response).ok)
                throw std::runtime_error("P6 checkpoint cannot acknowledge its observed limitation");
        }
    const auto a = left.advanceDaysDetailed(1);
    const auto b = right.advanceDaysDetailed(1);
    if (a.advancedDays != 1 || b.advancedDays != 1 || a.interrupted != b.interrupted ||
        a.stopReason != b.stopReason || a.issueSource != b.issueSource ||
        left.state().date.day != right.state().date.day ||
        left.state().eventLog.size() != right.state().eventLog.size())
        throw std::runtime_error("P6 checkpoint changes its next decision after Save/Load");
    for (std::size_t index = 0; index < left.state().eventLog.size(); ++index) {
        const auto& x = left.state().eventLog[index];
        const auto& y = right.state().eventLog[index];
        if (x.id != y.id || x.day != y.day ||
            save::eventPayloadToJson(x.payload) != save::eventPayloadToJson(y.payload))
            throw std::runtime_error("P6 checkpoint changes event history after Save/Load");
    }
    for (std::size_t index = 0; index < left.state().colonies.size(); ++index)
        if (left.state().colonies[index].stockpile.amount != right.state().colonies[index].stockpile.amount ||
            left.state().colonies[index].processedStockpile.amount !=
                right.state().colonies[index].processedStockpile.amount)
            throw std::runtime_error("P6 checkpoint changes physical stock after Save/Load");
    validateGameState(left.state());
    validateGameState(right.state());
}

GameState waitingIntent() {
    auto state = createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 0;
    Simulation simulation(state);
    const auto result = simulation.execute(
        AssignShipyardBuildCommand{state.colonies.front().id, state.shipClasses.front().id, 1});
    if (!result.ok)
        throw std::runtime_error("P6 waiting review order was rejected");
    return simulation.state();
}

void saveVerified(const std::filesystem::path& path, const GameState& state) {
    validateGameState(state);
    save::SaveGameRepository::save(path, state);
    verifyForeignKeys(path);
    const auto loaded = save::SaveGameRepository::load(path);
    validateGameState(loaded);
    compareOneDay(state, loaded);
}
} // namespace

void writeP6HumanReviewPack(const std::filesystem::path& destination) {
    if (std::filesystem::exists(destination))
        throw std::invalid_argument("P6 review destination must not exist; choose a new directory");
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::current_path();
    if (!std::filesystem::exists(parent))
        throw std::invalid_argument("P6 review destination parent directory does not exist");
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = parent / (destination.filename().string() + ".p6-staging-" + std::to_string(suffix));
    if (!std::filesystem::create_directory(staging))
        throw std::runtime_error("P6 review staging directory already exists");

    // Failed generation leaves its uniquely named staging directory available
    // for inspection; the requested final path appears only after all checks.
    const auto established = earnP6EstablishedLoop();
    auto zero = earnSiteDevelopmentFixture(90, false);
    const auto zeroIssueDay = zero.resourceSites.front().issue.episodeStartedDay;
    if (zeroIssueDay <= 0)
        throw std::runtime_error("P6 zero-result review issue was not earned");
    bool awaitingResponse = false;
    for (int day = static_cast<int>(zeroIssueDay); day <= 90; ++day) {
        zero = earnSiteDevelopmentFixture(day, false);
        if (zero.resourceSites.front().issue.cause == SiteOperatingIssueCause::ZeroRecovery &&
            !zero.resourceSites.front().issue.acknowledged) {
            awaitingResponse = true;
            break;
        }
    }
    if (!awaitingResponse)
        throw std::runtime_error("P6 limitation checkpoint does not await a human response");
    Simulation later(established.mature);
    for (int day = 0; day < 90; ++day) {
        const auto result = later.advanceDaysDetailed(1);
        if (result.advancedDays != 1 || result.interrupted)
            throw std::runtime_error("P6 delegated review checkpoint did not reach 90 later days");
    }
    const std::array<std::pair<const char*, GameState>, 7> checkpoints{
        {{"01-waiting-intent.sqlite", waitingIntent()},
         {"02-design-choice.sqlite", earnTechnicalDevelopmentFixture(6.0)},
         {"03-blind-development.sqlite", createP6EstablishedStartingWorld()},
         {"04-evidence-and-operation.sqlite", established.mature},
         {"05-accepted-bottleneck.sqlite", zero},
         {"06-lasting-capability.sqlite", established.mature},
         {"07-delegated-90-day.sqlite", later.state()}}};
    for (const auto& [filename, state] : checkpoints)
        saveVerified(staging / filename, state);
    std::ofstream manifest(staging / "REVIEW_MANIFEST.md");
    if (!manifest)
        throw std::runtime_error("P6 review manifest could not be created");
    manifest << "# P6 native review checkpoints\n\n"
             << "All seven saves use the active v18 schema and were reloaded, FK checked, "
                "and continued for one deterministic day.\n\n"
             << "01: accepted order waiting at zero yard capacity (H1).\n"
             << "02: earned established/precision ship-design alternatives (H2).\n"
             << "03: known body before geological investigation or site authorization (H3).\n"
             << "04: acquired observation, assessment and operating site (H4).\n"
             << "05: observed operating limitation awaiting acknowledgment (H5).\n"
             << "06: lasting site supply and industry consequence (H6).\n"
             << "07: delegated programs after a further 90 simulated days (H7).\n\n"
             << "Use docs/architecture/p6-human-play-review.md for the native protocol.\n";
    manifest.close();
    if (!manifest)
        throw std::runtime_error("P6 review manifest write failed");
    std::filesystem::rename(staging, destination);
}

} // namespace deep
