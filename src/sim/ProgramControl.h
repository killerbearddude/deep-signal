#pragma once

// Shared identity and opening-phase resource scratch space for five purpose-specific programs.
// Leases remain on programs; neither reverse ownership nor daily budgets are saved.
#include "sim/GameState.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <variant>

namespace deep {

using ProgramController = std::variant<SurveyProgramId, FreightProgramId, MaintenanceProgramId,
                                       AnalysisProgramId, SiteDevelopmentProgramId>;

// Derives the unique controller from canonical leases. Kind is part of identity.
[[nodiscard]] std::optional<ProgramController> controllingProgram(const GameState& state, FleetId fleetId);
[[nodiscard]] std::string programControllerLabel(const GameState& state, const ProgramController& owner);

// Derives exclusive ownership of the shared field/laboratory scientific workforce.
[[nodiscard]] std::optional<ProgramController> controllingScientificTeam(const GameState&, SurveyTeamId);

[[nodiscard]] std::optional<ProgramController> controllingEngineeringTeam(const GameState&,
                                                                          MaintenanceTeamId);
[[nodiscard]] std::optional<SiteDevelopmentProgramId> controllingSiteConstruction(const GameState&, SiteId);

// Site operating decisions do not confer fleet or team custody.
using DecisionSource = std::variant<ProgramController, SiteId>;
struct PendingDecision {
    DecisionSource source;
    std::string message;
};
[[nodiscard]] std::optional<PendingDecision> pendingDecision(const GameState&);
[[nodiscard]] std::string decisionSourceLabel(const GameState&, const DecisionSource&);

struct ProgramPendingIssue {
    ProgramController controller;
    std::string message;
};

// Returns the first pending issue in the same stable merged order as dispatch.
[[nodiscard]] std::optional<ProgramPendingIssue> pendingProgramIssue(const GameState& state);

// Head-only merge preserves stored within-kind vector ordering even when those
// vectors are not chronological. Equal creation dates give survey the tie.
[[nodiscard]] std::vector<ProgramController> programOpeningOrder(const GameState& state);

struct StockOpeningBudget {
    StockLocation location;
    ProcessedMaterialSet processed;
    MineralSet raw;
};
struct SiteOpeningBudget {
    SiteId siteId;
    std::size_t installedGroupCutoff = 0;
    double duty = 0;
    double remainingHandling = 0;
    double receivingRoom = 0;
    double extractionRate = 0;
    double rawCapacity = 0;
};

struct OpeningProgramContext {
    std::unordered_set<std::int64_t> occupiedFleets;
    std::unordered_set<std::int64_t> occupiedTeams;
    std::unordered_set<std::int64_t> occupiedMaintenanceTeams;
    // Holds are phase-local exclusions, not additional movement ownership.
    std::unordered_set<std::int64_t> serviceHolds;
    std::unordered_set<std::int64_t> eligibleServiceClients;
    std::unordered_set<std::int64_t> occupiedConstructionSites;
    std::vector<StockOpeningBudget> stock;
    std::vector<SiteOpeningBudget> sites;
    // Opening-only information eligibility and finite local lab throughput.
    std::unordered_set<std::int64_t> availableObservations;
    std::vector<std::pair<ColonyId, double>> analysisThroughput;

    // Snapshots real stock and all existing leases once per opening phase.
    explicit OpeningProgramContext(const GameState& state);
    // Bounds a withdrawal by both current stock and unspent opening stock.
    // An inbound credit does not increase remaining; floors apply to both.
    [[nodiscard]] double available(const GameState& state, StockLocation location, Commodity material,
                                   double floor) const;
    // Records an already validated actual withdrawal; does not touch inventory.
    void debit(StockLocation location, Commodity material, double amount);
    [[nodiscard]] double availableSiteRawHandling(SiteId) const;
    [[nodiscard]] double availableSiteRawRoom(SiteId) const;
    void debitSiteRawHandling(SiteId, double);
    void debitSiteRawRoom(SiteId, double);
};

} // namespace deep
