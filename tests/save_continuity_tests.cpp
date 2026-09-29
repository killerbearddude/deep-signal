#include "sim/EquipmentServiceRules.h"
#include "save/SaveGameRepository.h"
#include "sim/Commands.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

// Behavioral save continuity tests. Vector order is gameplay state: mining,
// shipyard capacity, fleet movement, and pooled fuel payment consume it. These
// comparisons deliberately never sort IDs or database rows before checking.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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
    explicit TestFailure(const std::string& message) : std::runtime_error{message} {}
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

template <typename T>
void same(const T& left, const T& right, const std::string& path) {
    require(left == right, path + " differs");
}

void near(const double left, const double right, const std::string& path) {
    // SQLite stores these as binary doubles. A strict absolute tolerance absorbs
    // only last-bit arithmetic drift; it cannot hide a unit of fuel/material.
    constexpr double tolerance = 1.0e-8;
    require(std::isfinite(left) && std::isfinite(right), path + " is not finite");
    require(std::abs(left - right) <= tolerance, path + " differs");
}

template <typename T, typename Compare>
void sequence(const std::vector<T>& left, const std::vector<T>& right,
              const std::string& path, Compare compare) {
    same(left.size(), right.size(), path + ".size");
    for (std::size_t index = 0; index < left.size(); ++index) {
        compare(left[index], right[index], path + "[" + std::to_string(index) + "]");
    }
}

template <typename T>
void exactSequence(const std::vector<T>& left, const std::vector<T>& right,
                   const std::string& path) {
    sequence(left, right, path, [](const T& a, const T& b, const std::string& itemPath) {
        same(a, b, itemPath);
    });
}

#define EXACT(field) same(a.field, b.field, path + "." #field)
#define FLOAT(field) near(a.field, b.field, path + "." #field)

void compare(const deep::IdCounters& a, const deep::IdCounters& b, const std::string& path) {
    EXACT(nextStarSystemId); EXACT(nextBodyId); EXACT(nextColonyId);
    EXACT(nextInstitutionId); EXACT(nextPersonId); EXACT(nextShipClassId);
    EXACT(nextShipyardOrderId); EXACT(nextShipId); EXACT(nextFleetId);
    EXACT(nextShipComponentId);
    EXACT(nextEventId);
    EXACT(nextSiteId); EXACT(nextSiteDevelopmentProgramId);
}

void compare(const deep::MineralSet& a, const deep::MineralSet& b, const std::string& path) {
    for (std::size_t index = 0; index < deep::mineralCount(); ++index) {
        near(a.amount[index], b.amount[index], path + "[" + std::to_string(index) + "]");
    }
}

void compare(const deep::ProcessedMaterialSet& a, const deep::ProcessedMaterialSet& b,
             const std::string& path) {
    for (std::size_t index = 0; index < deep::processedMaterialCount(); ++index) {
        near(a.amount[index], b.amount[index], path + "[" + std::to_string(index) + "]");
    }
}

void compare(const deep::StarSystem& a, const deep::StarSystem& b, const std::string& path) {
    EXACT(id); EXACT(name);
}

void compare(const deep::Institution& a, const deep::Institution& b, const std::string& path) {
    EXACT(id); EXACT(name); EXACT(type);
}

void compare(const deep::PersonCompetencies& a, const deep::PersonCompetencies& b,
             const std::string& path) {
    EXACT(logistics); EXACT(industry); EXACT(survey); EXACT(command);
    EXACT(administration); EXACT(engineering); EXACT(intelligence);
    EXACT(crisisManagement);
}

void compare(const deep::PersonServiceRecord& a, const deep::PersonServiceRecord& b,
             const std::string& path) {
    EXACT(successfulAssignments); EXACT(failedAssignments);
    EXACT(commendations); EXACT(controversies);
}

void compare(const deep::Person& a, const deep::Person& b, const std::string& path) {
    EXACT(id); EXACT(name); EXACT(institutionId);
    compare(a.competencies, b.competencies, path + ".competencies");
    EXACT(seniorityLevel);
    compare(a.serviceRecord, b.serviceRecord, path + ".serviceRecord");
}

void compare(const deep::Appointment& a, const deep::Appointment& b, const std::string& path) {
    EXACT(role); EXACT(scopeType); EXACT(scopeId); EXACT(personId); EXACT(appointedDay);
}

void compare(const deep::Body& a, const deep::Body& b, const std::string& path) {
    EXACT(id); EXACT(systemId); EXACT(name); EXACT(type); EXACT(strategicZone);
    EXACT(parentBodyId); FLOAT(orbitalRadiusKm); FLOAT(orbitalPeriodDays);
    FLOAT(phaseRadians); FLOAT(displayRadius); FLOAT(x); FLOAT(y);
}

void compare(const deep::MineralDeposit& a, const deep::MineralDeposit& b,
             const std::string& path) {
    EXACT(bodyId); EXACT(mineral); FLOAT(remaining); FLOAT(accessibility);
}

void compare(const deep::ProcessingAllocation& a, const deep::ProcessingAllocation& b,
             const std::string& path) {
    EXACT(material); FLOAT(weight);
}

void compare(const deep::Colony& a, const deep::Colony& b, const std::string& path) {
    EXACT(id); EXACT(bodyId); EXACT(name);
    compare(a.stockpile, b.stockpile, path + ".stockpile");
    compare(a.processedStockpile, b.processedStockpile, path + ".processedStockpile");
    compare(a.processedProductionTotals, b.processedProductionTotals, path + ".processedProductionTotals");
    FLOAT(mines); FLOAT(processorCapacity); FLOAT(shipyardCapacity);
    EXACT(processingPolicy);
    sequence(a.manualProcessingAllocations, b.manualProcessingAllocations,
             path + ".manualProcessingAllocations",
             [](const deep::ProcessingAllocation& left, const deep::ProcessingAllocation& right,
                const std::string& itemPath) { compare(left, right, itemPath); });
    EXACT(ownerInstitutionId);
}

void compare(const deep::ShipComponentDefinition& a, const deep::ShipComponentDefinition& b,
             const std::string& path) {
    EXACT(id); EXACT(name); EXACT(kind);
    FLOAT(mass); FLOAT(volume); FLOAT(internalVolumeCapacity);
    FLOAT(powerGeneration); FLOAT(powerDemand); FLOAT(propellantCapacity);
    FLOAT(surveyCapability); FLOAT(buildPoints);
    compare(a.buildCost, b.buildCost, path + ".buildCost");
}

void compare(const deep::ShipClass& a, const deep::ShipClass& b, const std::string& path) {
    EXACT(id); EXACT(name); EXACT(revision); EXACT(role); EXACT(basedOnClassId);
    exactSequence(a.components, b.components, path + ".components");
    FLOAT(speedKmPerDay);
}

void compare(const deep::ShipyardOrder& a, const deep::ShipyardOrder& b,
             const std::string& path) {
    EXACT(id); EXACT(colonyId); EXACT(shipClassId); EXACT(quantityRequested);
    EXACT(quantityCompleted); FLOAT(accumulatedBuildPoints); EXACT(status);
}

void compare(const deep::Ship& a, const deep::Ship& b, const std::string& path) {
    EXACT(id); EXACT(shipClassId); EXACT(name); EXACT(fleetId); FLOAT(fuel);
}

void compare(const deep::MapPosition& a, const deep::MapPosition& b, const std::string& path) {
    FLOAT(x); FLOAT(y);
}

void compare(const deep::FleetOrder& a, const deep::FleetOrder& b, const std::string& path) {
    EXACT(type); EXACT(targetBodyId); EXACT(daysRemaining); EXACT(departureBodyId);
    EXACT(departureDay); EXACT(arrivalDay);
    compare(a.departurePosition, b.departurePosition, path + ".departurePosition");
    compare(a.projectedArrivalPosition, b.projectedArrivalPosition,
            path + ".projectedArrivalPosition");
    FLOAT(transitDistanceKm); FLOAT(burnAccelerationG);
    compare(a.routeCurveControlPoint, b.routeCurveControlPoint,
            path + ".routeCurveControlPoint");
}

void compare(const deep::QueuedFleetOrder& a, const deep::QueuedFleetOrder& b,
             const std::string& path) {
    EXACT(type); EXACT(targetBodyId);
}

void compare(const deep::Fleet& a, const deep::Fleet& b, const std::string& path) {
    EXACT(id); EXACT(name); EXACT(currentBodyId); EXACT(destinationBodyId);
    exactSequence(a.shipIds, b.shipIds, path + ".shipIds");
    compare(a.activeOrder, b.activeOrder, path + ".activeOrder");
    sequence(a.queuedOrders, b.queuedOrders, path + ".queuedOrders",
             [](const deep::QueuedFleetOrder& left, const deep::QueuedFleetOrder& right,
                const std::string& itemPath) { compare(left, right, itemPath); });
    EXACT(ownerInstitutionId);
}

void compare(const deep::MineralExtractedEvent& a, const deep::MineralExtractedEvent& b,
             const std::string& path) {
    EXACT(colonyId); EXACT(bodyId); EXACT(mineral); FLOAT(amount); FLOAT(remainingDeposit);
}

void compare(const deep::ShipyardOrderCreatedEvent& a, const deep::ShipyardOrderCreatedEvent& b,
             const std::string& path) {
    EXACT(orderId); EXACT(colonyId); EXACT(shipClassId); EXACT(quantity);
}

void compare(const deep::ShipClassRevisionCreatedEvent& a, const deep::ShipClassRevisionCreatedEvent& b,
             const std::string& path) {
    EXACT(shipClassId); EXACT(basedOnClassId); EXACT(revision);
}

void compare(const deep::ShipCompletedEvent& a, const deep::ShipCompletedEvent& b,
             const std::string& path) {
    EXACT(orderId); EXACT(colonyId); EXACT(shipId); EXACT(fleetId); EXACT(shipClassId);
}

void compare(const deep::FleetOrderAssignedEvent& a, const deep::FleetOrderAssignedEvent& b,
             const std::string& path) {
    EXACT(fleetId); EXACT(originBodyId); EXACT(destinationBodyId); EXACT(daysRemaining);
}

void compare(const deep::FleetArrivedEvent& a, const deep::FleetArrivedEvent& b,
             const std::string& path) {
    EXACT(fleetId); EXACT(destinationBodyId);
}

void compare(const deep::ResourceSurveyCompletedEvent& a,
             const deep::ResourceSurveyCompletedEvent& b, const std::string& path) {
    EXACT(fleetId); EXACT(bodyId); EXACT(observationBatchId);
}

void compare(const deep::AnalysisProgramAuditEvent& a,const deep::AnalysisProgramAuditEvent& b,const std::string& path) {
    EXACT(programId); EXACT(kind); EXACT(jobId); EXACT(detail);
}
void compare(const deep::SiteDevelopmentAuditEvent& a,const deep::SiteDevelopmentAuditEvent& b,const std::string& path) {
    EXACT(programId); EXACT(kind); EXACT(siteId); EXACT(fleetId); EXACT(teamId); EXACT(workshopShipId);
    EXACT(leaderId); EXACT(charterRevision); EXACT(packageRow); FLOAT(amount); EXACT(detail);
}
void compare(const deep::SiteOperatingAuditEvent& a,const deep::SiteOperatingAuditEvent& b,const std::string& path) {
    EXACT(siteId); EXACT(kind); EXACT(operatingRevision); EXACT(cause); EXACT(episodeStartedDay); FLOAT(amount); EXACT(detail);
}

void compare(const deep::CommandRejectedEvent& a, const deep::CommandRejectedEvent& b,
             const std::string& path) {
    EXACT(reason);
}

void compare(const deep::SimEventPayload& a, const deep::SimEventPayload& b,
             const std::string& path) {
    same(a.index(), b.index(), path + ".variant");
    std::visit([&](const auto& left, const auto& right) {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (std::is_same_v<Left, Right>) {
            compare(left, right, path);
        } else {
            // The index assertion above reports this before this branch runs.
            throw TestFailure{path + ".variant differs"};
        }
    }, a, b);
}

void compare(const deep::SimEvent& a, const deep::SimEvent& b, const std::string& path) {
    EXACT(id); EXACT(day); EXACT(severity);
    compare(a.payload, b.payload, path + ".payload");
}

void compare(const deep::DailyEconomySnapshot& a, const deep::DailyEconomySnapshot& b,
             const std::string& path) {
    EXACT(day); EXACT(colonyId); EXACT(bodyId); EXACT(mineral);
    FLOAT(amount); FLOAT(remainingDeposit);
}

void compareDurableState(const deep::GameState& a, const deep::GameState& b,
                         const std::string& path) {
    same(a.date.day, b.date.day, path + ".date.day");
    compare(a.ids, b.ids, path + ".ids");
#define RECORDS(field) sequence(a.field, b.field, path + "." #field, \
    [](const auto& left, const auto& right, const std::string& itemPath) { \
        compare(left, right, itemPath); \
    })
    RECORDS(starSystems); RECORDS(institutions); RECORDS(people);
    RECORDS(appointments); RECORDS(bodies); RECORDS(mineralDeposits);
    RECORDS(colonies); RECORDS(shipComponents); RECORDS(shipClasses); RECORDS(shipyardOrders);
    RECORDS(ships); RECORDS(fleets); RECORDS(eventLog);
#undef RECORDS
    // dailyEconomySnapshots before a save boundary are session-only telemetry.
}

#undef EXACT
#undef FLOAT

class UniqueSavePath final {
public:
    UniqueSavePath() {
        std::string pattern = (std::filesystem::temp_directory_path() /
                               "deep_signal_h1_continuity_XXXXXX").string();
        const int fd = mkstemp(pattern.data());
        require(fd >= 0, "create unique temporary save path");
        close(fd);
        path_ = pattern;
        std::filesystem::remove(path_);
    }

    ~UniqueSavePath() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-journal", ignored);
    }

    UniqueSavePath(const UniqueSavePath&) = delete;
    UniqueSavePath& operator=(const UniqueSavePath&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void checkpoint(const deep::Simulation& uninterrupted, const deep::Simulation& continued,
                const std::string& label) {
    deep::validateGameState(uninterrupted.state());
    deep::validateGameState(continued.state());
    compareDurableState(uninterrupted.state(), continued.state(), label);
}

void compareNewTelemetry(const deep::Simulation& uninterrupted,
                         const std::size_t uninterruptedStart,
                         const deep::Simulation& continued,
                         const std::size_t continuedStart,
                         const std::string& label) {
    const auto& left = uninterrupted.state().dailyEconomySnapshots;
    const auto& right = continued.state().dailyEconomySnapshots;
    require(left.size() >= uninterruptedStart, label + ": telemetry start beyond end");
    require(right.size() >= continuedStart, label + ": loaded telemetry start beyond end");
    same(left.size() - uninterruptedStart, right.size() - continuedStart,
         label + ".telemetry.size");
    for (std::size_t index = 0; index < right.size() - continuedStart; ++index) {
        compare(left[uninterruptedStart + index], right[continuedStart + index],
                label + ".telemetry[" + std::to_string(index) + "]");
    }
}

void saveLoadCheckpoint(const deep::Simulation& uninterrupted,
                        std::unique_ptr<deep::Simulation>& continued,
                        const std::filesystem::path& path,
                        const std::string& label) {
    deep::save::SaveGameRepository::save(path, continued->state());
    deep::GameState loaded = deep::save::SaveGameRepository::load(path);
    require(loaded.dailyEconomySnapshots.empty(), label + ": load starts without telemetry");
    deep::validateGameState(loaded);
    compareDurableState(continued->state(), loaded, label + ".roundTrip");
    compareDurableState(uninterrupted.state(), loaded, label + ".baseline");
    continued = std::make_unique<deep::Simulation>(std::move(loaded));
}

deep::Colony& colonyById(deep::GameState& state, const deep::ColonyId id) {
    for (deep::Colony& colony : state.colonies) {
        if (colony.id == id) {
            return colony;
        }
    }
    throw TestFailure{"fixture colony not found"};
}

const deep::Colony& colonyById(const deep::GameState& state, const deep::ColonyId id) {
    for (const deep::Colony& colony : state.colonies) {
        if (colony.id == id) {
            return colony;
        }
    }
    throw TestFailure{"result colony not found"};
}

deep::GameState sharedDepositFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::ColonyId marsColonyId = state.colonies.at(1).id;
    const deep::BodyId marsId = state.colonies.at(1).bodyId;
    deep::Colony first = state.colonies.at(1);
    first.id = deep::ColonyId{state.ids.nextColonyId++};
    first.name = "First vector colony, higher ID";
    first.processorCapacity = 0.0;
    first.shipyardCapacity = 0.0;
    first.mines = 4.0;
    state.colonies.insert(state.colonies.begin() + 1, first);

    deep::Colony& second = colonyById(state, marsColonyId);
    second.processorCapacity = 0.0;
    second.shipyardCapacity = 0.0;
    second.mines = 4.0;
    require(state.colonies.at(1).id.value > state.colonies.at(2).id.value,
            "shared-deposit fixture reverses colony ID order");

    for (deep::MineralDeposit& deposit : state.mineralDeposits) {
        if (deposit.bodyId == marsId) {
            deposit.remaining = deposit.mineral == deep::Mineral::Iron ? 3.6 : 0.0;
        }
    }
    deep::validateGameState(state);
    return state;
}

void testSharedDepositCompetition() {
    deep::GameState a = sharedDepositFixture();
    deep::GameState b = sharedDepositFixture(); // Independently constructed.
    compareDurableState(a, b, "shared.initial");
    const deep::ColonyId firstId = a.colonies.at(1).id;
    const deep::ColonyId secondId = a.colonies.at(2).id;
    const double firstIron = colonyById(a, firstId).stockpile.get(deep::Mineral::Iron);
    const double secondIron = colonyById(a, secondId).stockpile.get(deep::Mineral::Iron);
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "shared.beforeDay1");
    const std::size_t telemetryStart = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t continuedTelemetryStart = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(1);
    continued->advanceDays(1);
    checkpoint(uninterrupted, *continued, "shared.day1");
    compareNewTelemetry(uninterrupted, telemetryStart, *continued,
                        continuedTelemetryStart, "shared.day1");

    const auto& result = uninterrupted.state();
    near(colonyById(result, firstId).stockpile.get(deep::Mineral::Iron) - firstIron,
         3.6, "first vector colony receives full iron allocation");
    near(colonyById(result, secondId).stockpile.get(deep::Mineral::Iron) - secondIron,
         0.0, "lower-ID second colony receives no exhausted iron");
    const auto iron = std::find_if(result.mineralDeposits.begin(), result.mineralDeposits.end(),
                                   [marsId = result.colonies.at(1).bodyId](const deep::MineralDeposit& deposit) {
                                       return deposit.bodyId == marsId && deposit.mineral == deep::Mineral::Iron;
                                   });
    require(iron != result.mineralDeposits.end(), "shared iron deposit exists");
    near(iron->remaining, 0.0, "shared iron deposit is exhausted after first colony");

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "shared.beforeDay2");
    const std::size_t nextTelemetryStart = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t nextContinuedTelemetryStart = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(1);
    continued->advanceDays(1);
    checkpoint(uninterrupted, *continued, "shared.day2");
    compareNewTelemetry(uninterrupted, nextTelemetryStart, *continued,
                        nextContinuedTelemetryStart, "shared.day2");
}

deep::GameState fifoShipyardFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::ColonyId colonyId = state.colonies.front().id;
    const deep::ShipClassId shipClassId = state.shipClasses.front().id;
    const deep::ShipyardOrderId lower{state.ids.nextShipyardOrderId++};
    const deep::ShipyardOrderId higher{state.ids.nextShipyardOrderId++};
    state.shipyardOrders.push_back(deep::ShipyardOrder{
        .id = higher, .colonyId = colonyId, .shipClassId = shipClassId,
        .quantityRequested = 1
    });
    state.shipyardOrders.push_back(deep::ShipyardOrder{
        .id = lower, .colonyId = colonyId, .shipClassId = shipClassId,
        .quantityRequested = 1
    });
    require(state.shipyardOrders.front().id.value > state.shipyardOrders.back().id.value,
            "shipyard fixture reverses order ID versus execution order");
    deep::validateGameState(state);
    return state;
}

void testFifoShipyardCompetition() {
    deep::GameState a = fifoShipyardFixture();
    deep::GameState b = fifoShipyardFixture(); // Independently constructed.
    compareDurableState(a, b, "shipyard.initial");
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "shipyard.beforeDay1");
    for (int day = 1; day <= 5; ++day) {
        const std::size_t telemetryStart = uninterrupted.state().dailyEconomySnapshots.size();
        const std::size_t continuedTelemetryStart = continued->state().dailyEconomySnapshots.size();
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "shipyard.day" + std::to_string(day));
        compareNewTelemetry(uninterrupted, telemetryStart, *continued, continuedTelemetryStart,
                            "shipyard.day" + std::to_string(day));
        if (day == 2) {
            saveLoadCheckpoint(uninterrupted, continued, save.path(), "shipyard.afterDay2");
        }
    }

    const deep::GameState& result = uninterrupted.state();
    require(result.shipyardOrders.size() == 2, "two shipyard orders remain tracked");
    require(result.shipyardOrders.at(0).id.value > result.shipyardOrders.at(1).id.value,
            "shipyard vector order remains opposite ID order");
    require(result.shipyardOrders.at(0).status == deep::ShipyardOrderStatus::Completed,
            "first vector order finishes first despite higher ID");
    require(result.shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Active,
            "second vector order remains active after first completion");
    require(result.shipyardOrders.at(1).quantityCompleted == 0,
            "second vector order has completed no hulls");
    near(result.shipyardOrders.at(1).accumulatedBuildPoints, 50.0,
         "second vector order receives only leftover capacity on day five");
    require(result.ships.size() == 1 && result.fleets.size() == 1,
            "only one ship and fleet are created by day five");
    require(std::holds_alternative<deep::ShipCompletedEvent>(result.eventLog.back().payload),
            "ship completion event emitted");
    same(std::get<deep::ShipCompletedEvent>(result.eventLog.back().payload).orderId,
         result.shipyardOrders.front().id, "first completion event identifies higher-ID first order");

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "shipyard.afterDay5");
    for (int day = 6; day <= 10; ++day) {
        const std::size_t telemetryStart = uninterrupted.state().dailyEconomySnapshots.size();
        const std::size_t continuedTelemetryStart = continued->state().dailyEconomySnapshots.size();
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "shipyard.day" + std::to_string(day));
        compareNewTelemetry(uninterrupted, telemetryStart, *continued, continuedTelemetryStart,
                            "shipyard.day" + std::to_string(day));
    }
    require(uninterrupted.state().shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Completed,
            "second vector order eventually completes");
    require(uninterrupted.state().ships.size() == 2,
            "two hulls are complete after ten days of shared capacity");
}

void testWaitingShipyardOrderContinuesAfterCapacityRecovery() {
    deep::GameState initial = deep::createHomeSystemScenario();
    initial.colonies.front().shipyardCapacity = 0.0;
    initial.colonies.front().processorCapacity = 0.0;
    deep::Simulation uninterrupted{initial};
    auto continued = std::make_unique<deep::Simulation>(std::move(initial));
    const deep::ColonyId colonyId = uninterrupted.state().colonies.front().id;
    const deep::ShipClassId shipClassId = uninterrupted.state().shipClasses.front().id;
    const deep::AssignShipyardBuildCommand command{
        .colonyId = colonyId, .shipClassId = shipClassId, .quantity = 1
    };
    require(uninterrupted.execute(command).ok && continued->execute(command).ok,
            "both branches accept waiting order");
    uninterrupted.advanceDays(30);
    continued->advanceDays(30);
    checkpoint(uninterrupted, *continued, "capacityWait.beforeSave");
    require(uninterrupted.state().shipyardOrders.front().accumulatedBuildPoints == 0.0,
            "thirty days without capacity produce no construction");

    UniqueSavePath save;
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "capacityWait.roundTrip");
    const deep::ShipyardOrderId orderId = continued->state().shipyardOrders.front().id;
    deep::GameState left = uninterrupted.state();
    deep::GameState right = continued->state();
    left.colonies.front().shipyardCapacity = 100.0;
    right.colonies.front().shipyardCapacity = 100.0;
    uninterrupted = deep::Simulation{std::move(left)};
    continued = std::make_unique<deep::Simulation>(std::move(right));
    for (int day = 1; day <= 5; ++day) {
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "capacityWait.recoveredDay" + std::to_string(day));
        if (day == 1) {
            require(uninterrupted.state().shipyardOrders.front().accumulatedBuildPoints > 0.0,
                    "same order progresses after capacity returns");
        }
    }
    require(uninterrupted.state().shipyardOrders.size() == 1 &&
            uninterrupted.state().shipyardOrders.front().id == orderId &&
            uninterrupted.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Completed,
            "loaded order completes without resubmission or replacement");
}

void testDesignedRevisionContinuesAcrossSave() {
    deep::GameState initial = deep::createHomeSystemScenario();
    initial.colonies.front().processorCapacity = 0.0;
    deep::Simulation uninterrupted{initial};
    auto continued = std::make_unique<deep::Simulation>(std::move(initial));
    auto draft = uninterrupted.state().shipClasses.front().components;
    draft.at(2).quantity = 2;
    const deep::CreateShipClassRevisionCommand revision{
        .name = "Long Range", .role = deep::ShipRole::Survey,
        .basedOnClassId = uninterrupted.state().shipClasses.front().id,
        .components = draft
    };
    require(uninterrupted.execute(revision).ok && continued->execute(revision).ok,
            "both branches create same immutable revision");
    const auto classId = uninterrupted.state().shipClasses.back().id;
    const auto colonyId = uninterrupted.state().colonies.front().id;
    const deep::AssignShipyardBuildCommand order{colonyId, classId, 1};
    require(uninterrupted.execute(order).ok && continued->execute(order).ok,
            "both branches order exact new revision");
    uninterrupted.advanceDays(2);
    continued->advanceDays(2);
    checkpoint(uninterrupted, *continued, "design.beforeSave");
    UniqueSavePath save;
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "design.roundTrip");
    for (int day = 3; day <= 6; ++day) {
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "design.day" + std::to_string(day));
    }
    require(uninterrupted.state().ships.size() == 1 &&
            uninterrupted.state().ships.front().shipClassId == classId &&
            uninterrupted.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Completed,
            "same revised hull and fuel transfer complete after load");
}

const deep::Fleet& fleetById(const deep::GameState& state, const deep::FleetId id) {
    for (const deep::Fleet& fleet : state.fleets) {
        if (fleet.id == id) {
            return fleet;
        }
    }
    throw TestFailure{"result fleet not found"};
}

const deep::Ship& shipById(const deep::GameState& state, const deep::ShipId id) {
    for (const deep::Ship& ship : state.ships) {
        if (ship.id == id) {
            return ship;
        }
    }
    throw TestFailure{"result ship not found"};
}

deep::FleetId addSingleShipFleet(deep::GameState& state, const deep::BodyId bodyId,
                                 const std::string& label) {
    const deep::FleetId fleetId{state.ids.nextFleetId++};
    const deep::ShipId shipId{state.ids.nextShipId++};
    const deep::ShipClass& shipClass = state.shipClasses.front();
    state.ships.push_back(deep::Ship{
        .id = shipId, .shipClassId = shipClass.id,
        .name = label + " Hull", .fleetId = fleetId, .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity
    });
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.fleets.push_back(deep::Fleet{
        .id = fleetId, .name = label, .currentBodyId = bodyId,
        .destinationBodyId = std::nullopt, .shipIds = {shipId},
        .activeOrder = {}, .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    return fleetId;
}

void executeBoth(deep::Simulation& uninterrupted, deep::Simulation& continued,
                 const deep::SimCommand& command, const std::string& label) {
    const deep::CommandResult left = uninterrupted.execute(command);
    const deep::CommandResult right = continued.execute(command);
    require(left.ok && right.ok, label + ": command must be accepted on both paths");
    checkpoint(uninterrupted, continued, label);
}

deep::GameState perHullFuelFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = state.bodies.front().id;
    const deep::ShipClassId classId = state.shipClasses.front().id;
    const deep::FleetId fleetId{state.ids.nextFleetId++};
    const deep::ShipId lowId{state.ids.nextShipId++};
    const deep::ShipId highId{state.ids.nextShipId++};
    state.ships.push_back(deep::Ship{
        .id = lowId, .shipClassId = classId, .name = "Later payer, lower ID",
        .fleetId = fleetId, .fuel = 1'000.0
    });
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.ships.push_back(deep::Ship{
        .id = highId, .shipClassId = classId, .name = "First payer, higher ID",
        .fleetId = fleetId, .fuel = 25.0
    });
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.fleets.push_back(deep::Fleet{
        .id = fleetId, .name = "Roster fuel ordering fleet", .currentBodyId = terraId,
        .destinationBodyId = std::nullopt, .shipIds = {highId, lowId},
        .activeOrder = {}, .queuedOrders = {}, .ownerInstitutionId = std::nullopt
    });
    require(state.ships[0].id == lowId && state.fleets[0].shipIds[0] == highId,
            "global ship order differs from fleet roster payment order");
    deep::validateGameState(state);
    return state;
}

void testPerHullFuelPayment() {
    deep::GameState a = perHullFuelFixture();
    deep::GameState b = perHullFuelFixture();
    compareDurableState(a, b, "fuel.initial");
    const deep::FleetId fleetId = a.fleets.front().id;
    const deep::ShipId firstPayer = a.fleets.front().shipIds[0];
    const deep::ShipId secondPayer = a.fleets.front().shipIds[1];
    const deep::BodyId marsId = a.bodies[1].id;
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "fuel.beforeMove");
    executeBoth(uninterrupted, *continued,
                deep::MoveFleetCommand{.fleetId = fleetId, .destinationBodyId = marsId},
                "fuel.move");
    const deep::GameState& result = uninterrupted.state();
    const deep::Fleet& moving = fleetById(result, fleetId);
    require(moving.activeOrder.type == deep::FleetOrderType::MoveToBody,
            "real move begins before comparing per-hull fuel");
    const double cost = moving.activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit;
    require(cost > 25.0 && cost < 1'000.0,
            "fuel fixture route crosses the first hull's partial tank");
    near(shipById(result, firstPayer).fuel, 0.0,
         "first roster hull pays its full 25 fuel before second hull");
    near(shipById(result, secondPayer).fuel, 1'000.0 - (cost - 25.0),
         "second roster hull pays only the remaining route cost");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "fuel.midTransit");
    const std::size_t baselineTelemetry = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t loadedTelemetry = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(1);
    continued->advanceDays(1);
    checkpoint(uninterrupted, *continued, "fuel.day1");
    compareNewTelemetry(uninterrupted, baselineTelemetry, *continued, loadedTelemetry,
                        "fuel.day1");
    near(shipById(continued->state(), firstPayer).fuel, 0.0,
         "loaded first hull retains paid fuel after a day");
}

deep::GameState sameDayArrivalsFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = state.bodies.front().id;
    const deep::FleetId lower = addSingleShipFleet(state, terraId, "Lower ID fleet");
    const deep::FleetId higher = addSingleShipFleet(state, terraId, "Higher ID fleet");
    std::reverse(state.fleets.begin(), state.fleets.end());
    require(state.fleets.front().id == higher && state.fleets.back().id == lower,
            "same-day arrival fixture reverses fleet ID and tick order");
    deep::validateGameState(state);
    return state;
}

void testSameDayArrivalsAndQueuePromotion() {
    deep::GameState a = sameDayArrivalsFixture();
    deep::GameState b = sameDayArrivalsFixture();
    compareDurableState(a, b, "arrival.initial");
    const deep::FleetId firstId = a.fleets.front().id;
    const deep::FleetId secondId = a.fleets.back().id;
    const deep::BodyId terraId = a.bodies.front().id;
    const deep::BodyId marsId = a.bodies[1].id;
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    executeBoth(uninterrupted, *continued,
                deep::MoveFleetCommand{.fleetId = firstId, .destinationBodyId = marsId},
                "arrival.moveFirst");
    executeBoth(uninterrupted, *continued,
                deep::MoveFleetCommand{.fleetId = secondId, .destinationBodyId = marsId},
                "arrival.moveSecond");
    executeBoth(uninterrupted, *continued,
                deep::QueueFleetMoveOrderCommand{.fleetId = firstId, .destinationBodyId = terraId},
                "arrival.queueFirstReturn");
    executeBoth(uninterrupted, *continued,
                deep::QueueFleetMoveOrderCommand{.fleetId = secondId, .destinationBodyId = terraId},
                "arrival.queueSecondReturn");
    const int eta = fleetById(uninterrupted.state(), firstId).activeOrder.daysRemaining;
    same(eta, fleetById(uninterrupted.state(), secondId).activeOrder.daysRemaining,
         "same-day fleets have equal arrival ETA");
    require(eta > 1, "same-day fixture has a mid-transit save boundary");

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "arrival.beforeDays");
    for (int day = 1; day <= eta; ++day) {
        const std::size_t baselineTelemetry = uninterrupted.state().dailyEconomySnapshots.size();
        const std::size_t loadedTelemetry = continued->state().dailyEconomySnapshots.size();
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "arrival.day" + std::to_string(day));
        compareNewTelemetry(uninterrupted, baselineTelemetry, *continued, loadedTelemetry,
                            "arrival.day" + std::to_string(day));
        if (day == eta / 2) {
            saveLoadCheckpoint(uninterrupted, continued, save.path(), "arrival.midTransit");
        }
    }

    const deep::GameState& result = uninterrupted.state();
    require(fleetById(result, firstId).currentBodyId == marsId &&
            fleetById(result, secondId).currentBodyId == marsId,
            "both fleets arrive at Mars on the same day");
    require(fleetById(result, firstId).activeOrder.targetBodyId == terraId &&
            fleetById(result, secondId).activeOrder.targetBodyId == terraId,
            "both queued returns promote immediately on arrival");
    require(fleetById(result, firstId).queuedOrders.empty() &&
            fleetById(result, secondId).queuedOrders.empty(),
            "promoted queued legs are removed from both queues");
    const std::size_t count = result.eventLog.size();
    require(count >= 4, "arrival and promotion events were appended");
    const auto& firstArrival = result.eventLog[count - 4];
    const auto& firstPromotion = result.eventLog[count - 3];
    const auto& secondArrival = result.eventLog[count - 2];
    const auto& secondPromotion = result.eventLog[count - 1];
    same(firstArrival.id.value + 1, firstPromotion.id.value,
         "first arrival and promotion event IDs are adjacent");
    same(firstPromotion.id.value + 1, secondArrival.id.value,
         "fleet vector order determines next arrival event ID");
    same(secondArrival.id.value + 1, secondPromotion.id.value,
         "second arrival and promotion event IDs are adjacent");
    require(std::holds_alternative<deep::FleetArrivedEvent>(firstArrival.payload) &&
            std::get<deep::FleetArrivedEvent>(firstArrival.payload).fleetId == firstId,
            "higher-ID first-vector fleet arrives first in event log");
    require(std::holds_alternative<deep::FleetOrderAssignedEvent>(firstPromotion.payload) &&
            std::get<deep::FleetOrderAssignedEvent>(firstPromotion.payload).fleetId == firstId,
            "first fleet queue promotion follows its arrival");
    require(std::holds_alternative<deep::FleetArrivedEvent>(secondArrival.payload) &&
            std::get<deep::FleetArrivedEvent>(secondArrival.payload).fleetId == secondId,
            "lower-ID second-vector fleet arrives second in event log");
    require(std::holds_alternative<deep::FleetOrderAssignedEvent>(secondPromotion.payload) &&
            std::get<deep::FleetOrderAssignedEvent>(secondPromotion.payload).fleetId == secondId,
            "second fleet queue promotion follows its arrival");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "arrival.afterPromotion");
}

deep::BodyId bodyIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Body& body : state.bodies) {
        if (body.name == name) {
            return body.id;
        }
    }
    throw TestFailure{"fixture body not found"};
}

deep::GameState mixedOrderingFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = bodyIdByName(state, "Terra");
    const deep::ColonyId terraColonyId = state.colonies.front().id;
    const deep::ShipClassId originalClassId = state.shipClasses.front().id;

    state.starSystems.push_back(deep::StarSystem{
        .id = deep::StarSystemId{state.ids.nextStarSystemId++},
        .name = "Empty secondary system"
    });
    std::reverse(state.starSystems.begin(), state.starSystems.end());
    std::reverse(state.institutions.begin(), state.institutions.end());
    std::reverse(state.people.begin(), state.people.end());
    std::reverse(state.bodies.begin(), state.bodies.end());
    std::reverse(state.colonies.begin(), state.colonies.end());
    std::reverse(state.mineralDeposits.begin(), state.mineralDeposits.end());
    std::reverse(state.appointments.begin(), state.appointments.end());

    deep::ShipClass alternate = state.shipClasses.front();
    alternate.id = deep::ShipClassId{state.ids.nextShipClassId++};
    alternate.name = "Alternate Survey Cutter";
    state.shipClasses.push_back(alternate);
    std::reverse(state.shipClasses.begin(), state.shipClasses.end());

    deep::Colony& terra = colonyById(state, terraColonyId);
    terra.processingPolicy = deep::ProcessingPolicy::Manual;
    terra.manualProcessingAllocations = {
        deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 2.0},
        deep::ProcessingAllocation{.material = deep::ProcessedMaterial::StructuralAlloys, .weight = 3.0},
        deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 1.0}
    };

    const deep::ShipyardOrderId lowOrder{state.ids.nextShipyardOrderId++};
    const deep::ShipyardOrderId highOrder{state.ids.nextShipyardOrderId++};
    state.shipyardOrders.push_back(deep::ShipyardOrder{
        .id = highOrder, .colonyId = terraColonyId,
        .shipClassId = originalClassId, .quantityRequested = 1
    });
    state.shipyardOrders.push_back(deep::ShipyardOrder{
        .id = lowOrder, .colonyId = terraColonyId,
        .shipClassId = originalClassId, .quantityRequested = 1
    });

    const deep::FleetId lowFleet = addSingleShipFleet(state, terraId, "Mixed lower fleet");
    const deep::FleetId highFleet = addSingleShipFleet(state, terraId, "Mixed higher fleet");
    std::reverse(state.ships.begin(), state.ships.end());
    std::reverse(state.fleets.begin(), state.fleets.end());
    state.appointments.push_back(deep::Appointment{
        .role = deep::AppointmentRole::FleetCommander,
        .scopeType = deep::AppointmentScopeType::Fleet,
        .scopeId = highFleet.value,
        .personId = state.people.front().id,
        .appointedDay = state.date.day
    });

    require(state.starSystems.front().id.value > state.starSystems.back().id.value &&
            state.institutions.front().id.value > state.institutions.back().id.value &&
            state.people.front().id.value > state.people.back().id.value &&
            state.bodies.front().id.value > state.bodies.back().id.value &&
            state.colonies.front().id.value > state.colonies.back().id.value &&
            state.shipClasses.front().id.value > state.shipClasses.back().id.value &&
            state.shipyardOrders.front().id.value > state.shipyardOrders.back().id.value &&
            state.ships.front().id.value > state.ships.back().id.value &&
            state.fleets.front().id.value > state.fleets.back().id.value,
            "mixed fixture reverses all ID-bearing vector orders");
    require(state.fleets.front().id == highFleet && state.fleets.back().id == lowFleet,
            "mixed fleet order has expected identities");
    require(state.mineralDeposits.front().bodyId.value >
            state.mineralDeposits.back().bodyId.value,
            "mixed deposit key order differs from ascending body IDs");
    deep::validateGameState(state);
    return state;
}

void testMixedDurableOrdering() {
    deep::GameState a = mixedOrderingFixture();
    deep::GameState b = mixedOrderingFixture();
    compareDurableState(a, b, "mixed.initial");
    const deep::FleetId movingId = a.fleets.front().id;
    const deep::BodyId marsId = bodyIdByName(a, "Mars");
    const deep::BodyId terraId = bodyIdByName(a, "Terra");
    const deep::BodyId lunaId = bodyIdByName(a, "Luna Yard Complex");
    const deep::ColonyId marsColonyId = [&]() {
        for (const deep::Colony& colony : a.colonies) {
            if (colony.bodyId == marsId) return colony.id;
        }
        throw TestFailure{"Mars colony not found"};
    }();
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "mixed.beforeCommands");
    executeBoth(uninterrupted, *continued,
                deep::SetColonyProcessingPolicyCommand{
                    .colonyId = marsColonyId, .policy = deep::ProcessingPolicy::FuelFocus,
                    .manualAllocations = {}
                }, "mixed.changePolicy");
    executeBoth(uninterrupted, *continued,
                deep::MoveFleetCommand{.fleetId = movingId, .destinationBodyId = marsId},
                "mixed.move");
    executeBoth(uninterrupted, *continued,
                deep::QueueFleetMoveOrderCommand{.fleetId = movingId, .destinationBodyId = terraId},
                "mixed.queueReturn");
    executeBoth(uninterrupted, *continued,
                deep::QueueFleetMoveOrderCommand{.fleetId = movingId, .destinationBodyId = lunaId},
                "mixed.queueLuna");
    const deep::Fleet& queued = fleetById(uninterrupted.state(), movingId);
    require(queued.queuedOrders.size() == 2 &&
            queued.queuedOrders[0].targetBodyId == terraId &&
            queued.queuedOrders[1].targetBodyId == lunaId,
            "mixed queue preserves two meaningful destinations in order");

    saveLoadCheckpoint(uninterrupted, continued, save.path(), "mixed.afterCommands");
    const std::size_t baselineTelemetry = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t loadedTelemetry = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(1);
    continued->advanceDays(1);
    checkpoint(uninterrupted, *continued, "mixed.day1");
    compareNewTelemetry(uninterrupted, baselineTelemetry, *continued, loadedTelemetry,
                        "mixed.day1");
    const deep::GameState& result = uninterrupted.state();
    require(result.starSystems.front().id.value > result.starSystems.back().id.value &&
            result.institutions.front().id.value > result.institutions.back().id.value &&
            result.people.front().id.value > result.people.back().id.value &&
            result.bodies.front().id.value > result.bodies.back().id.value &&
            result.colonies.front().id.value > result.colonies.back().id.value &&
            result.shipClasses.front().id.value > result.shipClasses.back().id.value &&
            result.shipyardOrders.front().id.value > result.shipyardOrders.back().id.value &&
            result.ships.front().id.value > result.ships.back().id.value &&
            result.fleets.front().id.value > result.fleets.back().id.value,
            "non-ID order survives through a simulation day");
    const deep::Colony& terra = colonyById(result, [&]() {
        for (const deep::Colony& colony : result.colonies) {
            if (colony.bodyId == terraId) return colony.id;
        }
        throw TestFailure{"Terra colony not found"};
    }());
    require(terra.manualProcessingAllocations.size() == 3 &&
            terra.manualProcessingAllocations[0].material == deep::ProcessedMaterial::Electronics &&
            terra.manualProcessingAllocations[1].material == deep::ProcessedMaterial::StructuralAlloys &&
            terra.manualProcessingAllocations[2].material == deep::ProcessedMaterial::Electronics,
            "nontrivial repeated manual allocation rows retain order");
    require(result.appointments.back().scopeType == deep::AppointmentScopeType::Fleet &&
            result.appointments.back().scopeId == movingId.value,
            "nontrivial fleet appointment retains its target");
    near(result.shipyardOrders.front().accumulatedBuildPoints, 110.0,
         "first higher-ID shipyard order gets the day's shared capacity");
    near(result.shipyardOrders.back().accumulatedBuildPoints, 0.0,
         "second lower-ID shipyard order waits after one day");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "mixed.afterDay1");
}

deep::GameState activeTransitFixture() {
    deep::GameState state = deep::createHomeSystemScenario();
    static_cast<void>(addSingleShipFleet(state, bodyIdByName(state, "Terra"),
                                         "Mid-leg continuation fleet"));
    deep::validateGameState(state);
    return state;
}

void testActiveTransitContinuationAndChunking() {
    deep::GameState a = activeTransitFixture();
    deep::GameState b = activeTransitFixture();
    compareDurableState(a, b, "transit.initial");
    const deep::FleetId fleetId = a.fleets.front().id;
    const deep::BodyId terraId = bodyIdByName(a, "Terra");
    const deep::BodyId marsId = bodyIdByName(a, "Mars");
    deep::Simulation uninterrupted{std::move(a)};
    auto continued = std::make_unique<deep::Simulation>(std::move(b));
    UniqueSavePath save;

    executeBoth(uninterrupted, *continued,
                deep::MoveFleetCommand{.fleetId = fleetId, .destinationBodyId = marsId},
                "transit.move");
    executeBoth(uninterrupted, *continued,
                deep::QueueFleetMoveOrderCommand{.fleetId = fleetId, .destinationBodyId = terraId},
                "transit.queueReturn");
    const int originalEta = fleetById(uninterrupted.state(), fleetId).activeOrder.daysRemaining;
    require(originalEta >= 2, "transit fixture has at least one mid-leg day");
    const int midDays = std::max(1, originalEta / 2);
    require(midDays < originalEta, "mid-leg save precedes arrival");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "transit.beforeChunk");

    // This deliberately compares one +N call with N separate +1 calls. Daily
    // semantics and resulting telemetry/event order must be identical.
    const std::size_t baselineTelemetry = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t loadedTelemetry = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(midDays);
    for (int day = 0; day < midDays; ++day) {
        continued->advanceDays(1);
        deep::validateGameState(continued->state());
    }
    checkpoint(uninterrupted, *continued, "transit.chunkedMidLeg");
    compareNewTelemetry(uninterrupted, baselineTelemetry, *continued, loadedTelemetry,
                        "transit.chunkedMidLeg");
    const deep::FleetOrder acceptedMidLeg = fleetById(uninterrupted.state(), fleetId).activeOrder;
    require(acceptedMidLeg.type == deep::FleetOrderType::MoveToBody &&
            acceptedMidLeg.targetBodyId == marsId && acceptedMidLeg.daysRemaining > 0,
            "fleet remains on its accepted first transit plan mid-leg");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "transit.midLeg");
    compare(acceptedMidLeg, fleetById(continued->state(), fleetId).activeOrder,
            "transit.acceptedPlanAfterLoad");

    for (int day = midDays + 1; day <= originalEta; ++day) {
        const std::size_t baselineStart = uninterrupted.state().dailyEconomySnapshots.size();
        const std::size_t loadedStart = continued->state().dailyEconomySnapshots.size();
        uninterrupted.advanceDays(1);
        continued->advanceDays(1);
        checkpoint(uninterrupted, *continued, "transit.day" + std::to_string(day));
        compareNewTelemetry(uninterrupted, baselineStart, *continued, loadedStart,
                            "transit.day" + std::to_string(day));
    }
    const deep::Fleet& promoted = fleetById(uninterrupted.state(), fleetId);
    require(promoted.currentBodyId == marsId &&
            promoted.activeOrder.type == deep::FleetOrderType::MoveToBody &&
            promoted.activeOrder.departureBodyId == marsId &&
            promoted.activeOrder.targetBodyId == terraId &&
            promoted.activeOrder.departureDay == uninterrupted.state().date.day &&
            promoted.queuedOrders.empty(),
            "arrival at Mars immediately promotes queued return to Terra");
    saveLoadCheckpoint(uninterrupted, continued, save.path(), "transit.afterPromotion");
    const std::size_t nextBaselineStart = uninterrupted.state().dailyEconomySnapshots.size();
    const std::size_t nextLoadedStart = continued->state().dailyEconomySnapshots.size();
    uninterrupted.advanceDays(1);
    continued->advanceDays(1);
    checkpoint(uninterrupted, *continued, "transit.nextLegDay1");
    compareNewTelemetry(uninterrupted, nextBaselineStart, *continued, nextLoadedStart,
                        "transit.nextLegDay1");
}

} // namespace

int main() {
    try {
        testSharedDepositCompetition();
        testFifoShipyardCompetition();
        testWaitingShipyardOrderContinuesAfterCapacityRecovery();
        testDesignedRevisionContinuesAcrossSave();
        testPerHullFuelPayment();
        testSameDayArrivalsAndQueuePromotion();
        testMixedDurableOrdering();
        testActiveTransitContinuationAndChunking();
        std::cout << "Save continuity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Save continuity test failed: " << error.what() << '\n';
        return 1;
    }
}
