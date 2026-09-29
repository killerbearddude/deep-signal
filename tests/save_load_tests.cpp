#include "app/SimulationService.h"
#include "save/Database.h"
#include "save/SaveGameRepository.h"
#include "sim/Commands.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"
#include "sim/TransitPlanning.h"

// Regression tests for SQLite save/load round-tripping.
// These tests verify that the current schema persists durable Prototype 0.1 state,
// including ID counters, institutions, ownership, production, fleet orders, and events.
// Runtime-only economy telemetry is tested separately as intentionally transient.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <unistd.h>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

bool almostEqual(const double lhs, const double rhs) noexcept {
    constexpr double kEpsilon = 1.0e-9;
    return std::fabs(lhs - rhs) <= kEpsilon;
}

template <typename IdT>
bool sameOptionalId(const std::optional<IdT> lhs, const std::optional<IdT> rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || lhs->value == rhs->value;
}

bool sameMineralSet(const deep::MineralSet& lhs, const deep::MineralSet& rhs) noexcept {
    for (std::size_t i = 0; i < deep::mineralCount(); ++i) {
        if (!almostEqual(lhs.amount.at(i), rhs.amount.at(i))) {
            return false;
        }
    }
    return true;
}

bool sameProcessedMaterialSet(const deep::ProcessedMaterialSet& lhs, const deep::ProcessedMaterialSet& rhs) noexcept {
    for (std::size_t i = 0; i < deep::processedMaterialCount(); ++i) {
        if (!almostEqual(lhs.amount.at(i), rhs.amount.at(i))) {
            return false;
        }
    }
    return true;
}

bool sameProcessingAllocations(const std::vector<deep::ProcessingAllocation>& lhs,
                               const std::vector<deep::ProcessingAllocation>& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs.at(i).material != rhs.at(i).material || !almostEqual(lhs.at(i).weight, rhs.at(i).weight)) {
            return false;
        }
    }
    return true;
}

bool samePayload(const deep::SimEventPayload& lhs, const deep::SimEventPayload& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }

    return std::visit([](const auto& left, const auto& right) -> bool {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) {
            return false;
        } else if constexpr (std::is_same_v<Left, deep::MineralExtractedEvent>) {
            return left.colonyId == right.colonyId &&
                   left.bodyId == right.bodyId &&
                   left.mineral == right.mineral &&
                   almostEqual(left.amount, right.amount) &&
                   almostEqual(left.remainingDeposit, right.remainingDeposit);
        } else if constexpr (std::is_same_v<Left, deep::ShipyardOrderCreatedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipClassId == right.shipClassId &&
                   left.quantity == right.quantity;
        } else if constexpr (std::is_same_v<Left, deep::ShipClassRevisionCreatedEvent>) {
            return left.shipClassId == right.shipClassId &&
                   left.basedOnClassId == right.basedOnClassId &&
                   left.revision == right.revision;
        } else if constexpr (std::is_same_v<Left, deep::ShipCompletedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipId == right.shipId &&
                   left.fleetId == right.fleetId &&
                   left.shipClassId == right.shipClassId;
        } else if constexpr (std::is_same_v<Left, deep::FleetOrderAssignedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.originBodyId == right.originBodyId &&
                   left.destinationBodyId == right.destinationBodyId &&
                   left.daysRemaining == right.daysRemaining;
        } else if constexpr (std::is_same_v<Left, deep::FleetArrivedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.destinationBodyId == right.destinationBodyId;
        } else if constexpr (std::is_same_v<Left, deep::ResourceSurveyCompletedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.bodyId == right.bodyId &&
                   left.depositsImproved == right.depositsImproved &&
                   almostEqual(left.averageConfidenceBefore, right.averageConfidenceBefore) &&
                   almostEqual(left.averageConfidenceAfter, right.averageConfidenceAfter);
        } else if constexpr (std::is_same_v<Left, deep::SurveyProgramAuditEvent>) {
            return left.programId == right.programId && left.kind == right.kind &&
                   left.fleetId == right.fleetId && left.bodyId == right.bodyId &&
                   left.leaderId == right.leaderId && left.approach == right.approach &&
                   left.charterRevision == right.charterRevision &&
                   left.passNumber == right.passNumber &&
                   almostEqual(left.fuelAmount, right.fuelAmount) && left.detail == right.detail;
        } else if constexpr (std::is_same_v<Left, deep::FreightProgramAuditEvent>) {
            return left.programId == right.programId && left.kind == right.kind &&
                   left.fleetId == right.fleetId && left.colonyId == right.colonyId &&
                   left.leaderId == right.leaderId && left.charterRevision == right.charterRevision &&
                   left.shipmentNumber == right.shipmentNumber &&
                   almostEqual(left.amount, right.amount) && left.detail == right.detail;
        } else if constexpr (std::is_same_v<Left, deep::CommandRejectedEvent>) {
            return left.reason == right.reason;
        }
    }, lhs, rhs);
}

void requireSameState(const deep::GameState& expected, const deep::GameState& actual) {
    // Verifies table-by-table persistence fidelity. These checks intentionally
    // compare many primitive fields because schema regressions often drop only
    // one column while leaving row counts correct. dailyEconomySnapshots is not
    // compared here because it is explicitly runtime-only telemetry.
    require(expected.date.day == actual.date.day, "date day round-trips");
    require(expected.ids.nextStarSystemId == actual.ids.nextStarSystemId, "star-system counter round-trips");
    require(expected.ids.nextBodyId == actual.ids.nextBodyId, "body counter round-trips");
    require(expected.ids.nextColonyId == actual.ids.nextColonyId, "colony counter round-trips");
    require(expected.ids.nextInstitutionId == actual.ids.nextInstitutionId, "institution counter round-trips");
    require(expected.ids.nextPersonId == actual.ids.nextPersonId, "person counter round-trips");
    require(expected.ids.nextFreightProgramId == actual.ids.nextFreightProgramId, "freight program counter round-trips");
    require(expected.ids.nextShipClassId == actual.ids.nextShipClassId, "ship-class counter round-trips");
    require(expected.ids.nextShipComponentId == actual.ids.nextShipComponentId, "component counter round-trips");
    require(expected.ids.nextShipyardOrderId == actual.ids.nextShipyardOrderId, "shipyard-order counter round-trips");
    require(expected.ids.nextShipId == actual.ids.nextShipId, "ship counter round-trips");
    require(expected.ids.nextFleetId == actual.ids.nextFleetId, "fleet counter round-trips");
    require(expected.ids.nextEventId == actual.ids.nextEventId, "event counter round-trips");
    require(expected.ids.nextSurveyProgramId == actual.ids.nextSurveyProgramId,
            "survey program counter round-trips");
    require(expected.ids.nextSurveyTeamId == actual.ids.nextSurveyTeamId,
            "survey team counter round-trips");

    require(expected.starSystems.size() == actual.starSystems.size(), "star-system row count round-trips");
    for (std::size_t i = 0; i < expected.starSystems.size(); ++i) {
        require(expected.starSystems.at(i).id == actual.starSystems.at(i).id, "star-system ID round-trips");
        require(expected.starSystems.at(i).name == actual.starSystems.at(i).name, "star-system name round-trips");
    }

    require(expected.institutions.size() == actual.institutions.size(), "institution row count round-trips");
    for (std::size_t i = 0; i < expected.institutions.size(); ++i) {
        const deep::Institution& left = expected.institutions.at(i);
        const deep::Institution& right = actual.institutions.at(i);
        require(left.id == right.id, "institution ID round-trips");
        require(left.name == right.name, "institution name round-trips");
        require(left.type == right.type, "institution type round-trips");
    }

    require(expected.people.size() == actual.people.size(), "person row count round-trips");
    for (std::size_t i = 0; i < expected.people.size(); ++i) {
        const deep::Person& left = expected.people.at(i);
        const deep::Person& right = actual.people.at(i);
        require(left.id == right.id, "person ID round-trips");
        require(left.name == right.name, "person name round-trips");
        require(left.institutionId == right.institutionId, "person institution reference round-trips");
        require(left.competencies.logistics == right.competencies.logistics, "person logistics competency round-trips");
        require(left.competencies.industry == right.competencies.industry, "person industry competency round-trips");
        require(left.competencies.survey == right.competencies.survey, "person survey competency round-trips");
        require(left.competencies.command == right.competencies.command, "person command competency round-trips");
        require(left.competencies.administration == right.competencies.administration,
                "person administration competency round-trips");
        require(left.competencies.engineering == right.competencies.engineering,
                "person engineering competency round-trips");
        require(left.competencies.intelligence == right.competencies.intelligence,
                "person intelligence competency round-trips");
        require(left.competencies.crisisManagement == right.competencies.crisisManagement,
                "person crisis-management competency round-trips");
        require(left.seniorityLevel == right.seniorityLevel, "person seniority level round-trips");
        require(left.serviceRecord.successfulAssignments == right.serviceRecord.successfulAssignments,
                "person successful assignment count round-trips");
        require(left.serviceRecord.failedAssignments == right.serviceRecord.failedAssignments,
                "person failed assignment count round-trips");
        require(left.serviceRecord.commendations == right.serviceRecord.commendations,
                "person commendation count round-trips");
        require(left.serviceRecord.controversies == right.serviceRecord.controversies,
                "person controversy count round-trips");
        require(left.surveyPlanningApproach == right.surveyPlanningApproach,
                "person survey planning approach round-trips");
    }

    require(expected.appointments.size() == actual.appointments.size(), "appointment row count round-trips");
    for (std::size_t i = 0; i < expected.appointments.size(); ++i) {
        const deep::Appointment& left = expected.appointments.at(i);
        const deep::Appointment& right = actual.appointments.at(i);
        require(left.role == right.role, "appointment role round-trips");
        require(left.scopeType == right.scopeType, "appointment scope type round-trips");
        require(left.scopeId == right.scopeId, "appointment scope ID round-trips");
        require(left.personId == right.personId, "appointment person ID round-trips");
        require(left.appointedDay == right.appointedDay, "appointment day round-trips");
    }

    require(expected.bodies.size() == actual.bodies.size(), "body row count round-trips");
    for (std::size_t i = 0; i < expected.bodies.size(); ++i) {
        const deep::Body& left = expected.bodies.at(i);
        const deep::Body& right = actual.bodies.at(i);
        require(left.id == right.id, "body ID round-trips");
        require(left.systemId == right.systemId, "body system ID round-trips");
        require(left.name == right.name, "body name round-trips");
        require(left.type == right.type, "body type round-trips");
        require(left.strategicZone == right.strategicZone, "body strategic zone round-trips");
        require(sameOptionalId(left.parentBodyId, right.parentBodyId), "body parent rail reference round-trips");
        require(almostEqual(left.orbitalRadiusKm, right.orbitalRadiusKm), "body orbital radius round-trips");
        require(almostEqual(left.orbitalPeriodDays, right.orbitalPeriodDays), "body orbital period round-trips");
        require(almostEqual(left.phaseRadians, right.phaseRadians), "body orbital phase round-trips");
        require(almostEqual(left.displayRadius, right.displayRadius), "body display radius round-trips");
        require(almostEqual(left.x, right.x), "body x coordinate round-trips");
        require(almostEqual(left.y, right.y), "body y coordinate round-trips");
    }

    require(expected.colonies.size() == actual.colonies.size(), "colony row count round-trips");
    for (std::size_t i = 0; i < expected.colonies.size(); ++i) {
        const deep::Colony& left = expected.colonies.at(i);
        const deep::Colony& right = actual.colonies.at(i);
        require(left.id == right.id, "colony ID round-trips");
        require(left.bodyId == right.bodyId, "colony body ID round-trips");
        require(left.name == right.name, "colony name round-trips");
        require(sameOptionalId(left.ownerInstitutionId, right.ownerInstitutionId), "colony owner institution round-trips");
        require(sameMineralSet(left.stockpile, right.stockpile), "colony raw stockpile round-trips");
        require(sameProcessedMaterialSet(left.processedStockpile, right.processedStockpile),
                "colony processed stockpile round-trips");
        require(almostEqual(left.mines, right.mines), "colony mines round-trip");
        require(almostEqual(left.processorCapacity, right.processorCapacity), "processor capacity round-trips");
        require(almostEqual(left.shipyardCapacity, right.shipyardCapacity), "shipyard capacity round-trips");
        require(left.processingPolicy == right.processingPolicy, "colony processing policy round-trips");
        require(sameProcessingAllocations(left.manualProcessingAllocations, right.manualProcessingAllocations),
                "colony manual processing allocations round-trip");
    }

    require(expected.mineralDeposits.size() == actual.mineralDeposits.size(), "deposit row count round-trips");
    for (std::size_t i = 0; i < expected.mineralDeposits.size(); ++i) {
        const deep::MineralDeposit& left = expected.mineralDeposits.at(i);
        const deep::MineralDeposit& right = actual.mineralDeposits.at(i);
        require(left.bodyId == right.bodyId, "deposit body ID round-trips");
        require(left.mineral == right.mineral, "deposit mineral round-trips");
        require(almostEqual(left.remaining, right.remaining), "deposit remaining amount round-trips");
        require(almostEqual(left.accessibility, right.accessibility), "deposit accessibility round-trips");
        require(almostEqual(left.confidence, right.confidence), "deposit confidence round-trips");
    }

    require(expected.shipComponents.size() == actual.shipComponents.size(), "component row count round-trips");
    for (std::size_t i = 0; i < expected.shipComponents.size(); ++i) {
        const auto& left = expected.shipComponents.at(i);
        const auto& right = actual.shipComponents.at(i);
        require(left.id == right.id && left.name == right.name && left.kind == right.kind,
                "component identity round-trips");
        require(almostEqual(left.mass, right.mass) && almostEqual(left.volume, right.volume) &&
                almostEqual(left.internalVolumeCapacity, right.internalVolumeCapacity) &&
                almostEqual(left.powerGeneration, right.powerGeneration) &&
                almostEqual(left.powerDemand, right.powerDemand) &&
                almostEqual(left.propellantCapacity, right.propellantCapacity) &&
                almostEqual(left.surveyCapability, right.surveyCapability) &&
                almostEqual(left.cargoCapacity, right.cargoCapacity) &&
                almostEqual(left.cargoHandlingPerDay, right.cargoHandlingPerDay) &&
                almostEqual(left.buildPoints, right.buildPoints) &&
                sameProcessedMaterialSet(left.buildCost, right.buildCost),
                "component physical and build data round-trips");
    }
    require(expected.shipClasses.size() == actual.shipClasses.size(), "ship-class row count round-trips");
    for (std::size_t i = 0; i < expected.shipClasses.size(); ++i) {
        const deep::ShipClass& left = expected.shipClasses.at(i);
        const deep::ShipClass& right = actual.shipClasses.at(i);
        require(left.id == right.id, "ship-class ID round-trips");
        require(left.name == right.name, "ship-class name round-trips");
        require(left.role == right.role, "ship-class role round-trips");
        require(left.revision == right.revision && left.basedOnClassId == right.basedOnClassId &&
                left.components == right.components, "ship-class revision and installation order round-trips");
        require(almostEqual(left.speedKmPerDay, right.speedKmPerDay), "ship-class speed round-trips");
    }

    require(expected.shipyardOrders.size() == actual.shipyardOrders.size(), "shipyard-order row count round-trips");
    for (std::size_t i = 0; i < expected.shipyardOrders.size(); ++i) {
        const deep::ShipyardOrder& left = expected.shipyardOrders.at(i);
        const deep::ShipyardOrder& right = actual.shipyardOrders.at(i);
        require(left.id == right.id, "shipyard-order ID round-trips");
        require(left.colonyId == right.colonyId, "shipyard-order colony ID round-trips");
        require(left.shipClassId == right.shipClassId, "shipyard-order ship-class ID round-trips");
        require(left.quantityRequested == right.quantityRequested, "shipyard-order requested quantity round-trips");
        require(left.quantityCompleted == right.quantityCompleted, "shipyard-order completed quantity round-trips");
        require(almostEqual(left.accumulatedBuildPoints, right.accumulatedBuildPoints), "shipyard-order progress round-trips");
        require(left.status == right.status, "shipyard-order status round-trips");
    }

    require(expected.fleets.size() == actual.fleets.size(), "fleet row count round-trips");
    for (std::size_t i = 0; i < expected.fleets.size(); ++i) {
        const deep::Fleet& left = expected.fleets.at(i);
        const deep::Fleet& right = actual.fleets.at(i);
        require(left.id == right.id, "fleet ID round-trips");
        require(left.name == right.name, "fleet name round-trips");
        require(sameOptionalId(left.ownerInstitutionId, right.ownerInstitutionId), "fleet owner institution round-trips");
        require(left.currentBodyId == right.currentBodyId, "fleet current body round-trips");
        require(sameOptionalId(left.destinationBodyId, right.destinationBodyId), "fleet destination body round-trips");
        require(left.shipIds == right.shipIds, "fleet ship ID list is rebuilt from ships");
        require(left.activeOrder.type == right.activeOrder.type, "fleet order type round-trips");
        require(sameOptionalId(left.activeOrder.targetBodyId, right.activeOrder.targetBodyId), "fleet order target round-trips");
        require(left.activeOrder.daysRemaining == right.activeOrder.daysRemaining, "fleet order days remaining round-trips");
        require(sameOptionalId(left.activeOrder.departureBodyId, right.activeOrder.departureBodyId),
                "fleet order departure body round-trips");
        require(left.activeOrder.departureDay == right.activeOrder.departureDay, "fleet order departure day round-trips");
        require(left.activeOrder.arrivalDay == right.activeOrder.arrivalDay, "fleet order arrival day round-trips");
        require(almostEqual(left.activeOrder.departurePosition.x, right.activeOrder.departurePosition.x),
                "fleet order departure x round-trips");
        require(almostEqual(left.activeOrder.departurePosition.y, right.activeOrder.departurePosition.y),
                "fleet order departure y round-trips");
        require(almostEqual(left.activeOrder.projectedArrivalPosition.x, right.activeOrder.projectedArrivalPosition.x),
                "fleet order projected arrival x round-trips");
        require(almostEqual(left.activeOrder.projectedArrivalPosition.y, right.activeOrder.projectedArrivalPosition.y),
                "fleet order projected arrival y round-trips");
        require(almostEqual(left.activeOrder.transitDistanceKm, right.activeOrder.transitDistanceKm),
                "fleet order transit distance round-trips");
        require(almostEqual(left.activeOrder.burnAccelerationG, right.activeOrder.burnAccelerationG),
                "fleet order burn acceleration round-trips");
        require(almostEqual(left.activeOrder.routeCurveControlPoint.x, right.activeOrder.routeCurveControlPoint.x),
                "fleet order curve control x round-trips");
        require(almostEqual(left.activeOrder.routeCurveControlPoint.y, right.activeOrder.routeCurveControlPoint.y),
                "fleet order curve control y round-trips");
        require(left.queuedOrders.size() == right.queuedOrders.size(), "fleet queued-order count round-trips");
        for (std::size_t j = 0; j < left.queuedOrders.size(); ++j) {
            require(left.queuedOrders.at(j).type == right.queuedOrders.at(j).type,
                    "fleet queued-order type round-trips");
            require(sameOptionalId(left.queuedOrders.at(j).targetBodyId, right.queuedOrders.at(j).targetBodyId),
                    "fleet queued-order target round-trips");
        }
    }

    require(expected.ships.size() == actual.ships.size(), "ship row count round-trips");
    for (std::size_t i = 0; i < expected.ships.size(); ++i) {
        const deep::Ship& left = expected.ships.at(i);
        const deep::Ship& right = actual.ships.at(i);
        require(left.id == right.id, "ship ID round-trips");
        require(left.shipClassId == right.shipClassId, "ship class reference round-trips");
        require(left.name == right.name, "ship name round-trips");
        require(left.fleetId == right.fleetId, "ship fleet reference round-trips");
        require(almostEqual(left.fuel, right.fuel), "ship fuel round-trips");
        require(left.cargo.has_value() == right.cargo.has_value(), "ship cargo presence round-trips");
        if (left.cargo) require(left.cargo->programId == right.cargo->programId &&
            left.cargo->shipmentNumber == right.cargo->shipmentNumber &&
            left.cargo->material == right.cargo->material && almostEqual(left.cargo->quantity, right.cargo->quantity),
            "physical ship cargo custody and quantity round-trip");
    }

    require(expected.surveyTeams.size() == actual.surveyTeams.size(), "survey team count round-trips");
    for (std::size_t i = 0; i < expected.surveyTeams.size(); ++i) {
        const auto& left = expected.surveyTeams.at(i);
        const auto& right = actual.surveyTeams.at(i);
        require(left.id == right.id && left.name == right.name &&
                left.locationKind == right.locationKind && left.colonyId == right.colonyId &&
                left.fleetId == right.fleetId, "survey team identity and location round-trip");
    }

    require(expected.surveyPrograms.size() == actual.surveyPrograms.size(), "survey program count round-trips");
    for (std::size_t i = 0; i < expected.surveyPrograms.size(); ++i) {
        const auto& left = expected.surveyPrograms.at(i);
        const auto& right = actual.surveyPrograms.at(i);
        require(left.id == right.id && left.charter.name == right.charter.name &&
                left.charter.homeColonyId == right.charter.homeColonyId &&
                left.pendingHomeColonyId == right.pendingHomeColonyId &&
                left.charter.requestedFleetId == right.charter.requestedFleetId &&
                left.charter.requestedLeaderId == right.charter.requestedLeaderId &&
                left.charter.requestedTeamId == right.charter.requestedTeamId &&
                left.charter.policy.maxAdditionalPropellant == right.charter.policy.maxAdditionalPropellant &&
                almostEqual(left.charter.policy.homeStockFloor, right.charter.policy.homeStockFloor) &&
                almostEqual(left.charter.policy.returnContingencyFraction,
                            right.charter.policy.returnContingencyFraction),
                "survey program charter and policy round-trip");
        require(left.charter.targets.size() == right.charter.targets.size(), "survey target count round-trips");
        for (std::size_t j = 0; j < left.charter.targets.size(); ++j) {
            const auto& a = left.charter.targets.at(j);
            const auto& b = right.charter.targets.at(j);
            require(a.bodyId == b.bodyId && a.priority == b.priority &&
                    a.requestedPasses == b.requestedPasses, "survey target order and quotas round-trip");
        }
        require(left.createdDay == right.createdDay && left.charterRevision == right.charterRevision &&
                left.lifecycle == right.lifecycle && left.closure == right.closure &&
                left.leasedFleetId == right.leasedFleetId && left.leasedTeamId == right.leasedTeamId &&
                left.task == right.task && left.taskBodyId == right.taskBodyId &&
                left.taskFleetId == right.taskFleetId && left.taskTeamId == right.taskTeamId &&
                left.taskLeaderId == right.taskLeaderId && left.taskApproach == right.taskApproach &&
                left.taskPassNumber == right.taskPassNumber &&
                left.workDaysCompleted == right.workDaysCompleted &&
                left.firstWorkDay == right.firstWorkDay &&
                left.lastSelectionReason == right.lastSelectionReason,
                "survey program lifecycle, lease, and partial task round-trip");
        require(left.receipts.size() == right.receipts.size(), "survey receipt count round-trips");
        for (std::size_t j = 0; j < left.receipts.size(); ++j) {
            const auto& a = left.receipts.at(j);
            const auto& b = right.receipts.at(j);
            require(a.bodyId == b.bodyId && a.passNumber == b.passNumber &&
                    a.fleetId == b.fleetId && a.teamId == b.teamId && a.leaderId == b.leaderId &&
                    a.approach == b.approach && a.firstWorkDay == b.firstWorkDay &&
                    a.completedDay == b.completedDay && a.workDays == b.workDays &&
                    a.depositsImproved == b.depositsImproved &&
                    almostEqual(a.averageConfidenceBefore, b.averageConfidenceBefore) &&
                    almostEqual(a.averageConfidenceAfter, b.averageConfidenceAfter),
                    "survey visit receipt order and facts round-trip");
        }
        require(almostEqual(left.fuelLoaded, right.fuelLoaded) &&
                almostEqual(left.fuelBurned, right.fuelBurned) &&
                left.totalWorkDays == right.totalWorkDays &&
                left.nextReportDay == right.nextReportDay &&
                left.reportStartDay == right.reportStartDay &&
                almostEqual(left.reportedFuelLoaded, right.reportedFuelLoaded) &&
                almostEqual(left.reportedFuelBurned, right.reportedFuelBurned) &&
                left.reportedWorkDays == right.reportedWorkDays &&
                left.reportedVisits == right.reportedVisits,
                "survey fuel totals and reporting cursor round-trip");
        require(left.reports.size() == right.reports.size(), "survey report count round-trips");
        for (std::size_t j = 0; j < left.reports.size(); ++j) {
            const auto& a = left.reports.at(j);
            const auto& b = right.reports.at(j);
            require(a.startDay == b.startDay && a.endDay == b.endDay &&
                    a.isNinetyDayReview == b.isNinetyDayReview &&
                    a.charterRevision == b.charterRevision && a.leaderId == b.leaderId &&
                    a.approach == b.approach && a.visitsCompleted == b.visitsCompleted &&
                    a.workDays == b.workDays && almostEqual(a.fuelLoaded, b.fuelLoaded) &&
                    almostEqual(a.fuelBurned, b.fuelBurned) && a.fleetId == b.fleetId &&
                    a.teamId == b.teamId && a.fleetBodyId == b.fleetBodyId &&
                    a.waitingReason == b.waitingReason,
                    "survey report interval, accounting, and snapshot round-trip");
        }
        require(left.issue.signature == right.issue.signature &&
                left.issue.message == right.issue.message &&
                left.issue.acknowledged == right.issue.acknowledged,
                "survey issue identity and acknowledgment round-trip");
    }

    require(expected.eventLog.size() == actual.eventLog.size(), "event log row count round-trips");
    for (std::size_t i = 0; i < expected.eventLog.size(); ++i) {
        const deep::SimEvent& left = expected.eventLog.at(i);
        const deep::SimEvent& right = actual.eventLog.at(i);
        require(left.id == right.id, "event ID round-trips");
        require(left.day == right.day, "event day round-trips");
        require(left.severity == right.severity, "event severity round-trips");
        require(samePayload(left.payload, right.payload), "typed event payload round-trips");
    }
}

std::filesystem::path testSavePath() {
    // Use the temp directory so repeated local test runs do not dirty the source
    // tree. The filename is fixed because the test is single-process.
    return std::filesystem::temp_directory_path() / "deep_signal_save_roundtrip.sqlite";
}

std::filesystem::path malformedSavePath(const std::string_view suffix) {
    // Each malformed-save test gets its own file so one failed corruption step
    // cannot influence a later validation case.
    return std::filesystem::temp_directory_path() / ("deep_signal_malformed_" + std::string{suffix} + ".sqlite");
}

class UniqueProgramSavePath final {
public:
    UniqueProgramSavePath() {
        std::string pattern = (std::filesystem::temp_directory_path() /
            "deep_signal_p3a_return_XXXXXX").string();
        const int descriptor = mkstemp(pattern.data());
        require(descriptor >= 0, "create unique P3A return-issue save path");
        close(descriptor);
        path_ = pattern;
        std::filesystem::remove(path_);
    }
    ~UniqueProgramSavePath() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-journal", ignored);
    }
    UniqueProgramSavePath(const UniqueProgramSavePath&) = delete;
    UniqueProgramSavePath& operator=(const UniqueProgramSavePath&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

deep::GameState makeWaitingSurveyProgramState() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::SurveyTeamId teamId{state.ids.nextSurveyTeamId++};
    const deep::SurveyProgramId programId{state.ids.nextSurveyProgramId++};
    state.people.front().surveyPlanningApproach = deep::SurveyPlanningApproach::PriorityFirst;
    state.surveyTeams.push_back(deep::SurveyTeam{
        .id = teamId,
        .name = "Terra field survey team",
        .locationKind = deep::SurveyTeamLocationKind::Colony,
        .colonyId = state.colonies.front().id,
        .fleetId = std::nullopt
    });
    deep::SurveyProgram program;
    program.id = programId;
    program.charter.name = "Outer belt survey";
    program.charter.homeColonyId = state.colonies.front().id;
    program.charter.requestedLeaderId = state.people.front().id;
    program.charter.requestedTeamId = teamId;
    program.charter.targets = {
        deep::SurveyProgramTarget{.bodyId = state.bodies.at(2).id, .priority = 2, .requestedPasses = 1},
        deep::SurveyProgramTarget{.bodyId = state.bodies.at(1).id, .priority = 1, .requestedPasses = 2}
    };
    program.charter.policy.maxAdditionalPropellant = 45.0;
    program.charter.policy.homeStockFloor = 10.0;
    program.charter.policy.returnContingencyFraction = 0.2;
    state.surveyPrograms.push_back(std::move(program));
    return state;
}

void corruptSave(const std::filesystem::path& path, std::string_view sql,
                 bool ignoreCheckConstraints, bool disableForeignKeys);
void requireRepositoryLoadFails(const std::filesystem::path& path, std::string_view message);
void requireServiceLoadFailsWithoutStateReplacement(const std::filesystem::path& path);

void test_waiting_survey_program_round_trips() {
    const auto path = malformedSavePath("waiting_survey_program");
    std::filesystem::remove(path);
    const deep::GameState source = makeWaitingSurveyProgramState();
    deep::save::SaveGameRepository::save(path, source);
    requireSameState(source, deep::save::SaveGameRepository::load(path));
    std::filesystem::remove(path);
}

deep::SurveyProgramCharter makeDelegatedSurveyCharter(const deep::GameState& state) {
    deep::SurveyProgramCharter charter;
    charter.name = "Three target survey";
    charter.homeColonyId = state.colonies.back().id;
    charter.requestedFleetId = state.fleets.front().id;
    charter.requestedLeaderId = state.people.back().id;
    charter.requestedTeamId = state.surveyTeams.front().id;
    charter.policy.maxAdditionalPropellant = 2'000.0;
    charter.policy.returnContingencyFraction = 0.1;
    for (std::size_t i = state.bodies.size() - 3; i < state.bodies.size(); ++i) {
        charter.targets.push_back(deep::SurveyProgramTarget{
            .bodyId = state.bodies.at(i).id, .priority = 0, .requestedPasses = 1
        });
    }
    return charter;
}

void test_active_survey_program_continues_after_save_load() {
    const auto path = malformedSavePath("active_survey_program");
    std::filesystem::remove(path);
    deep::Simulation uninterrupted{deep::createDelegatedSurveyScenario()};
    const deep::SurveyProgramCharter charter = makeDelegatedSurveyCharter(uninterrupted.state());
    require(uninterrupted.execute(deep::CreateSurveyProgramCommand{charter}).ok,
            "ready delegated survey charter is authorized");

    const auto nextDay = [&uninterrupted]() {
        const auto result = uninterrupted.advanceDaysDetailed(1);
        require(result.advancedDays == 1 && !result.interrupted,
                "delegated survey fixture advances one full day without issue");
    };
    nextDay();
    require(uninterrupted.state().surveyPrograms.front().fuelLoaded > 0.0,
            "first program day records an actual refill");

    std::optional<deep::Simulation> resumed;
    const auto reload = [&]() {
        deep::save::SaveGameRepository::save(path, uninterrupted.state());
        deep::GameState loaded = deep::save::SaveGameRepository::load(path);
        requireSameState(uninterrupted.state(), loaded);
        resumed.emplace(std::move(loaded));
    };
    reload();
    for (int day = 2; day <= 31; ++day) {
        nextDay();
        const auto result = resumed->advanceDaysDetailed(1);
        require(result.advancedDays == 1 && !result.interrupted,
                "loaded delegated survey continues one full day");
        requireSameState(uninterrupted.state(), resumed->state());
        if (day == 4) {
            require(uninterrupted.state().surveyPrograms.front().workDaysCompleted > 0,
                    "partly completed visit is present at Save/Load checkpoint");
            reload();
        } else if (day == 8) {
            require(uninterrupted.state().surveyPrograms.front().receipts.size() == 1,
                    "first visit receipt is present at Save/Load checkpoint");
            reload();
        } else if (day == 30) {
            require(uninterrupted.state().surveyPrograms.front().reports.size() == 1,
                    "30-day report is present at Save/Load checkpoint");
            reload();
        }
    }
    require(uninterrupted.state().surveyPrograms.front().receipts.size() == 3 &&
            uninterrupted.state().surveyPrograms.front().reports.size() == 1,
            "resuming after report boundary does not duplicate visit or report");
    std::filesystem::remove(path);
}

void test_completion_return_issue_allows_amend_and_suspend_after_load() {
    // This fixture removes onboard fuel only after a real final field pass. It
    // models a changed physical return condition without adding remote refuel
    // mechanics to P3A; both save branches later receive the same fixture input.
    deep::GameState fixture = deep::createDelegatedSurveyScenario();
    const deep::BodyId firstTarget = fixture.bodies.back().id;
    fixture.bodies.back().x = 20.0; // Multi-day real transit, still within tankage.
    deep::Simulation setup{std::move(fixture)};
    auto charter = makeDelegatedSurveyCharter(setup.state());
    charter.targets = {{firstTarget, 3, 1}};
    require(setup.execute(deep::CreateSurveyProgramCommand{charter}).ok,
            "final-pass return-issue fixture authorizes one target");
    for (int day = 0; day < 80; ++day) {
        const auto& program = setup.state().surveyPrograms.front();
        if (program.lifecycle == deep::SurveyProgramLifecycle::Closing &&
            program.closure == deep::SurveyProgramClosure::Completed &&
            program.task == deep::SurveyProgramTask::Return) break;
        const auto step = setup.advanceDaysDetailed(1);
        require(step.advancedDays == 1 && !step.interrupted,
                "final field pass reaches its physical return obligation");
    }
    const auto& beforeShortage = setup.state().surveyPrograms.front();
    require(beforeShortage.lifecycle == deep::SurveyProgramLifecycle::Closing &&
            beforeShortage.closure == deep::SurveyProgramClosure::Completed &&
            beforeShortage.receipts.size() == 1 &&
            setup.state().fleets.back().currentBodyId == firstTarget &&
            setup.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "completed final pass is stationary at target before return");

    deep::GameState depleted = setup.state();
    depleted.ships.back().fuel = 0.0;
    deep::Simulation blocked{std::move(depleted)};
    const auto stopped = blocked.advanceDaysDetailed(10);
    const auto& issueProgram = blocked.state().surveyPrograms.front();
    require(stopped.interrupted && stopped.advancedDays == 1 &&
            issueProgram.lifecycle == deep::SurveyProgramLifecycle::Closing &&
            issueProgram.closure == deep::SurveyProgramClosure::Completed &&
            !issueProgram.issue.acknowledged &&
            issueProgram.issue.signature.find("return:") == 0,
            "unaffordable completion return raises a consequential issue after one full day");
    UniqueProgramSavePath save;
    deep::save::SaveGameRepository::save(save.path(), blocked.state());
    deep::GameState loadedIssue = deep::save::SaveGameRepository::load(save.path());
    requireSameState(blocked.state(), loadedIssue);

    const auto replenishForNextReturn = [](deep::GameState state) {
        const auto& program = state.surveyPrograms.front();
        const auto& fleet = state.fleets.back();
        const auto home = std::find_if(state.colonies.begin(), state.colonies.end(),
            [&program](const deep::Colony& colony) { return colony.id == program.charter.homeColonyId; });
        require(home != state.colonies.end(), "return home exists");
        const double returnFuel = deep::adjustedFleetMoveFuelCost(
            state, fleet, fleet.currentBodyId, home->bodyId, state.date.day + 1);
        require(std::isfinite(returnFuel) && returnFuel + 5.0 < 1000.0,
                "fixture return replenishment fits the built tank");
        state.ships.back().fuel = returnFuel + 5.0;
        return state;
    };
    const auto advanceEquivalentToClosure = [](deep::Simulation& left, deep::Simulation& right,
                                                const int limit) {
        for (int day = 0; day < limit; ++day) {
            if (left.state().surveyPrograms.front().lifecycle == deep::SurveyProgramLifecycle::Closed) break;
            const auto a = left.advanceDaysDetailed(1);
            const auto b = right.advanceDaysDetailed(1);
            require(a.advancedDays == 1 && b.advancedDays == 1 && !a.interrupted && !b.interrupted,
                    "equivalent restored return branches advance without a new issue");
            requireSameState(left.state(), right.state());
        }
        require(left.state().surveyPrograms.front().lifecycle == deep::SurveyProgramLifecycle::Closed &&
                left.state().surveyPrograms.front().closure == deep::SurveyProgramClosure::Completed,
                "restored physical return eventually closes the completed charter");
    };

    deep::Simulation amended{loadedIssue};
    auto extended = amended.state().surveyPrograms.front().charter;
    extended.targets.push_back(deep::SurveyProgramTarget{
        .bodyId = amended.state().bodies.at(amended.state().bodies.size() - 2).id,
        .priority = 1, .requestedPasses = 1
    });
    const auto amendment = amended.execute(deep::AmendSurveyProgramCommand{
        .programId = amended.state().surveyPrograms.front().id, .charter = extended
    });
    const auto& reopened = amended.state().surveyPrograms.front();
    require(amendment.ok && reopened.lifecycle == deep::SurveyProgramLifecycle::Authorized &&
            reopened.closure == deep::SurveyProgramClosure::None &&
            reopened.task == deep::SurveyProgramTask::Return &&
            reopened.taskFleetId == issueProgram.taskFleetId &&
            reopened.taskTeamId == issueProgram.taskTeamId &&
            reopened.taskLeaderId == issueProgram.taskLeaderId &&
            amended.state().fleets.back().currentBodyId == firstTarget &&
            amended.state().fleets.back().activeOrder.type == deep::FleetOrderType::None &&
            amended.state().ships.back().fuel == 0.0 &&
            amended.state().surveyPrograms.front().fuelBurned == issueProgram.fuelBurned,
            "amendment reopens authority without moving, refueling, or replacing committed task assets");
    deep::save::SaveGameRepository::save(save.path(), amended.state());
    deep::GameState loadedAmendment = deep::save::SaveGameRepository::load(save.path());
    requireSameState(amended.state(), loadedAmendment);
    deep::Simulation amendedLive{replenishForNextReturn(amended.state())};
    deep::Simulation amendedLoaded{replenishForNextReturn(std::move(loadedAmendment))};
    advanceEquivalentToClosure(amendedLive, amendedLoaded, 120);
    require(amendedLive.state().surveyPrograms.front().receipts.size() == 2,
            "added target executes only after committed return and next planning boundary");

    deep::Simulation suspended{blocked.state()};
    const auto pause = suspended.execute(deep::SuspendSurveyProgramCommand{
        .programId = suspended.state().surveyPrograms.front().id
    });
    const auto& paused = suspended.state().surveyPrograms.front();
    require(pause.ok && paused.lifecycle == deep::SurveyProgramLifecycle::Suspended &&
            paused.closure == deep::SurveyProgramClosure::None &&
            paused.task == deep::SurveyProgramTask::Return &&
            paused.taskFleetId == issueProgram.taskFleetId &&
            paused.taskTeamId == issueProgram.taskTeamId &&
            suspended.state().fleets.back().currentBodyId == firstTarget &&
            suspended.state().ships.back().fuel == 0.0,
            "suspension pauses completion return without teleporting or canceling its task");
    deep::save::SaveGameRepository::save(save.path(), suspended.state());
    deep::GameState loadedSuspension = deep::save::SaveGameRepository::load(save.path());
    requireSameState(suspended.state(), loadedSuspension);
    deep::Simulation suspendedLive{replenishForNextReturn(suspended.state())};
    deep::Simulation suspendedLoaded{replenishForNextReturn(std::move(loadedSuspension))};
    const auto resume = deep::ResumeSurveyProgramCommand{suspendedLive.state().surveyPrograms.front().id};
    require(suspendedLive.execute(resume).ok && suspendedLoaded.execute(resume).ok,
            "both suspended branches resume the same unfinished return");
    advanceEquivalentToClosure(suspendedLive, suspendedLoaded, 80);
    require(suspendedLive.state().surveyPrograms.front().receipts.size() == 1,
            "resume closes after original visit without duplicate fieldwork");
}

void test_pending_survey_home_amendment_round_trips() {
    const auto path = malformedSavePath("pending_survey_home");
    std::filesystem::remove(path);
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    require(sim.execute(deep::CreateSurveyProgramCommand{makeDelegatedSurveyCharter(sim.state())}).ok,
            "pending-home fixture authorizes program");
    require(sim.advanceDaysDetailed(4).advancedDays == 4,
            "pending-home fixture reaches a partly completed visit");
    const deep::SurveyProgram& before = sim.state().surveyPrograms.front();
    require(before.task == deep::SurveyProgramTask::Survey &&
            before.workDaysCompleted > 0 && !before.lastSelectionReason.empty(),
            "pending-home fixture retains active task and decision rationale");
    deep::SurveyProgramCharter amended = before.charter;
    amended.homeColonyId = sim.state().colonies.front().id;
    require(sim.execute(deep::AmendSurveyProgramCommand{.programId = before.id, .charter = amended}).ok,
            "home amendment is accepted while survey visit remains committed");
    const deep::SurveyProgram& pending = sim.state().surveyPrograms.front();
    require(pending.pendingHomeColonyId == amended.homeColonyId &&
            pending.charter.homeColonyId != amended.homeColonyId,
            "new home remains pending until a safe physical boundary");
    deep::save::SaveGameRepository::save(path, sim.state());
    deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(sim.state(), loaded);
    deep::Simulation resumed{std::move(loaded)};
    require(sim.advanceDaysDetailed(1).advancedDays == 1 &&
            resumed.advanceDaysDetailed(1).advancedDays == 1,
            "both original and loaded pending-home worlds advance one day");
    requireSameState(sim.state(), resumed.state());
    std::filesystem::remove(path);
}

void test_malformed_survey_history_rows_are_rejected() {
    const auto base = malformedSavePath("program_history_base");
    std::filesystem::remove(base);
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    require(sim.execute(deep::CreateSurveyProgramCommand{makeDelegatedSurveyCharter(sim.state())}).ok,
            "history corruption fixture authorizes program");
    const auto advance = sim.advanceDaysDetailed(30);
    require(advance.advancedDays == 30 && !advance.interrupted &&
            sim.state().surveyPrograms.front().receipts.size() == 3 &&
            sim.state().surveyPrograms.front().reports.size() == 1,
            "history corruption fixture has completed receipts and a report");
    deep::save::SaveGameRepository::save(base, sim.state());

    const struct Corruption {
        const char* name;
        const char* sql;
        bool ignoreChecks = false;
        bool disableForeignKeys = false;
    } corruptions[] = {
        {"receipt_gap", "UPDATE survey_program_receipts SET ordinal=7 WHERE ordinal=2;"},
        {"report_gap", "UPDATE survey_program_reports SET ordinal=7 WHERE ordinal=0;"},
        {"receipt_overlap", "UPDATE survey_program_receipts SET first_work_day=5 WHERE ordinal=1;"},
        {"receipt_pass", "UPDATE survey_program_receipts SET pass_number=9 WHERE ordinal=0;"},
        {"report_interval", "UPDATE survey_program_reports SET end_day=29;"},
        {"report_start_gap", "UPDATE survey_program_reports SET start_day=10;"},
        {"report_cursor_mismatch", "UPDATE survey_programs SET reported_visits=2;"},
        {"report_work_sum_mismatch", "UPDATE survey_program_reports SET work_days=14;"},
        {"report_bad_review", "UPDATE survey_program_reports SET is_ninety_day_review=2;", true},
        {"report_bad_real", "UPDATE survey_program_reports SET fuel_loaded='1junk';", true},
        {"receipt_bad_real", "UPDATE survey_program_receipts SET average_confidence_before='1junk' WHERE ordinal=0;", true},
        {"pending_home_unknown", "UPDATE survey_programs SET pending_home_colony_id=999;", false, true}
    };
    for (const Corruption& test : corruptions) {
        const auto path = malformedSavePath(test.name);
        std::filesystem::remove(path);
        std::filesystem::copy_file(base, path);
        corruptSave(path, test.sql, test.ignoreChecks, test.disableForeignKeys);
        requireRepositoryLoadFails(path, "malformed survey history row is rejected");
        requireServiceLoadFailsWithoutStateReplacement(path);
        std::filesystem::remove(path);
    }
    std::filesystem::remove(base);
}

void test_malformed_survey_partial_rows_are_rejected() {
    const auto base = malformedSavePath("program_partial_base");
    std::filesystem::remove(base);
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    require(sim.execute(deep::CreateSurveyProgramCommand{makeDelegatedSurveyCharter(sim.state())}).ok,
            "partial corruption fixture authorizes program");
    const auto advance = sim.advanceDaysDetailed(4);
    const deep::SurveyProgram& program = sim.state().surveyPrograms.front();
    require(advance.advancedDays == 4 && !advance.interrupted &&
            program.task == deep::SurveyProgramTask::Survey && program.workDaysCompleted == 1 &&
            program.firstWorkDay == 4 && program.taskPassNumber == 1,
            "partial corruption fixture has one credited work day on pass one");
    deep::save::SaveGameRepository::save(base, sim.state());

    const struct Corruption { const char* name; const char* sql; } corruptions[] = {
        {"partial_missing_first_day", "UPDATE survey_programs SET first_work_day=0;"},
        {"partial_duplicate_pass", "UPDATE survey_programs SET task_pass_number=2;"},
        {"partial_already_complete", "UPDATE survey_programs SET work_days_completed=5, total_work_days=5;"}
    };
    for (const Corruption& test : corruptions) {
        const auto path = malformedSavePath(test.name);
        std::filesystem::remove(path);
        std::filesystem::copy_file(base, path);
        corruptSave(path, test.sql, false, false);
        requireRepositoryLoadFails(path, "malformed partial survey task is rejected");
        requireServiceLoadFailsWithoutStateReplacement(path);
        std::filesystem::remove(path);
    }
    std::filesystem::remove(base);
}

void test_malformed_survey_program_rows_are_rejected() {
    const auto base = malformedSavePath("program_base");
    std::filesystem::remove(base);
    const deep::GameState source = makeWaitingSurveyProgramState();
    deep::save::SaveGameRepository::save(base, source);
    const struct Corruption {
        const char* name;
        const char* sql;
        bool ignoreChecks = false;
        bool disableForeignKeys = false;
    } corruptions[] = {
        {"program_target_gap", "UPDATE survey_program_targets SET ordinal=7 WHERE ordinal=1;"},
        {"program_bad_int_type", "UPDATE survey_programs SET task_pass_number='1junk';", true},
        {"program_bad_real_type", "UPDATE survey_programs SET home_stock_floor='1junk';", true},
        {"program_bad_team_location", "UPDATE survey_teams SET location_kind=1, colony_id=NULL;", true},
        {"program_missing_requested_team", "UPDATE survey_programs SET requested_team_id=999;", false, true},
        {"program_stale_counter", "UPDATE id_counters SET value=1 WHERE key='next_survey_program_id';"},
        {"program_bad_issue_bool", "UPDATE survey_programs SET issue_acknowledged=2;", true},
        {"program_orphaned_task", "UPDATE survey_programs SET task=2, task_body_id=2, work_days_completed=1;"},
        {"program_false_completed", "UPDATE survey_programs SET lifecycle=3, closure=1;"}
    };
    for (const Corruption& test : corruptions) {
        const auto path = malformedSavePath(test.name);
        std::filesystem::remove(path);
        std::filesystem::copy_file(base, path);
        corruptSave(path, test.sql, test.ignoreChecks, test.disableForeignKeys);
        requireRepositoryLoadFails(path, "malformed survey program row is rejected");
        requireServiceLoadFailsWithoutStateReplacement(path);
        std::filesystem::remove(path);
    }
    std::filesystem::remove(base);
}

void createPopulatedSave(const std::filesystem::path& path) {
    // Builds a save with ships, fleets, active movement, and events so malformed
    // tests can damage specific rows without relying on empty scenario state.
    std::filesystem::remove(path);

    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted while preparing malformed save");
    service.advanceDays(5);
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = service.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "move order accepted while preparing malformed save");
    service.advanceDays(1);

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "populated malformed-test fixture saves successfully");
}

void corruptSave(const std::filesystem::path& path, const std::string_view sql,
                 const bool ignoreCheckConstraints = false, const bool disableForeignKeys = false) {
    // Corruption helpers deliberately use direct SQL because the public repository
    // API should never be able to create malformed state. Prepared statements are
    // unnecessary here because test SQL is static and contains no user data.
    deep::save::Database db{path};
    if (ignoreCheckConstraints) {
        db.execute("PRAGMA ignore_check_constraints = ON;");
    }
    if (disableForeignKeys) {
        db.execute("PRAGMA foreign_keys = OFF;");
    }
    db.execute(sql);
}

void requireRepositoryLoadFails(const std::filesystem::path& path, const std::string_view message) {
    try {
        (void)deep::save::SaveGameRepository::load(path);
    } catch (const std::exception&) {
        return;
    }
    throw TestFailure{message};
}

void requireServiceLoadFailsWithoutStateReplacement(const std::filesystem::path& path) {
    // SimulationService must load into a temporary GameState first. This protects
    // a running game from being partially replaced by a malformed save file.
    deep::SimulationService service;
    const std::int64_t originalDay = service.state().date.day;
    const std::size_t originalBodyCount = service.state().bodies.size();

    const auto result = service.loadGame(path);
    require(!result.ok, "service reports malformed save load failure");
    require(service.state().date.day == originalDay, "failed load preserves current day");
    require(service.state().bodies.size() == originalBodyCount, "failed load preserves current state contents");
}

void expectMalformedSaveRejected(const std::string_view suffix, const std::string_view corruptionSql,
                                 const bool ignoreCheckConstraints = false, const bool disableForeignKeys = false) {
    const std::filesystem::path path = malformedSavePath(suffix);
    createPopulatedSave(path);
    corruptSave(path, corruptionSql, ignoreCheckConstraints, disableForeignKeys);

    requireRepositoryLoadFails(path, "malformed save is rejected by repository load");
    requireServiceLoadFailsWithoutStateReplacement(path);

    std::filesystem::remove(path);
}

void test_zero_capacity_waiting_order_round_trip() {
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 0.0;
    deep::SimulationService service{std::move(state)};
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    for (int count = 0; count < 2; ++count) {
        require(service.execute(deep::AssignShipyardBuildCommand{
            .colonyId = colonyId, .shipClassId = shipClassId, .quantity = 1
        }).ok, "waiting FIFO order is accepted before save");
    }
    service.advanceDays(3);
    const deep::GameState expected = service.state();
    require(service.saveGame(path).ok, "waiting orders save under existing schema");
    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(expected, loaded);
    require(loaded.shipyardOrders.size() == 2 &&
            loaded.shipyardOrders.at(0).id == expected.shipyardOrders.at(0).id &&
            loaded.shipyardOrders.at(1).id == expected.shipyardOrders.at(1).id &&
            loaded.shipyardOrders.at(0).status == deep::ShipyardOrderStatus::Active &&
            loaded.shipyardOrders.at(0).accumulatedBuildPoints == 0.0,
            "loaded orders retain FIFO sequence and waiting progress");
    std::filesystem::remove(path);
}

void test_design_revision_round_trip() {
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);
    deep::SimulationService service;
    const auto original = service.state().shipClasses.front();
    auto draft = original.components;
    draft.at(2).quantity = 2;
    require(service.execute(deep::CreateShipClassRevisionCommand{
        .name = "Double Tank", .role = deep::ShipRole::Escort,
        .basedOnClassId = original.id, .components = draft
    }).ok, "revision is created before save");
    const auto revisedId = service.state().shipClasses.back().id;
    const auto colonyId = service.state().colonies.front().id;
    require(service.execute(deep::AssignShipyardBuildCommand{colonyId, revisedId, 1}).ok,
            "order binds revised ID before save");
    service.advanceDays(6);
    require(service.state().ships.size() == 1 &&
            service.state().ships.front().shipClassId == revisedId,
            "completed ship binds revised ID before save");
    require(service.execute(deep::AssignShipyardBuildCommand{colonyId, original.id, 1}).ok,
            "later order binds original revision before save");
    const deep::GameState expected = service.state();
    require(service.saveGame(path).ok, "v14 component revision snapshot saves");
    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(expected, loaded);
    require(loaded.shipClasses.back().components == draft &&
            loaded.shipyardOrders.back().shipClassId == original.id &&
            loaded.ships.front().shipClassId == revisedId,
            "load preserves component order and exact class bindings");
    std::filesystem::remove(path);
}

void test_non_constructible_revision_round_trip() {
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);
    deep::SimulationService service;
    auto overflow = service.state().shipClasses.front().components;
    overflow.at(2).quantity = 8;
    const auto sourceId = service.state().shipClasses.front().id;
    require(service.execute(deep::CreateShipClassRevisionCommand{
        .name = "Overflow", .role = deep::ShipRole::Survey,
        .basedOnClassId = sourceId, .components = overflow
    }).ok, "overflow revision saves as durable design intent");
    const auto classId = service.state().shipClasses.back().id;
    require(service.execute(deep::AssignShipyardBuildCommand{
        service.state().colonies.front().id, classId, 1
    }).ok, "overflow order is accepted before save");
    service.advanceDays(3);
    const deep::GameState expected = service.state();
    require(service.saveGame(path).ok, "non-constructible revision and order save");
    deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(expected, loaded);
    require(!deep::evaluateShipDesign(loaded.shipComponents, loaded.shipClasses.back().components).constructible &&
            loaded.shipyardOrders.front().accumulatedBuildPoints == 0.0,
            "loaded overflow revision still blocks physical progress");
    deep::Simulation resumed{std::move(loaded)};
    resumed.advanceDays(3);
    require(resumed.state().ships.empty() &&
            resumed.state().shipyardOrders.front().accumulatedBuildPoints == 0.0,
            "loaded non-constructible order remains durable and idle");
    std::filesystem::remove(path);
}

void test_sqlite_save_load_round_trip() {
    // Saves a non-trivial mid-operation state and reloads it. This catches schema
    // omissions such as active fleet orders, ID counters, event payloads, and
    // raw and processed stockpile/cost rows.
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);

    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before save");

    service.advanceDays(5);
    const deep::FleetId fleetId = service.state().fleets.front().id;
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "movement order accepted before save");
    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = service.state().bodies.front().id
    }).ok, "first queued follow-up order accepted before save");
    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "second queued follow-up order accepted before save");

    service.advanceDays(2);
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = deep::ColonyId{999},
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok == false, "invalid command creates warning event before save");
    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 2.5},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 1.5}
        }
    }).ok, "manual processing policy is set before save");

    const deep::GameState expected = service.state();
    const double activeFuelCost = expected.fleets.front().activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit;
    require(almostEqual(expected.ships.front().fuel, 1000.0 - activeFuelCost),
            "active movement consumes fuel before save/load round-trip");
    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves SQLite file");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(expected, loaded);

    deep::SimulationService loadedService;
    const auto loadResult = loadedService.loadGame(path);
    require(loadResult.ok, "service loads SQLite file");
    requireSameState(expected, loadedService.state());

    // Continue the loaded simulation to prove that rehydrated active movement
    // state is not merely present but still valid for rule execution.
    const int remainingActiveDays = loadedService.state().fleets.front().activeOrder.daysRemaining;
    loadedService.advanceDays(remainingActiveDays);
    require(loadedService.state().fleets.front().currentBodyId == marsId, "loaded fleet arrives after remaining movement days");
    const double fuelAfterFirstPromotion = loadedService.state().ships.front().fuel;
    require(fuelAfterFirstPromotion < expected.ships.front().fuel,
            "loaded fleet consumes fuel when first queued order starts");
    require(loadedService.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "loaded fleet starts persisted queued order after arrival");
    require(loadedService.state().fleets.front().activeOrder.targetBodyId == service.state().bodies.front().id,
            "persisted queued order keeps its destination after promotion");

    const int firstPromotedDays = loadedService.state().fleets.front().activeOrder.daysRemaining;
    loadedService.advanceDays(firstPromotedDays);
    require(loadedService.state().fleets.front().currentBodyId == service.state().bodies.front().id,
            "loaded fleet completes first promoted queued order");
    const double fuelAfterSecondPromotion = loadedService.state().ships.front().fuel;
    require(fuelAfterSecondPromotion < fuelAfterFirstPromotion,
            "loaded fleet consumes fuel when second queued order starts");
    require(loadedService.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "loaded fleet starts second persisted queued order");
    require(loadedService.state().fleets.front().activeOrder.targetBodyId == marsId,
            "second persisted queued order keeps its destination after promotion");

    const int secondPromotedDays = loadedService.state().fleets.front().activeOrder.daysRemaining;
    loadedService.advanceDays(secondPromotedDays);
    require(loadedService.state().fleets.front().currentBodyId == marsId,
            "loaded fleet completes second promoted queued order");
    require(almostEqual(loadedService.state().ships.front().fuel, fuelAfterSecondPromotion),
            "arriving does not consume additional fuel after start-of-move consumption");
    require(loadedService.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "loaded fleet clears movement order after queued route finishes");

    std::filesystem::remove(path);
}

void test_institution_identity_and_ownership_round_trip() {
    // Verifies the mature-home-system identity layer is durable before any
    // politics or access mechanics are added on top of the owner references.
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                      "deep_signal_institution_ownership_roundtrip.sqlite";
    std::filesystem::remove(path);

    deep::SimulationService service;
    require(service.state().institutions.size() >= 5, "starter scenario includes institution records");
    require(service.state().colonies.front().ownerInstitutionId.has_value(),
            "starter colony has an owner institution before save");

    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before institution round-trip");
    service.advanceDays(5);
    require(service.state().fleets.front().ownerInstitutionId == service.state().colonies.front().ownerInstitutionId,
            "new fleet inherits colony owner institution");

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves institution ownership state");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(service.state(), loaded);
    require(loaded.institutions.front().name == "Strategic Continuity Office",
            "institution name survives repository load");

    std::filesystem::remove(path);
}

void test_personnel_registry_round_trips() {
    // Personnel are durable simulation records, not generated UI names. This
    // verifies both starter personnel and a hand-authored record survive SQLite.
    const std::filesystem::path path = testSavePath();
    std::filesystem::remove(path);

    deep::GameState state = deep::createHomeSystemScenario();
    const deep::InstitutionId institutionId = state.institutions.front().id;
    const deep::PersonId personId{state.ids.nextPersonId++};
    state.people.push_back(deep::Person{
        .id = personId,
        .name = "Senior Controller Ada Park",
        .institutionId = institutionId,
        .competencies = deep::PersonCompetencies{
            .logistics = 6,
            .industry = 2,
            .survey = 3,
            .command = 4,
            .administration = 5,
            .engineering = 2,
            .intelligence = 4,
            .crisisManagement = 6
        },
        .seniorityLevel = 6,
        .serviceRecord = deep::PersonServiceRecord{
            .successfulAssignments = 21,
            .failedAssignments = 2,
            .commendations = 8,
            .controversies = 1
        }
    });

    deep::save::SaveGameRepository::save(path, state);
    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);

    requireSameState(state, loaded);
    require(loaded.people.back().name == "Senior Controller Ada Park", "added person name round-trips");
    require(loaded.people.back().competencies.crisisManagement == 6,
            "added person crisis management competency round-trips");
    require(loaded.people.back().serviceRecord.commendations == 8,
            "added person service record counter round-trips");

    std::filesystem::remove(path);
}

void test_manual_processing_policy_state_round_trips() {
    // Verifies the player-facing processing allocation controls are durable.
    // Without this coverage, loading a save can silently revert production intent
    // to Balanced and discard Manual weights.
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                      "deep_signal_processing_policy_roundtrip.sqlite";
    std::filesystem::remove(path);

    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::StructuralAlloys, .weight = 3.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 2.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 1.0}
        }
    }).ok, "manual processing policy is accepted before round-trip save");

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves manual processing policy state");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    requireSameState(service.state(), loaded);
    require(loaded.colonies.front().processingPolicy == deep::ProcessingPolicy::Manual,
            "manual processing policy survives repository load");
    require(loaded.colonies.front().manualProcessingAllocations.size() == 3,
            "manual processing allocation rows survive repository load");

    deep::SimulationService loadedService;
    const auto loadResult = loadedService.loadGame(path);
    require(loadResult.ok, "service loads manual processing policy state");
    requireSameState(service.state(), loadedService.state());

    std::filesystem::remove(path);
}

void test_daily_economy_snapshots_are_runtime_only() {
    // Confirms the current schema's contract for high-volume economy telemetry. The
    // stockpile/deposit state is durable, but per-day mining samples are a
    // current-session UI/forecast/debug aid and intentionally reload empty.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "deep_signal_transient_telemetry.sqlite";
    std::filesystem::remove(path);

    deep::SimulationService service;
    service.advanceDays(1);
    require(!service.state().dailyEconomySnapshots.empty(), "advancing simulation creates economy telemetry");

    const auto saveResult = service.saveGame(path);
    require(saveResult.ok, "service saves state with runtime telemetry present");

    const deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    require(loaded.dailyEconomySnapshots.empty(), "repository load does not restore runtime telemetry");

    deep::SimulationService loadedService;
    const auto loadResult = loadedService.loadGame(path);
    require(loadResult.ok, "service loads state with transient telemetry omitted");
    require(loadedService.state().dailyEconomySnapshots.empty(), "loaded service starts with no runtime telemetry");

    loadedService.advanceDays(1);
    require(!loadedService.state().dailyEconomySnapshots.empty(), "loaded simulation creates new telemetry normally");

    std::filesystem::remove(path);
}

void test_malformed_save_missing_schema_version_is_rejected() {
    // Deletes required schema metadata. This protects the loader from treating an
    // arbitrary SQLite file as a compatible save.
    expectMalformedSaveRejected("missing_schema", "DELETE FROM schema_version;");
}

void test_malformed_save_unsupported_schema_version_is_rejected() {
    // Simulates a future or corrupted schema version row. CHECK constraints are
    // disabled only for the injection step so load-time validation is exercised.
    expectMalformedSaveRejected("bad_schema", "UPDATE schema_version SET version = 999;", true);
}

void test_malformed_save_multiple_schema_versions_are_rejected() {
    // Schema version metadata is a singleton identity record. If multiple rows
    // exist, the loader must reject the save rather than accepting whichever row
    // SQLite returns first.
    expectMalformedSaveRejected("duplicate_schema_rows",
                                "INSERT INTO schema_version(id, version) VALUES (2, 999);",
                                true);
}

void test_malformed_save_missing_id_counter_is_rejected() {
    // ID counters are part of the deterministic allocation contract. Missing one
    // would risk duplicate IDs after load.
    expectMalformedSaveRejected("missing_counter", "DELETE FROM id_counters WHERE key = 'next_ship_id';");
}

void test_malformed_save_stale_id_counter_is_rejected() {
    // A positive but stale counter is dangerous because the next simulation
    // allocation would duplicate an existing row ID in memory.
    expectMalformedSaveRejected("stale_ship_counter",
                                "UPDATE id_counters SET value = '1' WHERE key = 'next_ship_id';");
}

void test_malformed_save_non_numeric_current_day_is_rejected() {
    // Metadata must be parsed as canonical integer text. SQLite CAST would turn
    // this into day zero, silently corrupting the campaign timeline.
    expectMalformedSaveRejected("bad_current_day",
                                "UPDATE game_meta SET value = 'abc' WHERE key = 'current_day';");
}

void test_malformed_save_current_day_before_event_history_is_rejected() {
    // The event log is persisted campaign history. Loading a save whose current
    // date predates its own event history would corrupt the audit timeline.
    expectMalformedSaveRejected("current_day_before_events",
                                "UPDATE game_meta SET value = '0' WHERE key = 'current_day';");
}

void test_malformed_save_future_event_day_is_rejected() {
    // Future-dated events imply history that has not happened yet in the loaded
    // simulation. The validator must reject them even though the row is otherwise
    // well-formed.
    expectMalformedSaveRejected("future_event_day",
                                "UPDATE event_log SET day = 999 WHERE id = (SELECT MAX(id) FROM event_log);");
}

void test_malformed_save_decreasing_event_days_are_rejected() {
    // Event IDs define append order. Event days may repeat within a day, but they
    // must not move backwards as IDs increase.
    expectMalformedSaveRejected("event_day_decreases",
                                "UPDATE event_log SET day = 0 WHERE id = (SELECT MAX(id) FROM event_log);");
}

void test_malformed_save_invalid_enum_is_rejected() {
    // Invalid enum ordinals must not be raw-cast into domain state because later
    // switch/visitor code assumes only known alternatives.
    expectMalformedSaveRejected("invalid_enum", "UPDATE bodies SET body_type = 99 WHERE id = 1;", true);
    expectMalformedSaveRejected("invalid_strategic_zone", "UPDATE bodies SET strategic_zone = 99 WHERE id = 1;", true);
}

void test_malformed_save_negative_stockpile_is_rejected() {
    // Stockpiles represent physical quantities. Negative amounts should fail
    // through both schema checks and MineralSet validation on load.
    expectMalformedSaveRejected("negative_stockpile",
                                "UPDATE colony_minerals SET amount = -1.0 WHERE colony_id = 1 AND mineral = 0;",
                                true);
}

void test_malformed_save_broken_ship_fleet_reference_is_rejected() {
    // Foreign-key corruption can happen if users edit saves with constraints off.
    // The loader runs PRAGMA foreign_key_check to reject this before fix-up.
    expectMalformedSaveRejected("broken_ship_fk", "UPDATE ships SET fleet_id = 999 WHERE id = 1;", false, true);
}

void test_malformed_save_broken_fleet_target_reference_is_rejected() {
    // Active movement targets must reference existing bodies; otherwise movement
    // completion would write an impossible body ID into fleet state.
    expectMalformedSaveRejected("broken_fleet_target",
                                "UPDATE fleets SET destination_body_id = 999, order_target_body_id = 999 WHERE id = 1;",
                                false, true);
}

void test_malformed_save_impossible_idle_fleet_order_is_rejected() {
    // Idle fleets must not retain stale destination/target fields. Without this
    // check a UI could display movement state the simulation will never resolve.
    expectMalformedSaveRejected("idle_fleet_with_destination",
                                "UPDATE fleets SET order_type = 0, destination_body_id = 2, "
                                "order_target_body_id = 2, order_days_remaining = 3 WHERE id = 1;",
                                true);
}

void test_malformed_save_bad_queued_fleet_order_is_rejected() {
    // Queued orders are durable player commands. Broken queue rows should fail
    // load before the simulation can promote them into active movement.
    expectMalformedSaveRejected("bad_queued_fleet_order",
                                "INSERT INTO fleet_order_queue(fleet_id, ordinal, order_type, target_body_id) "
                                "VALUES (1, 0, 0, 1);",
                                true);
}

void test_malformed_save_queued_fleet_order_bad_destination_is_rejected() {
    // Foreign-key validation catches hand-edited queued moves that point at
    // missing bodies before app queries or fleet promotion can inspect them.
    expectMalformedSaveRejected("bad_queued_fleet_order_destination",
                                "INSERT INTO fleet_order_queue(fleet_id, ordinal, order_type, target_body_id) "
                                "VALUES (1, 0, 1, 999);",
                                false,
                                true);
}

void test_malformed_save_completed_order_with_build_progress_is_rejected() {
    // Completed production orders are terminal snapshots. Keeping build progress
    // on a completed order would make a future production tick ambiguous.
    expectMalformedSaveRejected("completed_order_with_progress",
                                "UPDATE shipyard_orders SET accumulated_build_points = 1.0 WHERE id = 1;",
                                true);
}

void test_malformed_save_active_order_already_complete_is_rejected() {
    // Active orders must still have work remaining. This prevents corrupted saves
    // from loading an order that should already be in the Completed state.
    expectMalformedSaveRejected("active_order_already_complete",
                                "UPDATE shipyard_orders SET status = 0 WHERE id = 1;",
                                true);
}

void test_malformed_save_unknown_order_status_is_rejected() {
    // Status ordinal 2 was used by an earlier prototype-only production state.
    // That state was removed, so v1 loading treats the old ordinal as malformed.
    expectMalformedSaveRejected("unknown_order_status",
                                "UPDATE shipyard_orders SET status = 2 WHERE id = 1;",
                                true);
}

void test_malformed_save_broken_appointment_person_is_rejected() {
    // Appointments are responsibility records. A slot pointing at a missing
    // person must not load because future effects will trust the person ID.
    expectMalformedSaveRejected("broken_appointment_person",
                                "UPDATE appointments SET person_id = 999 WHERE role = 5;",
                                false,
                                true);
}

void test_malformed_save_broken_appointment_scope_is_rejected() {
    // Appointment target scopes are polymorphic, so SQLite cannot foreign-key
    // them directly. GameState validation must reject missing fleet/colony/
    // institution targets after loading rows.
    expectMalformedSaveRejected("broken_appointment_scope",
                                "UPDATE appointments SET scope_id = 999 WHERE role = 5;",
                                false,
                                false);
}

void test_malformed_save_broken_person_institution_reference_is_rejected() {
    // Personnel are tied to institutions. A broken reference would make later
    // appointment or merit systems operate on an unowned person record.
    expectMalformedSaveRejected("broken_person_institution",
                                "UPDATE people SET institution_id = 999 WHERE id = 1;",
                                false,
                                true);
}

void test_malformed_save_negative_person_counter_is_rejected() {
    // Service record counters are append-only audit data. Negative values are
    // invalid even when imported from hand-edited SQLite files.
    expectMalformedSaveRejected("negative_person_counter",
                                "UPDATE people SET commendations = -1 WHERE id = 1;",
                                true);
}

void test_malformed_save_broken_owner_institution_reference_is_rejected() {
    // Ownership is soft gameplay data in v1, but a present owner ID must still
    // point at a real institution so future access/trust rules have safe inputs.
    expectMalformedSaveRejected("broken_colony_owner",
                                "UPDATE colonies SET owner_institution_id = 999 WHERE id = 1;",
                                false,
                                true);
    expectMalformedSaveRejected("broken_fleet_owner",
                                "UPDATE fleets SET owner_institution_id = 999 WHERE id = 1;",
                                false,
                                true);
}

void test_malformed_save_negative_colony_mines_is_rejected() {
    // SQLite CHECK constraints are not authoritative because external tools can
    // disable them. The loaded GameState graph must reject negative production.
    expectMalformedSaveRejected("negative_mines",
                                "UPDATE colonies SET mines = -5.0 WHERE id = 1;",
                                true);
}

void test_malformed_save_non_finite_numeric_value_is_rejected() {
    // Non-finite doubles are invalid domain quantities even when SQLite accepts
    // the textual/REAL representation. Forecasting and simulation math require
    // finite values.
    expectMalformedSaveRejected("infinite_build_points",
                                "UPDATE ship_components SET build_points = 1e999 WHERE id = 1;");
}

void test_malformed_component_snapshot_is_rejected() {
    expectMalformedSaveRejected("missing_component_cost",
                            "DELETE FROM ship_component_material_costs WHERE component_id = 1 AND material = 0;");
    expectMalformedSaveRejected("bad_component_quantity",
                            "UPDATE ship_class_installs SET quantity = 0 WHERE ship_class_id = 1 AND ordinal = 0;",
                            true);
    expectMalformedSaveRejected("bad_component_kind",
                            "UPDATE ship_components SET kind = 99 WHERE id = 1;", true);
    expectMalformedSaveRejected("missing_component_reference",
                            "UPDATE ship_class_installs SET component_id = 999 WHERE ship_class_id = 1 AND ordinal = 0;",
                            false, true);
    expectMalformedSaveRejected("bad_root_revision",
                            "UPDATE ship_classes SET revision = 2 WHERE id = 1;");
}

void test_malformed_save_event_payload_missing_field_is_rejected() {
    // Event payload JSON is typed audit data. Missing fields should fail instead
    // of producing default IDs or zero amounts.
    expectMalformedSaveRejected("bad_event_payload", "UPDATE event_log SET payload_json = '{}' WHERE id = 1;");
}

} // namespace

int main() {
    try {
        test_waiting_survey_program_round_trips();
        test_active_survey_program_continues_after_save_load();
        test_completion_return_issue_allows_amend_and_suspend_after_load();
        test_pending_survey_home_amendment_round_trips();
        test_malformed_survey_history_rows_are_rejected();
        test_malformed_survey_partial_rows_are_rejected();
        test_malformed_survey_program_rows_are_rejected();
        test_zero_capacity_waiting_order_round_trip();
        test_design_revision_round_trip();
        test_non_constructible_revision_round_trip();
        test_sqlite_save_load_round_trip();
        test_institution_identity_and_ownership_round_trip();
        test_personnel_registry_round_trips();
        test_manual_processing_policy_state_round_trips();
        test_daily_economy_snapshots_are_runtime_only();
        test_malformed_save_missing_schema_version_is_rejected();
        test_malformed_save_unsupported_schema_version_is_rejected();
        test_malformed_save_multiple_schema_versions_are_rejected();
        test_malformed_save_missing_id_counter_is_rejected();
        test_malformed_save_stale_id_counter_is_rejected();
        test_malformed_save_non_numeric_current_day_is_rejected();
        test_malformed_save_current_day_before_event_history_is_rejected();
        test_malformed_save_future_event_day_is_rejected();
        test_malformed_save_decreasing_event_days_are_rejected();
        test_malformed_save_invalid_enum_is_rejected();
        test_malformed_save_negative_stockpile_is_rejected();
        test_malformed_save_broken_ship_fleet_reference_is_rejected();
        test_malformed_save_broken_fleet_target_reference_is_rejected();
        test_malformed_save_impossible_idle_fleet_order_is_rejected();
        test_malformed_save_bad_queued_fleet_order_is_rejected();
        test_malformed_save_queued_fleet_order_bad_destination_is_rejected();
        test_malformed_save_completed_order_with_build_progress_is_rejected();
        test_malformed_save_active_order_already_complete_is_rejected();
        test_malformed_save_unknown_order_status_is_rejected();
        test_malformed_save_broken_appointment_person_is_rejected();
        test_malformed_save_broken_appointment_scope_is_rejected();
        test_malformed_save_broken_person_institution_reference_is_rejected();
        test_malformed_save_negative_person_counter_is_rejected();
        test_malformed_save_broken_owner_institution_reference_is_rejected();
        test_malformed_save_negative_colony_mines_is_rejected();
        test_malformed_save_non_finite_numeric_value_is_rejected();
        test_malformed_component_snapshot_is_rejected();
        test_malformed_save_event_payload_missing_field_is_rejected();
    } catch (const std::exception& ex) {
        std::cerr << "Save/load test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal save/load tests passed.\n";
    return EXIT_SUCCESS;
}
