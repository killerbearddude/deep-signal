#pragma once

// Defines prototype mineral categories and fixed-size mineral accounting.
// The fixed array representation is deliberately simple and persistence-friendly;
// future data-driven minerals should keep a stable serialized ordering.

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace deep {

// Prototype resource categories. Values are persisted by ordinal, so append new
// minerals before Count only through a schema/data migration in later versions.
enum class Mineral : std::size_t {
    Structural = 0,
    Propulsion = 1,
    Electronics = 2,
    Fuel = 3,
    Ordnance = 4,
    Count = 5
};

// Number of mineral slots in MineralSet. Kept as a function so template arguments
// can stay readable while still being compile-time constants.
[[nodiscard]] inline constexpr std::size_t mineralCount() noexcept {
    return static_cast<std::size_t>(Mineral::Count);
}

// Display names are for CLI/debug output only. Designer-facing localization can
// replace this later without changing the Mineral enum ordering.
inline constexpr std::array<std::string_view, mineralCount()> mineralNames{
    "Structural",
    "Propulsion",
    "Electronics",
    "Fuel",
    "Ordnance"
};

// Converts a mineral enum to its backing array index. Callers must not pass Count
// as a gameplay mineral; validation paths should reject it before mutation.
[[nodiscard]] inline constexpr std::size_t mineralIndex(const Mineral mineral) noexcept {
    return static_cast<std::size_t>(mineral);
}

// Returns a human-readable mineral name for diagnostics and CLI output.
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
    // The epsilon prevents tiny floating-point residue from blocking completion.
    [[nodiscard]] bool canPay(const MineralSet& cost) const noexcept {
        constexpr double kMineralComparisonEpsilon = 1.0e-9;
        for (std::size_t i = 0; i < amount.size(); ++i) {
            if (amount[i] + kMineralComparisonEpsilon < cost.amount[i]) {
                return false;
            }
        }
        return true;
    }

    // Subtracts a full mineral cost. Callers get an exception instead of a silent
    // negative stockpile if validation was missed or state was corrupted.
    void subtract(const MineralSet& cost) {
        if (!canPay(cost)) {
            throw std::runtime_error{"Insufficient minerals"};
        }

        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] -= cost.amount[i];
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
