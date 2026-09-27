#include "sim/ProcessingAllocationRules.h"

// Direct tests for the allocation contract shared by commands, imported-state
// validation, processing, and read projections. Expected ratios are calculated
// from the submitted weights rather than from another consumer of this helper.

#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

void require(const bool condition, const std::string_view message) {
    if (!condition) throw TestFailure{message};
}

void requireNear(const double actual, const double expected, const std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-12) {
        throw TestFailure{message};
    }
}

deep::ProcessingAllocationResult evaluate(
    const std::initializer_list<deep::ProcessingAllocation> rows,
    const bool requireActiveManual) {
    return deep::evaluateProcessingAllocations(
        std::span<const deep::ProcessingAllocation>{rows.begin(), rows.size()},
        requireActiveManual);
}

void requireError(const deep::ProcessingAllocationResult& result,
                  const deep::ProcessingAllocationError expected,
                  const std::string_view message) {
    require(!result.valid() && result.error == expected, message);
}

void test_known_manual_ratios_and_duplicate_rows() {
    const auto electronic = deep::processedMaterialIndex(deep::ProcessedMaterial::Electronics);
    const auto alloys = deep::processedMaterialIndex(deep::ProcessedMaterial::StructuralAlloys);

    const auto direct = evaluate({
        {deep::ProcessedMaterial::Electronics, 75.0},
        {deep::ProcessedMaterial::StructuralAlloys, 25.0}
    }, true);
    require(direct.valid(), "75/25 manual allocation is valid");
    requireNear(direct.totalWeight, 100.0, "75/25 total is 100");
    requireNear(direct.shares[electronic], 0.75, "75/25 electronics share is 0.75");
    requireNear(direct.shares[alloys], 0.25, "75/25 alloys share is 0.25");

    const auto duplicate = evaluate({
        {deep::ProcessedMaterial::Electronics, 50.0},
        {deep::ProcessedMaterial::StructuralAlloys, 25.0},
        {deep::ProcessedMaterial::Electronics, 25.0},
        {deep::ProcessedMaterial::Electronics, 0.0}
    }, true);
    require(duplicate.valid(), "duplicate and zero rows are valid");
    requireNear(duplicate.weights[electronic], 75.0, "duplicates aggregate to 75");
    requireNear(duplicate.shares[electronic], 0.75, "duplicate electronics share is 0.75");
    requireNear(duplicate.shares[alloys], 0.25, "duplicate alloys share is 0.25");
}

void test_empty_and_epsilon_boundary() {
    requireError(evaluate({}, true), deep::ProcessingAllocationError::InsufficientManualTotal,
                 "empty Manual allocation is invalid");
    const auto dormant = evaluate({}, false);
    require(dormant.valid() && dormant.totalWeight == 0.0,
            "empty dormant manual storage is valid and has zero total");
    for (const double share : dormant.shares) {
        require(share == 0.0, "empty dormant storage has no active shares");
    }

    const double epsilon = deep::kProcessedMaterialComparisonEpsilon;
    const double below = std::nextafter(epsilon, 0.0);
    const double above = std::nextafter(epsilon, std::numeric_limits<double>::infinity());
    requireError(evaluate({{deep::ProcessedMaterial::Electronics, below}}, true),
                 deep::ProcessingAllocationError::InsufficientManualTotal,
                 "Manual total just below epsilon is invalid");
    requireError(evaluate({{deep::ProcessedMaterial::Electronics, epsilon}}, true),
                 deep::ProcessingAllocationError::InsufficientManualTotal,
                 "Manual total at epsilon is invalid");
    require(evaluate({{deep::ProcessedMaterial::Electronics, above}}, true).valid(),
            "Manual total just above epsilon is valid");

    const auto repeatedTiny = evaluate({
        {deep::ProcessedMaterial::Electronics, epsilon * 0.6},
        {deep::ProcessedMaterial::Electronics, epsilon * 0.6}
    }, true);
    require(repeatedTiny.valid(), "sub-epsilon positive rows can form a valid total");
    requireNear(repeatedTiny.shares[deep::processedMaterialIndex(deep::ProcessedMaterial::Electronics)],
                1.0, "repeated tiny rows retain a full electronics share");
}

void test_bad_rows_are_rejected_even_for_dormant_storage() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const bool active : {false, true}) {
        requireError(evaluate({{static_cast<deep::ProcessedMaterial>(999), 1.0}}, active),
                     deep::ProcessingAllocationError::InvalidMaterial,
                     "unknown material is rejected under every policy");
        requireError(evaluate({{deep::ProcessedMaterial::Electronics, -1.0}}, active),
                     deep::ProcessingAllocationError::NegativeWeight,
                     "negative weight is rejected under every policy");
        for (const double weight : {nan, infinity, -infinity}) {
            requireError(evaluate({{deep::ProcessedMaterial::Electronics, weight}}, active),
                         deep::ProcessingAllocationError::NonFiniteWeight,
                         "non-finite weight is rejected under every policy");
        }
    }
}

void test_overflow_categories_and_large_valid_weight() {
    const double max = std::numeric_limits<double>::max();
    for (const bool active : {false, true}) {
        requireError(evaluate({
            {deep::ProcessedMaterial::Electronics, max},
            {deep::ProcessedMaterial::Electronics, max}
        }, active), deep::ProcessingAllocationError::MaterialSubtotalOverflow,
        "duplicate material subtotal overflow is rejected");

        requireError(evaluate({
            {deep::ProcessedMaterial::StructuralAlloys, max},
            {deep::ProcessedMaterial::Electronics, max}
        }, active), deep::ProcessingAllocationError::CombinedTotalOverflow,
        "different material combined total overflow is rejected");
    }

    const auto large = evaluate({{deep::ProcessedMaterial::Electronics, max}}, true);
    require(large.valid(), "one maximum finite weight is valid");
    const double share = large.shares[deep::processedMaterialIndex(deep::ProcessedMaterial::Electronics)];
    requireNear(share, 1.0, "one maximum weight has a full share");
    requireNear(share * 100.0, 100.0, "percentage formed from the share remains finite");

    const auto representable = evaluate({
        {deep::ProcessedMaterial::StructuralAlloys, max / 4.0},
        {deep::ProcessedMaterial::Electronics, max / 4.0}
    }, true);
    require(representable.valid(), "large representable combined total is valid");
    requireNear(representable.shares[deep::processedMaterialIndex(deep::ProcessedMaterial::StructuralAlloys)],
                0.5, "large representable weights normalize to one half each");
}

void test_normalize_primitive_validates_arrays() {
    std::array<double, deep::processedMaterialCount()> weights{};
    requireError(deep::normalizeProcessingWeights(weights, true),
                 deep::ProcessingAllocationError::InsufficientManualTotal,
                 "zero array cannot be active Manual distribution");
    require(deep::normalizeProcessingWeights(weights, false).valid(),
            "zero array is valid dormant storage");

    const auto index = deep::processedMaterialIndex(deep::ProcessedMaterial::Electronics);
    weights[index] = -1.0;
    requireError(deep::normalizeProcessingWeights(weights, false),
                 deep::ProcessingAllocationError::NegativeWeight,
                 "normalization rejects a negative array element");
    weights[index] = std::numeric_limits<double>::infinity();
    requireError(deep::normalizeProcessingWeights(weights, false),
                 deep::ProcessingAllocationError::NonFiniteWeight,
                 "normalization rejects a non-finite array element");

    weights[index] = std::numeric_limits<double>::max();
    weights[deep::processedMaterialIndex(deep::ProcessedMaterial::StructuralAlloys)] =
        std::numeric_limits<double>::max();
    requireError(deep::normalizeProcessingWeights(weights, true),
                 deep::ProcessingAllocationError::CombinedTotalOverflow,
                 "normalization rejects an overflowing combined total");
}

void test_active_preset_cutoff_does_not_invalidate_dormant_manual_storage() {
    const double epsilon = deep::kProcessedMaterialComparisonEpsilon;
    std::array<double, deep::processedMaterialCount()> weights{};
    const auto index = deep::processedMaterialIndex(deep::ProcessedMaterial::Electronics);
    for (const double total : {std::nextafter(epsilon, 0.0), epsilon}) {
        weights[index] = total;
        const auto allocation = deep::normalizeProcessingWeights(weights, false);
        require(allocation.valid() && allocation.shares[index] == 1.0,
                "small positive dormant Manual storage stays valid and normalized");
        const auto active = deep::processingSharesForActivePolicy(
            allocation, deep::ProcessingPolicy::StockpileRecovery);
        require(active[index] == 0.0,
                "active preset retains the baseline zero-share cutoff through epsilon");
    }
    weights[index] = std::nextafter(epsilon, std::numeric_limits<double>::infinity());
    const auto above = deep::normalizeProcessingWeights(weights, false);
    require(above.valid() && deep::processingSharesForActivePolicy(
        above, deep::ProcessingPolicy::StockpileRecovery)[index] == 1.0,
        "active preset resumes normalized allocation just above epsilon");

    weights.fill(1.0);
    const auto ordinary = deep::normalizeProcessingWeights(weights, false);
    const auto ordinaryShares = deep::processingSharesForActivePolicy(
        ordinary, deep::ProcessingPolicy::StockpileRecovery);
    for (const double share : ordinaryShares) {
        requireNear(share, 1.0 / 6.0,
                    "ordinary equal Recovery weights retain one-sixth shares");
    }
}

} // namespace

int main() {
    try {
        test_known_manual_ratios_and_duplicate_rows();
        test_empty_and_epsilon_boundary();
        test_bad_rows_are_rejected_even_for_dormant_storage();
        test_overflow_categories_and_large_valid_weight();
        test_normalize_primitive_validates_arrays();
        test_active_preset_cutoff_does_not_invalidate_dormant_manual_storage();
    } catch (const std::exception& error) {
        std::cerr << "Processing allocation rule test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal processing allocation rule tests passed.\n";
    return EXIT_SUCCESS;
}
