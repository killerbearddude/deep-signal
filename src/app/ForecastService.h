#pragma once

// Responsibility: expose explainable, advisory forecasts as owned value DTOs.
// ForecastService borrows live SimulationService state without mutating it. It
// does not advance a copied Simulation, reserve resources, or guarantee outcomes.
// Amounts use the simulation's abstract resource units; rates are units per game
// day; modifier percentages use points. Geological reserves remain unmeasured.

#include "app/SimulationService.h"
#include "sim/Domain.h"
#include "sim/IdTypes.h"
#include "sim/Minerals.h"

#include <optional>
#include <string>
#include <vector>

namespace deep {

// Appointment effect contribution row used by forecasts. Values are percentage
// points and include a cap-adjustment row when the raw modifier exceeds v1 caps.
struct ForecastModifierBreakdownRow {
    std::string label;
    double percent = 0.0;
};

// Current-session observed mining output for each colony/declared channel.
// incomePerDay projects that recorded rate only; zero without a telemetry row
// is not a claim that no deposit exists. No physical geology is inspected.
struct MineralIncomeForecast {
    ColonyId colonyId;
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    std::string colonyName;
    std::string bodyName;
    std::string mineralName;
    std::string knowledgeLimit = "Reserve quantity unmeasured";
    double incomePerDay = 0.0;
    std::string explanation;
    bool operator==(const MineralIncomeForecast&) const = default;
};

// One explanation row in a forecast cause chain. Flow rows use signed units per
// game day. Geological reserve amounts are not measured by P4A profiles.
struct MineralForecastCauseRow {
    std::string label;
    double amountPerDay = 0.0;
    std::string explanation;
    bool operator==(const MineralForecastCauseRow&) const = default;
};

// Empire-level raw mineral forecast with simple cause rows for UI explanation.
// Uses recorded current-session mining output and known processing inputs.
// Runout extrapolates a constant net rate; it does not simulate policy changes,
// future depletion, or transport between colonies. nullopt means the current net
// rate does not predict runout, not that supply is guaranteed indefinitely.
struct MineralForecastCauseChain {
    Mineral mineral = Mineral::Iron;
    std::string mineralName;
    double stockpile = 0.0;
    double miningIncomePerDay = 0.0;
    double committedDemandPerDay = 0.0;
    double netPerDay = 0.0;
    std::optional<int> stockpileRunoutDays;
    std::string uncertaintyWarning;
    std::vector<MineralForecastCauseRow> causes;
    bool operator==(const MineralForecastCauseChain&) const = default;
};

// Empire-level processed-material forecast. Processing income comes from current
// colony processing policies and current raw stockpiles, without next-tick mining.
// Demand amortizes each active order over its standalone capacity-only ETA; this
// is a pressure indicator, not the shared FIFO queue's daily spending schedule.
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

// Knowledge-limited lifetime advisory per public body and declared channel.
// P4A has no reserve quantity measurement, so exhaustionDays remains absent;
// this view never inspects hidden remaining quantity or accessibility.
struct DepositExhaustionForecast {
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    std::string bodyName;
    std::string mineralName;
    std::string knowledgeLimit = "Reserve quantity unmeasured";
    double incomePerDay = 0.0;
    std::optional<int> exhaustionDays;
    std::string explanation;
    bool operator==(const DepositExhaustionForecast&) const = default;
};

// Capacity-only shipyard completion estimate for one production order.
// Treats the order as the sole consumer of its colony's effective capacity and
// ignores queue predecessors and material shortages. Use productionBacklog for
// queue-aware capacity estimates. etaDays is relative to the current game day,
// zero for completed orders, and empty when a usable capacity is unavailable.
struct ShipyardOrderEtaForecast {
    ShipyardOrderId orderId;
    ColonyId colonyId;
    ShipClassId shipClassId;
    std::string colonyName;
    std::string shipClassName;
    int shipsRemaining = 0;
    double buildPointsRemaining = 0.0;
    double effectiveShipyardCapacity = 0.0;
    double shipyardModifierPercent = 0.0;
    std::vector<ForecastModifierBreakdownRow> shipyardModifierBreakdown;
    std::optional<int> etaDays;
    std::string explanation;
};

// Production backlog row for one shipyard order. The forecast models colony
// capacity as a single FIFO pool and exposes an immutable design blocker, but
// does not simulate material delays. The
// shortage flag compares this order's entire remaining cost with current stock;
// it neither reserves stock for predecessors nor describes a next-ship blocker.
// queuePosition is one-based for active orders and zero for completed orders.
// statusName is derived display text, not an authoritative production state.
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
    double effectiveShipyardCapacity = 0.0;
    double shipyardModifierPercent = 0.0;
    std::vector<ForecastModifierBreakdownRow> shipyardModifierBreakdown;
    double accumulatedBuildPoints = 0.0;
    double buildPointsRemaining = 0.0;
    ProcessedMaterialSet requiredMaterialsRemaining;
    bool blockedByMaterial = false;
    bool blockedByComponentSupply = false;
    std::optional<ProcessedMaterial> blockingMaterial;
    std::string blockingMaterialName;
    std::string componentSupplyExplanation;
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

// Fuel/range summary for one fleet. Fuel uses abstract propellant units; range
// uses map-distance units with the current commander efficiency modifier. Range
// is a scalar budget, not a promise that a moving target or queued route is
// reachable. fuelPercent uses 100 for a full tank; validation permits tiny
// floating-point tolerance above capacity and this projection does not clamp it.
struct FleetFuelForecast {
    FleetId fleetId;
    std::string fleetName;
    double currentFuel = 0.0;
    double fuelCapacity = 0.0;
    double fuelPercent = 0.0;
    double currentRange = 0.0;
    double fuelEfficiencyModifierPercent = 0.0;
    std::vector<ForecastModifierBreakdownRow> fuelModifierBreakdown;
    std::string explanation;
};

// Read-only forecasting facade over a live SimulationService reference. Each
// call reads current state and returns independent DTO values; constructing the
// facade does not capture a snapshot. Keep the service alive and at a stable
// address, and serialize forecasts with commands/load/new-game calls. A group of
// forecast calls only describes one game day if no mutations occur between them.
class ForecastService {
public:
    // Binds the forecast service to an application service. The caller must keep
    // the referenced service alive longer than this object.
    explicit ForecastService(const SimulationService& service) noexcept;

    // Returns declared colony/channel rows with current-session observed output.
    [[nodiscard]] std::vector<MineralIncomeForecast> mineralIncomePerDay() const;

    // Returns empire-level raw mineral cause chains showing mining income,
    // processing demand, net flow, and stockpile runout where applicable.
    [[nodiscard]] std::vector<MineralForecastCauseChain> mineralForecastCauseChains() const;

    // Returns empire-level processed-material cause chains showing processor
    // output, active shipyard demand, net flow, and runout where applicable.
    [[nodiscard]] std::vector<ProcessedMaterialForecastCauseChain> processedMaterialForecastCauseChains() const;

    // Returns explicit insufficient-evidence rows without geological ETAs.
    [[nodiscard]] std::vector<DepositExhaustionForecast> depositExhaustionEstimates() const;

    // Returns standalone capacity-only ETAs, ignoring other queued orders.
    [[nodiscard]] std::vector<ShipyardOrderEtaForecast> shipyardOrderEtas() const;

    // Returns FIFO capacity estimates and whole-order material pressure. ETAs
    // remain optimistic when stock shortages delay this order or predecessors.
    [[nodiscard]] std::vector<ProductionBacklogForecast> productionBacklog() const;

    // Returns fleet arrival ETAs for idle and moving fleets.
    [[nodiscard]] std::vector<FleetArrivalEtaForecast> fleetArrivalEtas() const;

    // Returns current propellant and range forecasts for each fleet.
    [[nodiscard]] std::vector<FleetFuelForecast> fleetFuelForecasts() const;

private:
    const SimulationService& service_;
};

} // namespace deep
