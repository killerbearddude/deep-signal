#pragma once
// Earned inspection fixture; every vessel, delivery and installed module is
// created by normal simulation commands and elapsed physical work.
#include "sim/GameState.h"
namespace deep {
[[nodiscard]] GameState earnSiteDevelopmentFixture(int throughDay = 90, bool usefulIce = true);
} // namespace deep
