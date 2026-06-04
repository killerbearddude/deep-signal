#pragma once

// Contains plain simulation domain records for Prototype 0.1.
// These structs intentionally avoid behavior-heavy inheritance so GameState can
// own data by value and the future SQLite layer can map records directly.

#include "sim/IdTypes.h"
#include "sim/Minerals.h"

#include <optional>
#include <string>
#include <vector>

namespace deep {

// Prototype v1 fuel cost. Map coordinates remain abstract, so one unit of
// body-to-body distance consumes one unit of ship propellant capacity.
inline constexpr double kPrototypeFuelPerMapUnit = 1.0;

// Shared tolerance for fleet fuel affordability and post-consumption clamping.
inline constexpr double kFuelComparisonEpsilon = 1.0e-6;


// Coarse institutional roles for the mature home-system start. These are
// identity/category labels only; v1 ownership does not add politics, trust,
// contracts, access rights, or autonomous behavior.
enum class InstitutionType {
    InnerAuthority,
    NavalConstruction,
    ExtractionCombine,
    FuelTrust,
    SurveyOffice,
    PrivateHauler,
    DevelopmentBureau,
    ContinuityOffice
};

// Lightweight institution identity record. Institutions own or influence assets
// by ID references on core domain records, while all policy mechanics are left
// for later patches.
struct Institution {
    InstitutionId id;
    std::string name;
    InstitutionType type = InstitutionType::ContinuityOffice;
};

// Areas of professional capability recorded for durable personnel identity.
// V1 stores values only; no gameplay modifiers or merit calculations consume
// these competencies yet.
struct PersonCompetencies {
    int logistics = 0;
    int industry = 0;
    int survey = 0;
    int command = 0;
    int administration = 0;
    int engineering = 0;
    int intelligence = 0;
    int crisisManagement = 0;
};

// Lightweight service-history counters for future appointment and merit systems.
// Counters are non-negative audit inputs only in v1.
struct PersonServiceRecord {
    int successfulAssignments = 0;
    int failedAssignments = 0;
    int commendations = 0;
    int controversies = 0;
};

// Durable personnel identity tied to a home-system institution. People are
// simulation records rather than UI-only names so later appointment systems can
// reference them by stable ID without changing save structure again.
struct Person {
    PersonId id;
    std::string name;
    InstitutionId institutionId;
    PersonCompetencies competencies;
    int seniorityLevel = 0;
    PersonServiceRecord serviceRecord;
};

// A star system container. Prototype 0.1 starts with a single Sol system.
struct StarSystem {
    StarSystemId id;
    std::string name;
};

// Coarse body classification for map display and future rule branching.
enum class BodyType {
    Star,
    Terrestrial,
    GasGiant,
    Moon,
    Asteroid
};

// A body in a star system. Coordinates are abstract map coordinates, not orbital
// mechanics; this keeps movement deterministic for the first vertical slice.
struct Body {
    BodyId id;
    StarSystemId systemId;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    double x = 0.0;
    double y = 0.0;
};

// A mineable deposit on a body. Accessibility is a multiplier in [0, 1+] for now;
// values above 1.0 can model unusually rich deposits in later scenario data.
struct MineralDeposit {
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    double remaining = 0.0;
    double accessibility = 1.0;
};


// High-level policy used to distribute one colony's daily processing capacity
// across processed materials. Manual uses explicit user-provided weights, while
// the other policies are deterministic presets for early gameplay control.
enum class ProcessingPolicy {
    Balanced,
    ShipbuildingFocus,
    FuelFocus,
    ElectronicsFocus,
    StockpileRecovery,
    Manual
};

// Weight assigned to one processed material when a colony uses Manual policy.
// Weights are relative, not percentages; simulation normalizes positive weights
// against the colony's current processor capacity each day.
struct ProcessingAllocation {
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    double weight = 0.0;
};

// A settled body with raw and processed stockpiles plus industrial capacity.
// mines controls extraction, processorCapacity converts raw resources into
// processed materials, and shipyardCapacity applies build points per day.
struct Colony {
    ColonyId id;
    BodyId bodyId;
    std::string name;
    MineralSet stockpile;
    ProcessedMaterialSet processedStockpile;
    double mines = 0.0;
    double processorCapacity = 0.0;
    double shipyardCapacity = 0.0;
    ProcessingPolicy processingPolicy = ProcessingPolicy::Balanced;
    std::vector<ProcessingAllocation> manualProcessingAllocations;
    // Optional owner/influence reference used to identify which home-system
    // institution is responsible for the colony. V1 has no access rules.
    std::optional<InstitutionId> ownerInstitutionId;
};

// Prototype ship roles used for filtering and future UI grouping.
enum class ShipRole {
    Survey,
    Freighter,
    Escort
};

// Buildable ship class definition. In Prototype 0.1 these are hardcoded scenario
// records; later they can be generated by a ship designer or data files.
// buildCost is expressed in processed industrial materials, not raw minerals.
struct ShipClass {
    ShipClassId id;
    std::string name;
    ShipRole role = ShipRole::Survey;
    ProcessedMaterialSet buildCost;
    double buildPoints = 0.0;
    double speedKmPerDay = 0.0;
    // Maximum propellant capacity contributed by one ship of this class.
    // Starter ships are initialized full; v1 movement consumes this directly.
    double fuelCapacity = 0.0;
};

// Order lifecycle for shipyard production.
// Only states produced by valid Prototype 0.1 simulation ticks are modeled here.
// Recoverable mineral shortages keep an order Active rather than introducing a
// separate paused/blocked state.
enum class ShipyardOrderStatus {
    Active,
    Completed
};

// A production order assigned to a colony shipyard. quantityCompleted allows
// multi-ship orders to continue across multiple completions.
struct ShipyardOrder {
    ShipyardOrderId id;
    ColonyId colonyId;
    ShipClassId shipClassId;
    int quantityRequested = 1;
    int quantityCompleted = 0;
    double accumulatedBuildPoints = 0.0;
    ShipyardOrderStatus status = ShipyardOrderStatus::Active;
};

// A constructed ship. Ships are assigned to exactly one fleet in this prototype.
struct Ship {
    ShipId id;
    ShipClassId shipClassId;
    std::string name;
    FleetId fleetId;
    // Current propellant amount for this hull. Fuel is consumed when fleet
    // movement starts and must not exceed the owning ship class capacity.
    double fuel = 0.0;
};

// Fleet order type. Only body-to-body movement exists in Prototype 0.1.
enum class FleetOrderType {
    None,
    MoveToBody
};

// Active fleet order state. targetBodyId is optional so invalid or cleared orders
// can be represented explicitly during validation and save/load repair.
struct FleetOrder {
    FleetOrderType type = FleetOrderType::None;
    std::optional<BodyId> targetBodyId;
    int daysRemaining = 0;
};

// Queued fleet order state for the small v1 command queue. Queued orders do not
// store remaining days because duration is assigned only when the order starts.
struct QueuedFleetOrder {
    FleetOrderType type = FleetOrderType::MoveToBody;
    std::optional<BodyId> targetBodyId;
};

// A group of ships sharing a location, one active order, and a small visible
// order queue. shipIds reference GameState::ships records; the fleet does not
// own ship memory.
struct Fleet {
    FleetId id;
    std::string name;
    BodyId currentBodyId;
    std::optional<BodyId> destinationBodyId;
    std::vector<ShipId> shipIds;
    FleetOrder activeOrder;
    std::vector<QueuedFleetOrder> queuedOrders;
    // Optional owner/influence reference used for audit and future command
    // constraints. V1 does not restrict fleet orders by owner.
    std::optional<InstitutionId> ownerInstitutionId;
};

} // namespace deep
