// Developed-component supply remains local and per hull. Established components
// are unaffected, and a partial set of prototypes cannot create a partial hull.
#include "sim/TechnicalShipyardRules.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace deep {
namespace {
template <class Rows, class Id> auto* find(Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
template <class Rows, class Id> const auto* find(const Rows& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
} // namespace

bool classRequiresDevelopedComponent(const GameState& state, const ShipClass& shipClass) noexcept {
    return std::any_of(shipClass.components.begin(), shipClass.components.end(), [&](const auto& install) {
        return developedRevisionForComponent(state, install.componentId) != nullptr;
    });
}

DevelopedSupplyPlanResult planDevelopedComponentSupply(const GameState& state, const ShipyardOrder& order,
                                                       const ShipClass& shipClass, ColonyId colony,
                                                       std::int64_t day) {
    DevelopedSupplyPlanResult result;
    const auto full = evaluateShipDesign(state.shipComponents, shipClass.components);
    if (!full.constructible) {
        result.explanation = "Waiting for a physically constructible immutable design";
        return result;
    }
    ShipyardCurrentHullSupplyPlan plan;
    plan.shipClassId = shipClass.id;
    plan.hullNumber = order.quantityCompleted + 1;
    plan.effectiveBuildCost = full.buildCost;
    plan.effectiveBuildPoints = full.buildPoints;
    plan.boundDay = day;
    for (const auto& install : shipClass.components) {
        if (!developedRevisionForComponent(state, install.componentId))
            continue;
        DevelopedComponentSupply supply;
        supply.componentId = install.componentId;
        supply.quantity = install.quantity;
        if (serialProductionAvailable(state, install.componentId, colony, day)) {
            supply.kind = DevelopedComponentSupplyKind::SerialProduction;
        } else {
            const auto available = availablePrototypeUnits(state, install.componentId, colony, day);
            if (available.size() < static_cast<std::size_t>(install.quantity)) {
                result.explanation =
                    "Waiting for a local prototype unit or qualified serial production process";
                return result;
            }
            supply.kind = DevelopedComponentSupplyKind::PrototypeUnit;
            supply.prototypeUnits.assign(available.begin(), available.begin() + install.quantity);
            const auto* component = find(state.shipComponents, install.componentId);
            if (!component) {
                result.explanation = "Developed component definition is missing";
                return result;
            }
            for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
                const double credit = component->buildCost.amount[material] * install.quantity;
                if (plan.effectiveBuildCost.amount[material] + 1e-9 < credit) {
                    result.explanation = "Prototype embodied-material credit exceeds the hull requirement";
                    return result;
                }
                plan.effectiveBuildCost.amount[material] =
                    std::max(0.0, plan.effectiveBuildCost.amount[material] - credit);
            }
            plan.effectiveBuildPoints -= component->buildPoints * install.quantity;
        }
        plan.developedComponents.push_back(std::move(supply));
    }
    if (!(plan.effectiveBuildPoints > 0.0) || !std::isfinite(plan.effectiveBuildPoints)) {
        result.explanation = "Prototype-backed hull has no representable integration work";
        return result;
    }
    result.ready = true;
    result.explanation = "Developed-component supply is physically available for the next hull";
    result.plan = std::move(plan);
    return result;
}

void reservePrototypeSupply(GameState& state, const ShipyardOrder& order,
                            const ShipyardCurrentHullSupplyPlan& plan) {
    for (const auto& supply : plan.developedComponents) {
        if (supply.kind != DevelopedComponentSupplyKind::PrototypeUnit)
            continue;
        for (auto id : supply.prototypeUnits) {
            auto* prototype = find(state.prototypeComponentUnits, id);
            if (!prototype || prototype->state != PrototypeComponentState::Available ||
                prototype->colonyId != order.colonyId || prototype->componentId != supply.componentId)
                throw std::logic_error("Prototype supply changed before current-hull plan binding");
            prototype->state = PrototypeComponentState::ReservedForShipyard;
            prototype->reservedOrderId = order.id;
            prototype->reservedHullNumber = plan.hullNumber;
        }
    }
}

void consumePrototypeSupply(GameState& state, const ShipyardOrder& order, ShipId shipId, std::int64_t day) {
    if (!order.currentHullSupplyPlan)
        return;
    for (const auto& supply : order.currentHullSupplyPlan->developedComponents) {
        if (supply.kind != DevelopedComponentSupplyKind::PrototypeUnit)
            continue;
        for (auto id : supply.prototypeUnits) {
            auto* prototype = find(state.prototypeComponentUnits, id);
            if (!prototype || prototype->state != PrototypeComponentState::ReservedForShipyard ||
                prototype->reservedOrderId != order.id ||
                prototype->reservedHullNumber != order.currentHullSupplyPlan->hullNumber)
                throw std::logic_error("Reserved prototype supply is inconsistent at hull completion");
            prototype->state = PrototypeComponentState::Consumed;
            prototype->consumedShipId = shipId;
            prototype->reservedOrderId.reset();
            prototype->reservedHullNumber.reset();
            state.prototypeIntegrationReceipts.push_back(PrototypeIntegrationReceipt{
                id, order.id, order.currentHullSupplyPlan->hullNumber, shipId, day});
        }
    }
}

} // namespace deep
