#pragma once

// Defines raw mineral/resource categories and fixed-size mineral accounting.
// The fixed array representation is deliberately simple and persistence-friendly;
// enum order is schema-visible because save files store mineral ordinals.

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace deep {

// Raw extractable resources used by the v1 economy foundation. Processing chains
// will later convert these into industrial intermediates, but mining, stockpiles,
// and schema-v1 saves currently use these stable ordinals directly.
enum class Mineral : std::size_t {
    Iron = 0,
    Nickel = 1,
    Titanium = 2,
    Aluminum = 3,
    Copper = 4,
    Silicon = 5,
    Lithium = 6,
    Uranium = 7,
    Thorium = 8,
    RareEarthElements = 9,
    PlatinumGroupMetals = 10,
    WaterIce = 11,
    CarbonCompounds = 12,
    Volatiles = 13,
    Count = 14
};

// Number of mineral slots in MineralSet. Kept as a function so template arguments
// can stay readable while still being compile-time constants.
[[nodiscard]] inline constexpr std::size_t mineralCount() noexcept {
    return static_cast<std::size_t>(Mineral::Count);
}

// Shared tolerance for mineral affordability checks. This exists only to absorb
// tiny binary floating-point residue; larger shortages are gameplay errors.
inline constexpr double kMineralComparisonEpsilon = 1.0e-9;

// Display names are for CLI/debug/UI output only. Designer-facing localization can
// replace this later without changing the Mineral enum ordering.
inline constexpr std::array<std::string_view, mineralCount()> mineralNames{
    "Iron",
    "Nickel",
    "Titanium",
    "Aluminum",
    "Copper",
    "Silicon",
    "Lithium",
    "Uranium",
    "Thorium",
    "Rare Earth Elements",
    "Platinum Group Metals",
    "Water Ice",
    "Carbon Compounds",
    "Volatiles"
};

// Converts a mineral enum to its backing array index. Callers must not pass Count
// as a gameplay mineral; validation paths should reject it before mutation.
[[nodiscard]] inline constexpr std::size_t mineralIndex(const Mineral mineral) noexcept {
    return static_cast<std::size_t>(mineral);
}

// Returns a human-readable mineral name for diagnostics and CLI/UI output.
[[nodiscard]] inline constexpr std::string_view toString(const Mineral mineral) noexcept {
    const std::size_t index = mineralIndex(mineral);
    return index < mineralNames.size() ? mineralNames[index] : std::string_view{"Unknown"};
}

// Holds one amount for every Mineral enum value. Negative values are invalid
// because stockpiles and build costs represent physical quantities.
struct MineralSet {
    std::array<double, mineralCount()> amount{};

    // Returns the stored amount for the requested mineral.
    // Throws std::out_of_range if a corrupted enum value is supplied.
    [[nodiscard]] double get(Mineral mineral) const {
        return amount.at(mineralIndex(mineral));
    }

    // Replaces the amount for one mineral. The value must be non-negative.
    void set(Mineral mineral, double value) {
        if (value < 0.0) {
            throw std::invalid_argument{"Mineral values cannot be negative"};
        }
        amount.at(mineralIndex(mineral)) = value;
    }

    // Adds a non-negative amount to one mineral stockpile.
    void add(Mineral mineral, double value) {
        if (value < 0.0) {
            throw std::invalid_argument{"Mineral addition cannot be negative"};
        }
        amount.at(mineralIndex(mineral)) += value;
    }

    // Returns true when this set has enough of every mineral to pay cost.
    // The shared epsilon prevents tiny floating-point residue from blocking
    // completion while still rejecting meaningful shortages.
    [[nodiscard]] bool canPay(const MineralSet& cost) const noexcept {
        for (std::size_t i = 0; i < amount.size(); ++i) {
            if (amount[i] + kMineralComparisonEpsilon < cost.amount[i]) {
                return false;
            }
        }
        return true;
    }

    // Subtracts a full mineral cost. Epsilon-sized negative residue is clamped
    // to zero so an allowed payment cannot leave an invalid stockpile behind.
    void subtract(const MineralSet& cost) {
        if (!canPay(cost)) {
            throw std::runtime_error{"Insufficient minerals"};
        }

        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] -= cost.amount[i];

            if (amount[i] < 0.0 && amount[i] > -kMineralComparisonEpsilon) {
                amount[i] = 0.0;
            }

            if (amount[i] < 0.0) {
                throw std::runtime_error{"Mineral subtraction produced negative stockpile."};
            }
        }
    }

    // Adds another set slot-by-slot. Used for future bulk economy operations.
    void addSet(const MineralSet& other) noexcept {
        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] += other.amount[i];
        }
    }
};

} // namespace deep
