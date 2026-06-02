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
// incomePerDay uses the same prototype mining formula as Simulation.
struct MineralIncomeForecast {
    ColonyId colonyId;
    BodyId bodyId;
    Mineral mineral = Mineral::Structural;
    std::string colonyName;
    std::string bodyName;
    std::string mineralName;
    double incomePerDay = 0.0;
    std::string explanation;
};

// Deposit lifetime estimate for one currently-known mineral deposit.
// exhaustionDays is empty when no positive extraction rate exists.
struct DepositExhaustionForecast {
    BodyId bodyId;
    Mineral mineral = Mineral::Structural;
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

    // Returns next-day mineral income rows for each colony/deposit pair.
    [[nodiscard]] std::vector<MineralIncomeForecast> mineralIncomePerDay() const;

    // Returns deposit lifetime estimates using current daily extraction rates.
    [[nodiscard]] std::vector<DepositExhaustionForecast> depositExhaustionEstimates() const;

    // Returns capacity-only ETAs for shipyard orders.
    [[nodiscard]] std::vector<ShipyardOrderEtaForecast> shipyardOrderEtas() const;

    // Returns fleet arrival ETAs for idle and moving fleets.
    [[nodiscard]] std::vector<FleetArrivalEtaForecast> fleetArrivalEtas() const;

private:
    const SimulationService& service_;
};

} // namespace deep
