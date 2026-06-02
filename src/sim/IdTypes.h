#pragma once

// Defines strongly typed integer IDs for simulation entities.
// IDs are stable handles into GameState-owned vectors; they do not imply pointer
// ownership and are safe to persist later in SQLite rows.

#include <cstdint>

namespace deep {

// Lightweight tagged identifier used to prevent accidental cross-use of entity IDs.
// A value of 0 is reserved as invalid/unassigned; valid IDs are positive.
template <typename Tag>
struct Id {
    std::int64_t value = 0;

    constexpr auto operator<=>(const Id&) const = default;

    // Returns true when the ID is assigned. This is useful for validation paths
    // without allowing implicit conversion back to the raw integer value.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value > 0; }
};

// Empty tag types make each ID alias a distinct C++ type at compile time.
struct StarSystemTag;
struct BodyTag;
struct ColonyTag;
struct ShipClassTag;
struct ShipTag;
struct FleetTag;
struct ShipyardOrderTag;
struct EventTag;

using StarSystemId = Id<StarSystemTag>;
using BodyId = Id<BodyTag>;
using ColonyId = Id<ColonyTag>;
using ShipClassId = Id<ShipClassTag>;
using ShipId = Id<ShipTag>;
using FleetId = Id<FleetTag>;
using ShipyardOrderId = Id<ShipyardOrderTag>;
using EventId = Id<EventTag>;

} // namespace deep
