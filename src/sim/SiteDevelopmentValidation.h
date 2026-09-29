#pragma once
// Validates the current construction graph and earned work conservation. Save
// loading calls this without repairing missing history or granting capacities.
#include "sim/GameState.h"
namespace deep {
// Throws on malformed authority, custody, dates, row accounting or installation.
void validateSiteDevelopmentState(const GameState&);
} // namespace deep
