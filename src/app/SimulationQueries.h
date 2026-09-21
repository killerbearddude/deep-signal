#pragma once

// Responsibility: project live simulation records into owned, display-ready
// values for UI consumers. Queries resolve names, score candidates, and preview
// routes; they do not issue commands, persist data, or own simulation entities.
// Resource/fuel amounts use abstract simulation units. Confidence uses [0, 1],
// percentages use 100 for a full allocation/tank and signed percentage points for
// modifiers. Map positions use kKilometersPerMapUnit scaling, not screen pixels.

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

// Small appointment effect contribution row. Values are percentage points and
// sum to the visible modifier after cap adjustment, keeping operational effects
// auditable in UI and tests.
struct AppointmentModifierBreakdownRow {
    std::string label;
    double percent = 0.0;
};

// Route visual style describes how map/UI should present a planned movement. V1
// movement is sustained-burn only; the low-energy value is reserved so future
// transfer mechanics can be added without overloading current DTO semantics.
enum class RouteVisualStyle {
    SustainedBurn,
    LowEnergyTransferLater
};

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
// current state so UI code cannot mutate GameState accidentally. Processor
// capacity is output units per game day; shipyard capacity is build points per
// game day. Summed stockpiles are display totals across unlike resource types.
struct ColonySummary {
    ColonyId id;
    BodyId bodyId;
    std::string name;
    std::string bodyName;
    double mines = 0.0;
    double processorCapacity = 0.0;
    ProcessingPolicy processingPolicy = ProcessingPolicy::Balanced;
    std::string processingPolicyName;
    std::optional<InstitutionId> ownerInstitutionId;
    std::string ownerInstitutionName;
    std::vector<ProcessingAllocationSummary> manualProcessingAllocations;
    std::vector<ProcessingAllocationSummary> effectiveProcessingAllocations;
    std::vector<ProcessedMaterialStockpileSummary> processedStockpiles;
    double shipyardCapacity = 0.0;
    double effectiveShipyardCapacity = 0.0;
    double shipyardModifierPercent = 0.0;
    std::vector<AppointmentModifierBreakdownRow> shipyardModifierBreakdown;
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
    double effectiveShipyardCapacity = 0.0;
    double shipyardModifierPercent = 0.0;
    std::vector<AppointmentModifierBreakdownRow> shipyardModifierBreakdown;
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

// Display-ready personnel row. This preserves durable personnel identity for
// future appointment/merit UI without exposing mutable GameState records.
struct PersonSummary {
    PersonId id;
    std::string name;
    InstitutionId institutionId;
    std::string institutionName;
    PersonCompetencies competencies;
    int seniorityLevel = 0;
    PersonServiceRecord serviceRecord;
};


// Display-ready current appointment row. These rows expose responsibility slots
// for debugging and future UI panels without applying any personnel modifiers.
struct AppointmentSummary {
    AppointmentRole role = AppointmentRole::InstitutionHead;
    std::string roleName;
    AppointmentScopeType scopeType = AppointmentScopeType::Institution;
    std::string scopeTypeName;
    std::int64_t scopeId = 0;
    std::string scopeName;
    PersonId personId;
    std::string personName;
    InstitutionId personInstitutionId;
    std::string personInstitutionName;
    std::int64_t appointedDay = 0;
};


// One explainable score contribution used by appointment candidate ranking.
// Values may be negative for service risks; totalScore is the sum of rows.
struct AppointmentScoreBreakdownRow {
    std::string label;
    double value = 0.0;
};

// Read-only candidate score for a possible appointment. This DTO supports
// player choice only: queries rank candidates but never assign anyone.
struct AppointmentCandidateScore {
    PersonId personId;
    std::string personName;
    std::string institutionName;
    AppointmentRole role = AppointmentRole::InstitutionHead;
    std::string roleName;
    double totalScore = 0.0;
    std::vector<AppointmentScoreBreakdownRow> scoreBreakdown;
    std::vector<std::string> riskNotes;
    std::vector<std::string> tradeoffNotes;
};

// Read-only effect row for current appointments that have v1 operational impact.
// It explains the small capped modifier without applying any automatic changes.
struct AppointmentOperationalEffectSummary {
    AppointmentRole role = AppointmentRole::InstitutionHead;
    std::string roleName;
    AppointmentScopeType scopeType = AppointmentScopeType::Institution;
    std::string scopeTypeName;
    std::int64_t scopeId = 0;
    std::string scopeName;
    PersonId personId;
    std::string personName;
    std::string operationName;
    double modifierPercent = 0.0;
    std::vector<AppointmentModifierBreakdownRow> modifierBreakdown;
    std::string explanation;
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
    double transitDistanceKm = 0.0;
    double burnAccelerationG = 0.0;
    RouteVisualStyle routeVisualStyle = RouteVisualStyle::SustainedBurn;
    std::string routeVisualStyleName;

    // Absolute simulation days projected by shared sustained-burn planning from
    // the current state. Each leg starts at its predecessor's projected arrival;
    // future command changes or start-time fuel rejection can invalidate this.
    std::int64_t projectedStartDay = 0;
    std::int64_t projectedArrivalDay = 0;
    double fuelCost = 0.0;
    // Clamped at zero for display; use fuelAffordable to detect a deficit.
    double projectedFuelRemaining = 0.0;
    bool fuelAffordable = true;
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
    std::optional<InstitutionId> ownerInstitutionId;
    std::string ownerInstitutionName;
    FleetOrderType activeOrderType = FleetOrderType::None;
    std::string activeOrderName;
    bool hasActiveOrder = false;
    int daysRemaining = 0;
    double currentFuel = 0.0;
    double fuelCapacity = 0.0;
    double fuelPercent = 0.0;
    double currentRange = 0.0;
    double fuelEfficiencyModifierPercent = 0.0;
    std::vector<AppointmentModifierBreakdownRow> fuelModifierBreakdown;

    // Durations count whole game days from now. activeOrderEtaDays is empty when
    // idle; totalRouteDurationDays includes the active and projected queued legs.
    // activeOrderProjectedArrivalDay is the current absolute day when idle.
    std::optional<int> activeOrderEtaDays;
    int totalRouteDurationDays = 0;
    std::int64_t activeOrderProjectedArrivalDay = 0;
    double activeOrderTransitDistanceKm = 0.0;
    double activeOrderBurnAccelerationG = 0.0;
    RouteVisualStyle activeOrderRouteVisualStyle = RouteVisualStyle::SustainedBurn;
    std::string activeOrderRouteVisualStyleName;
    std::string activeOrderBurnPhase;
    std::vector<FleetQueuedOrderSummary> queuedOrders;
};


// Preview for ordering one additional fleet move from the current queue state.
// The preview is advisory UI data; Simulation still performs authoritative
// command validation before accepting movement. canAfford only describes fuel;
// it does not establish all command preconditions such as a different target.
struct FleetMovePreview {
    FleetId fleetId;
    BodyId destinationBodyId;
    std::string destinationBodyName;
    double fuelAvailable = 0.0;
    // Includes both existing queued moves and this proposed additional move.
    // The active leg has already paid its fuel cost and is not charged again.
    double queuedFuelRequired = 0.0;
    double newMoveFuelCost = 0.0;
    double transitDistanceKm = 0.0;
    int etaDays = 0;
    double burnAccelerationG = 0.0;
    RouteVisualStyle routeVisualStyle = RouteVisualStyle::SustainedBurn;
    std::string routeVisualStyleName;
    double projectedArrivalX = 0.0;
    double projectedArrivalY = 0.0;
    double routeControlX = 0.0;
    double routeControlY = 0.0;
    double fuelEfficiencyModifierPercent = 0.0;
    double projectedFuelRemaining = 0.0;
    bool canAfford = false;
    std::string warningText;
};

// Read-only preflight summary for the immediate resource-survey command. V1
// surveys are instant and require the fleet to be stationary at the target body.
struct ResourceSurveyPreview {
    FleetId fleetId;
    BodyId bodyId;
    std::string bodyName;
    std::size_t surveyableDepositCount = 0;
    double averageConfidenceBefore = 0.0;
    double projectedAverageConfidenceAfter = 0.0;
    bool canSurvey = false;
    std::string warningText;
};

// Display-ready body/system overview row. Counts are resolved in the app layer
// so UI overview panels do not need to scan raw GameState vectors.
struct BodySystemSummary {
    BodyId id;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    std::string typeName;
    StrategicZone strategicZone = StrategicZone::InnerCore;
    std::string strategicZoneName;
    std::optional<InstitutionId> ownerInstitutionId;
    std::string ownerInstitutionName;
    std::optional<BodyId> parentBodyId;
    std::string parentBodyName;
    double orbitalRadiusKm = 0.0;
    double orbitalPeriodDays = 0.0;
    double displayRadius = 0.0;
    std::size_t colonyCount = 0;
    std::size_t mineralDepositCount = 0;
    std::size_t knownDepositCount = 0;
    std::size_t estimatedDepositCount = 0;
    std::size_t unknownDepositCount = 0;
    double confirmedDepositQuantity = 0.0;
    double estimatedDepositQuantity = 0.0;
    double uncertainDepositQuantity = 0.0;
    std::size_t fleetCount = 0;
};

// Display-ready mineral deposit row. Confidence partitions physical remaining
// quantity into confirmed and uncertain supply; estimated quantity is hidden for
// unknown deposits. A survey improves confidence without creating new reserves.
struct BodyDepositSummary {
    BodyId bodyId;
    std::string bodyName;
    Mineral mineral = Mineral::Iron;
    std::string mineralName;
    double confidence = 1.0;
    DepositSurveyState surveyState = DepositSurveyState::Known;
    std::string surveyStateName;
    double confirmedQuantity = 0.0;
    double estimatedQuantity = 0.0;
    double uncertainQuantity = 0.0;
    double accessibility = 1.0;
    bool shortageRelevant = false;
    std::string strategicRelevance;
};

// One low-confidence reserve row in the exploration intelligence summary. These
// rows answer "where should we survey next?" without introducing survey AI.
struct ExplorationDepositIntelligenceRow {
    BodyId bodyId;
    std::string bodyName;
    Mineral mineral = Mineral::Iron;
    std::string mineralName;
    DepositSurveyState surveyState = DepositSurveyState::Unknown;
    std::string surveyStateName;
    double confidence = 0.0;
    double confirmedQuantity = 0.0;
    double estimatedQuantity = 0.0;
    double unknownPotentialQuantity = 0.0;
    double accessibility = 1.0;
    bool shortageRelevant = false;
    std::string strategicRelevance;
};

// Compact audit row for completed surveys. It intentionally references the
// existing survey-completed event rather than adding save/schema state.
struct RecentSurveyResultSummary {
    EventId eventId;
    std::int64_t day = 0;
    FleetId fleetId;
    std::string fleetName;
    BodyId bodyId;
    std::string bodyName;
    int depositsImproved = 0;
    double averageConfidenceBefore = 0.0;
    double averageConfidenceAfter = 0.0;
    std::string summary;
};

// Exploration intelligence is a read-only briefing over deposit confidence and
// survey events. It does not choose missions or mutate survey state.
struct ExplorationIntelligenceSummary {
    std::vector<ExplorationDepositIntelligenceRow> lowConfidenceDeposits;
    std::vector<RecentSurveyResultSummary> recentSurveyResults;
    std::vector<std::string> warnings;
};

// Map-ready body row with rail positions projected at the current game day.
// x/y are scaled map coordinates; orbitalRadiusKm remains physical kilometers.
// Body fallback coordinates are used when rail projection cannot resolve.
struct StrategicBodySummary {
    BodyId id;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    std::string typeName;
    StrategicZone strategicZone = StrategicZone::InnerCore;
    std::string strategicZoneName;
    std::optional<InstitutionId> ownerInstitutionId;
    std::string ownerInstitutionName;
    std::optional<BodyId> parentBodyId;
    std::string parentBodyName;
    double orbitalRadiusKm = 0.0;
    double orbitalPeriodDays = 0.0;
    double displayRadius = 0.0;
    double x = 0.0;
    double y = 0.0;
};

// Map-ready fleet marker. Idle markers follow their body's current rail position;
// moving markers use elapsed-day interpolation along a presentation-only curve.
// currentBodyId remains the authoritative departure body until arrival. The
// destinationX/Y fields locate the target now, while projectedArrivalX/Y locate
// the planned future intercept. All coordinate fields use scaled map units.
struct StrategicFleetSummary {
    FleetId id;
    std::string name;
    BodyId currentBodyId;
    double x = 0.0;
    double y = 0.0;
    double departureX = 0.0;
    double departureY = 0.0;
    std::optional<BodyId> destinationBodyId;
    double destinationX = 0.0;
    double destinationY = 0.0;
    double controlX = 0.0;
    double controlY = 0.0;
    double projectedArrivalX = 0.0;
    double projectedArrivalY = 0.0;
    bool moving = false;
    int daysRemaining = 0;
    RouteVisualStyle routeVisualStyle = RouteVisualStyle::SustainedBurn;
    std::string routeVisualStyleName;
    std::string burnPhase;
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

// Read-only query facade borrowing a live SimulationService. Keep that service
// alive and at a stable address. Construction captures no state: each call reads
// the current game and returns independent DTO values. Serialize reads with
// commands/new/load; consecutive calls can describe different worlds or days if
// the caller mutates the service between them. No internal locking is provided.
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

    // Returns display-ready personnel rows with resolved institution names.
    [[nodiscard]] std::vector<PersonSummary> personnel() const;

    // Returns current appointment rows with resolved person and scope names.
    [[nodiscard]] std::vector<AppointmentSummary> appointments() const;

    // Returns ranked, explainable candidates for an existing appointment scope;
    // an unresolved scope returns an empty vector. Ranking is advice, not command
    // authorization, and never changes an appointment or a person's record.
    [[nodiscard]] std::vector<AppointmentCandidateScore> appointmentCandidatesFor(
        AppointmentRole role,
        AppointmentScopeType scopeType,
        std::int64_t scopeId) const;

    // Returns current appointment effects as explainable, capped modifiers.
    [[nodiscard]] std::vector<AppointmentOperationalEffectSummary> appointmentOperationalEffects() const;

    // Returns a display name for an institution reference. Unknown IDs produce a
    // stable placeholder so UI/tests can show broken references clearly.
    [[nodiscard]] std::string institutionDisplayName(InstitutionId id) const;

    // Returns fuel affordability for appending one move to the current queue.
    // Missing fleet/body IDs or malformed queued orders return nullopt; fuel
    // shortage instead returns a populated preview with canAfford == false.
    [[nodiscard]] std::optional<FleetMovePreview> fleetMovePreview(FleetId fleetId, BodyId destinationBodyId) const;

    // Returns preflight advice for an immediate survey. Missing fleet/body IDs
    // return nullopt; ordinary precondition failures return a warning in the DTO.
    [[nodiscard]] std::optional<ResourceSurveyPreview> resourceSurveyPreview(FleetId fleetId, BodyId bodyId) const;

    // Returns one overview row per body with colony, deposit, and fleet counts.
    [[nodiscard]] std::vector<BodySystemSummary> bodySystemOverview() const;

    // Returns display-ready deposit rows for a body. Unknown rows intentionally
    // hide estimated quantity until a resource survey improves confidence.
    [[nodiscard]] std::vector<BodyDepositSummary> bodyDeposits(BodyId bodyId) const;

    // Returns survey intelligence over low-confidence deposits and recent survey
    // events so UI panels can explain what exploration changed.
    [[nodiscard]] ExplorationIntelligenceSummary explorationIntelligence() const;

    // Returns one map row per body with current-day projected rail coordinates.
    [[nodiscard]] std::vector<StrategicBodySummary> strategicBodies() const;

    // Returns a single map-ready body summary when the ID exists.
    [[nodiscard]] std::optional<StrategicBodySummary> strategicBody(BodyId id) const;

    // Returns one map row per fleet; moving markers interpolate planned routes
    // for display without changing the simulation's discrete body location.
    [[nodiscard]] std::vector<StrategicFleetSummary> strategicFleets() const;

    // Returns the newest event-log entries, preserving chronological order
    // within the returned window. A limit of zero returns an empty vector.
    [[nodiscard]] std::vector<EventLogEntrySummary> recentEvents(std::size_t limit) const;

private:
    const SimulationService& service_;
};

} // namespace deep
