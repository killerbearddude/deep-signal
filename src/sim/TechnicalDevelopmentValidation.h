#pragma once

// Validates the P5 graph, artifact provenance, local production/support records,
// and frozen prototype-backed shipyard plans at external state boundaries.
#include "sim/GameState.h"

namespace deep {
// Throws on any P5 graph, accounting, provenance, locality, or reservation violation.
void validateTechnicalDevelopmentState(const GameState&);
} // namespace deep
