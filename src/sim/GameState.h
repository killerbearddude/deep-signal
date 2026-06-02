#pragma once

// Defines the aggregate mutable state for one running simulation.
// GameState owns all domain records by value; references between records are
// stable typed IDs rather than owning pointers.

#include "sim/Domain.h"
#include "sim/Events.h"
#include "sim/GameDate.h"

#include <cstdint>
#include <vector>

namespace deep {

// Monotonic ID counters for each entity type. Counters are part of save state so
// loaded games can continue allocating IDs without collisions.
struct IdCounters {
    std::int64_t nextStarSystemId = 1;
    std::int64_t nextBodyId = 1;
    std::int64_t nextColonyId = 1;
    std::int64_t nextShipClassId = 1;
    std::int64_t nextShipyardOrderId = 1;
    std::int64_t nextShipId = 1;
    std::int64_t nextFleetId = 1;
    std::int64_t nextEventId = 1;
};

// Complete state snapshot for the headless simulation. Public vectors are kept
// simple for prototype inspectability; mutation is still routed through
// Simulation so invariants and events stay centralized.
struct GameState {
    GameDate date;
    IdCounters ids;

    std::vector<StarSystem> starSystems;
    std::vector<Body> bodies;
    std::vector<MineralDeposit> mineralDeposits;
    std::vector<Colony> colonies;
    std::vector<ShipClass> shipClasses;
    std::vector<ShipyardOrder> shipyardOrders;
    std::vector<Ship> ships;
    std::vector<Fleet> fleets;
    std::vector<SimEvent> eventLog;
};

} // namespace deep
