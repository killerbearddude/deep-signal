#include "sim/AnalysisProgramExecution.h"
#include "sim/Simulation.h"
#include "sim/ObservationAcquisition.h"
#include "sim/ObservationRules.h"
#include "sim/GameStateValidation.h"
#include "sim/ProcessingAllocationRules.h"
#include "sim/ShipDesignRules.h"
#include "sim/SurveyProgramRules.h"
#include "sim/SurveyProgramExecution.h"
#include "sim/FreightProgramExecution.h"
#include "sim/FreightProgramRules.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/MaintenanceProgramExecution.h"
#include "sim/TransitPlanning.h"

// Implements deterministic daily simulation rules and command validation.
// This file is intentionally self-contained within the sim layer; it should not
// include UI, database, threading, or platform-specific headers.
// State transitions run serially; callers must not observe or mutate state during
// a command/tick. Collection order is part of deterministic resource allocation.

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deep {
namespace {

// Finds a mutable object by typed ID. The returned pointer is non-owning and may
// be null when validation fails; callers must not store it across vector mutation.
template <typename T, typename IdT>
[[nodiscard]] T* findById(std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// Finds a read-only object by typed ID. This overload preserves const-correctness
// for validation paths that must inspect state without mutation.
template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// Creates deterministic display names from an entity prefix and ID. IDs are used
// instead of counters local to a function so save/load and tests remain stable.
[[nodiscard]] std::string idLabel(const std::string_view prefix, const std::int64_t value) {
    std::ostringstream out;
    out << prefix << ' ' << value;
    return out.str();
}


struct ProcessingRecipe {
    ProcessedMaterial output;
    MineralSet rawCostPerUnit;
};

[[nodiscard]] MineralSet makeRawCost(const std::initializer_list<std::pair<Mineral, double>> inputs) {
    MineralSet cost;
    for (const auto& [mineral, amount] : inputs) {
        cost.set(mineral, amount);
    }
    return cost;
}

[[nodiscard]] std::vector<ProcessingRecipe> processingRecipes() {
    // Fixed v1 processing chains. Each recipe produces one processed-material
    // unit per processor-capacity point and consumes the listed raw resources.
    // The order mirrors ProcessedMaterial and determines who consumes shared
    // inputs first. ForecastService currently duplicates these recipes and must
    // be updated with rule changes until the calculations share an implementation.
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

[[nodiscard]] bool isValidProcessedMaterial(const ProcessedMaterial material) noexcept {
    return processedMaterialIndex(material) < processedMaterialCount();
}

[[nodiscard]] bool isValidProcessingPolicy(const ProcessingPolicy policy) noexcept {
    switch (policy) {
    case ProcessingPolicy::Balanced:
    case ProcessingPolicy::ShipbuildingFocus:
    case ProcessingPolicy::FuelFocus:
    case ProcessingPolicy::ElectronicsFocus:
    case ProcessingPolicy::StockpileRecovery:
    case ProcessingPolicy::Manual:
        return true;
    }
    return false;
}

[[nodiscard]] const char* processingAllocationRejection(const ProcessingAllocationError error) noexcept {
    switch (error) {
    case ProcessingAllocationError::InvalidMaterial:
        return "Manual processing allocation has invalid material";
    case ProcessingAllocationError::NonFiniteWeight:
        return "Manual processing allocation weight must be finite";
    case ProcessingAllocationError::NegativeWeight:
        return "Manual processing allocation weight cannot be negative";
    case ProcessingAllocationError::MaterialSubtotalOverflow:
    case ProcessingAllocationError::CombinedTotalOverflow:
    case ProcessingAllocationError::InvalidNormalizedShare:
        return "Manual processing allocation total is too large";
    case ProcessingAllocationError::InsufficientManualTotal:
        return "Manual processing policy requires positive allocation weight";
    case ProcessingAllocationError::None:
        break;
    }
    return "Manual processing allocation is invalid";
}

[[nodiscard]] bool isValidAppointmentRole(const AppointmentRole role) noexcept {
    switch (role) {
    case AppointmentRole::FleetCommander:
    case AppointmentRole::ColonyAdministrator:
    case AppointmentRole::ShipyardDirector:
    case AppointmentRole::SurveyChief:
    case AppointmentRole::LogisticsCoordinator:
    case AppointmentRole::InstitutionHead:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidAppointmentScopeType(const AppointmentScopeType scopeType) noexcept {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
    case AppointmentScopeType::Colony:
    case AppointmentScopeType::Institution:
        return true;
    }
    return false;
}

void addProcessingWeight(ProcessingShares& weights, const ProcessedMaterial material, const double weight) noexcept {
    if (weight <= 0.0 || !isValidProcessedMaterial(material)) {
        return;
    }

    weights[processedMaterialIndex(material)] += weight;
}

[[nodiscard]] ProcessingShares balancedProcessingWeights() noexcept {
    ProcessingShares weights{};
    for (std::size_t i = 0; i < weights.size(); ++i) {
        weights[i] = 1.0;
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
            // Lower stockpiles receive more capacity while every material keeps a
            // non-zero share. This is intentionally simple until automation has
            // explicit shortage targets and logistics context.
            weights[i] = 1.0 / (1.0 + std::max(0.0, colony.processedStockpile.amount[i]));
        }
        break;

    case ProcessingPolicy::Manual:
        // Manual rows are evaluated separately, preserving duplicate order.
        break;
    }

    return weights;
}

[[nodiscard]] ProcessingShares normalizedProcessingShares(const Colony& colony) {
    const ProcessingAllocationResult allocation = colony.processingPolicy == ProcessingPolicy::Manual
        ? evaluateProcessingAllocations(colony.manualProcessingAllocations, true)
        : normalizeProcessingWeights(policyProcessingWeights(colony), false);
    // Simulation admission validates stored rows, including dormant Manual
    // intent. An unexpected failure here is corrupt live state, not zero output.
    if (!allocation.valid()) throw std::logic_error{"Invalid live processing allocation"};
    return processingSharesForActivePolicy(allocation, colony.processingPolicy);
}


[[nodiscard]] const Appointment* activeAppointmentFor(const GameState& state,
                                                      const AppointmentRole role,
                                                      const AppointmentScopeType scopeType,
                                                      const std::int64_t scopeId) noexcept {
    const auto it = std::find_if(state.appointments.begin(), state.appointments.end(), [role, scopeType, scopeId](const Appointment& appointment) {
        return appointment.role == role && appointment.scopeType == scopeType && appointment.scopeId == scopeId;
    });
    return it == state.appointments.end() ? nullptr : &(*it);
}

[[nodiscard]] const Person* appointedPersonFor(const GameState& state,
                                               const AppointmentRole role,
                                               const AppointmentScopeType scopeType,
                                               const std::int64_t scopeId) noexcept {
    const Appointment* appointment = activeAppointmentFor(state, role, scopeType, scopeId);
    return appointment == nullptr ? nullptr : findById(state.people, appointment->personId);
}

[[nodiscard]] double appointmentModifierFor(const GameState& state,
                                            const AppointmentRole role,
                                            const AppointmentScopeType scopeType,
                                            const std::int64_t scopeId) noexcept {
    const Person* person = appointedPersonFor(state, role, scopeType, scopeId);
    return person == nullptr ? 0.0 : appointmentOperationalModifier(*person, role);
}

[[nodiscard]] double effectiveShipyardCapacity(const GameState& state, const Colony& colony) noexcept {
    const double baseCapacity = std::max(0.0, colony.shipyardCapacity);
    const double modifier = appointmentModifierFor(state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value);
    return std::max(0.0, baseCapacity * (1.0 + modifier));
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

[[nodiscard]] double queuedRouteFuelRequirement(const GameState& state,
                                                const Fleet& fleet,
                                                const BodyId newDestinationBodyId) noexcept {
    // The active leg has already paid its fuel. Project only queued/new legs,
    // using today's commander modifier; each leg is checked again when started
    // because appointments or a cancellation can change the actual future route.
    BodyId projectedOrigin = fleet.currentBodyId;
    std::int64_t projectedDepartureDay = state.date.day;
    if (fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.activeOrder.targetBodyId.has_value()) {
        projectedOrigin = *fleet.activeOrder.targetBodyId;
        projectedDepartureDay = std::max(projectedDepartureDay, fleet.activeOrder.arrivalDay);
    }

    double requiredFuel = 0.0;
    const auto addProjectedMove = [&](const BodyId destinationBodyId) {
        const FleetOrder projectedPlan = planFleetTransit(state, projectedOrigin, destinationBodyId, projectedDepartureDay);
        const double cost = adjustedFleetMoveFuelCost(state, fleet, projectedOrigin, destinationBodyId, projectedDepartureDay);
        requiredFuel += cost;
        projectedOrigin = destinationBodyId;
        projectedDepartureDay = projectedPlan.arrivalDay > projectedDepartureDay ? projectedPlan.arrivalDay : projectedDepartureDay + 1;
    };

    for (const QueuedFleetOrder& queuedOrder : fleet.queuedOrders) {
        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            return std::numeric_limits<double>::infinity();
        }
        addProjectedMove(*queuedOrder.targetBodyId);
    }

    addProjectedMove(newDestinationBodyId);
    return requiredFuel;
}

[[nodiscard]] bool fleetHasFuelFor(const GameState& state, const Fleet& fleet, const double fuelCost) noexcept {
    return fleetCurrentFuel(state, fleet) + kFuelComparisonEpsilon >= fuelCost;
}

bool consumeFleetFuel(GameState& state, const Fleet& fleet, const double fuelCost) noexcept {
    // Fuel is one fleet-wide pool, drawn from hulls in persisted roster order.
    // There is no per-hull travel requirement or proportional sharing in v1.
    if (fuelCost <= kFuelComparisonEpsilon) {
        return true;
    }

    if (!fleetHasFuelFor(state, fleet, fuelCost)) {
        return false;
    }

    double remainingCost = fuelCost;
    for (const ShipId shipId : fleet.shipIds) {
        Ship* ship = findById(state.ships, shipId);
        if (ship == nullptr || ship->fuel <= 0.0) {
            continue;
        }

        const double consumed = std::min(ship->fuel, remainingCost);
        ship->fuel -= consumed;
        remainingCost -= consumed;

        if (ship->fuel < kFuelComparisonEpsilon) {
            ship->fuel = 0.0;
        }
        if (remainingCost <= kFuelComparisonEpsilon) {
            return true;
        }
    }

    return remainingCost <= kFuelComparisonEpsilon;
}

} // namespace

Simulation::Simulation(GameState initialState)
    : state_{std::move(initialState)} {
    // External snapshots are an explicit trust boundary. Validate before the
    // simulation can allocate IDs, append events, or advance time from them.
    validateGameState(state_);
}

const GameState& Simulation::state() const noexcept {
    return state_;
}

CommandResult Simulation::execute(const SimCommand& command) {
    return std::visit([this](const auto& concreteCommand) -> CommandResult {
        using Command = std::decay_t<decltype(concreteCommand)>;

        // Keep command dispatch explicit so adding a new SimCommand alternative
        // forces a visible validation branch in this central mutation boundary.
        if constexpr (std::is_same_v<Command, AdvanceDaysCommand>) {
            if (concreteCommand.days <= 0) {
                appendEvent(EventSeverity::Warning, CommandRejectedEvent{"AdvanceDaysCommand requires days > 0"});
                return CommandResult::failure("AdvanceDaysCommand requires days > 0");
            }
            const AdvanceResult result = advanceDaysDetailed(concreteCommand.days);
            std::string message = "Advanced " + std::to_string(result.advancedDays) + " day(s)";
            if (result.interrupted) message += "; stopped: " + result.stopReason;
            return CommandResult::success(std::move(message));
        } else if constexpr (std::is_same_v<Command, AssignShipyardBuildCommand>) {
            return assignShipyardBuild(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CreateShipClassRevisionCommand>) {
            return createShipClassRevision(concreteCommand);
        } else if constexpr (std::is_same_v<Command, MoveFleetCommand>) {
            return moveFleet(concreteCommand);
        } else if constexpr (std::is_same_v<Command, QueueFleetMoveOrderCommand>) {
            return queueFleetMoveOrder(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ClearFleetOrderQueueCommand>) {
            return clearFleetOrderQueue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CancelFleetOrderCommand>) {
            return cancelFleetOrder(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ResourceSurveyCommand>) {
            return resourceSurvey(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CreateSurveyProgramCommand>) {
            return createSurveyProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AmendSurveyProgramCommand>) {
            return amendSurveyProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SuspendSurveyProgramCommand>) {
            return suspendSurveyProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ResumeSurveyProgramCommand>) {
            return resumeSurveyProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CancelSurveyProgramCommand>) {
            return cancelSurveyProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AcknowledgeSurveyProgramIssueCommand>) {
            return acknowledgeSurveyProgramIssue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CreateFreightProgramCommand>) {
            return createFreightProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AmendFreightProgramCommand>) {
            return amendFreightProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SuspendFreightProgramCommand>) {
            return suspendFreightProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, ResumeFreightProgramCommand>) {
            return resumeFreightProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CancelFreightProgramCommand>) {
            return cancelFreightProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AcknowledgeFreightProgramIssueCommand>) {
            return acknowledgeFreightProgramIssue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CreateMaintenanceProgramCommand>) {
            return createMaintenanceProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AmendMaintenanceProgramCommand>) {
            return amendMaintenanceProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SuspendMaintenanceProgramCommand>) {
            return setMaintenanceLifecycle(concreteCommand.programId,MaintenanceProgramLifecycle::Suspended);
        } else if constexpr (std::is_same_v<Command, ResumeMaintenanceProgramCommand>) {
            return setMaintenanceLifecycle(concreteCommand.programId,MaintenanceProgramLifecycle::Authorized);
        } else if constexpr (std::is_same_v<Command, CancelMaintenanceProgramCommand>) {
            return setMaintenanceLifecycle(concreteCommand.programId,MaintenanceProgramLifecycle::Closed);
        } else if constexpr (std::is_same_v<Command, AcknowledgeMaintenanceIssueCommand>) {
            return acknowledgeMaintenanceIssue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, CreateAnalysisProgramCommand>) {
            return createAnalysisProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AmendAnalysisProgramCommand>) {
            return amendAnalysisProgram(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SuspendAnalysisProgramCommand>) {
            return setAnalysisLifecycle(concreteCommand.programId,AnalysisLifecycle::Suspended);
        } else if constexpr (std::is_same_v<Command, ResumeAnalysisProgramCommand>) {
            return setAnalysisLifecycle(concreteCommand.programId,AnalysisLifecycle::Authorized);
        } else if constexpr (std::is_same_v<Command, CancelAnalysisProgramCommand>) {
            return setAnalysisLifecycle(concreteCommand.programId,AnalysisLifecycle::Closed);
        } else if constexpr (std::is_same_v<Command, AcknowledgeAnalysisIssueCommand>) {
            return acknowledgeAnalysisIssue(concreteCommand);
        } else if constexpr (std::is_same_v<Command, AssignAppointmentCommand>) {
            return assignAppointment(concreteCommand);
        } else if constexpr (std::is_same_v<Command, SetColonyProcessingPolicyCommand>) {
            return setColonyProcessingPolicy(concreteCommand);
        }
    }, command);
}

std::vector<SimEvent> Simulation::advanceDays(const int days) {
    return advanceDaysDetailed(days).events;
}

AdvanceResult Simulation::advanceDaysDetailed(const int days) {
    AdvanceResult result{.requestedDays = days};

    if (days <= 0) {
        emitEvent(result.events, EventSeverity::Warning, CommandRejectedEvent{"advanceDays requires days > 0"});
        result.stopReason = "advanceDays requires days > 0";
        return result;
    }

    for (int i = 0; i < days; ++i) {
        if (const auto pending = pendingProgramIssue(state_)) {
            result.interrupted = true;
            result.issueProgramId = pending->controller;
            result.stopReason = pending->message;
            break;
        }
        if (state_.date.day == std::numeric_limits<std::int64_t>::max()) {
            result.interrupted = true;
            result.stopReason = "Simulation date limit reached";
            break;
        }
        const auto nextDay = state_.date.day + 1;
        // A due report also needs its next durable cursor. Stop before any
        // physical phase if that date cannot be represented; otherwise a late
        // report exception would leave a partly advanced, unsaveable world.
        if (nextDay > std::numeric_limits<std::int64_t>::max() - 30 &&
            (std::any_of(state_.surveyPrograms.begin(), state_.surveyPrograms.end(),
                [=](const auto& p) { return p.nextReportDay == nextDay; }) ||
             std::any_of(state_.freightPrograms.begin(), state_.freightPrograms.end(),
                [=](const auto& p) { return p.lifecycle != FreightProgramLifecycle::Closed && p.nextReportDay == nextDay; }) ||
             std::any_of(state_.maintenancePrograms.begin(), state_.maintenancePrograms.end(),
                [=](const auto& p) { return p.lifecycle != MaintenanceProgramLifecycle::Closed && p.nextReportDay == nextDay; }) ||
             std::any_of(state_.analysisPrograms.begin(),state_.analysisPrograms.end(),
                [=](const auto& p){return p.lifecycle!=AnalysisLifecycle::Closed && p.nextReportDay==nextDay;}))) {
            result.interrupted = true;
            result.stopReason = "Program reporting date limit reached";
            break;
        }
        simulateOneDay(result.events);
        ++result.advancedDays;
        if (const auto raised = pendingProgramIssue(state_)) {
            result.interrupted = true;
            result.issueProgramId = raised->controller;
            result.stopReason = raised->message;
            break;
        }
    }

    return result;
}

CommandResult Simulation::assignShipyardBuild(const AssignShipyardBuildCommand& command) {
    if (command.quantity <= 0) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Shipyard build quantity must be greater than zero"});
        return CommandResult::failure("Shipyard build quantity must be greater than zero");
    }

    const Colony* colony = findColony(command.colonyId);
    if (colony == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony does not exist"});
        return CommandResult::failure("Colony does not exist");
    }

    if (findShipClass(command.shipClassId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Ship class does not exist"});
        return CommandResult::failure("Ship class does not exist");
    }

    ShipyardOrder order{
        .id = allocateShipyardOrderId(),
        .colonyId = command.colonyId,
        .shipClassId = command.shipClassId,
        .quantityRequested = command.quantity,
        .quantityCompleted = 0,
        .accumulatedBuildPoints = 0.0,
        .status = ShipyardOrderStatus::Active
    };

    // Capture the ID before moving/copying the order into GameState so the event
    // precisely identifies the persisted order record.
    const ShipyardOrderId orderId = order.id;
    state_.shipyardOrders.push_back(order);

    appendEvent(EventSeverity::Info, ShipyardOrderCreatedEvent{
        .orderId = orderId,
        .colonyId = command.colonyId,
        .shipClassId = command.shipClassId,
        .quantity = command.quantity
    });

    return CommandResult::success("Shipyard build order accepted");
}

CommandResult Simulation::createShipClassRevision(const CreateShipClassRevisionCommand& command) {
    const auto reject = [this](const std::string& reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    if (command.name.empty()) return reject("Ship class name must be non-empty");
    if (command.role < ShipRole::Survey || command.role > ShipRole::Escort) {
        return reject("Ship class role is invalid");
    }
    const ShipClass* source = command.basedOnClassId.has_value()
        ? findShipClass(*command.basedOnClassId) : nullptr;
    if (command.basedOnClassId.has_value() && source == nullptr) {
        return reject("Base ship class does not exist");
    }
    const ShipDesignEvaluation design = evaluateShipDesign(state_.shipComponents, command.components);
    for (const std::string& constraint : design.constraints) {
        if (constraint != "Internal volume exceeds hull capacity") return reject(constraint);
    }
    if (source != nullptr && source->revision == std::numeric_limits<int>::max()) {
        return reject("Ship class revision limit reached");
    }
    const int revision = source == nullptr ? 1 : source->revision + 1;
    const ShipClassId id{state_.ids.nextShipClassId++};
    state_.shipClasses.push_back(ShipClass{
        .id = id, .name = command.name, .revision = revision, .role = command.role,
        .basedOnClassId = command.basedOnClassId, .components = command.components,
        .speedKmPerDay = source == nullptr ? 0.0 : source->speedKmPerDay
    });
    appendEvent(EventSeverity::Info, ShipClassRevisionCreatedEvent{
        .shipClassId = id, .basedOnClassId = command.basedOnClassId, .revision = revision
    });
    return CommandResult::success("Ship class revision created");
}

std::optional<std::string> Simulation::beginFleetMove(Fleet& fleet, const BodyId destination,
                                                       double& chargedFuel, std::vector<SimEvent>* emitted) {
    chargedFuel = 0.0;
    if (findBody(destination) == nullptr) return "Destination body does not exist";
    if (fleet.currentBodyId == destination) return "Fleet is already at destination body";
    if (fleet.activeOrder.type != FleetOrderType::None || fleet.destinationBodyId.has_value()) {
        return "Fleet already has an active order";
    }
    const BodyId origin = fleet.currentBodyId;
    const double cost = adjustedFleetMoveFuelCost(state_, fleet, origin, destination, state_.date.day);
    if (!std::isfinite(cost) || !fleetHasFuelFor(state_, fleet, cost)) {
        return "Fleet has insufficient fuel for move";
    }
    const FleetOrder plan = planFleetTransit(state_, origin, destination, state_.date.day);
    if (plan.type != FleetOrderType::MoveToBody || plan.daysRemaining <= 0) {
        return "Fleet route cannot be planned";
    }
    if (!consumeFleetFuel(state_, fleet, cost)) return "Fleet has insufficient fuel for move";
    chargedFuel = cost;
    fleet.destinationBodyId = destination;
    fleet.activeOrder = plan;
    const FleetOrderAssignedEvent assigned{
        .fleetId = fleet.id, .originBodyId = origin,
        .destinationBodyId = destination, .daysRemaining = plan.daysRemaining
    };
    if (emitted == nullptr) appendEvent(EventSeverity::Info, assigned);
    else emitEvent(*emitted, EventSeverity::Info, assigned);
    return std::nullopt;
}

bool Simulation::startProgramMove(const ProgramController programId, const FleetId fleetId,
                                  const BodyId destination, double& chargedFuel,
                                  std::vector<SimEvent>& emitted) {
    if (controllingProgram(state_, fleetId) != programId) return false;
    Fleet* fleet = findFleet(fleetId);
    if (fleet == nullptr || !fleet->queuedOrders.empty()) return false;
    return !beginFleetMove(*fleet, destination, chargedFuel, &emitted).has_value();
}

CommandResult Simulation::moveFleet(const MoveFleetCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }
    if (const auto owner = controllingProgram(state_, fleet->id)) {
        const std::string reason = "Fleet is controlled by " + programControllerLabel(state_, *owner);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    double chargedFuel = 0.0;
    if (const auto reason = beginFleetMove(*fleet, command.destinationBodyId, chargedFuel, nullptr)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{*reason});
        return CommandResult::failure(*reason);
    }
    return CommandResult::success("Fleet movement order accepted");
}

CommandResult Simulation::queueFleetMoveOrder(const QueueFleetMoveOrderCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (const auto owner = controllingProgram(state_, fleet->id)) {
        const std::string reason = "Fleet is controlled by " + programControllerLabel(state_, *owner);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    if (findBody(command.destinationBodyId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Destination body does not exist"});
        return CommandResult::failure("Destination body does not exist");
    }

    if (fleet->activeOrder.type == FleetOrderType::None && fleet->currentBodyId == command.destinationBodyId) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet is already at destination body"});
        return CommandResult::failure("Fleet is already at destination body");
    }

    const double projectedFuelRequired = queuedRouteFuelRequirement(state_, *fleet, command.destinationBodyId);
    if (!fleetHasFuelFor(state_, *fleet, projectedFuelRequired)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet has insufficient fuel for queued move"});
        return CommandResult::failure("Fleet has insufficient fuel for queued move");
    }

    fleet->queuedOrders.push_back(QueuedFleetOrder{
        .type = FleetOrderType::MoveToBody,
        .targetBodyId = command.destinationBodyId
    });

    // Idle fleets should not force the player to advance time before the first
    // queued order becomes the current order. Active fleets keep the new order in
    // the queue until movement completion starts it from the new location.
    if (fleet->activeOrder.type == FleetOrderType::None) {
        static_cast<void>(startNextQueuedFleetOrder(*fleet, nullptr));
        return CommandResult::success("Queued fleet move order started");
    }

    return CommandResult::success("Fleet move order queued");
}

CommandResult Simulation::clearFleetOrderQueue(const ClearFleetOrderQueueCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (const auto owner = controllingProgram(state_, fleet->id)) {
        const std::string reason = "Fleet is controlled by " + programControllerLabel(state_, *owner);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    fleet->queuedOrders.clear();
    return CommandResult::success("Fleet order queue cleared");
}

CommandResult Simulation::cancelFleetOrder(const CancelFleetOrderCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (const auto owner = controllingProgram(state_, fleet->id)) {
        const std::string reason = "Fleet is controlled by " + programControllerLabel(state_, *owner);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    if (fleet->activeOrder.type == FleetOrderType::None) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet has no active order"});
        return CommandResult::failure("Fleet has no active order");
    }

    // Map positions interpolate the route, but the logical currentBodyId stays
    // at departure until arrival. Cancellation clears the plan, returning the
    // display to that body; fuel paid at departure is not refunded.
    // FIXME: Remaining queued legs stay dormant because the daily movement pass
    // skips idle fleets. A later queue command can restart this retained route;
    // cancellation needs an explicit resume/clear policy before that is changed.
    fleet->destinationBodyId = std::nullopt;
    fleet->activeOrder = FleetOrder{};

    return CommandResult::success("Fleet order cancelled");
}

CommandResult Simulation::resourceSurvey(const ResourceSurveyCommand& command) {
    Fleet* fleet = findFleet(command.fleetId);
    if (fleet == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet does not exist"});
        return CommandResult::failure("Fleet does not exist");
    }

    if (const auto owner = controllingProgram(state_, fleet->id)) {
        const std::string reason = "Fleet is controlled by " + programControllerLabel(state_, *owner);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    if (findBody(command.bodyId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey target body does not exist"});
        return CommandResult::failure("Survey target body does not exist");
    }

    if (fleet->activeOrder.type != FleetOrderType::None || fleet->destinationBodyId.has_value()) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet must be stationary to survey"});
        return CommandResult::failure("Fleet must be stationary to survey");
    }

    if (fleet->currentBodyId != command.bodyId) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Fleet must be at survey target body"});
        return CommandResult::failure("Fleet must be at survey target body");
    }

    const auto survey = prepareSurveyDuty(state_, *fleet, 5.0);
    if (survey.usableCapability <= 0.0) {
        const std::string reason = survey.condition;
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    std::vector<InstrumentExposure> exposures;
    for (const auto& contributor : survey.contributors)
        accumulateObservationExposure(exposures,contributor,state_.date.day,5);
    // All sampler/date allocations happen before the physical duty debit.
    auto batch=prepareObservationBatch(state_,fleet->id,command.bodyId,exposures,{state_.date.day});
    state_.observations.reserve(state_.observations.size()+1);
    if(survey.changes.size()+1>static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()-state_.ids.nextEventId))
        throw std::runtime_error("Observation audit identity limit reached");
    state_.eventLog.reserve(state_.eventLog.size()+survey.changes.size()+1);
    const auto batchId=batch.id;
    applySurveyDuty(state_, survey);
    state_.observations.push_back(std::move(batch));
    ++state_.ids.nextObservationBatchId;
    for (const auto& change : survey.changes) {
        appendEvent(EventSeverity::Info, EquipmentDutyUsedEvent{
            fleet->id, change.shipId, change.componentId, std::nullopt, 5.0,
            change.beforeUsedDuty, change.afterUsedDuty});
    }
    appendEvent(EventSeverity::Info,ResourceSurveyCompletedEvent{
        .fleetId=fleet->id,.bodyId=command.bodyId,.observationBatchId=batchId});
    return CommandResult::success("Resource survey acquired raw observations; analysis is separate");
}

CommandResult Simulation::createSurveyProgram(const CreateSurveyProgramCommand& command) {
    const auto reject = [this](const std::string& reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    if (const auto reason = validateSurveyProgramCharter(state_, command.charter)) return reject(*reason);
    if (state_.ids.nextSurveyProgramId == std::numeric_limits<std::int64_t>::max()) {
        return reject("Survey program ID limit reached");
    }
    std::int64_t nextReportDay = 0;
    try { nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30); }
    catch (const std::exception&) { return reject("Survey reporting date limit reached"); }
    SurveyProgram program{
        .id = SurveyProgramId{state_.ids.nextSurveyProgramId},
        .charter = command.charter,
        .createdDay = state_.date.day,
        .nextReportDay = nextReportDay,
        .reportStartDay = state_.date.day
    };
    const SurveyProgramId id = program.id;
    state_.surveyPrograms.push_back(std::move(program));
    ++state_.ids.nextSurveyProgramId;
    acknowledgeKnownSurveyProgramLimitAtDecision(state_, state_.surveyPrograms.back());
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = id, .kind = SurveyProgramAuditKind::Authorized,
        .charterRevision = 1, .detail = "Survey charter authorized"
    });
    return CommandResult::success("Program authorized; " +
        surveyProgramExecutionCondition(state_, state_.surveyPrograms.back()));
}

CommandResult Simulation::amendSurveyProgram(const AmendSurveyProgramCommand& command) {
    const auto reject = [this](const std::string& reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    SurveyProgram* program = findById(state_.surveyPrograms, command.programId);
    if (program == nullptr) return reject("Survey program does not exist");
    const bool reopeningCompletion = program->lifecycle == SurveyProgramLifecycle::Closing &&
        program->closure == SurveyProgramClosure::Completed;
    if (program->lifecycle == SurveyProgramLifecycle::Closed ||
        (program->lifecycle == SurveyProgramLifecycle::Closing && !reopeningCompletion)) {
        return reject("Survey program is cancelled, closing, or closed");
    }
    if (const auto reason = validateSurveyProgramCharter(state_, command.charter)) return reject(*reason);
    if (program->charterRevision == std::numeric_limits<int>::max()) return reject("Charter revision limit reached");
    SurveyProgramCharter prepared = command.charter;
    if (prepared.policy.maintenanceProgramId != program->charter.policy.maintenanceProgramId ||
        prepared.requestedFleetId != program->charter.requestedFleetId ||
        prepared.requestedTeamId != program->charter.requestedTeamId || prepared.homeColonyId != program->charter.homeColonyId) {
        withdrawClientService(state_,program->id,"Client support or physical assignment amended",
            MaintenanceExecutionHooks{[this](EventSeverity severity,SimEventPayload event){appendEvent(severity,std::move(event));}});
    }
    const Fleet* leased = program->leasedFleetId ? findFleet(*program->leasedFleetId) : nullptr;
    const bool committed = program->task != SurveyProgramTask::None ||
        (leased != nullptr && leased->activeOrder.type == FleetOrderType::MoveToBody);
    if (committed && prepared.homeColonyId != program->charter.homeColonyId) {
        program->pendingHomeColonyId = prepared.homeColonyId;
        prepared.homeColonyId = program->charter.homeColonyId;
    } else {
        program->pendingHomeColonyId.reset();
    }
    std::swap(program->charter, prepared);
    ++program->charterRevision;
    if (reopeningCompletion) {
        // Completion still had an unfulfilled physical return. The new charter
        // reopens authority without changing the paid route, task, or lease.
        program->lifecycle = SurveyProgramLifecycle::Authorized;
        program->closure = SurveyProgramClosure::None;
    }
    // A new charter is a new decision context. Existing physical route/task
    // stays committed; the runner reevaluates once that activity stops safely.
    acknowledgeKnownSurveyProgramLimitAtDecision(state_, *program);
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program->id, .kind = SurveyProgramAuditKind::Amended,
        .charterRevision = program->charterRevision,
        .detail = program->pendingHomeColonyId ? "Survey charter amended; home change pending safe boundary"
                                                 : "Survey charter amended"
    });
    return CommandResult::success("Survey charter amended");
}

CommandResult Simulation::suspendSurveyProgram(const SuspendSurveyProgramCommand& command) {
    SurveyProgram* program = findById(state_.surveyPrograms, command.programId);
    const bool completionReturn = program != nullptr &&
        program->lifecycle == SurveyProgramLifecycle::Closing &&
        program->closure == SurveyProgramClosure::Completed;
    if (program == nullptr || (program->lifecycle != SurveyProgramLifecycle::Authorized && !completionReturn)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey program is not authorized or returning from completion"});
        return CommandResult::failure("Survey program is not authorized or returning from completion");
    }
    program->lifecycle = SurveyProgramLifecycle::Suspended;
    withdrawClientService(state_,program->id,"Client survey suspended",
        MaintenanceExecutionHooks{[this](EventSeverity severity,SimEventPayload event){appendEvent(severity,std::move(event));}});
    program->closure = SurveyProgramClosure::None;
    program->issue.acknowledged = true;
    static_cast<void>(releaseSurveyProgramLease(state_, *program));
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program->id, .kind = SurveyProgramAuditKind::Suspended,
        .charterRevision = program->charterRevision,
        .detail = program->leasedFleetId ? "Suspending after current transit" : "Survey program suspended"
    });
    return CommandResult::success(program->leasedFleetId
        ? "Suspending after current transit" : "Survey program suspended");
}

CommandResult Simulation::resumeSurveyProgram(const ResumeSurveyProgramCommand& command) {
    SurveyProgram* program = findById(state_.surveyPrograms, command.programId);
    if (program == nullptr || program->lifecycle != SurveyProgramLifecycle::Suspended) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey program is not suspended"});
        return CommandResult::failure("Survey program is not suspended");
    }
    program->lifecycle = SurveyProgramLifecycle::Authorized;
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program->id, .kind = SurveyProgramAuditKind::Resumed,
        .charterRevision = program->charterRevision, .detail = "Survey program resumed"
    });
    return CommandResult::success("Survey program resumed; readiness will be checked at the next day boundary");
}

CommandResult Simulation::cancelSurveyProgram(const CancelSurveyProgramCommand& command) {
    SurveyProgram* program = findById(state_.surveyPrograms, command.programId);
    if (program == nullptr || program->lifecycle == SurveyProgramLifecycle::Closed) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey program does not exist or is closed"});
        return CommandResult::failure("Survey program does not exist or is closed");
    }
    const Fleet* fleet = program->leasedFleetId ? findFleet(*program->leasedFleetId) : nullptr;
    const bool moving = fleet != nullptr && fleet->activeOrder.type == FleetOrderType::MoveToBody;
    withdrawClientService(state_,program->id,"Client survey cancelled",
        MaintenanceExecutionHooks{[this](EventSeverity severity,SimEventPayload event){appendEvent(severity,std::move(event));}});
    program->closure = SurveyProgramClosure::Cancelled;
    program->lifecycle = moving ? SurveyProgramLifecycle::Closing : SurveyProgramLifecycle::Closed;
    program->pendingHomeColonyId.reset();
    program->issue.acknowledged = true;
    if (!moving) static_cast<void>(releaseSurveyProgramLease(state_, *program));
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program->id, .kind = SurveyProgramAuditKind::CancelRequested,
        .charterRevision = program->charterRevision,
        .detail = moving ? "Cancellation pending current transit arrival" : "Survey program cancelled"
    });
    return CommandResult::success(moving ? "Cancelling after current transit" : "Survey program cancelled");
}

CommandResult Simulation::acknowledgeSurveyProgramIssue(const AcknowledgeSurveyProgramIssueCommand& command) {
    SurveyProgram* program = findById(state_.surveyPrograms, command.programId);
    if (program == nullptr || program->issue.signature.empty() || program->issue.acknowledged ||
        program->issue.signature != command.signature) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Survey program issue identity is not pending"});
        return CommandResult::failure("Survey program issue identity is not pending");
    }
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, SurveyProgramAuditEvent{
        .programId = program->id, .kind = SurveyProgramAuditKind::IssueAcknowledged,
        .charterRevision = program->charterRevision, .detail = command.signature
    });
    return CommandResult::success("Issue acknowledged; program keeps current limits");
}

CommandResult Simulation::createFreightProgram(const CreateFreightProgramCommand& command) {
    const auto reject = [this](const std::string& reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    if (const auto reason = validateFreightProgramCharter(state_, command.charter)) return reject(*reason);
    if (state_.ids.nextFreightProgramId == std::numeric_limits<std::int64_t>::max()) return reject("Freight program ID limit reached");
    FreightProgram prepared;
    prepared.id = FreightProgramId{state_.ids.nextFreightProgramId};
    prepared.charter = command.charter;
    prepared.createdDay = state_.date.day;
    prepared.reportStartDay = state_.date.day;
    try { prepared.nextReportDay = nextGlobalSurveyBoundary(state_.date.day, 30); }
    catch (const std::exception&) { return reject("Freight reporting date limit reached"); }
    acknowledgeKnownFreightLimitAtDecision(state_, prepared);
    state_.freightPrograms.push_back(std::move(prepared));
    ++state_.ids.nextFreightProgramId;
    const auto& program = state_.freightPrograms.back();
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program.id, .kind = FreightProgramAuditKind::Authorized,
        .detail = "Freight charter authorized; no stock reserved"
    });
    return CommandResult::success("Freight program authorized; " + freightProgramExecutionCondition(state_, program));
}

CommandResult Simulation::amendFreightProgram(const AmendFreightProgramCommand& command) {
    const auto reject = [this](const std::string& reason) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    };
    auto* program = findById(state_.freightPrograms, command.programId);
    if (program == nullptr || program->lifecycle == FreightProgramLifecycle::Closed) return reject("Freight program does not exist or is closed");
    if (program->charterRevision == std::numeric_limits<int>::max()) return reject("Freight charter revision limit reached");
    auto prepared = program->charter;
    applyFreightAmendment(prepared, command.amendment);
    if (const auto reason = validateFreightProgramCharter(state_, prepared, true)) return reject(*reason);
    std::swap(program->charter, prepared);
    ++program->charterRevision;
    // Closing remains physical work: reopen future authorization while keeping
    // its committed return. Cancellation settlement is never silently renewed.
    if (program->closure == FreightProgramClosure::Completed) {
        program->closure = FreightProgramClosure::None;
        if (program->lifecycle == FreightProgramLifecycle::Closing) program->lifecycle = FreightProgramLifecycle::Authorized;
    }
    acknowledgeKnownFreightLimitAtDecision(state_, *program);
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program->id, .kind = FreightProgramAuditKind::Amended,
        .charterRevision = program->charterRevision, .detail = "Freight charter amended; current shipment retained"
    });
    return CommandResult::success("Freight charter amended");
}

CommandResult Simulation::suspendFreightProgram(const SuspendFreightProgramCommand& command) {
    auto* program = findById(state_.freightPrograms, command.programId);
    if (program == nullptr || program->lifecycle == FreightProgramLifecycle::Closed ||
        program->lifecycle == FreightProgramLifecycle::Suspended) {
        const std::string reason = "Freight program cannot be suspended in this state";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    program->lifecycle = FreightProgramLifecycle::Suspended;
    program->issue.acknowledged = true;
    static_cast<void>(releaseFreightProgramLease(state_, *program));
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program->id, .kind = FreightProgramAuditKind::Suspended,
        .charterRevision = program->charterRevision,
        .detail = "Suspended; cargo custody and paid transit retained"
    });
    return CommandResult::success(freightProgramExecutionCondition(state_, *program));
}

CommandResult Simulation::resumeFreightProgram(const ResumeFreightProgramCommand& command) {
    auto* program = findById(state_.freightPrograms, command.programId);
    if (program == nullptr || program->lifecycle != FreightProgramLifecycle::Suspended) {
        const std::string reason = "Freight program is not suspended";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    program->lifecycle = program->closure == FreightProgramClosure::None
        ? FreightProgramLifecycle::Authorized : FreightProgramLifecycle::Closing;
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program->id, .kind = FreightProgramAuditKind::Resumed,
        .charterRevision = program->charterRevision, .detail = "Freight authority resumed with retained disposition"
    });
    return CommandResult::success("Freight program resumed; readiness checked at the next opening boundary");
}

CommandResult Simulation::cancelFreightProgram(const CancelFreightProgramCommand& command) {
    auto* program = findById(state_.freightPrograms, command.programId);
    if (program == nullptr || program->lifecycle == FreightProgramLifecycle::Closed) {
        const std::string reason = "Freight program does not exist or is closed";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    program->closure = FreightProgramClosure::Cancelled;
    program->lifecycle = FreightProgramLifecycle::Closing;
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program->id, .kind = FreightProgramAuditKind::Cancelled,
        .charterRevision = program->charterRevision,
        .detail = "Future pickups cancelled; existing physical cargo and transit will settle"
    });
    const FreightProgramExecutionHooks hooks{
        .startProgramMove = {},
        .emit = [this](EventSeverity severity, SimEventPayload payload) { appendEvent(severity, std::move(payload)); }
    };
    settleFreightCancellationAtDecision(state_, *program, hooks);
    return CommandResult::success("Future pickups cancelled; " + freightProgramExecutionCondition(state_, *program));
}

CommandResult Simulation::acknowledgeFreightProgramIssue(const AcknowledgeFreightProgramIssueCommand& command) {
    auto* program = findById(state_.freightPrograms, command.programId);
    if (program == nullptr || program->issue.signature.empty() || program->issue.acknowledged ||
        program->issue.signature != command.signature) {
        const std::string reason = "Freight program issue identity is not pending";
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }
    program->issue.acknowledged = true;
    appendEvent(EventSeverity::Info, FreightProgramAuditEvent{
        .programId = program->id, .kind = FreightProgramAuditKind::IssueAcknowledged,
        .charterRevision = program->charterRevision, .detail = command.signature
    });
    return CommandResult::success("Freight issue acknowledged; existing limits retained");
}

CommandResult Simulation::assignAppointment(const AssignAppointmentCommand& command) {
    if (!isValidAppointmentRole(command.role)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment role is invalid"});
        return CommandResult::failure("Appointment role is invalid");
    }

    if (!isValidAppointmentScopeType(command.scopeType)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment scope type is invalid"});
        return CommandResult::failure("Appointment scope type is invalid");
    }

    if (command.scopeId <= 0) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment scope ID must be positive"});
        return CommandResult::failure("Appointment scope ID must be positive");
    }

    const Person* person = findPerson(command.personId);
    if (person == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment person does not exist"});
        return CommandResult::failure("Appointment person does not exist");
    }

    if (findInstitution(person->institutionId) == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment person institution does not exist"});
        return CommandResult::failure("Appointment person institution does not exist");
    }

    const bool scopeExists = [this, &command] {
        switch (command.scopeType) {
        case AppointmentScopeType::Fleet:
            return findFleet(FleetId{command.scopeId}) != nullptr;
        case AppointmentScopeType::Colony:
            return findColony(ColonyId{command.scopeId}) != nullptr;
        case AppointmentScopeType::Institution:
            return findInstitution(InstitutionId{command.scopeId}) != nullptr;
        }
        return false;
    }();

    if (!scopeExists) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Appointment target scope does not exist"});
        return CommandResult::failure("Appointment target scope does not exist");
    }

    const auto sameSlot = [&command](const Appointment& appointment) {
        return appointment.role == command.role &&
               appointment.scopeType == command.scopeType &&
               appointment.scopeId == command.scopeId;
    };

    const auto it = std::find_if(state_.appointments.begin(), state_.appointments.end(), sameSlot);
    if (it != state_.appointments.end()) {
        // Appointments represent current slots, not history yet. Reassigning a
        // slot updates the responsible person while preserving the uniqueness
        // invariant that validation enforces for imported/saved states.
        it->personId = command.personId;
        it->appointedDay = state_.date.day;
    } else {
        state_.appointments.push_back(Appointment{
            .role = command.role,
            .scopeType = command.scopeType,
            .scopeId = command.scopeId,
            .personId = command.personId,
            .appointedDay = state_.date.day
        });
    }

    return CommandResult::success("Appointment assigned");
}

CommandResult Simulation::setColonyProcessingPolicy(const SetColonyProcessingPolicyCommand& command) {
    Colony* colony = findColony(command.colonyId);
    if (colony == nullptr) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Colony does not exist"});
        return CommandResult::failure("Colony does not exist");
    }

    if (!isValidProcessingPolicy(command.policy)) {
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{"Processing policy is invalid"});
        return CommandResult::failure("Processing policy is invalid");
    }

    const ProcessingAllocationResult allocation = evaluateProcessingAllocations(
        command.manualAllocations, command.policy == ProcessingPolicy::Manual);
    if (!allocation.valid()) {
        const char* reason = processingAllocationRejection(allocation.error);
        appendEvent(EventSeverity::Warning, CommandRejectedEvent{reason});
        return CommandResult::failure(reason);
    }

    // Prepare every allocating success value before either field changes.
    // Vector swap and enum assignment are non-throwing for this allocator; this
    // is a focused command guarantee, not global rollback for all commands.
    std::vector<ProcessingAllocation> preparedManual;
    if (command.policy == ProcessingPolicy::Manual) {
        // Manual weights are persistent player intent. Preset policies derive
        // their own weights every day, so do not overwrite the last manual
        // setup when the player temporarily switches to a preset.
        preparedManual = command.manualAllocations;
    }
    CommandResult success = CommandResult::success("Colony processing policy updated");
    static_assert(std::is_nothrow_swappable_v<std::vector<ProcessingAllocation>>);
    static_assert(std::is_nothrow_move_constructible_v<CommandResult>);
    if (command.policy == ProcessingPolicy::Manual) {
        colony->manualProcessingAllocations.swap(preparedManual);
    }
    colony->processingPolicy = command.policy;
    return success;
}

bool Simulation::startNextQueuedFleetOrder(Fleet& fleet, std::vector<SimEvent>* emitted) {
    while (!fleet.queuedOrders.empty()) {
        const QueuedFleetOrder queuedOrder = fleet.queuedOrders.front();
        fleet.queuedOrders.erase(fleet.queuedOrders.begin());

        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            // Defensive repair for imported/future states. Validation rejects
            // malformed queues, but this keeps runtime robust if a queue is
            // mutated before validation has a chance to run.
            const CommandRejectedEvent rejected{"Queued fleet order was invalid"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const BodyId destinationBodyId = *queuedOrder.targetBodyId;
        if (findBody(destinationBodyId) == nullptr) {
            const CommandRejectedEvent rejected{"Queued fleet order referenced missing destination body"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        if (fleet.currentBodyId == destinationBodyId) {
            const CommandRejectedEvent rejected{"Queued fleet order already at destination body"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const double fuelCost = adjustedFleetMoveFuelCost(state_, fleet, fleet.currentBodyId, destinationBodyId, state_.date.day);
        if (!consumeFleetFuel(state_, fleet, fuelCost)) {
            const CommandRejectedEvent rejected{"Queued fleet order lacked sufficient fuel"};
            if (emitted == nullptr) {
                appendEvent(EventSeverity::Warning, rejected);
            } else {
                emitEvent(*emitted, EventSeverity::Warning, rejected);
            }
            continue;
        }

        const BodyId originBodyId = fleet.currentBodyId;
        FleetOrder plannedOrder = planFleetTransit(state_, originBodyId, destinationBodyId, state_.date.day);
        fleet.destinationBodyId = destinationBodyId;
        fleet.activeOrder = plannedOrder;

        FleetOrderAssignedEvent assigned{
            .fleetId = fleet.id,
            .originBodyId = originBodyId,
            .destinationBodyId = destinationBodyId,
            .daysRemaining = plannedOrder.daysRemaining
        };
        if (emitted == nullptr) {
            appendEvent(EventSeverity::Info, assigned);
        } else {
            emitEvent(*emitted, EventSeverity::Info, assigned);
        }
        return true;
    }

    return false;
}

void Simulation::simulateOneDay(std::vector<SimEvent>& emitted) {
    ++state_.date.day;
    OpeningProgramContext opening(state_);
    const SurveyProgramExecutionHooks programHooks{
        .startProgramMove = [this, &emitted](const SurveyProgramId programId, const FleetId fleetId,
                                              const BodyId destination, double& chargedFuel) {
            return startProgramMove(programId, fleetId, destination, chargedFuel, emitted);
        },
        .emit = [this, &emitted](const EventSeverity severity, SimEventPayload payload) {
            emitEvent(emitted, severity, std::move(payload));
        },
        .opening = &opening,
        .prepareEvents = [this,&emitted](std::size_t count) {
            if(count>static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()-state_.ids.nextEventId))
                throw std::runtime_error("Scientific audit identity limit reached");
            state_.eventLog.reserve(state_.eventLog.size()+count);
            emitted.reserve(emitted.size()+count);
        }
    };
    const FreightProgramExecutionHooks freightHooks{
        .startProgramMove = [this, &emitted](FreightProgramId id, FleetId fleet, BodyId body, double& fuel) {
            return startProgramMove(id, fleet, body, fuel, emitted);
        },
        .emit = programHooks.emit
    };
    const MaintenanceExecutionHooks maintenanceHooks{programHooks.emit};
    // Compare only unvisited vector heads. Released assets and inbound stock
    // stay unavailable to later programs until the next opening boundary.
    for (const auto& owner : programOpeningOrder(state_)) {
        std::visit([&](auto id) {
            if constexpr (std::is_same_v<decltype(id), SurveyProgramId>) {
                runSurveyProgramOpeningDay(state_, *findById(state_.surveyPrograms, id), opening, programHooks);
            } else if constexpr (std::is_same_v<decltype(id), FreightProgramId>) {
                runFreightProgramOpeningDay(state_, *findById(state_.freightPrograms, id), opening, freightHooks);
            } else if constexpr (std::is_same_v<decltype(id), MaintenanceProgramId>) {
                runMaintenanceProgramOpeningDay(state_, *findById(state_.maintenancePrograms, id), opening, maintenanceHooks);
            } else {
                runAnalysisOpeningDay(state_,*findById(state_.analysisPrograms,id),opening,{programHooks.emit,programHooks.prepareEvents});
            }
        }, owner);
    }

    // Ordering is gameplay: today's mining feeds today's processing, and its
    // output can pay for today's ship completions. New fleets remain idle until
    // a later command, while existing arrivals can start their next queued leg.
    // All telemetry/events below carry the newly advanced simulation day.
    simulateMining(emitted);
    simulateProcessing();
    simulateShipyards(emitted);
    simulateFleetMovement(emitted);
    finishSurveyProgramsDay(state_, programHooks);
    finishFreightProgramsDay(state_, freightHooks);
    finishMaintenanceProgramsDay(state_, maintenanceHooks);
    finishAnalysisDay(state_,{programHooks.emit,programHooks.prepareEvents});
}

void Simulation::simulateMining(std::vector<SimEvent>&) {
    for (Colony& colony : state_.colonies) {
        for (MineralDeposit& deposit : state_.mineralDeposits) {
            if (deposit.bodyId != colony.bodyId || deposit.remaining <= 0.0) {
                continue;
            }

            // Each mine contributes one base unit per day to every local deposit,
            // scaled by accessibility and capped by remaining material. Observations
            // affect player knowledge only; mining never requires investigation.
            const double potentialExtraction = colony.mines * deposit.accessibility;
            const double extracted = std::min(deposit.remaining, std::max(0.0, potentialExtraction));
            if (extracted <= 0.0) {
                continue;
            }

            deposit.remaining -= extracted;
            colony.stockpile.add(deposit.mineral, extracted);

            // Mining is routine runtime telemetry, not persisted audit history.
            // Store one append-only row per extracted mineral for current-session
            // UI/forecast/debug views without flooding the player-facing event log.
            state_.dailyEconomySnapshots.push_back(DailyEconomySnapshot{
                .day = state_.date.day,
                .colonyId = colony.id,
                .bodyId = colony.bodyId,
                .mineral = deposit.mineral,
                .amount = extracted,
                .remainingDeposit = deposit.remaining
            });
        }
    }
}

void Simulation::simulateProcessing() {
    const std::vector<ProcessingRecipe> recipes = processingRecipes();

    for (Colony& colony : state_.colonies) {
        const double dailyCapacity = std::max(0.0, colony.processorCapacity);
        if (dailyCapacity <= 0.0) {
            continue;
        }

        const ProcessingShares shares = normalizedProcessingShares(colony);
        for (const ProcessingRecipe& recipe : recipes) {
            const double targetOutput = dailyCapacity * shares[processedMaterialIndex(recipe.output)];
            if (targetOutput <= kProcessedMaterialComparisonEpsilon) {
                continue;
            }

            // Capacity shares are fixed for this day. Missing inputs reduce this
            // recipe's output; unused capacity is not redistributed to others.
            // Only the colony's local raw stockpile is available. Research,
            // building upgrades, and inter-body supply are outside this model.
            const double producible = std::min(targetOutput, maxRecipeOutput(colony.stockpile, recipe.rawCostPerUnit));
            if (producible <= kMineralComparisonEpsilon) {
                continue;
            }

            colony.stockpile.subtract(scaledMineralCost(recipe.rawCostPerUnit, producible));
            colony.processedStockpile.add(recipe.output, producible);
        }
    }
}

void Simulation::simulateShipyards(std::vector<SimEvent>& emitted) {
    constexpr double kBuildPointEpsilon = 1.0e-9;

    struct ShipyardCapacityPool {
        ColonyId colonyId;
        double remainingBuildPoints = 0.0;
    };

    std::vector<ShipyardCapacityPool> capacityPools;
    capacityPools.reserve(state_.colonies.size());
    for (const Colony& colony : state_.colonies) {
        // Each colony has exactly one daily shipyard pool. Orders draw down this
        // pool in persistent order-vector order, which gives deterministic FIFO
        // behavior without adding a separate production-queue type yet.
        capacityPools.push_back(ShipyardCapacityPool{
            .colonyId = colony.id,
            .remainingBuildPoints = effectiveShipyardCapacity(state_, colony)
        });
    }

    for (ShipyardOrder& order : state_.shipyardOrders) {
        if (order.status != ShipyardOrderStatus::Active) {
            continue;
        }

        Colony* colony = findColony(order.colonyId);
        const ShipClass* shipClass = findShipClass(order.shipClassId);
        if (colony == nullptr || shipClass == nullptr) {
            // Valid GameState snapshots cannot contain dangling production
            // references. If defensive runtime code still encounters one, skip
            // the order without inventing a persistent zombie status.
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order references missing colony or ship class"});
            continue;
        }

        const auto poolIt = std::find_if(capacityPools.begin(), capacityPools.end(), [colony](const ShipyardCapacityPool& pool) {
            return pool.colonyId == colony->id;
        });
        if (poolIt == capacityPools.end()) {
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order references missing colony capacity pool"});
            continue;
        }

        const ShipDesignEvaluation design = evaluateShipDesign(state_.shipComponents, shipClass->components);
        if (!design.constructible) {
            // An immutable invalid-volume revision remains ordered intent. It
            // holds FIFO position without inventing build points or warnings.
            poolIt->remainingBuildPoints = 0.0;
            continue;
        }

        // Complete as many ships as the order's existing progress plus this
        // colony's remaining daily capacity allows. When this order still needs
        // build points, it consumes the colony pool before later orders at the
        // same colony can receive any capacity.
        while (order.quantityCompleted < order.quantityRequested) {
            if (order.accumulatedBuildPoints + kBuildPointEpsilon < design.buildPoints) {
                if (poolIt->remainingBuildPoints <= kBuildPointEpsilon) {
                    break;
                }

                const double buildPointsNeeded = design.buildPoints - order.accumulatedBuildPoints;
                const double allocatedBuildPoints = std::min(poolIt->remainingBuildPoints, buildPointsNeeded);
                order.accumulatedBuildPoints += allocatedBuildPoints;
                poolIt->remainingBuildPoints -= allocatedBuildPoints;

                if (order.accumulatedBuildPoints + kBuildPointEpsilon < design.buildPoints) {
                    break;
                }
            }

            if (!colony->processedStockpile.canPay(design.buildCost)) {
                // Processed-material shortages are temporary production pauses.
                // Keep the order Active so future processing can satisfy the
                // cost and complete the ship automatically. The blocked FIFO
                // order also holds the queue for this colony today;
                // later orders should not leapfrog a material-starved order.
                poolIt->remainingBuildPoints = 0.0;
                emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Shipyard order waiting for sufficient processed materials"});
                break;
            }

            colony->processedStockpile.subtract(design.buildCost);
            order.accumulatedBuildPoints -= design.buildPoints;
            ++order.quantityCompleted;

            const double transferredPropellant = std::min(design.propellantCapacity,
                colony->processedStockpile.get(ProcessedMaterial::Propellant));
            colony->processedStockpile.set(ProcessedMaterial::Propellant,
                colony->processedStockpile.get(ProcessedMaterial::Propellant) - transferredPropellant);

            const FleetId fleetId = allocateFleetId();
            const ShipId shipId = allocateShipId();

            Fleet fleet{
                .id = fleetId,
                .name = idLabel(shipClass->name + " Fleet", fleetId.value),
                .currentBodyId = colony->bodyId,
                .destinationBodyId = std::nullopt,
                .shipIds = {shipId},
                .activeOrder = FleetOrder{},
                .queuedOrders = {},
                .ownerInstitutionId = colony->ownerInstitutionId
            };

            Ship ship{
                .id = shipId,
                .shipClassId = shipClass->id,
                .name = idLabel(shipClass->name, shipId.value),
                .fleetId = fleetId,
                .fuel = transferredPropellant
            };
            initializeShipEquipmentCondition(state_, ship);

            state_.fleets.push_back(std::move(fleet));
            state_.ships.push_back(std::move(ship));

            emitEvent(emitted, EventSeverity::Info, ShipCompletedEvent{
                .orderId = order.id,
                .colonyId = colony->id,
                .shipId = shipId,
                .fleetId = fleetId,
                .shipClassId = shipClass->id
            });
        }

        if (order.quantityCompleted >= order.quantityRequested) {
            order.status = ShipyardOrderStatus::Completed;
            order.accumulatedBuildPoints = 0.0;
        }
    }
}

void Simulation::simulateFleetMovement(std::vector<SimEvent>& emitted) {
    for (Fleet& fleet : state_.fleets) {
        if (fleet.activeOrder.type != FleetOrderType::MoveToBody) {
            continue;
        }

        if (!fleet.activeOrder.targetBodyId.has_value()) {
            // Defensive repair for corrupted or future-loaded state: an active
            // movement order without a target cannot be completed safely.
            fleet.activeOrder = FleetOrder{};
            fleet.destinationBodyId = std::nullopt;
            emitEvent(emitted, EventSeverity::Warning, CommandRejectedEvent{"Fleet move order had no target body"});
            continue;
        }

        if (fleet.activeOrder.arrivalDay > state_.date.day) {
            fleet.activeOrder.daysRemaining = static_cast<int>(fleet.activeOrder.arrivalDay - state_.date.day);
            continue;
        }

        const BodyId destination = *fleet.activeOrder.targetBodyId;
        fleet.currentBodyId = destination;
        fleet.destinationBodyId = std::nullopt;
        fleet.activeOrder = FleetOrder{};

        emitEvent(emitted, EventSeverity::Info, FleetArrivedEvent{
            .fleetId = fleet.id,
            .destinationBodyId = destination
        });

        // Starting the next queued order on the same tick keeps the queue
        // actionable: a completed current order immediately promotes the
        // next queued move and records the promotion in the event log.
        static_cast<void>(startNextQueuedFleetOrder(fleet, &emitted));
    }
}

SimEvent Simulation::makeEvent(const EventSeverity severity, SimEventPayload payload) {
    return SimEvent{
        .id = allocateEventId(),
        .day = state_.date.day,
        .severity = severity,
        .payload = std::move(payload)
    };
}

void Simulation::emitEvent(std::vector<SimEvent>& emitted, const EventSeverity severity, SimEventPayload payload) {
    SimEvent event = makeEvent(severity, std::move(payload));

    // Store a copy in the authoritative log and return the emitted event by value
    // to the caller. This preserves the emitted audit history without exposing
    // mutable references; the log is not a replay stream of every state mutation.
    state_.eventLog.push_back(event);
    emitted.push_back(std::move(event));
}

void Simulation::appendEvent(const EventSeverity severity, SimEventPayload payload) {
    SimEvent event = makeEvent(severity, std::move(payload));
    state_.eventLog.push_back(std::move(event));
}

Colony* Simulation::findColony(const ColonyId id) noexcept {
    return findById(state_.colonies, id);
}

const Colony* Simulation::findColony(const ColonyId id) const noexcept {
    return findById(state_.colonies, id);
}

Body* Simulation::findBody(const BodyId id) noexcept {
    return findById(state_.bodies, id);
}

const Body* Simulation::findBody(const BodyId id) const noexcept {
    return findById(state_.bodies, id);
}

ShipClass* Simulation::findShipClass(const ShipClassId id) noexcept {
    return findById(state_.shipClasses, id);
}

const ShipClass* Simulation::findShipClass(const ShipClassId id) const noexcept {
    return findById(state_.shipClasses, id);
}

Fleet* Simulation::findFleet(const FleetId id) noexcept {
    return findById(state_.fleets, id);
}

const Fleet* Simulation::findFleet(const FleetId id) const noexcept {
    return findById(state_.fleets, id);
}

const Institution* Simulation::findInstitution(const InstitutionId id) const noexcept {
    return findById(state_.institutions, id);
}

const Person* Simulation::findPerson(const PersonId id) const noexcept {
    return findById(state_.people, id);
}

ShipyardOrderId Simulation::allocateShipyardOrderId() noexcept {
    return ShipyardOrderId{state_.ids.nextShipyardOrderId++};
}

ShipId Simulation::allocateShipId() noexcept {
    return ShipId{state_.ids.nextShipId++};
}

FleetId Simulation::allocateFleetId() noexcept {
    return FleetId{state_.ids.nextFleetId++};
}

EventId Simulation::allocateEventId() noexcept {
    return EventId{state_.ids.nextEventId++};
}

} // namespace deep
