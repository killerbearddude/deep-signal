#pragma once

// Typed access to existing colony/site inventories. No balance is duplicated;
// mutable references are short-lived within serialized simulation actions.
#include "sim/GameState.h"
#include "sim/StockTypes.h"

namespace deep {
// Checks the selected namespace's reference without consulting physical deposits.
[[nodiscard]] bool stockLocationExists(const GameState&, const StockLocation&);
// Public metadata for routes and labels; unknown references throw, not default.
[[nodiscard]] BodyId stockLocationBody(const GameState&, const StockLocation&);
[[nodiscard]] std::string stockLocationName(const GameState&, const StockLocation&);
// Selects exactly one raw or processed balance at exactly one storage identity.
// No permission, capacity, floor, supply or transfer is implied by this accessor.
[[nodiscard]] double stockQuantity(const GameState&, const StockLocation&, const Commodity&);
// Mutation callers prepare finite debits/credits and capacity constraints first.
[[nodiscard]] double& stockQuantity(GameState&, const StockLocation&, const Commodity&);
} // namespace deep
