#pragma once

// Validates and normalizes processing weights without owning GameState or
// changing stored player intent. Every caller uses the same checked aggregate.

#include "sim/Domain.h"

#include <array>
#include <span>

namespace deep {

enum class ProcessingAllocationError {
    None,
    InvalidMaterial,
    NonFiniteWeight,
    NegativeWeight,
    MaterialSubtotalOverflow,
    CombinedTotalOverflow,
    InsufficientManualTotal,
    InvalidNormalizedShare
};

// Failed results contain no partial aggregate. Zero stored weight is valid for
// a preset but provides no active Manual distribution.
struct ProcessingAllocationResult {
    ProcessingAllocationError error = ProcessingAllocationError::None;
    std::array<double, processedMaterialCount()> weights{};
    double totalWeight = 0.0;
    std::array<double, processedMaterialCount()> shares{};

    [[nodiscard]] bool valid() const noexcept { return error == ProcessingAllocationError::None; }
};

// Rows are aggregated in supplied order within each material, then subtotals
// are summed in ProcessedMaterial order. The returned shares are calculated as
// weight / total; neither the input span nor its storage is retained.
[[nodiscard]] ProcessingAllocationResult evaluateProcessingAllocations(
    std::span<const ProcessingAllocation> rows, bool requireActiveManual) noexcept;

// Applies the same checked total/share calculation to fixed preset weights or
// already-aggregated weights. This does not replace the row-level validation
// required at command and imported-state boundaries.
[[nodiscard]] ProcessingAllocationResult normalizeProcessingWeights(
    const std::array<double, processedMaterialCount()>& weights,
    bool requireActiveManual) noexcept;

} // namespace deep
