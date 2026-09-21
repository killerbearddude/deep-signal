#pragma once

// Defines raw mineral/resource categories and fixed-size mineral accounting.
// The fixed array representation is deliberately simple and persistence-friendly;
// enum order is schema-visible because save files store mineral ordinals.

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace deep {

// Raw extractable resources used by the economy and processing recipes. Saves
// persist these ordinals directly; reordering requires a schema/data migration.
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

// Holds abstract resource units for every Mineral enum value. Amounts and costs
// must be finite and non-negative. These arithmetic helpers reject negatives in
// set/add but do not check finiteness or overflow; validateGameState checks those
// invariants on imported snapshots, and runtime callers must preserve them.
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

    // Rejects an unaffordable cost before payment and clamps negative residue
    // strictly smaller than epsilon. This is not a rollback transaction if the
    // post-subtraction check throws after a slot has already been changed.
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

    // Adds a validated set without per-slot checks; callers must avoid overflow.
    void addSet(const MineralSet& other) noexcept {
        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] += other.amount[i];
        }
    }
};


// Processed industrial materials produced from raw minerals by colony processors.
// These are the first shipyard-facing resources; save files persist their stable
// ordinals separately from raw Mineral ordinals.
enum class ProcessedMaterial : std::size_t {
    StructuralAlloys = 0,
    Electronics = 1,
    Propellant = 2,
    ReactorFuel = 3,
    IndustrialComposites = 4,
    OrdnanceMaterials = 5,
    Count = 6
};

[[nodiscard]] inline constexpr std::size_t processedMaterialCount() noexcept {
    return static_cast<std::size_t>(ProcessedMaterial::Count);
}

inline constexpr double kProcessedMaterialComparisonEpsilon = 1.0e-9;

inline constexpr std::array<std::string_view, processedMaterialCount()> processedMaterialNames{
    "Structural Alloys",
    "Electronics",
    "Propellant",
    "Reactor Fuel",
    "Industrial Composites",
    "Ordnance Materials"
};

[[nodiscard]] inline constexpr std::size_t processedMaterialIndex(const ProcessedMaterial material) noexcept {
    return static_cast<std::size_t>(material);
}

[[nodiscard]] inline constexpr std::string_view toString(const ProcessedMaterial material) noexcept {
    const std::size_t index = processedMaterialIndex(material);
    return index < processedMaterialNames.size() ? processedMaterialNames[index] : std::string_view{"Unknown"};
}

// Holds abstract output units for every processed material. The finite,
// non-negative preconditions and checked-index behavior mirror MineralSet.
// subtract may throw after mutation on a residue check; it is not transactional.
struct ProcessedMaterialSet {
    std::array<double, processedMaterialCount()> amount{};

    [[nodiscard]] double get(ProcessedMaterial material) const {
        return amount.at(processedMaterialIndex(material));
    }

    void set(ProcessedMaterial material, double value) {
        if (value < 0.0) {
            throw std::invalid_argument{"Processed material values cannot be negative"};
        }
        amount.at(processedMaterialIndex(material)) = value;
    }

    void add(ProcessedMaterial material, double value) {
        if (value < 0.0) {
            throw std::invalid_argument{"Processed material addition cannot be negative"};
        }
        amount.at(processedMaterialIndex(material)) += value;
    }

    [[nodiscard]] bool canPay(const ProcessedMaterialSet& cost) const noexcept {
        for (std::size_t i = 0; i < amount.size(); ++i) {
            if (amount[i] + kProcessedMaterialComparisonEpsilon < cost.amount[i]) {
                return false;
            }
        }
        return true;
    }

    void subtract(const ProcessedMaterialSet& cost) {
        if (!canPay(cost)) {
            throw std::runtime_error{"Insufficient processed materials"};
        }

        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] -= cost.amount[i];

            if (amount[i] < 0.0 && amount[i] > -kProcessedMaterialComparisonEpsilon) {
                amount[i] = 0.0;
            }

            if (amount[i] < 0.0) {
                throw std::runtime_error{"Processed material subtraction produced negative stockpile."};
            }
        }
    }

    void addSet(const ProcessedMaterialSet& other) noexcept {
        for (std::size_t i = 0; i < amount.size(); ++i) {
            amount[i] += other.amount[i];
        }
    }
};

} // namespace deep
