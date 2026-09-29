#pragma once
// Trust-boundary checks for finite teams, stationary tender custody and durable
// full-service jobs/receipts. Missing readiness remains valid player intent.
#include "sim/GameState.h"
namespace deep {
void validateMaintenanceState(const GameState& state);
}
