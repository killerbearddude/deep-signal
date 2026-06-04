#include "app/ForecastService.h"

// Implements lightweight app-layer forecasts over the current simulation state.
// These projections intentionally mirror current Prototype 0.1 formulas without
// moving forecast logic into src/sim or introducing UI dependencies.

#include "sim/GameState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string_view>

namespace deep {
namespace {

template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

[[nodiscard]] std::string bodyName(const GameState& state, const BodyId id) {
    const Body* body = findById(state.bodies, id);
    return body == nullptr ? std::string{"<unknown body>"} : body->name;
}

[[nodiscard]] std::string colonyName(const GameState& state, const ColonyId id) {
    const Colony* colony = findById(state.colonies, id);
    return colony == nullptr ? std::string{"<unknown colony>"} : colony->name;
}

[[nodiscard]] std::string shipClassName(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? std::string{"<unknown ship class>"} : shipClass->name;
}

[[nodiscard]] std::string mineralName(const Mineral mineral) {
    return std::string{toString(mineral)};
}

[[nodiscard]] std::string depositSurveyStateName(const DepositSurveyState state) {
    switch (state) {
    case DepositSurveyState::Unknown:
        return "Unknown";
    case DepositSurveyState::Estimated:
        return "Estimated";
    case DepositSurveyState::Known:
        return "Known";
    }

    return "Unknown";
}

[[nodiscard]] std::string materialName(const ProcessedMaterial material) {
    return std::string{toString(material)};
}

[[nodiscard]] int ceilToNonNegativeDays(const double days) noexcept {
    if (days <= 0.0) {
        return 0;
    }

    return static_cast<int>(std::ceil(days));
}

// Mirrors Simulation::simulateMining for a single colony/deposit pair without
// mutating state. availableRemaining is the shared amount left after earlier
// colonies in deterministic state order have taken their forecast extraction.
[[nodiscard]] double dailyExtraction(const Colony& colony,
                                     const MineralDeposit& deposit,
                                     const double availableRemaining) noexcept {
    if (availableRemaining <= 0.0) {
        return 0.0;
    }

    const double potentialExtraction = colony.mines * deposit.accessibility;
    return std::min(availableRemaining, std::max(0.0, potentialExtraction));
}

[[nodiscard]] std::string mineralIncomeExplanation(const Colony& colony, const MineralDeposit& deposit, const double incomePerDay) {
    std::ostringstream out;
    out << colony.mines << " mines * " << deposit.accessibility
        << " accessibility = " << incomePerDay << " per day";
    return out.str();
}

using MineralAmountTotals = std::array<double, mineralCount()>;
using ProcessedMaterialAmountTotals = std::array<double, processedMaterialCount()>;

struct ProcessingRecipe {
    ProcessedMaterial output;
    MineralSet rawCostPerUnit;
};

struct ProcessingForecastTotals {
    MineralAmountTotals rawDemand{};
    ProcessedMaterialAmountTotals materialIncome{};
};

[[nodiscard]] MineralSet makeRawCost(const std::initializer_list<std::pair<Mineral, double>> inputs) {
    MineralSet cost;
    for (const auto& [mineral, amount] : inputs) {
        cost.set(mineral, amount);
    }
    return cost;
}

[[nodiscard]] std::vector<ProcessingRecipe> processingRecipes() {
    // Keep recipe order synchronized with Simulation::simulateProcessing. This
    // duplication is intentional for now so forecasts can explain the sim rule
    // without moving app-layer DTO logic into src/sim.
    return {
        ProcessingRecipe{ProcessedMaterial::StructuralAlloys,
                         makeRawCost({{Mineral::Iron, 1.0}, {Mineral::Nickel, 0.5}, {Mineral::Titanium, 0.25}})},
        ProcessingRecipe{ProcessedMaterial::Electronics,
                         makeRawCost({{Mineral::Copper, 0.5}, {Mineral::Silicon, 0.5}, {Mineral::RareEarthElements, 0.1}})},
        ProcessingRecipe{ProcessedMaterial::Propellant,
                         makeRawCost({{Mineral::WaterIce, 1.0}, {Mineral::Volatiles, 0.5}})},
        ProcessingRecipe{ProcessedMaterial::ReactorFuel,
                         makeRawCost({{Mineral::Uranium, 0.2}})},
        ProcessingRecipe{ProcessedMaterial::IndustrialComposites,
                         makeRawCost({{Mineral::CarbonCompounds, 0.5}, {Mineral::Aluminum, 0.5}})},
        ProcessingRecipe{ProcessedMaterial::OrdnanceMaterials,
                         makeRawCost({{Mineral::PlatinumGroupMetals, 0.1}, {Mineral::CarbonCompounds, 0.5}})}
    };
}

[[nodiscard]] double maxRecipeOutput(const MineralSet& stockpile, const MineralSet& rawCostPerUnit) noexcept {
    double maxOutput = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < rawCostPerUnit.amount.size(); ++i) {
        const double cost = rawCostPerUnit.amount[i];
        if (cost <= 0.0) {
            continue;
        }
        maxOutput = std::min(maxOutput, stockpile.amount[i] / cost);
    }
    return std::isinf(maxOutput) ? 0.0 : std::max(0.0, maxOutput);
}

[[nodiscard]] MineralSet scaledMineralCost(const MineralSet& cost, const double scale) noexcept {
    MineralSet result;
    if (scale <= 0.0) {
        return result;
    }
    for (std::size_t i = 0; i < result.amount.size(); ++i) {
        result.amount[i] = cost.amount[i] * scale;
    }
    return result;
}

using ProcessingShares = std::array<double, processedMaterialCount()>;

void addProcessingWeight(ProcessingShares& weights, const ProcessedMaterial material, const double weight) noexcept {
    if (weight <= 0.0 || processedMaterialIndex(material) >= processedMaterialCount()) {
        return;
    }

    weights[processedMaterialIndex(material)] += weight;
}

[[nodiscard]] ProcessingShares balancedProcessingWeights() noexcept {
    ProcessingShares weights{};
    for (double& weight : weights) {
        weight = 1.0;
    }
    return weights;
}

[[nodiscard]] ProcessingShares policyProcessingWeights(const Colony& colony) noexcept {
    ProcessingShares weights{};

    switch (colony.processingPolicy) {
    case ProcessingPolicy::Balanced:
        return balancedProcessingWeights();
    case ProcessingPolicy::ShipbuildingFocus:
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 4.0);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::OrdnanceMaterials, 0.5);
        break;
    case ProcessingPolicy::FuelFocus:
        addProcessingWeight(weights, ProcessedMaterial::Propellant, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::ReactorFuel, 2.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 0.5);
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 0.5);
        break;
    case ProcessingPolicy::ElectronicsFocus:
        addProcessingWeight(weights, ProcessedMaterial::Electronics, 5.0);
        addProcessingWeight(weights, ProcessedMaterial::StructuralAlloys, 1.0);
        addProcessingWeight(weights, ProcessedMaterial::IndustrialComposites, 1.0);
        break;
    case ProcessingPolicy::StockpileRecovery:
        for (std::size_t i = 0; i < weights.size(); ++i) {
            weights[i] = 1.0 / (1.0 + std::max(0.0, colony.processedStockpile.amount[i]));
        }
        break;
    case ProcessingPolicy::Manual:
        for (const ProcessingAllocation& allocation : colony.manualProcessingAllocations) {
            addProcessingWeight(weights, allocation.material, allocation.weight);
        }
        break;
    }

    return weights;
}

[[nodiscard]] ProcessingShares normalizedProcessingShares(const Colony& colony) noexcept {
    ProcessingShares weights = policyProcessingWeights(colony);
    double totalWeight = 0.0;
    for (const double weight : weights) {
        totalWeight += weight;
    }

    if (totalWeight <= kProcessedMaterialComparisonEpsilon) {
        return {};
    }

    // Manual allocations and preset policies both use relative weights. Convert
    // them to shares here so forecasts match Simulation::simulateProcessing and
    // never treat UI-entered values as literal percentages.
    for (double& weight : weights) {
        weight /= totalWeight;
    }
    return weights;
}

void addMineralSet(MineralAmountTotals& totals, const MineralSet& minerals, const double scale = 1.0) noexcept {
    for (std::size_t i = 0; i < totals.size(); ++i) {
        totals[i] += minerals.amount[i] * scale;
    }
}

void addProcessedMaterialSet(ProcessedMaterialAmountTotals& totals, const ProcessedMaterialSet& materials, const double scale = 1.0) noexcept {
    for (std::size_t i = 0; i < totals.size(); ++i) {
        totals[i] += materials.amount[i] * scale;
    }
}

[[nodiscard]] Mineral mineralFromIndex(const std::size_t index) noexcept {
    return static_cast<Mineral>(index);
}

[[nodiscard]] ProcessedMaterial materialFromIndex(const std::size_t index) noexcept {
    return static_cast<ProcessedMaterial>(index);
}

[[nodiscard]] std::optional<int> stockpileRunoutDays(const double stockpile, const double netPerDay) {
    if (netPerDay >= -kMineralComparisonEpsilon) {
        return std::nullopt;
    }

    if (stockpile <= 0.0) {
        return 0;
    }

    return ceilToNonNegativeDays(stockpile / -netPerDay);
}

[[nodiscard]] std::string miningCauseExplanation(const double incomePerDay) {
    std::ostringstream out;
    out << incomePerDay << " per day from current mines and accessible deposits";
    return out.str();
}

[[nodiscard]] std::string processingDemandCauseExplanation(const double demandPerDay) {
    std::ostringstream out;
    out << demandPerDay << " raw units per day consumed by fixed processing recipes";
    return out.str();
}

[[nodiscard]] std::string processingIncomeCauseExplanation(const double incomePerDay) {
    std::ostringstream out;
    out << incomePerDay << " per day from colony processor capacity and raw-resource availability";
    return out.str();
}

[[nodiscard]] std::string shipyardDemandCauseExplanation(const double demandPerDay) {
    std::ostringstream out;
    out << demandPerDay << " per day committed to active shipyard orders; "
        << "demand is amortized over capacity-only order ETAs";
    return out.str();
}

[[nodiscard]] MineralAmountTotals totalColonyStockpiles(const GameState& state) noexcept {
    MineralAmountTotals totals{};
    for (const Colony& colony : state.colonies) {
        addMineralSet(totals, colony.stockpile);
    }
    return totals;
}

[[nodiscard]] ProcessedMaterialAmountTotals totalProcessedStockpiles(const GameState& state) noexcept {
    ProcessedMaterialAmountTotals totals{};
    for (const Colony& colony : state.colonies) {
        addProcessedMaterialSet(totals, colony.processedStockpile);
    }
    return totals;
}

[[nodiscard]] MineralAmountTotals miningIncomeByMineral(const std::vector<MineralIncomeForecast>& incomeRows) noexcept {
    MineralAmountTotals totals{};
    for (const MineralIncomeForecast& row : incomeRows) {
        totals[mineralIndex(row.mineral)] += row.incomePerDay;
    }
    return totals;
}

struct DepositQuantityTotals {
    MineralAmountTotals confirmed{};
    MineralAmountTotals estimated{};
    MineralAmountTotals unknownPotential{};
    MineralAmountTotals uncertain{};
};

[[nodiscard]] DepositQuantityTotals depositQuantityTotals(const GameState& state) noexcept {
    DepositQuantityTotals totals{};
    for (const MineralDeposit& deposit : state.mineralDeposits) {
        const std::size_t index = mineralIndex(deposit.mineral);
        totals.confirmed[index] += confirmedDepositQuantity(deposit);
        totals.uncertain[index] += uncertainDepositQuantity(deposit);

        // Estimated deposits are partially surveyed reserves. Unknown potential
        // remains physically present in the save but should be presented as a
        // survey target rather than a reliable reserve estimate.
        switch (depositSurveyState(deposit)) {
        case DepositSurveyState::Estimated:
            totals.estimated[index] += estimatedDepositQuantity(deposit);
            break;
        case DepositSurveyState::Unknown:
            totals.unknownPotential[index] += deposit.remaining;
            break;
        case DepositSurveyState::Known:
            break;
        }
    }
    return totals;
}

[[nodiscard]] bool dependsMostlyOnEstimatedSupply(const DepositQuantityTotals& quantities, const std::size_t index) noexcept {
    const double uncertainVisibleSupply = quantities.estimated[index] + quantities.unknownPotential[index];
    return uncertainVisibleSupply > 0.0 && uncertainVisibleSupply > quantities.confirmed[index];
}

[[nodiscard]] std::string uncertaintyWarningText(const Mineral mineral,
                                                 const DepositQuantityTotals& quantities,
                                                 const std::size_t index) {
    if (!dependsMostlyOnEstimatedSupply(quantities, index)) {
        return {};
    }

    std::ostringstream out;
    out << mineralName(mineral)
        << " depends mostly on estimated or unknown deposits; prioritize resource survey before planning around this reserve.";
    return out.str();
}

[[nodiscard]] ProcessingForecastTotals processingTotals(const GameState& state) {
    const std::vector<ProcessingRecipe> recipes = processingRecipes();
    ProcessingForecastTotals totals{};

    for (const Colony& colony : state.colonies) {
        MineralSet availableRaw = colony.stockpile;
        const double dailyCapacity = std::max(0.0, colony.processorCapacity);
        const ProcessingShares shares = normalizedProcessingShares(colony);

        for (const ProcessingRecipe& recipe : recipes) {
            const double targetOutput = dailyCapacity * shares[processedMaterialIndex(recipe.output)];
            if (targetOutput <= kProcessedMaterialComparisonEpsilon) {
                continue;
            }

            const double output = std::min(targetOutput, maxRecipeOutput(availableRaw, recipe.rawCostPerUnit));
            if (output <= kMineralComparisonEpsilon) {
                continue;
            }

            const MineralSet consumed = scaledMineralCost(recipe.rawCostPerUnit, output);
            availableRaw.subtract(consumed);
            addMineralSet(totals.rawDemand, consumed);
            totals.materialIncome[processedMaterialIndex(recipe.output)] += output;
        }
    }

    return totals;
}

[[nodiscard]] double totalDailyExtraction(const GameState& state, const MineralDeposit& deposit) noexcept {
    double potentialIncome = 0.0;
    for (const Colony& colony : state.colonies) {
        if (colony.bodyId == deposit.bodyId) {
            potentialIncome += std::max(0.0, colony.mines * deposit.accessibility);
        }
    }

    return deposit.remaining <= 0.0 ? 0.0 : std::min(deposit.remaining, potentialIncome);
}

[[nodiscard]] std::optional<int> exhaustionDays(const MineralDeposit& deposit, const double incomePerDay) {
    if (deposit.remaining <= 0.0) {
        return 0;
    }

    if (incomePerDay <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays(deposit.remaining / incomePerDay);
}

[[nodiscard]] std::string exhaustionExplanation(const MineralDeposit& deposit, const double incomePerDay, const std::optional<int> etaDays) {
    if (deposit.remaining <= 0.0) {
        return "Deposit is already exhausted";
    }

    if (!etaDays.has_value()) {
        return "No positive daily extraction rate; exhaustion cannot be estimated";
    }

    std::ostringstream out;
    out << deposit.remaining << " remaining / " << incomePerDay
        << " per day = " << *etaDays << " day(s)";
    return out.str();
}

[[nodiscard]] double totalBuildPointsRemaining(const ShipyardOrder& order, const ShipClass& shipClass) noexcept {
    const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
    if (shipsRemaining == 0) {
        return 0.0;
    }

    // accumulatedBuildPoints applies to the next ship in the order. Treat it as
    // progress against the remaining total capacity requirement, then clamp away
    // small or corrupted over-progress so projections never report negative work.
    const double remaining = static_cast<double>(shipsRemaining) * shipClass.buildPoints - order.accumulatedBuildPoints;
    return std::max(0.0, remaining);
}

struct ForecastModifierDetails {
    double modifier = 0.0;
    std::vector<ForecastModifierBreakdownRow> breakdown;
};

[[nodiscard]] const Appointment* activeAppointmentFor(const GameState& state,
                                                      const AppointmentRole role,
                                                      const AppointmentScopeType scopeType,
                                                      const std::int64_t scopeId) noexcept {
    const auto it = std::find_if(state.appointments.begin(), state.appointments.end(), [role, scopeType, scopeId](const Appointment& appointment) {
        return appointment.role == role && appointment.scopeType == scopeType && appointment.scopeId == scopeId;
    });
    return it == state.appointments.end() ? nullptr : &(*it);
}

[[nodiscard]] std::string competencyName(const PersonnelCompetency competency) {
    switch (competency) {
    case PersonnelCompetency::Logistics:
        return "Logistics";
    case PersonnelCompetency::Industry:
        return "Industry";
    case PersonnelCompetency::Survey:
        return "Survey";
    case PersonnelCompetency::Command:
        return "Command";
    case PersonnelCompetency::Administration:
        return "Administration";
    case PersonnelCompetency::Engineering:
        return "Engineering";
    case PersonnelCompetency::Intelligence:
        return "Intelligence";
    case PersonnelCompetency::CrisisManagement:
        return "Crisis Management";
    }

    return "Unknown";
}

void addModifierRow(std::vector<ForecastModifierBreakdownRow>& rows, const std::string& label, const double fraction) {
    rows.push_back(ForecastModifierBreakdownRow{.label = label, .percent = fraction * 100.0});
}

[[nodiscard]] ForecastModifierDetails appointmentModifierDetailsFor(const GameState& state,
                                                                    const AppointmentRole role,
                                                                    const AppointmentScopeType scopeType,
                                                                    const std::int64_t scopeId) {
    const Appointment* appointment = activeAppointmentFor(state, role, scopeType, scopeId);
    if (appointment == nullptr) {
        return ForecastModifierDetails{};
    }

    const Person* person = findById(state.people, appointment->personId);
    if (person == nullptr) {
        return ForecastModifierDetails{};
    }

    const AppointmentEffectProfile profile = appointmentEffectProfile(role);
    const double primary = static_cast<double>(competencyValue(person->competencies, profile.primary)) * kAppointmentPrimaryCompetencyWeight;
    const double secondary = static_cast<double>(competencyValue(person->competencies, profile.secondary)) * kAppointmentSecondaryCompetencyWeight;
    const double seniority = static_cast<double>(person->seniorityLevel) * kAppointmentSeniorityWeight;
    const double successes = static_cast<double>(person->serviceRecord.successfulAssignments) * kAppointmentSuccessWeight;
    const double failures = static_cast<double>(person->serviceRecord.failedAssignments) * kAppointmentFailurePenalty;
    const double commendations = static_cast<double>(person->serviceRecord.commendations) * kAppointmentCommendationWeight;
    const double controversies = static_cast<double>(person->serviceRecord.controversies) * kAppointmentControversyPenalty;

    std::vector<ForecastModifierBreakdownRow> breakdown;
    breakdown.reserve(8);
    addModifierRow(breakdown, competencyName(profile.primary) + " primary competency", primary);
    addModifierRow(breakdown, competencyName(profile.secondary) + " secondary competency", secondary);
    addModifierRow(breakdown, "Seniority", seniority);
    addModifierRow(breakdown, "Successful assignments", successes);
    addModifierRow(breakdown, "Failed assignments", failures);
    addModifierRow(breakdown, "Commendations", commendations);
    addModifierRow(breakdown, "Controversies", controversies);

    const double rawModifier = primary + secondary + seniority + successes + failures + commendations + controversies;
    const double modifier = clampAppointmentModifier(rawModifier);
    addModifierRow(breakdown, "Cap adjustment", modifier - rawModifier);

    return ForecastModifierDetails{.modifier = modifier, .breakdown = std::move(breakdown)};
}

[[nodiscard]] double effectiveShipyardCapacity(const GameState& state, const Colony& colony) {
    return std::max(0.0, colony.shipyardCapacity * (1.0 + appointmentModifierDetailsFor(
        state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value).modifier));
}

[[nodiscard]] double effectiveFuelRange(const double currentFuel, const double fuelEfficiencyModifier) noexcept {
    const double costMultiplier = std::max(kFuelComparisonEpsilon, 1.0 - fuelEfficiencyModifier);
    return currentFuel / (kPrototypeFuelPerMapUnit * costMultiplier);
}

[[nodiscard]] std::optional<int> shipyardEtaDays(const ShipyardOrder& order,
                                                 const ShipClass& shipClass,
                                                 const Colony&,
                                                 const double effectiveCapacity) {
    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return 0;
    }

    if (shipClass.buildPoints <= 0.0 || effectiveCapacity <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays(totalBuildPointsRemaining(order, shipClass) / effectiveCapacity);
}

[[nodiscard]] ProcessedMaterialAmountTotals activeShipyardDemandByMaterial(const GameState& state) {
    ProcessedMaterialAmountTotals totals{};

    for (const ShipyardOrder& order : state.shipyardOrders) {
        if (order.status != ShipyardOrderStatus::Active || order.quantityCompleted >= order.quantityRequested) {
            continue;
        }

        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        if (colony == nullptr || shipClass == nullptr) {
            continue;
        }

        const std::optional<int> etaDays = shipyardEtaDays(order, *shipClass, *colony, effectiveShipyardCapacity(state, *colony));
        if (!etaDays.has_value() || *etaDays <= 0) {
            continue;
        }

        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double perDayScale = static_cast<double>(shipsRemaining) / static_cast<double>(*etaDays);
        addProcessedMaterialSet(totals, shipClass->buildCost, perDayScale);
    }

    return totals;
}

[[nodiscard]] ProcessedMaterialSet scaledMaterialSet(const ProcessedMaterialSet& materials, const int scale) noexcept {
    ProcessedMaterialSet result{};
    if (scale <= 0) {
        return result;
    }

    for (std::size_t i = 0; i < result.amount.size(); ++i) {
        result.amount[i] = materials.amount[i] * static_cast<double>(scale);
    }
    return result;
}

[[nodiscard]] std::optional<ProcessedMaterial> firstBlockingMaterial(const ProcessedMaterialSet& stockpile,
                                                                     const ProcessedMaterialSet& required) noexcept {
    for (std::size_t i = 0; i < required.amount.size(); ++i) {
        if (stockpile.amount[i] + kProcessedMaterialComparisonEpsilon < required.amount[i]) {
            return materialFromIndex(i);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<int> queueAwareShipyardEtaDays(const double buildPointsAhead,
                                                           const double orderBuildPointsRemaining,
                                                           const double colonyCapacity) {
    if (orderBuildPointsRemaining <= 0.0) {
        return 0;
    }

    if (colonyCapacity <= 0.0) {
        return std::nullopt;
    }

    return ceilToNonNegativeDays((buildPointsAhead + orderBuildPointsRemaining) / colonyCapacity);
}

[[nodiscard]] std::string productionBacklogStatusName(const ShipyardOrder& order,
                                                      const bool blockedByMaterial) {
    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return "Complete";
    }

    if (blockedByMaterial) {
        return "Waiting for materials";
    }

    return "Building";
}

[[nodiscard]] std::string productionBacklogExplanation(const int queuePosition,
                                                       const double buildPointsAhead,
                                                       const double orderBuildPointsRemaining,
                                                       const double colonyCapacity,
                                                       const std::optional<int> etaDays,
                                                       const bool blockedByMaterial,
                                                       const std::string& blockingMaterialName) {
    std::ostringstream out;
    out << "Queue position " << queuePosition << "; "
        << buildPointsAhead << " build points ahead + "
        << orderBuildPointsRemaining << " order build points remaining";

    if (etaDays.has_value()) {
        out << " / " << colonyCapacity << " colony capacity per day = " << *etaDays << " day(s)";
    } else {
        out << "; no positive colony shipyard capacity, so ETA cannot be estimated";
    }

    if (blockedByMaterial) {
        out << "; current processed stockpiles are short of " << blockingMaterialName;
    }

    return out.str();
}

[[nodiscard]] std::string shipyardEtaExplanation(const ShipyardOrder& order,
                                                 const ShipClass* shipClass,
                                                 const Colony* colony,
                                                 const double effectiveCapacity,
                                                 const double modifierPercent,
                                                 const std::optional<int> etaDays) {
    if (shipClass == nullptr || colony == nullptr) {
        return "Order references missing colony or ship class; ETA cannot be estimated";
    }

    if (order.status == ShipyardOrderStatus::Completed || order.quantityCompleted >= order.quantityRequested) {
        return "Order is already complete";
    }

    if (!etaDays.has_value()) {
        return "No positive shipyard capacity or build-point requirement; ETA cannot be estimated";
    }

    std::ostringstream out;
    out << totalBuildPointsRemaining(order, *shipClass) << " build points remaining / "
        << effectiveCapacity << " effective capacity per day = " << *etaDays
        << " day(s); shipyard appointment modifier " << modifierPercent
        << "% ; processed material shortages may pause completion";
    return out.str();
}

[[nodiscard]] std::optional<int> fleetEtaDays(const Fleet& fleet) noexcept {
    if (fleet.activeOrder.type != FleetOrderType::MoveToBody) {
        return std::nullopt;
    }

    if (!fleet.activeOrder.targetBodyId.has_value()) {
        return std::nullopt;
    }

    return std::max(0, fleet.activeOrder.daysRemaining);
}

[[nodiscard]] std::string fleetEtaExplanation(const Fleet& fleet, const std::optional<int> etaDays) {
    if (fleet.activeOrder.type == FleetOrderType::None) {
        return "Fleet is idle; no arrival ETA";
    }

    if (!fleet.activeOrder.targetBodyId.has_value()) {
        return "Fleet has movement order without a target body; ETA cannot be estimated";
    }

    std::ostringstream out;
    out << "Fleet will arrive in " << *etaDays << " day(s) if the active order continues";
    return out.str();
}


[[nodiscard]] double fleetCurrentFuel(const GameState& state, const Fleet& fleet) noexcept {
    double total = 0.0;
    for (const ShipId shipId : fleet.shipIds) {
        const Ship* ship = findById(state.ships, shipId);
        if (ship != nullptr) {
            total += ship->fuel;
        }
    }
    return total;
}

[[nodiscard]] double fleetFuelCapacity(const GameState& state, const Fleet& fleet) noexcept {
    double total = 0.0;
    for (const ShipId shipId : fleet.shipIds) {
        const Ship* ship = findById(state.ships, shipId);
        if (ship == nullptr) {
            continue;
        }
        const ShipClass* shipClass = findById(state.shipClasses, ship->shipClassId);
        if (shipClass != nullptr) {
            total += shipClass->fuelCapacity;
        }
    }
    return total;
}

[[nodiscard]] std::string fleetFuelExplanation(const double currentFuel,
                                                const double fuelCapacity,
                                                const double fuelEfficiencyModifierPercent) {
    std::ostringstream out;
    out << currentFuel << " / " << fuelCapacity
        << " propellant; fleet commander fuel-efficiency modifier "
        << fuelEfficiencyModifierPercent << "%";
    return out.str();
}

} // namespace

ForecastService::ForecastService(const SimulationService& service) noexcept
    : service_{service} {}

std::vector<MineralIncomeForecast> ForecastService::mineralIncomePerDay() const {
    const GameState& state = service_.state();
    std::vector<MineralIncomeForecast> forecasts;
    forecasts.reserve(state.colonies.size() * state.mineralDeposits.size());

    std::vector<double> remainingByDeposit;
    remainingByDeposit.reserve(state.mineralDeposits.size());
    for (const MineralDeposit& deposit : state.mineralDeposits) {
        remainingByDeposit.push_back(deposit.remaining);
    }

    for (const Colony& colony : state.colonies) {
        for (std::size_t depositIndex = 0; depositIndex < state.mineralDeposits.size(); ++depositIndex) {
            const MineralDeposit& deposit = state.mineralDeposits[depositIndex];
            if (deposit.bodyId != colony.bodyId) {
                continue;
            }

            // Forecast extraction must share each deposit in the same deterministic
            // colony/deposit order used by Simulation::simulateMining. Without
            // this shared running balance, multiple colonies on one body would
            // each cap against the full deposit and overstate total income.
            const double income = dailyExtraction(colony, deposit, remainingByDeposit[depositIndex]);
            remainingByDeposit[depositIndex] -= income;

            const DepositSurveyState surveyState = depositSurveyState(deposit);
            forecasts.push_back(MineralIncomeForecast{
                .colonyId = colony.id,
                .bodyId = colony.bodyId,
                .mineral = deposit.mineral,
                .colonyName = colony.name,
                .bodyName = bodyName(state, colony.bodyId),
                .mineralName = mineralName(deposit.mineral),
                .confidence = deposit.confidence,
                .surveyStateName = depositSurveyStateName(surveyState),
                .confirmedQuantity = confirmedDepositQuantity(deposit),
                .estimatedQuantity = estimatedDepositQuantity(deposit),
                .uncertainQuantity = uncertainDepositQuantity(deposit),
                .incomePerDay = income,
                .explanation = mineralIncomeExplanation(colony, deposit, income)
            });
        }
    }

    return forecasts;
}

std::vector<MineralForecastCauseChain> ForecastService::mineralForecastCauseChains() const {
    const GameState& state = service_.state();
    const MineralAmountTotals stockpiles = totalColonyStockpiles(state);
    const MineralAmountTotals miningIncome = miningIncomeByMineral(mineralIncomePerDay());
    const DepositQuantityTotals depositQuantities = depositQuantityTotals(state);
    const ProcessingForecastTotals processing = processingTotals(state);

    std::vector<MineralForecastCauseChain> forecasts;
    forecasts.reserve(mineralCount());

    for (std::size_t i = 0; i < mineralCount(); ++i) {
        const Mineral mineral = mineralFromIndex(i);
        const double netPerDay = miningIncome[i] - processing.rawDemand[i];
        const std::optional<int> runoutDays = stockpileRunoutDays(stockpiles[i], netPerDay);
        const bool mostlyEstimated = dependsMostlyOnEstimatedSupply(depositQuantities, i);
        const bool critical = netPerDay < -kMineralComparisonEpsilon || runoutDays.has_value();

        forecasts.push_back(MineralForecastCauseChain{
            .mineral = mineral,
            .mineralName = mineralName(mineral),
            .stockpile = stockpiles[i],
            .confirmedDepositQuantity = depositQuantities.confirmed[i],
            .estimatedDepositQuantity = depositQuantities.estimated[i],
            .unknownPotentialQuantity = depositQuantities.unknownPotential[i],
            .uncertainDepositQuantity = depositQuantities.uncertain[i],
            .miningIncomePerDay = miningIncome[i],
            .committedDemandPerDay = processing.rawDemand[i],
            .netPerDay = netPerDay,
            .stockpileRunoutDays = runoutDays,
            .dependsMostlyOnEstimatedSupply = mostlyEstimated,
            .uncertaintyWarning = mostlyEstimated && critical ? uncertaintyWarningText(mineral, depositQuantities, i) : std::string{},
            .causes = {
                MineralForecastCauseRow{
                    .label = "Confirmed deposits",
                    .amountPerDay = depositQuantities.confirmed[i],
                    .explanation = "Reserve quantity supported by current survey confidence"
                },
                MineralForecastCauseRow{
                    .label = "Estimated deposits",
                    .amountPerDay = depositQuantities.estimated[i],
                    .explanation = "Partially surveyed reserve estimate; further surveys can convert more of it into confirmed supply"
                },
                MineralForecastCauseRow{
                    .label = "Unknown potential",
                    .amountPerDay = depositQuantities.unknownPotential[i],
                    .explanation = "Hidden or unsurveyed reserve potential; do not treat as reliable supply until surveyed"
                },
                MineralForecastCauseRow{
                    .label = "Mining",
                    .amountPerDay = miningIncome[i],
                    .explanation = miningCauseExplanation(miningIncome[i])
                },
                MineralForecastCauseRow{
                    .label = "Processing recipes",
                    .amountPerDay = -processing.rawDemand[i],
                    .explanation = processingDemandCauseExplanation(processing.rawDemand[i])
                }
            }
        });
    }

    return forecasts;
}

std::vector<ProcessedMaterialForecastCauseChain> ForecastService::processedMaterialForecastCauseChains() const {
    const GameState& state = service_.state();
    const ProcessedMaterialAmountTotals stockpiles = totalProcessedStockpiles(state);
    const ProcessingForecastTotals processing = processingTotals(state);
    const ProcessedMaterialAmountTotals shipyardDemand = activeShipyardDemandByMaterial(state);

    std::vector<ProcessedMaterialForecastCauseChain> forecasts;
    forecasts.reserve(processedMaterialCount());

    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const ProcessedMaterial material = materialFromIndex(i);
        const double netPerDay = processing.materialIncome[i] - shipyardDemand[i];

        forecasts.push_back(ProcessedMaterialForecastCauseChain{
            .material = material,
            .materialName = materialName(material),
            .stockpile = stockpiles[i],
            .processingIncomePerDay = processing.materialIncome[i],
            .committedDemandPerDay = shipyardDemand[i],
            .netPerDay = netPerDay,
            .stockpileRunoutDays = stockpileRunoutDays(stockpiles[i], netPerDay),
            .causes = {
                MineralForecastCauseRow{
                    .label = "Processing recipes",
                    .amountPerDay = processing.materialIncome[i],
                    .explanation = processingIncomeCauseExplanation(processing.materialIncome[i])
                },
                MineralForecastCauseRow{
                    .label = "Active shipyard orders",
                    .amountPerDay = -shipyardDemand[i],
                    .explanation = shipyardDemandCauseExplanation(shipyardDemand[i])
                }
            }
        });
    }

    return forecasts;
}

std::vector<DepositExhaustionForecast> ForecastService::depositExhaustionEstimates() const {
    const GameState& state = service_.state();
    std::vector<DepositExhaustionForecast> forecasts;
    forecasts.reserve(state.mineralDeposits.size());

    for (const MineralDeposit& deposit : state.mineralDeposits) {
        // Sum all colony extraction on this body so the deposit forecast remains
        // correct if later scenarios add multiple settlements to one body.
        const double income = totalDailyExtraction(state, deposit);
        const std::optional<int> eta = exhaustionDays(deposit, income);

        const DepositSurveyState surveyState = depositSurveyState(deposit);
        forecasts.push_back(DepositExhaustionForecast{
            .bodyId = deposit.bodyId,
            .mineral = deposit.mineral,
            .bodyName = bodyName(state, deposit.bodyId),
            .mineralName = mineralName(deposit.mineral),
            .confidence = deposit.confidence,
            .surveyStateName = depositSurveyStateName(surveyState),
            .remainingDeposit = deposit.remaining,
            .confirmedDeposit = confirmedDepositQuantity(deposit),
            .estimatedDeposit = estimatedDepositQuantity(deposit),
            .uncertainDeposit = uncertainDepositQuantity(deposit),
            .incomePerDay = income,
            .exhaustionDays = eta,
            .explanation = exhaustionExplanation(deposit, income, eta)
        });
    }

    return forecasts;
}

std::vector<ShipyardOrderEtaForecast> ForecastService::shipyardOrderEtas() const {
    const GameState& state = service_.state();
    std::vector<ShipyardOrderEtaForecast> forecasts;
    forecasts.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double remainingBuildPoints = shipClass == nullptr ? 0.0 : totalBuildPointsRemaining(order, *shipClass);
        const ForecastModifierDetails modifier = colony == nullptr
            ? ForecastModifierDetails{}
            : appointmentModifierDetailsFor(state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony->id.value);
        const double effectiveCapacity = colony == nullptr ? 0.0 : effectiveShipyardCapacity(state, *colony);
        const std::optional<int> eta = (colony == nullptr || shipClass == nullptr)
            ? std::nullopt
            : shipyardEtaDays(order, *shipClass, *colony, effectiveCapacity);

        forecasts.push_back(ShipyardOrderEtaForecast{
            .orderId = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .shipsRemaining = shipsRemaining,
            .buildPointsRemaining = remainingBuildPoints,
            .effectiveShipyardCapacity = effectiveCapacity,
            .shipyardModifierPercent = modifier.modifier * 100.0,
            .shipyardModifierBreakdown = modifier.breakdown,
            .etaDays = eta,
            .explanation = shipyardEtaExplanation(order, shipClass, colony, effectiveCapacity, modifier.modifier * 100.0, eta)
        });
    }

    return forecasts;
}

std::vector<ProductionBacklogForecast> ForecastService::productionBacklog() const {
    const GameState& state = service_.state();

    struct ColonyQueueForecastState {
        ColonyId colonyId;
        int nextQueuePosition = 1;
        double buildPointsAhead = 0.0;
    };

    std::vector<ColonyQueueForecastState> queueStates;
    queueStates.reserve(state.colonies.size());
    for (const Colony& colony : state.colonies) {
        queueStates.push_back(ColonyQueueForecastState{
            .colonyId = colony.id,
            .nextQueuePosition = 1,
            .buildPointsAhead = 0.0
        });
    }

    std::vector<ProductionBacklogForecast> forecasts;
    forecasts.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        const Colony* colony = findById(state.colonies, order.colonyId);
        const ShipClass* shipClass = findById(state.shipClasses, order.shipClassId);
        const int shipsRemaining = std::max(0, order.quantityRequested - order.quantityCompleted);
        const double buildPointsRemaining = shipClass == nullptr ? 0.0 : totalBuildPointsRemaining(order, *shipClass);
        const ProcessedMaterialSet requiredMaterials = shipClass == nullptr
            ? ProcessedMaterialSet{}
            : scaledMaterialSet(shipClass->buildCost, shipsRemaining);

        int queuePosition = 0;
        double buildPointsAhead = 0.0;
        const ForecastModifierDetails modifier = colony == nullptr
            ? ForecastModifierDetails{}
            : appointmentModifierDetailsFor(state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony->id.value);
        double colonyCapacity = colony == nullptr ? 0.0 : effectiveShipyardCapacity(state, *colony);
        std::optional<int> etaDays;

        if (order.status == ShipyardOrderStatus::Completed || shipsRemaining == 0) {
            etaDays = 0;
        } else if (colony != nullptr && shipClass != nullptr) {
            auto queueIt = std::find_if(queueStates.begin(), queueStates.end(), [order](const ColonyQueueForecastState& queueState) {
                return queueState.colonyId == order.colonyId;
            });

            if (queueIt != queueStates.end()) {
                queuePosition = queueIt->nextQueuePosition;
                buildPointsAhead = queueIt->buildPointsAhead;
                etaDays = queueAwareShipyardEtaDays(buildPointsAhead, buildPointsRemaining, colonyCapacity);

                // Match simulation's FIFO capacity rule: this order's remaining
                // build-point need is queued before later active orders at the
                // same colony, regardless of whether materials later delay it.
                ++queueIt->nextQueuePosition;
                queueIt->buildPointsAhead += buildPointsRemaining;
            }
        }

        const std::optional<ProcessedMaterial> blockingMaterial = colony == nullptr
            ? std::nullopt
            : firstBlockingMaterial(colony->processedStockpile, requiredMaterials);
        const bool blockedByMaterial = blockingMaterial.has_value()
            && order.status == ShipyardOrderStatus::Active
            && shipsRemaining > 0;
        const std::string blockerName = blockingMaterial.has_value() ? materialName(*blockingMaterial) : std::string{};

        forecasts.push_back(ProductionBacklogForecast{
            .orderId = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .quantityRequested = order.quantityRequested,
            .quantityCompleted = order.quantityCompleted,
            .shipsRemaining = shipsRemaining,
            .queuePosition = queuePosition,
            .colonyShipyardCapacity = colony == nullptr ? 0.0 : std::max(0.0, colony->shipyardCapacity),
            .effectiveShipyardCapacity = colonyCapacity,
            .shipyardModifierPercent = modifier.modifier * 100.0,
            .shipyardModifierBreakdown = modifier.breakdown,
            .accumulatedBuildPoints = order.accumulatedBuildPoints,
            .buildPointsRemaining = buildPointsRemaining,
            .requiredMaterialsRemaining = requiredMaterials,
            .blockedByMaterial = blockedByMaterial,
            .blockingMaterial = blockedByMaterial ? blockingMaterial : std::nullopt,
            .blockingMaterialName = blockedByMaterial ? blockerName : std::string{},
            .etaDays = etaDays,
            .statusName = productionBacklogStatusName(order, blockedByMaterial),
            .explanation = productionBacklogExplanation(queuePosition,
                                                        buildPointsAhead,
                                                        buildPointsRemaining,
                                                        colonyCapacity,
                                                        etaDays,
                                                        blockedByMaterial,
                                                        blockerName)
        });
    }

    return forecasts;
}

std::vector<FleetArrivalEtaForecast> ForecastService::fleetArrivalEtas() const {
    const GameState& state = service_.state();
    std::vector<FleetArrivalEtaForecast> forecasts;
    forecasts.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const std::optional<int> eta = fleetEtaDays(fleet);
        forecasts.push_back(FleetArrivalEtaForecast{
            .fleetId = fleet.id,
            .fleetName = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .currentBodyName = bodyName(state, fleet.currentBodyId),
            .destinationBodyId = fleet.activeOrder.targetBodyId,
            .destinationBodyName = fleet.activeOrder.targetBodyId.has_value()
                ? bodyName(state, *fleet.activeOrder.targetBodyId)
                : std::string{},
            .etaDays = eta,
            .explanation = fleetEtaExplanation(fleet, eta)
        });
    }

    return forecasts;
}


std::vector<FleetFuelForecast> ForecastService::fleetFuelForecasts() const {
    const GameState& state = service_.state();
    std::vector<FleetFuelForecast> forecasts;
    forecasts.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const double currentFuel = fleetCurrentFuel(state, fleet);
        const double capacity = fleetFuelCapacity(state, fleet);
        const ForecastModifierDetails modifier = appointmentModifierDetailsFor(
            state, AppointmentRole::FleetCommander, AppointmentScopeType::Fleet, fleet.id.value);
        forecasts.push_back(FleetFuelForecast{
            .fleetId = fleet.id,
            .fleetName = fleet.name,
            .currentFuel = currentFuel,
            .fuelCapacity = capacity,
            .fuelPercent = capacity <= kFuelComparisonEpsilon ? 0.0 : (currentFuel * 100.0 / capacity),
            .currentRange = effectiveFuelRange(currentFuel, modifier.modifier),
            .fuelEfficiencyModifierPercent = modifier.modifier * 100.0,
            .fuelModifierBreakdown = modifier.breakdown,
            .explanation = fleetFuelExplanation(currentFuel, capacity, modifier.modifier * 100.0)
        });
    }

    return forecasts;
}

} // namespace deep
