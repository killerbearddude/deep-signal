// Inventory selection and public endpoint metadata only. Physical geology and
// transfer eligibility are deliberately outside this narrow location boundary.
#include "sim/StockAccess.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace deep {
namespace {
template <class Rows, class Id> auto* find(Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
template <class State, class Id> auto& location(State& state, Id id) {
    if constexpr (std::is_same_v<Id, ColonyId>) {
        const auto it = std::find_if(state.colonies.begin(), state.colonies.end(),
                                     [&](const auto& c) { return c.id == id; });
        if (it == state.colonies.end())
            throw std::runtime_error("Unknown colony stock location");
        return *it;
    } else {
        const auto it = std::find_if(state.resourceSites.begin(), state.resourceSites.end(),
                                     [&](const auto& s) { return s.id == id; });
        if (it == state.resourceSites.end())
            throw std::runtime_error("Unknown site stock location");
        return *it;
    }
}
template <class State>
decltype(auto) quantity(State& state, const StockLocation& loc, const Commodity& commodity) {
    if (!validStockLocation(loc) || !validCommodity(commodity))
        throw std::runtime_error("Invalid typed stock identity");
    using Balance = std::conditional_t<std::is_const_v<State>, const double&, double&>;
    return std::visit(
        [&](auto id) -> Balance {
            auto& at = location(state, id);
            if (const auto* processed = std::get_if<ProcessedMaterial>(&commodity)) {
                if constexpr (std::is_same_v<decltype(id), ColonyId>)
                    return at.processedStockpile.amount.at(processedMaterialIndex(*processed));
                else
                    return at.processedStock.amount.at(processedMaterialIndex(*processed));
            }
            const auto mineral = std::get<Mineral>(commodity);
            if constexpr (std::is_same_v<decltype(id), ColonyId>)
                return at.stockpile.amount.at(mineralIndex(mineral));
            else
                return at.rawStock.amount.at(mineralIndex(mineral));
        },
        loc);
}
} // namespace
bool stockLocationExists(const GameState& state, const StockLocation& loc) {
    if (!validStockLocation(loc))
        return false;
    return std::visit(
        [&](auto id) {
            if constexpr (std::is_same_v<decltype(id), ColonyId>)
                return find(state.colonies, id) != nullptr;
            else
                return find(state.resourceSites, id) != nullptr;
        },
        loc);
}
BodyId stockLocationBody(const GameState& state, const StockLocation& loc) {
    return std::visit([&](auto id) { return location(state, id).bodyId; }, loc);
}
std::string stockLocationName(const GameState& state, const StockLocation& loc) {
    return std::visit([&](auto id) { return location(state, id).name; }, loc);
}
double stockQuantity(const GameState& state, const StockLocation& loc, const Commodity& commodity) {
    return quantity(state, loc, commodity);
}
double& stockQuantity(GameState& state, const StockLocation& loc, const Commodity& commodity) {
    return quantity(state, loc, commodity);
}
} // namespace deep
