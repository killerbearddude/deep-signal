#pragma once

// Contains plain simulation domain records for Prototype 0.1.
// These structs intentionally avoid behavior-heavy inheritance so GameState can
// own data by value and persistence can map records directly. Shared pure helpers
// express domain calculations; command validation and mutation belong to Simulation.

#include "sim/IdTypes.h"
#include "sim/Minerals.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep {

// Prototype v1 fuel cost. Map coordinates remain abstract, so one unit of
// body-to-body distance consumes one unit of ship propellant capacity.
inline constexpr double kPrototypeFuelPerMapUnit = 1.0;

// One rendered map unit represents this many kilometers. Orbit rails store
// physical-ish kilometers while the UI consumes scaled map coordinates.
inline constexpr double kKilometersPerMapUnit = 1'000'000.0;

// Sustained-burn transit constants. The acceleration is intentionally modest so
// prototype trips take days rather than hours while remaining deterministic.
inline constexpr double kSecondsPerGameDay = 86'400.0;
inline constexpr double kStandardGravityMetersPerSecondSquared = 9.80665;
inline constexpr double kPrototypeBurnAccelerationG = 0.05;
inline constexpr int kTransitPlanningIterations = 5;

// Sustained-burn routes are rendered as shallow projected-intercept arcs, not
// low-energy orbital transfers. These caps keep the route visual close to the
// direct chord while still showing acceleration/deceleration phases.
inline constexpr double kSustainedBurnRouteCurveFraction = 0.10;
inline constexpr double kSustainedBurnRouteCurveMaxMapUnits = 120.0;

// Shared tolerance for fleet fuel affordability and post-consumption clamping.
inline constexpr double kFuelComparisonEpsilon = 1.0e-6;

// Resource survey v1 improves existing hand-authored deposit knowledge without
// generating new deposits. Unknown deposits become actionable estimates, while
// partially estimated deposits can advance to fully known state.
inline constexpr double kResourceSurveyConfidenceGain = 0.50;
inline constexpr double kResourceSurveyMinimumRevealedConfidence = 0.50;


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
// Merit scoring and small appointment modifiers consume these values, while
// personnel records themselves remain durable data with no behavior methods.
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

// Lightweight service-history counters for appointment merit and operational
// modifier calculations. Counters remain non-negative audit inputs, not events.
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


// Operational roles that can be assigned to durable personnel records. Current
// appointments may apply small deterministic operational modifiers, but still do
// not model morale, politics, trust, or approval mechanics.
enum class AppointmentRole {
    FleetCommander,
    ColonyAdministrator,
    ShipyardDirector,
    SurveyChief,
    LogisticsCoordinator,
    InstitutionHead
};

// Scope category for an appointment target. The paired scopeId stores the typed
// ID value for the selected Fleet, Colony, or Institution record.
enum class AppointmentScopeType {
    Fleet,
    Colony,
    Institution
};

// Current personnel assignment to one operational slot. Appointments are stored
// separately from assets so future history/effects can evolve without changing
// fleet, colony, or institution record shape again.
// Invariant: (role, scopeType, scopeId) is unique and personId resolves. A person
// may hold multiple slots; role/scope compatibility is not currently enforced.
struct Appointment {
    AppointmentRole role = AppointmentRole::InstitutionHead;
    AppointmentScopeType scopeType = AppointmentScopeType::Institution;
    std::int64_t scopeId = 0;
    PersonId personId;
    std::int64_t appointedDay = 0;
};

// Personnel competencies used by appointment effects. Keeping this selector in
// the sim domain lets sim, app queries, and forecasts use the same deterministic
// role profile without exposing appointment scoring as mutable game state.
enum class PersonnelCompetency {
    Logistics,
    Industry,
    Survey,
    Command,
    Administration,
    Engineering,
    Intelligence,
    CrisisManagement
};

// Pair of competencies that drive a role's small operational modifier. The
// values intentionally match the merit-scoring role profile so UI advice and
// simulation effects remain legible to the player.
struct AppointmentEffectProfile {
    PersonnelCompetency primary = PersonnelCompetency::Administration;
    PersonnelCompetency secondary = PersonnelCompetency::CrisisManagement;
};

// Tight caps prevent personnel from becoming hero bonuses. A +10% effect is a
// useful operational nudge; a -5% effect makes poor appointments visible without
// crippling the prototype economy or fleet movement.
inline constexpr double kAppointmentModifierMinimum = -0.05;
inline constexpr double kAppointmentModifierMaximum = 0.10;
inline constexpr double kAppointmentPrimaryCompetencyWeight = 0.015;
inline constexpr double kAppointmentSecondaryCompetencyWeight = 0.0075;
inline constexpr double kAppointmentSeniorityWeight = 0.0025;
inline constexpr double kAppointmentSuccessWeight = 0.0020;
inline constexpr double kAppointmentFailurePenalty = -0.0060;
inline constexpr double kAppointmentCommendationWeight = 0.0030;
inline constexpr double kAppointmentControversyPenalty = -0.0050;

[[nodiscard]] inline int competencyValue(const PersonCompetencies& competencies,
                                         const PersonnelCompetency competency) noexcept {
    switch (competency) {
    case PersonnelCompetency::Logistics:
        return competencies.logistics;
    case PersonnelCompetency::Industry:
        return competencies.industry;
    case PersonnelCompetency::Survey:
        return competencies.survey;
    case PersonnelCompetency::Command:
        return competencies.command;
    case PersonnelCompetency::Administration:
        return competencies.administration;
    case PersonnelCompetency::Engineering:
        return competencies.engineering;
    case PersonnelCompetency::Intelligence:
        return competencies.intelligence;
    case PersonnelCompetency::CrisisManagement:
        return competencies.crisisManagement;
    }

    return 0;
}

[[nodiscard]] inline AppointmentEffectProfile appointmentEffectProfile(const AppointmentRole role) noexcept {
    switch (role) {
    case AppointmentRole::FleetCommander:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Command, .secondary = PersonnelCompetency::Logistics};
    case AppointmentRole::ColonyAdministrator:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Administration, .secondary = PersonnelCompetency::CrisisManagement};
    case AppointmentRole::ShipyardDirector:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Industry, .secondary = PersonnelCompetency::Engineering};
    case AppointmentRole::SurveyChief:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Survey, .secondary = PersonnelCompetency::Intelligence};
    case AppointmentRole::LogisticsCoordinator:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Logistics, .secondary = PersonnelCompetency::Administration};
    case AppointmentRole::InstitutionHead:
        return AppointmentEffectProfile{.primary = PersonnelCompetency::Administration, .secondary = PersonnelCompetency::CrisisManagement};
    }

    return AppointmentEffectProfile{};
}

[[nodiscard]] inline double clampAppointmentModifier(const double rawModifier) noexcept {
    if (rawModifier < kAppointmentModifierMinimum) {
        return kAppointmentModifierMinimum;
    }
    if (rawModifier > kAppointmentModifierMaximum) {
        return kAppointmentModifierMaximum;
    }
    return rawModifier;
}

// Returns a bounded fractional effect, not a percentage or final multiplier.
// Consumers choose its sign: shipyard capacity uses 1 + effect, fuel cost 1 - effect.
[[nodiscard]] inline double appointmentOperationalModifier(const Person& person,
                                                           const AppointmentRole role) noexcept {
    const AppointmentEffectProfile profile = appointmentEffectProfile(role);
    const double rawModifier =
        static_cast<double>(competencyValue(person.competencies, profile.primary)) * kAppointmentPrimaryCompetencyWeight +
        static_cast<double>(competencyValue(person.competencies, profile.secondary)) * kAppointmentSecondaryCompetencyWeight +
        static_cast<double>(person.seniorityLevel) * kAppointmentSeniorityWeight +
        static_cast<double>(person.serviceRecord.successfulAssignments) * kAppointmentSuccessWeight +
        static_cast<double>(person.serviceRecord.failedAssignments) * kAppointmentFailurePenalty +
        static_cast<double>(person.serviceRecord.commendations) * kAppointmentCommendationWeight +
        static_cast<double>(person.serviceRecord.controversies) * kAppointmentControversyPenalty;

    return clampAppointmentModifier(rawModifier);
}

// A star system container. Prototype 0.1 starts with a single Sol system.
struct StarSystem {
    StarSystemId id;
    std::string name;
};

// Two-dimensional map-space point used for projected rails and fleet transit
// plans. Coordinates are scaled display units, not mutable orbital state.
struct MapPosition {
    double x = 0.0;
    double y = 0.0;
};

// Coarse body classification for map display and future rule branching.
enum class BodyType {
    Star,
    Terrestrial,
    GasGiant,
    Moon,
    Asteroid
};

// Strategic zones describe a body's operational role in the mature home-system
// start. These labels do not themselves grant access, route logistics, or drive
// institutional AI; surveying is handled by a separate command.
enum class StrategicZone {
    InnerCore,
    MilitaryIndustrial,
    BeltIndustrial,
    OuterLogistics,
    DeepSurveyFrontier
};

// A body in a star system. Bodies may sit on simple circular rails around an
// optional parent body. Positions are computed from date and rail metadata rather
// than mutated each simulation day, giving the map a live solar-system feel
// without gravity or transfer-window mechanics.
struct Body {
    BodyId id;
    StarSystemId systemId;
    std::string name;
    BodyType type = BodyType::Terrestrial;
    StrategicZone strategicZone = StrategicZone::InnerCore;
    std::optional<BodyId> parentBodyId;
    double orbitalRadiusKm = 0.0;
    double orbitalPeriodDays = 0.0;
    double phaseRadians = 0.0;
    // Base map glyph radius in screen pixels, enlarged when selected;
    // this is not a physical body radius and never affects transit planning.
    double displayRadius = 8.0;
    // Fixed/fallback map position. Stars and non-railed prototype bodies use
    // this directly; railed bodies use it only when orbital metadata is absent.
    double x = 0.0;
    double y = 0.0;
};

// Coarse survey knowledge state for one mineral deposit. The simulation stores a
// confidence value and derives this display state so resource surveys can
// improve knowledge without maintaining a second authoritative state field.
enum class DepositSurveyState {
    Unknown,
    Estimated,
    Known
};

// A mineable deposit on a body. Accessibility is a multiplier in [0, 1+] for now;
// values above 1.0 can model unusually rich deposits in later scenario data.
// confidence models survey knowledge: 0.0 is hidden/unknown, partial values are
// estimates, and 1.0 is fully confirmed. The physical remaining amount is still
// stored so deterministic saves can reveal it later without procedural rolls.
// Quantities use abstract mineral units. (bodyId, mineral) must be unique;
// remaining/accessibility are finite and non-negative, confidence is in [0, 1].
struct MineralDeposit {
    BodyId bodyId;
    Mineral mineral = Mineral::Iron;
    double remaining = 0.0;
    double accessibility = 1.0;
    double confidence = 1.0;
};

[[nodiscard]] inline bool isDepositSurveyed(const MineralDeposit& deposit) noexcept {
    return deposit.confidence > 0.0;
}

[[nodiscard]] inline bool isDepositKnown(const MineralDeposit& deposit) noexcept {
    return deposit.confidence >= 1.0;
}

[[nodiscard]] inline DepositSurveyState depositSurveyState(const MineralDeposit& deposit) noexcept {
    if (!isDepositSurveyed(deposit)) {
        return DepositSurveyState::Unknown;
    }
    if (isDepositKnown(deposit)) {
        return DepositSurveyState::Known;
    }
    return DepositSurveyState::Estimated;
}

// Confidence partitions the displayed reserve; it does not reserve separate
// physical material or reduce the amount available to the mining tick.
[[nodiscard]] inline double confirmedDepositQuantity(const MineralDeposit& deposit) noexcept {
    return deposit.remaining * deposit.confidence;
}

[[nodiscard]] inline double uncertainDepositQuantity(const MineralDeposit& deposit) noexcept {
    return deposit.remaining - confirmedDepositQuantity(deposit);
}

[[nodiscard]] inline double estimatedDepositQuantity(const MineralDeposit& deposit) noexcept {
    return isDepositSurveyed(deposit) ? deposit.remaining : 0.0;
}

[[nodiscard]] inline double surveyedDepositConfidence(const MineralDeposit& deposit) noexcept {
    return std::min(1.0, std::max(deposit.confidence + kResourceSurveyConfidenceGain,
                                  kResourceSurveyMinimumRevealedConfidence));
}


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
// Repeated materials are allowed and their weights add together.
struct ProcessingAllocation {
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    double weight = 0.0;
};

// A settled body with raw and processed stockpiles plus industrial capacity.
// mines supplies base mineral units per deposit per day before accessibility;
// processorCapacity is processed output units per day shared across recipes;
// shipyardCapacity is build points per day shared across local production orders.
// Stockpiles and capacities must be finite and non-negative. All industry uses
// this colony's stockpiles; there is no automatic transfer between colonies.
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
    // Work required per hull, paid with the colony's daily shipyard build points.
    double buildPoints = 0.0;
    // Retained class metadata in kilometers/day; sustained-burn timing currently
    // uses the shared acceleration constant instead of this value.
    double speedKmPerDay = 0.0;
    // Maximum abstract fuel units per hull. Newly completed ships start full;
    // no refueling command currently transfers colony propellant into this tank.
    double fuelCapacity = 0.0;
};

// Order lifecycle for shipyard production.
// Only states produced by valid Prototype 0.1 simulation ticks are modeled here.
// Recoverable processed-material shortages keep an order Active rather than
// introducing a separate paused/blocked state.
enum class ShipyardOrderStatus {
    Active,
    Completed
};

// A production order assigned to a colony shipyard. quantityCompleted allows
// multi-ship orders to continue across multiple completions. Progress is for the
// next hull; material cost is deducted only when that hull completes. Completed
// orders remain in state for audit references and retain zero build progress.
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

// Active fleet order state. None has no target and zero remaining days. A move
// requires a target matching Fleet::destinationBodyId and a persisted transit
// plan; validation rejects missing metadata rather than repairing a loaded route.
// The plan is fixed at departure so save/load and map rendering use the same
// projected endpoint even while bodies continue along their rails.
struct FleetOrder {
    FleetOrderType type = FleetOrderType::None;
    std::optional<BodyId> targetBodyId;
    // Whole simulated days; equals arrivalDay - GameState::date.day while moving.
    int daysRemaining = 0;
    std::optional<BodyId> departureBodyId;
    std::int64_t departureDay = 0;
    std::int64_t arrivalDay = 0;
    MapPosition departurePosition;
    MapPosition projectedArrivalPosition;
    double transitDistanceKm = 0.0;
    // Fraction of Earth gravity, independent of the presentation curve.
    double burnAccelerationG = 0.0;
    // Map units; display-only curvature does not change distance, time, or fuel.
    MapPosition routeCurveControlPoint;
};

// Queued fleet order state for the small v1 command queue. Queued orders do not
// store remaining days or reserve fuel; each leg is replanned when it starts.
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
    // Last reached body, retained during transit. Intermediate map positions are
    // derived from activeOrder; only arrival changes this logical location.
    BodyId currentBodyId;
    std::optional<BodyId> destinationBodyId;
    // Each ship must point back to this fleet and appear in exactly one roster.
    // Order also determines which hull pays fuel first from the pooled supply.
    std::vector<ShipId> shipIds;
    FleetOrder activeOrder;
    std::vector<QueuedFleetOrder> queuedOrders;
    // Optional owner/influence reference used for audit and future command
    // constraints. V1 does not restrict fleet orders by owner.
    std::optional<InstitutionId> ownerInstitutionId;
};

} // namespace deep
