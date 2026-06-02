#pragma once

// Defines simulation time for Prototype 0.1.
// Time is represented as an integer day counter to keep the headless simulation
// deterministic and independent from wall-clock time.

#include <cstdint>

namespace deep {

// Current simulation date. Day 0 is scenario start; positive values advance one
// deterministic daily tick at a time.
struct GameDate {
    std::int64_t day = 0;
};

} // namespace deep
