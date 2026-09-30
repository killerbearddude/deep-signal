#pragma once

// Plans and commits local supply for developed components on one current hull.
// Pure planning never reserves a prototype; binding occurs only with positive
// yard capacity at the authoritative shipyard tick.
#include "sim/TechnicalDevelopmentRules.h"

namespace deep {

struct DevelopedSupplyPlanResult {
    bool ready = false;
    std::string explanation;
    std::optional<ShipyardCurrentHullSupplyPlan> plan;
};

// Established classes bypass P5 supply; only demonstrated components return true.
[[nodiscard]] bool classRequiresDevelopedComponent(const GameState&, const ShipClass&) noexcept;
// Purely plans a complete serial/prototype source for every developed install.
[[nodiscard]] DevelopedSupplyPlanResult planDevelopedComponentSupply(const GameState&, const ShipyardOrder&,
                                                                     const ShipClass&, ColonyId,
                                                                     std::int64_t day);
// Atomically marks each planned local prototype as owned by this order/hull.
void reservePrototypeSupply(GameState&, const ShipyardOrder&, const ShipyardCurrentHullSupplyPlan&);
// Consumes reserved units once and appends produced-ship integration history.
void consumePrototypeSupply(GameState&, const ShipyardOrder&, ShipId, std::int64_t day);

} // namespace deep
