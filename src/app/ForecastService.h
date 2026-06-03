#pragma once

// Declares small app-layer forecast DTOs for explainable UI projections.
// ForecastService reads SimulationService state but never mutates simulation data,
// keeping future ImGui panels decoupled from raw GameState vectors.

#include "app/SimulationService.h"
#include "sim/Domain.h"
#include "sim/IdTypes.h"
#include "sim/Minerals.h"

#include <optional>
#include <string>
#include <vector>

namespace deep {

// Next-day mineral production forecast for one colony/deposit pair.
// incomePerDay uses the same prototype mining formula and shared-deposit
// consumption order as Simulation::simulateMining.
struct MineralIncomeForecast {
    ColonyId colonyId;
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    std::string colonyName;
    std::string bodyName;
    std::string mineralName;
    double incomePerDay = 0.0;
    std::string explanation;
};

// One signed contribution row in a mineral forecast cause chain. Positive values
// increase stockpile, while negative values represent committed demand.
struct MineralForecastCauseRow {
    std::string label;
    double amountPerDay = 0.0;
    std::string explanation;
};

// Empire-level raw mineral forecast with simple cause rows for UI explanation.
// This v1 forecast uses current mining income and fixed processing recipe demand;
// it does not introduce new economy systems or future automation.
struct MineralForecastCauseChain {
    Mineral mineral = Mineral::Iron;
    std::string mineralName;
    double stockpile = 0.0;
    double miningIncomePerDay = 0.0;
    double committedDemandPerDay = 0.0;
    double netPerDay = 0.0;
    std::optional<int> stockpileRunoutDays;
    std::vector<MineralForecastCauseRow> causes;
};

// Empire-level processed-material forecast. Processing income comes from current
// colony processing policies and raw-resource availability, while demand is
// active shipyard commitments amortized over ETA.
struct ProcessedMaterialForecastCauseChain {
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    std::string materialName;
    double stockpile = 0.0;
    double processingIncomePerDay = 0.0;
    double committedDemandPerDay = 0.0;
    double netPerDay = 0.0;
    std::optional<int> stockpileRunoutDays;
    std::vector<MineralForecastCauseRow> causes;
};

// Deposit lifetime estimate for one currently-known mineral deposit.
// exhaustionDays is empty when no positive extraction rate exists.
struct DepositExhaustionForecast {
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    std::string bodyName;
    std::string mineralName;
    double remainingDeposit = 0.0;
    double incomePerDay = 0.0;
    std::optional<int> exhaustionDays;
    std::string explanation;
};

// Capacity-only shipyard completion estimate for one production order.
// This deliberately does not simulate future mineral shortages; explanation text
// makes that limitation visible to callers and future UI panels.
struct ShipyardOrderEtaForecast {
    ShipyardOrderId orderId;
    ColonyId colonyId;
    ShipClassId shipClassId;
    std::string colonyName;
    std::string shipClassName;
    int shipsRemaining = 0;
    double buildPointsRemaining = 0.0;
    std::optional<int> etaDays;
    std::string explanation;
};

// Production backlog row for one shipyard order. The forecast models colony
// capacity as a single FIFO pool, matching simulation production allocation,
// and reports mineral pressure without changing production mechanics.
struct ProductionBacklogForecast {
    ShipyardOrderId orderId;
    ColonyId colonyId;
    ShipClassId shipClassId;
    std::string colonyName;
    std::string shipClassName;
    int quantityRequested = 0;
    int quantityCompleted = 0;
    int shipsRemaining = 0;
    int queuePosition = 0;
    double colonyShipyardCapacity = 0.0;
    double accumulatedBuildPoints = 0.0;
    double buildPointsRemaining = 0.0;
    ProcessedMaterialSet requiredMaterialsRemaining;
    bool blockedByMaterial = false;
    std::optional<ProcessedMaterial> blockingMaterial;
    std::string blockingMaterialName;
    std::optional<int> etaDays;
    std::string statusName;
    std::string explanation;
};

// Arrival estimate for one fleet. etaDays is empty when the fleet is idle or has
// invalid movement state that cannot produce an arrival projection.
struct FleetArrivalEtaForecast {
    FleetId fleetId;
    std::string fleetName;
    BodyId currentBodyId;
    std::string currentBodyName;
    std::optional<BodyId> destinationBodyId;
    std::string destinationBodyName;
    std::optional<int> etaDays;
    std::string explanation;
};

// Read-only forecasting facade over SimulationService. Forecasts are copied out
// as DTOs so UI code can display projections without owning simulation rules.
class ForecastService {
public:
    // Binds the forecast service to an application service. The caller must keep
    // the referenced service alive longer than this object.
    explicit ForecastService(const SimulationService& service) noexcept;

    // Returns next-day mineral income rows for each colony/deposit pair. Rows
    // share deposit remaining in deterministic colony/deposit order.
    [[nodiscard]] std::vector<MineralIncomeForecast> mineralIncomePerDay() const;

    // Returns empire-level raw mineral cause chains showing mining income,
    // processing demand, net flow, and stockpile runout where applicable.
    [[nodiscard]] std::vector<MineralForecastCauseChain> mineralForecastCauseChains() const;

    // Returns empire-level processed-material cause chains showing processor
    // output, active shipyard demand, net flow, and runout where applicable.
    [[nodiscard]] std::vector<ProcessedMaterialForecastCauseChain> processedMaterialForecastCauseChains() const;

    // Returns deposit lifetime estimates using current daily extraction rates.
    [[nodiscard]] std::vector<DepositExhaustionForecast> depositExhaustionEstimates() const;

    // Returns capacity-only ETAs for shipyard orders.
    [[nodiscard]] std::vector<ShipyardOrderEtaForecast> shipyardOrderEtas() const;

    // Returns production backlog rows that account for FIFO colony capacity and
    // current mineral blockers. This is read-only forecasting only.
    [[nodiscard]] std::vector<ProductionBacklogForecast> productionBacklog() const;

    // Returns fleet arrival ETAs for idle and moving fleets.
    [[nodiscard]] std::vector<FleetArrivalEtaForecast> fleetArrivalEtas() const;

private:
    const SimulationService& service_;
};

} // namespace deep
