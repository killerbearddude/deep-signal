#pragma once

// Explicit inventory identities for P4B freight and site stores. A raw mineral
// and a processed material never alias even when their enum ordinals match.
#include "sim/IdTypes.h"
#include "sim/Minerals.h"
#include <stdexcept>
#include <variant>

namespace deep {
using Commodity = std::variant<ProcessedMaterial, Mineral>;
using StockLocation = std::variant<ColonyId, SiteId>;

// Stable schema tags. These are not indexes into either material's inventory.
enum class CommodityKind { Processed, Raw };
enum class StockLocationKind { Colony, Site };

// Stable explicit tags for persistence and owned DTOs. Unknown tags are rejected
// by the decoding helpers; neither enum's ordinal is reinterpreted as the other.
[[nodiscard]] inline CommodityKind commodityKind(const Commodity& value) {
    return std::holds_alternative<ProcessedMaterial>(value) ? CommodityKind::Processed : CommodityKind::Raw;
}
[[nodiscard]] inline std::int64_t commodityOrdinal(const Commodity& value) {
    return std::visit([](auto resource) { return static_cast<std::int64_t>(resource); }, value);
}
[[nodiscard]] inline StockLocationKind stockLocationKind(const StockLocation& value) {
    return std::holds_alternative<ColonyId>(value) ? StockLocationKind::Colony : StockLocationKind::Site;
}
[[nodiscard]] inline std::int64_t stockLocationId(const StockLocation& value) {
    return std::visit([](auto id) { return id.value; }, value);
}
// Decodes only the two supported resource alternatives after checking ranges.
[[nodiscard]] inline Commodity commodityFromStored(std::int64_t kind, std::int64_t value) {
    if (kind == static_cast<std::int64_t>(CommodityKind::Processed) && value >= 0 &&
        value < static_cast<std::int64_t>(processedMaterialCount()))
        return static_cast<ProcessedMaterial>(value);
    if (kind == static_cast<std::int64_t>(CommodityKind::Raw) && value >= 0 &&
        value < static_cast<std::int64_t>(mineralCount()))
        return static_cast<Mineral>(value);
    throw std::runtime_error("Invalid freight commodity kind/value pair");
}
// Checks namespace and positive identity; existence is checked against GameState.
[[nodiscard]] inline StockLocation stockLocationFromStored(std::int64_t kind, std::int64_t value) {
    if (value > 0) {
        if (kind == static_cast<std::int64_t>(StockLocationKind::Colony))
            return ColonyId{value};
        if (kind == static_cast<std::int64_t>(StockLocationKind::Site))
            return SiteId{value};
    }
    throw std::runtime_error("Invalid stock location kind/value pair");
}

// Rejects malformed alternatives/ordinals without interpreting one kind as another.
[[nodiscard]] inline bool validCommodity(const Commodity& commodity) noexcept {
    if (const auto* processed = std::get_if<ProcessedMaterial>(&commodity))
        return processedMaterialIndex(*processed) < processedMaterialCount();
    if (const auto* raw = std::get_if<Mineral>(&commodity))
        return mineralIndex(*raw) < mineralCount();
    return false;
}
// Checks identity shape only; actual colony/site existence is a world lookup.
[[nodiscard]] inline bool validStockLocation(const StockLocation& location) noexcept {
    if (const auto* colony = std::get_if<ColonyId>(&location))
        return bool(*colony);
    if (const auto* site = std::get_if<SiteId>(&location))
        return bool(*site);
    return false;
}
// Display labels are resolved from the selected resource enum, never a cast.
[[nodiscard]] inline std::string_view commodityName(const Commodity& commodity) {
    return std::visit([](auto resource) { return toString(resource); }, commodity);
}
} // namespace deep
