#include "sim/ProcessingAllocationRules.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace deep {
namespace {

constexpr double kShareTolerance = 1.0e-12;

[[nodiscard]] ProcessingAllocationResult failure(const ProcessingAllocationError error) noexcept {
    return {.error = error, .weights = {}, .totalWeight = 0.0, .shares = {}};
}

[[nodiscard]] ProcessingAllocationResult normalizeChecked(
    const std::array<double, processedMaterialCount()>& weights,
    const bool requireActiveManual) noexcept {
    double total = 0.0;
    for (const double weight : weights) {
        if (!std::isfinite(weight)) return failure(ProcessingAllocationError::NonFiniteWeight);
        if (weight < 0.0) return failure(ProcessingAllocationError::NegativeWeight);
        if (weight > std::numeric_limits<double>::max() - total) {
            return failure(ProcessingAllocationError::CombinedTotalOverflow);
        }
        total += weight;
        if (!std::isfinite(total)) return failure(ProcessingAllocationError::CombinedTotalOverflow);
    }

    if (requireActiveManual && total <= kProcessedMaterialComparisonEpsilon) {
        return failure(ProcessingAllocationError::InsufficientManualTotal);
    }

    ProcessingAllocationResult result{.error = ProcessingAllocationError::None,
                                      .weights = weights, .totalWeight = total, .shares = {}};
    if (total == 0.0) return result;

    double shareTotal = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const double share = weights[i] / total;
        if (!std::isfinite(share) || share < 0.0 || share > 1.0 + kShareTolerance) {
            return failure(ProcessingAllocationError::InvalidNormalizedShare);
        }
        result.shares[i] = share;
        shareTotal += share;
    }
    // Six non-negative double shares incur only ordinary rounding/underflow;
    // this tolerance is not a license to accept an unrepresentable weight sum.
    if (!std::isfinite(shareTotal) || std::abs(shareTotal - 1.0) > kShareTolerance) {
        return failure(ProcessingAllocationError::InvalidNormalizedShare);
    }
    return result;
}

} // namespace

ProcessingAllocationResult evaluateProcessingAllocations(
    const std::span<const ProcessingAllocation> rows, const bool requireActiveManual) noexcept {
    std::array<double, processedMaterialCount()> weights{};
    for (const ProcessingAllocation& row : rows) {
        const std::size_t index = processedMaterialIndex(row.material);
        if (index >= weights.size()) return failure(ProcessingAllocationError::InvalidMaterial);
        if (!std::isfinite(row.weight)) return failure(ProcessingAllocationError::NonFiniteWeight);
        if (row.weight < 0.0) return failure(ProcessingAllocationError::NegativeWeight);
        if (row.weight > std::numeric_limits<double>::max() - weights[index]) {
            return failure(ProcessingAllocationError::MaterialSubtotalOverflow);
        }
        weights[index] += row.weight;
        if (!std::isfinite(weights[index])) return failure(ProcessingAllocationError::MaterialSubtotalOverflow);
    }
    return normalizeChecked(weights, requireActiveManual);
}

ProcessingAllocationResult normalizeProcessingWeights(
    const std::array<double, processedMaterialCount()>& weights,
    const bool requireActiveManual) noexcept {
    return normalizeChecked(weights, requireActiveManual);
}

std::array<double, processedMaterialCount()> processingSharesForActivePolicy(
    const ProcessingAllocationResult& allocation, const ProcessingPolicy policy) noexcept {
    if (policy != ProcessingPolicy::Manual &&
        allocation.totalWeight <= kProcessedMaterialComparisonEpsilon) {
        return {};
    }
    return allocation.shares;
}

} // namespace deep
