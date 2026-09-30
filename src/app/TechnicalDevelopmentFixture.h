#pragma once

// Builds the earned P5 inspection world through ordinary commands and elapsed
// days. No advanced component, prototype, process, observation, or repair is seeded.
#include "sim/GameState.h"

namespace deep {
// Returns a fully earned reference world; achievedThreshold selects only the
// deterministic test outcome and is never exposed before physical test work.
[[nodiscard]] GameState earnTechnicalDevelopmentFixture(double achievedThreshold = 6.0);
} // namespace deep
