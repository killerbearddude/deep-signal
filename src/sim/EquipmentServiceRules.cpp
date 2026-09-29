// Shared instance-condition and conservative repair calculations. No inventory,
// program lease or hidden deposit knowledge is created by these pure previews.
#include "sim/EquipmentServiceRules.h"
#include "sim/ShipDesignRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace deep {
namespace {
template <class T, class Id> const T* find(const std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}
bool nonnegative(double x) { return std::isfinite(x) && x >= 0.0; }
// Execution rounding uses relative scale only. The absolute history tolerance
// must never turn a genuinely tiny positive repair into free material use.
bool relativeRepairEqual(double a, double b) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <= kEquipmentRelativeTolerance * std::max(std::abs(a), std::abs(b));
}
void check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
const EquipmentCondition* condition(const Ship& ship, ShipComponentId id) {
    const auto it = std::find_if(ship.equipmentCondition.begin(), ship.equipmentCondition.end(),
                                 [=](const auto& r) { return r.componentId == id; });
    return it == ship.equipmentCondition.end() ? nullptr : &*it;
}
} // namespace

bool equipmentNearlyEqual(double a, double b) noexcept {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <=
               kEquipmentAbsoluteTolerance + kEquipmentRelativeTolerance * std::max(std::abs(a), std::abs(b));
}
bool validServiceProfile(const EquipmentServiceProfile& p) noexcept {
    if (!p.familyId || !std::isfinite(p.dutyCapacity) || p.dutyCapacity <= 0.0 ||
        !std::isfinite(p.teamWorkdaysPerDuty) || p.teamWorkdaysPerDuty <= 0.0)
        return false;
    return std::all_of(p.materialsPerDuty.amount.begin(), p.materialsPerDuty.amount.end(), nonnegative);
}

void initializeShipEquipmentCondition(const GameState& state, Ship& ship) {
    check(ship.equipmentCondition.empty(), "Cannot initialize existing physical equipment condition");
    const auto* cls = find(state.shipClasses, ship.shipClassId);
    check(cls != nullptr, "Condition initializer requires an existing exact class revision");
    std::vector<EquipmentCondition> prepared;
    for (const auto& install : cls->components) {
        const auto* component = find(state.shipComponents, install.componentId);
        check(component != nullptr, "Condition initializer requires valid component references");
        if (component->serviceProfile)
            prepared.push_back({component->id, 0.0});
    }
    ship.equipmentCondition = std::move(prepared);
}

SurveyDutyEvaluation prepareSurveyDuty(const GameState& state, const Fleet& fleet, double requiredDuty) {
    SurveyDutyEvaluation result;
    result.requiredDuty = requiredDuty;
    if (!std::isfinite(requiredDuty) || requiredDuty <= 0.0) {
        result.condition = "Survey duty demand must be finite and positive";
        return result;
    }
    for (ShipId id : fleet.shipIds) {
        const Ship* ship = find(state.ships, id);
        const ShipClass* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
        if (!cls)
            continue;
        const auto design = evaluateShipDesign(state.shipComponents, cls->components);
        for (const auto& install : cls->components) {
            const auto* component = find(state.shipComponents, install.componentId);
            if (!component || component->surveyCapability <= 0.0)
                continue;
            const double capability = component->surveyCapability * install.quantity;
            result.nominalCapability += capability;
            if (design.powerMargin < 0.0)
                continue;
            result.poweredCapability += capability;
            if (component->serviceProfile) {
                const auto* row = condition(*ship, component->id);
                if (!row)
                    continue;
                const double capacity = component->serviceProfile->dutyCapacity;
                const double remaining = capacity - row->usedDuty;
                if (remaining < requiredDuty && !equipmentNearlyEqual(remaining, requiredDuty))
                    continue;
                const double after = std::min(capacity, row->usedDuty + requiredDuty);
                if (after <= row->usedDuty || !equipmentNearlyEqual(after - row->usedDuty, requiredDuty))
                    continue;
                result.changes.push_back({ship->id, component->id, row->usedDuty, after});
            }
            result.usableCapability += capability;
            if (component->measurementProfileId)
                result.contributors.push_back({ship->id,cls->id,component->id,
                    *component->measurementProfileId,0,{}});
        }
    }
    if (!std::isfinite(result.nominalCapability) || !std::isfinite(result.poweredCapability) ||
        !std::isfinite(result.usableCapability)) {
        result.usableCapability = 0.0;
        result.changes.clear();
        result.contributors.clear();
        result.condition = "Survey capability totals exceed numeric limits";
    } else if (result.nominalCapability <= 0.0)
        result.condition = "Fleet has no installed survey capability";
    else if (result.poweredCapability <= 0.0)
        result.condition = "Survey equipment is unavailable due to power deficit";
    else if (result.usableCapability <= 0.0)
        result.condition = "Insufficient usable survey duty for this action";
    else
        result.condition = "Survey equipment has usable duty for this action";
    return result;
}

void applySurveyDuty(GameState& state, const SurveyDutyEvaluation& plan) {
    check(plan.usableCapability > 0.0, "Cannot apply an unavailable survey action");
    // Validate the complete prepared set before debiting its first physical row.
    std::vector<EquipmentCondition*> rows;
    rows.reserve(plan.changes.size());
    for (const auto& change : plan.changes) {
        auto ship = std::find_if(state.ships.begin(), state.ships.end(),
                                 [&](const auto& s) { return s.id == change.shipId; });
        check(ship != state.ships.end(), "Prepared survey ship disappeared");
        auto row = std::find_if(ship->equipmentCondition.begin(), ship->equipmentCondition.end(),
                                [&](const auto& c) { return c.componentId == change.componentId; });
        check(row != ship->equipmentCondition.end() && row->usedDuty == change.beforeUsedDuty,
              "Prepared survey duty is stale");
        rows.push_back(&*row);
    }
    for (std::size_t i = 0; i < rows.size(); ++i)
        rows[i]->usedDuty = plan.changes[i].afterUsedDuty;
}

double operationalWorkshopRate(const GameState& state, const Ship& ship, EquipmentFamilyId family) {
    const auto* cls = find(state.shipClasses, ship.shipClassId);
    if (!cls || evaluateShipDesign(state.shipComponents, cls->components).powerMargin < 0.0)
        return 0.0;
    double total = 0.0;
    for (const auto& install : cls->components) {
        const auto* component = find(state.shipComponents, install.componentId);
        if (!component)
            continue;
        for (const auto& rate : component->workshopRates)
            if (rate.familyId == family) {
                total += rate.teamWorkdaysPerDay * install.quantity;
            }
    }
    return std::isfinite(total) ? total : 0.0;
}
bool maintenanceTeamQualified(const MaintenanceTeam& team, EquipmentFamilyId family) {
    return std::find(team.qualifiedFamilies.begin(), team.qualifiedFamilies.end(), family) !=
           team.qualifiedFamilies.end();
}

EquipmentServicePlan planEquipmentService(const GameState& state, ShipId shipId, ShipComponentId componentId,
                                          double workshopRate, double teamRate,
                                          const ProcessedMaterialSet& available) {
    EquipmentServicePlan result;
    result.shipId = shipId;
    result.componentId = componentId;
    const auto* ship = find(state.ships, shipId);
    const auto* cls = ship ? find(state.shipClasses, ship->shipClassId) : nullptr;
    const auto* component = find(state.shipComponents, componentId);
    const auto* row = ship ? condition(*ship, componentId) : nullptr;
    if (!cls || !component || !component->serviceProfile || !row) {
        result.condition = "Service target is not a managed installation";
        return result;
    }
    const auto install = std::find_if(cls->components.begin(), cls->components.end(),
                                      [=](const auto& r) { return r.componentId == componentId; });
    if (install == cls->components.end()) {
        result.condition = "Service installation is missing";
        return result;
    }
    const auto& profile = *component->serviceProfile;
    result.familyId = profile.familyId;
    result.quantity = install->quantity;
    result.beforeUsedDuty = row->usedDuty;
    if (row->usedDuty <= 0.0) {
        result.condition = "Installation already fully serviced";
        return result;
    }
    if (!nonnegative(workshopRate) || workshopRate <= 0.0) {
        result.condition = "No compatible powered workshop throughput";
        return result;
    }
    if (!nonnegative(teamRate) || teamRate <= 0.0) {
        result.condition = "No qualified engineering team throughput";
        return result;
    }
    const double laborPerDuty = install->quantity * profile.teamWorkdaysPerDuty;
    double restored = std::min(row->usedDuty, std::min(workshopRate, teamRate) / laborPerDuty);
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        const double coefficient = install->quantity * profile.materialsPerDuty.amount[j];
        if (!std::isfinite(coefficient) || !nonnegative(available.amount[j])) {
            result.condition = "Service material arithmetic exceeds numeric limits";
            return result;
        }
        if (coefficient > 0.0)
            restored = std::min(restored, available.amount[j] / coefficient);
    }
    if (!std::isfinite(laborPerDuty) || !std::isfinite(restored) || restored <= 0.0) {
        result.condition = "Waiting for service supplies above floors and within allowance";
        return result;
    }
    // Resolve accumulated floating-point residue on the final step. Actual
    // debits below are capped at available stock; only relatively equivalent
    // recipe/throughput values may normalize, never a meaningful shortage.
    if (relativeRepairEqual(restored, row->usedDuty)) {
        bool fullMaterialsFit = true;
        for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
            const double needed = install->quantity * row->usedDuty * profile.materialsPerDuty.amount[j];
            fullMaterialsFit = fullMaterialsFit && (needed <= available.amount[j] ||
                                                    relativeRepairEqual(needed, available.amount[j]));
        }
        if (fullMaterialsFit)
            restored = row->usedDuty;
    }
    result.afterUsedDuty = row->usedDuty - restored;
    if (result.afterUsedDuty == row->usedDuty ||
        !equipmentNearlyEqual(row->usedDuty - result.afterUsedDuty, restored)) {
        result.condition = "Service restoration cannot be represented conservatively";
        return result;
    }
    result.restoredDuty = restored;
    result.teamWorkdays = restored * laborPerDuty;
    const double dailyWork = std::min(workshopRate, teamRate);
    if (result.teamWorkdays > dailyWork && relativeRepairEqual(result.teamWorkdays, dailyWork))
        result.teamWorkdays = dailyWork;
    if (!std::isfinite(result.teamWorkdays) || result.teamWorkdays <= 0.0) {
        result.condition = "Service labor cannot be represented";
        return result;
    }
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        result.consumed.amount[j] = install->quantity * restored * profile.materialsPerDuty.amount[j];
        if (result.consumed.amount[j] > available.amount[j] &&
            relativeRepairEqual(result.consumed.amount[j], available.amount[j]))
            result.consumed.amount[j] = available.amount[j];
        if (!nonnegative(result.consumed.amount[j]) || result.consumed.amount[j] > available.amount[j] ||
            (profile.materialsPerDuty.amount[j] > 0.0 && result.consumed.amount[j] == 0.0)) {
            result.condition = "Service material debit cannot be represented conservatively";
            return result;
        }
    }
    result.ready = true;
    result.condition = "Ready for one installation group's service work";
    return result;
}

void validateEquipmentState(const GameState& state) {
    std::unordered_set<std::int64_t> ids;
    for (const auto& family : state.equipmentFamilies) {
        check(family.id && family.id.value < state.ids.nextEquipmentFamilyId &&
                  ids.insert(family.id.value).second && !family.name.empty(),
              "Equipment family IDs/names/counter must be valid");
    }
    check(state.ids.nextEquipmentFamilyId > 0, "Equipment family counter must be positive");
    for (const auto& component : state.shipComponents) {
        if (component.serviceProfile) {
            check(component.kind == ShipComponentKind::SurveySensor && component.surveyCapability > 0.0 &&
                      validServiceProfile(*component.serviceProfile) &&
                      find(state.equipmentFamilies, component.serviceProfile->familyId),
                  "Invalid survey service profile/family");
        }
        std::unordered_set<std::int64_t> families;
        for (const auto& rate : component.workshopRates) {
            check(component.kind == ShipComponentKind::Workshop &&
                      find(state.equipmentFamilies, rate.familyId) && nonnegative(rate.teamWorkdaysPerDay) &&
                      families.insert(rate.familyId.value).second,
                  "Workshop family references/rates must be valid and unique");
        }
    }
    for (const auto& ship : state.ships) {
        const auto* cls = find(state.shipClasses, ship.shipClassId);
        check(cls != nullptr, "Equipment condition requires a valid ship class");
        std::size_t index = 0;
        for (const auto& install : cls->components) {
            const auto* component = find(state.shipComponents, install.componentId);
            check(component != nullptr, "Equipment component reference is missing");
            if (!component->serviceProfile)
                continue;
            check(index < ship.equipmentCondition.size(), "Managed installation condition is missing");
            const auto& row = ship.equipmentCondition[index++];
            const auto& profile = *component->serviceProfile;
            check(row.componentId == component->id && nonnegative(row.usedDuty) &&
                      row.usedDuty <= profile.dutyCapacity,
                  "Equipment condition key/order/range is invalid");
            check(std::isfinite(install.quantity * profile.teamWorkdaysPerDuty * profile.dutyCapacity),
                  "Installed service labor exceeds finite limits");
            for (double amount : profile.materialsPerDuty.amount) {
                check(std::isfinite(install.quantity * amount * profile.dutyCapacity),
                      "Installed service materials exceed finite limits");
            }
        }
        check(index == ship.equipmentCondition.size(), "Extra or duplicate managed equipment condition row");
    }
}
} // namespace deep
