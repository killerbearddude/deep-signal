// The native-review pack is a generated inspection artifact, not seeded game
// state. The writer itself reloads, FK-checks and continues each v18 checkpoint.
#include "app/P6ReviewPack.h"
#include "save/SaveGameRepository.h"
#include "sim/GameStateValidation.h"
#include "support/P6StateFingerprint.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void earned_native_review_checkpoints() {
    deep::p6test::TemporaryDirectory temp;
    const auto pack = temp.path / "review";
    deep::writeP6HumanReviewPack(pack);
    require(std::filesystem::exists(pack / "REVIEW_MANIFEST.md"),
            "pack publishes its developer review protocol map");
    const auto load = [&](const char* filename) {
        const auto path = pack / filename;
        require(std::filesystem::exists(path), "required native checkpoint exists");
        auto state = deep::save::SaveGameRepository::load(path);
        deep::validateGameState(state);
        return state;
    };
    const auto waiting = load("01-waiting-intent.sqlite");
    require(waiting.shipyardOrders.size() == 1 && waiting.ships.empty() &&
                waiting.shipyardOrders.front().accumulatedBuildPoints == 0,
            "first checkpoint is accepted zero-capacity intent");
    const auto choice = load("02-design-choice.sqlite");
    require(!choice.developedComponentRevisions.empty() && choice.shipClasses.size() >= 3,
            "second checkpoint earned distinct ship design alternatives");
    const auto blind = load("03-blind-development.sqlite");
    require(blind.resourceSites.empty() && blind.observations.empty() && blind.assessments.empty(),
            "blind site checkpoint has no geological interaction");
    const auto evidence = load("04-evidence-and-operation.sqlite");
    require(!evidence.observations.empty() && !evidence.assessments.empty() &&
                !evidence.resourceSites.front().extractionReceipts.empty(),
            "evidence and operating outcomes are both command-earned");
    const auto bottleneck = load("05-accepted-bottleneck.sqlite");
    require(!bottleneck.resourceSites.front().issue.acknowledged,
            "limitation checkpoint awaits a real native acknowledgment");
    const auto lasting = load("06-lasting-capability.sqlite");
    const auto delegated = load("07-delegated-90-day.sqlite");
    require(lasting.colonies.back().processedProductionTotals.get(deep::ProcessedMaterial::Propellant) > 0 &&
                delegated.date.day == lasting.date.day + 90,
            "later checkpoint retains the earned supply consequence after routine months");
    std::size_t sqliteFiles = 0;
    for (const auto& file : std::filesystem::directory_iterator(pack))
        sqliteFiles += file.path().extension() == ".sqlite";
    require(sqliteFiles == 7, "pack contains exactly the seven documented v18 checkpoints");
}
} // namespace

int main() {
    try {
        earned_native_review_checkpoints();
        std::cout << "P6 native review pack passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 review pack proof failed: " << error.what() << '\n';
        return 1;
    }
}
