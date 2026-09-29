#pragma once
// Current-format site stock, operating authority and dated evidence validation.
// Validation never discovers geology or reconstructs missing operating records.
#include "sim/GameState.h"
namespace deep {
// Throws on invalid stock/capacity, historical hardware cutoff or duty accounting.
void validateSiteOperationState(const GameState&);
} // namespace deep
