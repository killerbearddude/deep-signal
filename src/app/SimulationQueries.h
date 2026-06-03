#pragma once

// Declares read-only application query DTOs for simulation state.
// Future UI code should consume these summaries instead of reaching through
// SimulationService into raw GameState vectors.

#include "app/SimulationService.h"
#include "sim/Domain.h"
#include "sim/Events.h"
#include "sim/IdTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep {

// Display-ready processing allocation row. Weights are relative and are
// normalized by the simulation when daily processor capacity is spent.
struct ProcessingAllocationSummary {
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    std::string materialName;
    double weight = 0.0;
    double normalizedPercent = 0.0;
};

// Display-ready processed material stockpile row used by allocation previews.
// The Colony panel needs per-material amounts for Stockpile Recovery without
// reaching into raw Colony::processedStockpile arrays.
struct ProcessedMaterialStockpileSummary {
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    std::string materialName;
    double amount = 0.0;
};

// Display-ready colony row for overview panels. Values are copied out of the
// simulation snapshot so UI code cannot mutate GameState accidentally.
struct ColonySummary {
    ColonyId id;
    BodyId bodyId;
    std::string name;
    std::string bodyName;
    double mines = 0.0;
    double processorCapacity = 0.0;
    ProcessingPolicy processingPolicy = ProcessingPolicy::Balanced;
    std::string processingPolicyName;
    std::vector<ProcessingAllocationSummary> manualProcessingAllocations;
    std::vector<ProcessingAllocationSummary> effectiveProcessingAllocations;
    std::vector<ProcessedMaterialStockpileSummary> processedStockpiles;
    double shipyardCapacity = 0.0;
    double totalRawStockpile = 0.0;
    double totalProcessedStockpile = 0.0;
};

// Display-ready shipyard order row with resolved names for common UI tables.
// IDs remain available so future commands can still target the selected row.
struct ShipyardOrderSummary {
    ShipyardOrderId id;
    ColonyId colonyId;
    ShipClassId shipClassId;
    std::string colonyName;
    std::string shipClassName;
    int quantityRequested = 0;
    int quantityCompleted = 0;
    double accumulatedBuildPoints = 0.0;
    double requiredBuildPoints = 0.0;
    ShipyardOrderStatus status = ShipyardOrderStatus::Active;
    std::string statusName;
};

// Display-ready production backlog row. This mirrors app-layer production
// forecasts so the UI can show queue ETA and blockers without reading raw state.
struct ProductionBacklogSummary {
    ShipyardOrderId orderId;
    ColonyId colonyId;
    ShipClassId shipClassId;
    std::string colonyName;
    std::string shipClassName;
    int quantityRequested = 0;
    int quantityCompleted = 0;
    int queuePosition = 0;
    int shipsRemaining = 0;
    double accumulatedBuildPoints = 0.0;
    double buildPointsRemaining = 0.0;
    // Nonzero remaining processed-material requirements for the incomplete
    // portion of this order, suitable for direct display in production tables.
    std::vector<ProcessedMaterialStockpileSummary> requiredMaterialsRemaining;
    std::optional<int> etaDays;
    std::string blockingMaterialName;
    std::string statusName;
};

// Display-ready buildable ship class row. The UI can use these IDs to submit
// production commands without reading GameState::shipClasses directly.
struct ShipClassSummary {
    ShipClassId id;
    std::string name;
    ShipRole role = ShipRole::Survey;
    std::string roleName;
    double buildPoints = 0.0;
};

// Display-ready queued fleet order row. The queue position is one-based so UI
// tables can present the same ordering players expect from command queues.
struct FleetQueuedOrderSummary {
    std::size_t queuePosition = 0;
    FleetOrderType orderType = FleetOrderType::MoveToBody;
    std::string orderName;
    std::optional<BodyId> destinationBodyId;
    std::string destinationBodyName;
    int etaDays = 0;
};

// Display-ready fleet row with resolved body names and order state. Destination
// remains optional because idle fleets intentionally have no target body.
struct FleetSummary {
    FleetId id;
    std::string name;
    BodyId currentBodyId;
    std::string currentBodyName;
    std::optional<BodyId> destinationBodyId;
    std::string destinationBodyName;
    std::size_t shipCount = 0;
    FleetOrderType activeOrderType = FleetOrderType::None;
    std::string activeOrderName;
    bool hasActiveOrder = false;
    int daysRemaining = 0;
    std::vector<FleetQueuedOrderSummary> queuedOrders;
};

// Display-ready body/system overview row. Counts are resolved in the app layer
// so UI overview panels do not need to scan raw GameState vectors.
struct BodySystemSummary {
    BodyId id;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    std::string typeName;
    std::size_t colonyCount = 0;
    std::size_t mineralDepositCount = 0;
    std::size_t fleetCount = 0;
};

// Map-ready body row with coarse simulation coordinates copied from GameState.
// These DTOs let the strategic map draw the system without exposing body vectors.
struct StrategicBodySummary {
    BodyId id;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    std::string typeName;
    double x = 0.0;
    double y = 0.0;
};

// Map-ready fleet marker. Fleet coordinates are resolved through current and
// destination bodies so UI panels do not need direct body lookups.
struct StrategicFleetSummary {
    FleetId id;
    std::string name;
    BodyId currentBodyId;
    double x = 0.0;
    double y = 0.0;
    std::optional<BodyId> destinationBodyId;
    double destinationX = 0.0;
    double destinationY = 0.0;
    bool moving = false;
    int daysRemaining = 0;
};

// Display-ready event log entry. The payload variant is flattened into type and
// message text so UI code does not need to duplicate event visitor logic.
struct EventLogEntrySummary {
    EventId id;
    std::int64_t day = 0;
    EventSeverity severity = EventSeverity::Info;
    std::string severityName;
    std::string eventType;
    std::string message;
};

// Read-only query facade over SimulationService. The facade does not own the
// service; callers must ensure the referenced service outlives the query object.
class SimulationQueries {
public:
    // Binds queries to an application service. Returned summaries are snapshots
    // copied from the service state at the time each query is called.
    explicit SimulationQueries(const SimulationService& service) noexcept;

    // Returns one summary row per colony, including resolved body names.
    [[nodiscard]] std::vector<ColonySummary> colonies() const;

    // Returns one summary row per shipyard order, including colony/class names.
    [[nodiscard]] std::vector<ShipyardOrderSummary> shipyardOrders() const;

    // Returns one production backlog forecast row per shipyard order.
    [[nodiscard]] std::vector<ProductionBacklogSummary> productionBacklog() const;

    // Returns one summary row per buildable ship class.
    [[nodiscard]] std::vector<ShipClassSummary> shipClasses() const;

    // Returns one summary row per fleet, including location and order state.
    [[nodiscard]] std::vector<FleetSummary> fleets() const;

    // Returns a single fleet summary when the ID exists in the active snapshot.
    [[nodiscard]] std::optional<FleetSummary> fleet(FleetId id) const;

    // Returns one overview row per body with colony, deposit, and fleet counts.
    [[nodiscard]] std::vector<BodySystemSummary> bodySystemOverview() const;

    // Returns one map row per body, including abstract prototype coordinates.
    [[nodiscard]] std::vector<StrategicBodySummary> strategicBodies() const;

    // Returns a single map-ready body summary when the ID exists.
    [[nodiscard]] std::optional<StrategicBodySummary> strategicBody(BodyId id) const;

    // Returns one map row per fleet, resolving marker coordinates from bodies.
    [[nodiscard]] std::vector<StrategicFleetSummary> strategicFleets() const;

    // Returns the newest event-log entries, preserving chronological order
    // within the returned window. A limit of zero returns an empty vector.
    [[nodiscard]] std::vector<EventLogEntrySummary> recentEvents(std::size_t limit) const;

private:
    const SimulationService& service_;
};

} // namespace deep
