#include "app/SimulationQueries.h"

#include "app/ForecastService.h"

// Responsibility: resolve names, display summaries, appointment advice, and map
// geometry from live GameState. DTOs own their output; lookup pointers borrow
// state only during the serialized call. This layer depends on neither ImGui nor
// SDL and must not turn a preview into an authoritative gameplay mutation.

#include "sim/GameState.h"
#include "sim/Minerals.h"
#include "sim/ProcessingAllocationRules.h"
#include "sim/SurveyProgramRules.h"
#include "sim/SurveyProgramExecution.h"
#include "sim/FreightProgramRules.h"
#include "sim/TransitPlanning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace deep {
namespace {

// Finds a read-only record by ID for name resolution. The returned pointer is
// non-owning and never escapes the query function that requested it.
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

[[nodiscard]] std::string institutionName(const GameState& state, const InstitutionId id) {
    const Institution* institution = findById(state.institutions, id);
    return institution == nullptr ? std::string{"<unknown institution>"} : institution->name;
}

[[nodiscard]] std::string optionalInstitutionName(const GameState& state, const std::optional<InstitutionId> id) {
    return id.has_value() ? institutionName(state, *id) : std::string{};
}

[[nodiscard]] std::string personName(const GameState& state, const PersonId id) {
    const Person* person = findById(state.people, id);
    return person == nullptr ? std::string{"<unknown person>"} : person->name;
}

[[nodiscard]] const Body* bodyById(const GameState& state, const BodyId id) noexcept {
    return findById(state.bodies, id);
}

[[nodiscard]] std::string bodyTypeName(const BodyType type) {
    switch (type) {
    case BodyType::Star:
        return "Star";
    case BodyType::Terrestrial:
        return "Terrestrial";
    case BodyType::GasGiant:
        return "Gas Giant";
    case BodyType::Moon:
        return "Moon";
    case BodyType::Asteroid:
        return "Asteroid";
    }

    return "Unknown";
}

[[nodiscard]] std::string strategicZoneName(const StrategicZone zone) {
    switch (zone) {
    case StrategicZone::InnerCore:
        return "Inner Core";
    case StrategicZone::MilitaryIndustrial:
        return "Military Industrial";
    case StrategicZone::BeltIndustrial:
        return "Belt Industrial";
    case StrategicZone::OuterLogistics:
        return "Outer Logistics";
    case StrategicZone::DeepSurveyFrontier:
        return "Deep Survey Frontier";
    }

    return "Unknown";
}

[[nodiscard]] std::string routeVisualStyleName(const RouteVisualStyle style) {
    switch (style) {
    case RouteVisualStyle::SustainedBurn:
        return "Sustained burn";
    case RouteVisualStyle::LowEnergyTransferLater:
        return "Low-energy transfer (reserved)";
    }

    return "Unknown route";
}

[[nodiscard]] std::string mineralDisplayName(const Mineral mineral) {
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

[[nodiscard]] const MineralForecastCauseChain* mineralForecastFor(
    const std::vector<MineralForecastCauseChain>& forecasts,
    const Mineral mineral) noexcept {
    const auto it = std::find_if(forecasts.begin(), forecasts.end(), [mineral](const MineralForecastCauseChain& forecast) {
        return forecast.mineral == mineral;
    });
    return it == forecasts.end() ? nullptr : &(*it);
}

[[nodiscard]] bool isMaterialShortageRelevant(const MineralForecastCauseChain* forecast) noexcept {
    return forecast != nullptr &&
           (forecast->netPerDay < -kMineralComparisonEpsilon || forecast->stockpileRunoutDays.has_value());
}

[[nodiscard]] std::string depositStrategicRelevance(
    const MineralDeposit& deposit,
    const std::vector<MineralForecastCauseChain>& forecasts) {
    const MineralForecastCauseChain* forecast = mineralForecastFor(forecasts, deposit.mineral);
    const bool shortageRelevant = isMaterialShortageRelevant(forecast);
    const DepositSurveyState state = depositSurveyState(deposit);

    if (shortageRelevant) {
        std::ostringstream out;
        out << "Survey priority: " << mineralDisplayName(deposit.mineral)
            << " is under forecast pressure";
        if (forecast != nullptr && forecast->stockpileRunoutDays.has_value()) {
            out << " with stockpile runout in " << *forecast->stockpileRunoutDays << " day(s)";
        }
        if (state == DepositSurveyState::Known) {
            out << "; this is confirmed supply.";
        } else {
            out << "; surveying this body can clarify whether the reserve helps the shortage.";
        }
        return out.str();
    }

    switch (state) {
    case DepositSurveyState::Unknown:
        return "Unsurveyed potential: survey before treating this reserve as actionable supply.";
    case DepositSurveyState::Estimated:
        return "Estimated reserve: additional survey can convert more of this estimate into confirmed supply.";
    case DepositSurveyState::Known:
        return "Confirmed reserve: no additional resource survey value for this deposit in v1.";
    }

    return {};
}

[[nodiscard]] std::string fleetName(const GameState& state, const FleetId id) {
    const Fleet* fleet = findById(state.fleets, id);
    return fleet == nullptr ? std::string{"<unknown fleet>"} : fleet->name;
}

[[nodiscard]] std::optional<InstitutionId> bodyOwnerInstitutionId(const GameState& state, const BodyId bodyId) noexcept {
    const auto colonyIt = std::find_if(state.colonies.begin(), state.colonies.end(), [bodyId](const Colony& colony) {
        return colony.bodyId == bodyId && colony.ownerInstitutionId.has_value();
    });
    return colonyIt == state.colonies.end() ? std::optional<InstitutionId>{} : colonyIt->ownerInstitutionId;
}

[[nodiscard]] std::string colonyName(const GameState& state, const ColonyId id) {
    const Colony* colony = findById(state.colonies, id);
    return colony == nullptr ? std::string{"<unknown colony>"} : colony->name;
}

[[nodiscard]] std::string surveyTeamName(const GameState& state, const SurveyTeamId id) {
    const SurveyTeam* team = findById(state.surveyTeams, id);
    return team == nullptr ? std::string{"<unknown survey team>"} : team->name;
}

[[nodiscard]] std::string surveyTeamLocationName(const GameState& state, const SurveyTeam& team) {
    if (team.locationKind == SurveyTeamLocationKind::Colony) {
        return team.colonyId ? "At " + colonyName(state, *team.colonyId) : "At an unknown colony";
    }
    if (!team.fleetId) return "Aboard an unknown fleet";
    const Fleet* fleet = findById(state.fleets, *team.fleetId);
    if (fleet == nullptr) return "Aboard an unknown fleet";
    std::string location = "Aboard " + fleet->name + " at " + bodyName(state, fleet->currentBodyId);
    if (fleet->activeOrder.type == FleetOrderType::MoveToBody && fleet->destinationBodyId) {
        location += " (in transit to " + bodyName(state, *fleet->destinationBodyId) + ")";
    }
    return location;
}

[[nodiscard]] std::string surveyApproachName(const SurveyPlanningApproach approach) {
    return approach == SurveyPlanningApproach::CoverageFirst ? "Coverage first" : "Priority first";
}

[[nodiscard]] std::string surveyLifecycleName(const SurveyProgram& program) {
    switch (program.lifecycle) {
    case SurveyProgramLifecycle::Authorized: return "Authorized";
    case SurveyProgramLifecycle::Suspended: return "Suspended";
    case SurveyProgramLifecycle::Closing: return "Closing";
    case SurveyProgramLifecycle::Closed:
        return program.closure == SurveyProgramClosure::Completed ? "Completed" : "Cancelled";
    }
    return "Unknown";
}

[[nodiscard]] std::string surveyTaskName(const SurveyProgramTask task) {
    switch (task) {
    case SurveyProgramTask::None: return "Planning";
    case SurveyProgramTask::Outbound: return "Outbound";
    case SurveyProgramTask::Survey: return "Survey visit";
    case SurveyProgramTask::Return: return "Return to base";
    }
    return "Unknown";
}

[[nodiscard]] std::string shipClassName(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? std::string{"<unknown ship class>"} : shipClass->name;
}

[[nodiscard]] std::string shipRoleName(const ShipRole role) {
    switch (role) {
    case ShipRole::Survey:
        return "Survey";
    case ShipRole::Freighter:
        return "Freighter";
    case ShipRole::Escort:
        return "Escort";
    }

    return "Unknown";
}

[[nodiscard]] double shipClassBuildPoints(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? 0.0 : evaluateShipDesign(state.shipComponents, shipClass->components).buildPoints;
}

[[nodiscard]] std::string statusName(const ShipyardOrderStatus status) {
    switch (status) {
    case ShipyardOrderStatus::Active:
        return "Active";
    case ShipyardOrderStatus::Completed:
        return "Completed";
    }

    return "Unknown";
}

struct FleetFuelTotals {
    double currentFuel = 0.0;
    double fuelCapacity = 0.0;
};

[[nodiscard]] MapPosition fallbackBodyPosition(const Body& body) noexcept {
    return MapPosition{.x = body.x, .y = body.y};
}

[[nodiscard]] FleetFuelTotals fleetFuelTotals(const GameState& state, const Fleet& fleet) {
    FleetFuelTotals totals;
    for (const ShipId shipId : fleet.shipIds) {
        const Ship* ship = findById(state.ships, shipId);
        if (ship == nullptr) {
            continue;
        }

        totals.currentFuel += ship->fuel;
        const ShipClass* shipClass = findById(state.shipClasses, ship->shipClassId);
        if (shipClass != nullptr) {
            totals.fuelCapacity += evaluateShipDesign(state.shipComponents, shipClass->components).propellantCapacity;
        }
    }
    return totals;
}

[[nodiscard]] BodyId projectedQueueOrigin(const Fleet& fleet) noexcept {
    if (fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.activeOrder.targetBodyId.has_value()) {
        return *fleet.activeOrder.targetBodyId;
    }
    return fleet.currentBodyId;
}

[[nodiscard]] std::string fleetOrderName(const FleetOrderType type) {
    switch (type) {
    case FleetOrderType::None:
        return "None";
    case FleetOrderType::MoveToBody:
        return "MoveToBody";
    }

    return "Unknown";
}

[[nodiscard]] std::string appointmentRoleName(const AppointmentRole role) {
    switch (role) {
    case AppointmentRole::FleetCommander:
        return "Fleet Commander";
    case AppointmentRole::ColonyAdministrator:
        return "Colony Administrator";
    case AppointmentRole::ShipyardDirector:
        return "Shipyard Director";
    case AppointmentRole::SurveyChief:
        return "Survey Chief";
    case AppointmentRole::LogisticsCoordinator:
        return "Logistics Coordinator";
    case AppointmentRole::InstitutionHead:
        return "Institution Head";
    }

    return "Unknown";
}

[[nodiscard]] std::string appointmentScopeTypeName(const AppointmentScopeType scopeType) {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
        return "Fleet";
    case AppointmentScopeType::Colony:
        return "Colony";
    case AppointmentScopeType::Institution:
        return "Institution";
    }

    return "Unknown";
}

[[nodiscard]] std::string appointmentScopeName(const GameState& state, const AppointmentScopeType scopeType, const std::int64_t scopeId) {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
        if (const Fleet* fleet = findById(state.fleets, FleetId{scopeId}); fleet != nullptr) {
            return fleet->name;
        }
        return "<unknown fleet>";
    case AppointmentScopeType::Colony:
        if (const Colony* colony = findById(state.colonies, ColonyId{scopeId}); colony != nullptr) {
            return colony->name;
        }
        return "<unknown colony>";
    case AppointmentScopeType::Institution:
        return institutionName(state, InstitutionId{scopeId});
    }

    return "<unknown scope>";
}


// Internal competency labels keep appointment scoring role weights explicit while
// preserving the public PersonCompetencies aggregate as simple saved data.
enum class CandidateCompetency {
    Logistics,
    Industry,
    Survey,
    Command,
    Administration,
    Engineering,
    Intelligence,
    CrisisManagement
};

struct AppointmentRoleWeights {
    CandidateCompetency primary = CandidateCompetency::Administration;
    CandidateCompetency secondary = CandidateCompetency::CrisisManagement;
};

[[nodiscard]] std::string competencyName(const CandidateCompetency competency) {
    switch (competency) {
    case CandidateCompetency::Logistics:
        return "Logistics";
    case CandidateCompetency::Industry:
        return "Industry";
    case CandidateCompetency::Survey:
        return "Survey";
    case CandidateCompetency::Command:
        return "Command";
    case CandidateCompetency::Administration:
        return "Administration";
    case CandidateCompetency::Engineering:
        return "Engineering";
    case CandidateCompetency::Intelligence:
        return "Intelligence";
    case CandidateCompetency::CrisisManagement:
        return "Crisis Management";
    }

    return "Unknown";
}

[[nodiscard]] int competencyValue(const PersonCompetencies& competencies, const CandidateCompetency competency) noexcept {
    switch (competency) {
    case CandidateCompetency::Logistics:
        return competencies.logistics;
    case CandidateCompetency::Industry:
        return competencies.industry;
    case CandidateCompetency::Survey:
        return competencies.survey;
    case CandidateCompetency::Command:
        return competencies.command;
    case CandidateCompetency::Administration:
        return competencies.administration;
    case CandidateCompetency::Engineering:
        return competencies.engineering;
    case CandidateCompetency::Intelligence:
        return competencies.intelligence;
    case CandidateCompetency::CrisisManagement:
        return competencies.crisisManagement;
    }

    return 0;
}

[[nodiscard]] AppointmentRoleWeights roleWeights(const AppointmentRole role) noexcept {
    switch (role) {
    case AppointmentRole::FleetCommander:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Command, .secondary = CandidateCompetency::Logistics};
    case AppointmentRole::ColonyAdministrator:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Administration, .secondary = CandidateCompetency::CrisisManagement};
    case AppointmentRole::ShipyardDirector:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Industry, .secondary = CandidateCompetency::Engineering};
    case AppointmentRole::SurveyChief:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Survey, .secondary = CandidateCompetency::Intelligence};
    case AppointmentRole::LogisticsCoordinator:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Logistics, .secondary = CandidateCompetency::Administration};
    case AppointmentRole::InstitutionHead:
        return AppointmentRoleWeights{.primary = CandidateCompetency::Administration, .secondary = CandidateCompetency::CrisisManagement};
    }

    return AppointmentRoleWeights{};
}

[[nodiscard]] std::optional<InstitutionId> appointmentOwnerInstitution(
    const GameState& state,
    const AppointmentScopeType scopeType,
    const std::int64_t scopeId) {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
        if (const Fleet* fleet = findById(state.fleets, FleetId{scopeId}); fleet != nullptr) {
            return fleet->ownerInstitutionId;
        }
        return std::nullopt;
    case AppointmentScopeType::Colony:
        if (const Colony* colony = findById(state.colonies, ColonyId{scopeId}); colony != nullptr) {
            return colony->ownerInstitutionId;
        }
        return std::nullopt;
    case AppointmentScopeType::Institution:
        if (findById(state.institutions, InstitutionId{scopeId}) != nullptr) {
            return InstitutionId{scopeId};
        }
        return std::nullopt;
    }

    return std::nullopt;
}

[[nodiscard]] bool appointmentScopeExists(
    const GameState& state,
    const AppointmentScopeType scopeType,
    const std::int64_t scopeId) noexcept {
    switch (scopeType) {
    case AppointmentScopeType::Fleet:
        return findById(state.fleets, FleetId{scopeId}) != nullptr;
    case AppointmentScopeType::Colony:
        return findById(state.colonies, ColonyId{scopeId}) != nullptr;
    case AppointmentScopeType::Institution:
        return findById(state.institutions, InstitutionId{scopeId}) != nullptr;
    }

    return false;
}

void addScoreRow(std::vector<AppointmentScoreBreakdownRow>& rows, const std::string& label, const double value) {
    rows.push_back(AppointmentScoreBreakdownRow{.label = label, .value = value});
}

[[nodiscard]] double sumScoreRows(const std::vector<AppointmentScoreBreakdownRow>& rows) noexcept {
    double total = 0.0;
    for (const AppointmentScoreBreakdownRow& row : rows) {
        total += row.value;
    }
    return total;
}

[[nodiscard]] AppointmentCandidateScore scoreAppointmentCandidate(
    const GameState& state,
    const Person& person,
    const AppointmentRole role,
    const std::optional<InstitutionId> ownerInstitutionId) {
    constexpr double kPrimaryCompetencyWeight = 10.0;
    constexpr double kSecondaryCompetencyWeight = 5.0;
    constexpr double kSeniorityWeight = 2.0;
    constexpr double kSuccessfulAssignmentWeight = 2.0;
    constexpr double kFailedAssignmentPenalty = -4.0;
    constexpr double kCommendationWeight = 3.0;
    constexpr double kControversyPenalty = -3.0;
    constexpr double kInstitutionOwnerMatchBonus = 10.0;

    const AppointmentRoleWeights weights = roleWeights(role);
    std::vector<AppointmentScoreBreakdownRow> breakdown;
    breakdown.reserve(8);

    addScoreRow(
        breakdown,
        competencyName(weights.primary) + " primary competency",
        static_cast<double>(competencyValue(person.competencies, weights.primary)) * kPrimaryCompetencyWeight);
    addScoreRow(
        breakdown,
        competencyName(weights.secondary) + " secondary competency",
        static_cast<double>(competencyValue(person.competencies, weights.secondary)) * kSecondaryCompetencyWeight);
    addScoreRow(breakdown, "Seniority", static_cast<double>(person.seniorityLevel) * kSeniorityWeight);
    addScoreRow(
        breakdown,
        "Successful assignments",
        static_cast<double>(person.serviceRecord.successfulAssignments) * kSuccessfulAssignmentWeight);
    addScoreRow(
        breakdown,
        "Failed assignments",
        static_cast<double>(person.serviceRecord.failedAssignments) * kFailedAssignmentPenalty);
    addScoreRow(breakdown, "Commendations", static_cast<double>(person.serviceRecord.commendations) * kCommendationWeight);
    addScoreRow(breakdown, "Controversies", static_cast<double>(person.serviceRecord.controversies) * kControversyPenalty);

    std::vector<std::string> riskNotes;
    std::vector<std::string> tradeoffNotes;

    if (ownerInstitutionId.has_value() && person.institutionId == *ownerInstitutionId) {
        addScoreRow(breakdown, "Institution owner match", kInstitutionOwnerMatchBonus);
    } else {
        addScoreRow(breakdown, "Institution owner match", 0.0);
        if (ownerInstitutionId.has_value()) {
            tradeoffNotes.push_back(
                "Institution mismatch: " + institutionName(state, person.institutionId) +
                " vs " + institutionName(state, *ownerInstitutionId));
        } else {
            tradeoffNotes.push_back("No owner institution context for this appointment scope");
        }
    }

    if (person.serviceRecord.failedAssignments > 0) {
        riskNotes.push_back("Failed assignments: " + std::to_string(person.serviceRecord.failedAssignments));
    }
    if (person.serviceRecord.controversies > 0) {
        riskNotes.push_back("Controversies: " + std::to_string(person.serviceRecord.controversies));
    }

    return AppointmentCandidateScore{
        .personId = person.id,
        .personName = person.name,
        .institutionName = institutionName(state, person.institutionId),
        .role = role,
        .roleName = appointmentRoleName(role),
        .totalScore = sumScoreRows(breakdown),
        .scoreBreakdown = std::move(breakdown),
        .riskNotes = std::move(riskNotes),
        .tradeoffNotes = std::move(tradeoffNotes)
    };
}

struct AppointmentModifierDetails {
    double modifier = 0.0;
    PersonId personId{};
    std::string personName;
    std::vector<AppointmentModifierBreakdownRow> breakdown;
};

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

void addModifierRow(std::vector<AppointmentModifierBreakdownRow>& rows, const std::string& label, const double fraction) {
    rows.push_back(AppointmentModifierBreakdownRow{.label = label, .percent = fraction * 100.0});
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

[[nodiscard]] std::optional<AppointmentModifierDetails> appointmentModifierDetailsFor(const GameState& state,
                                                                                      const AppointmentRole role,
                                                                                      const AppointmentScopeType scopeType,
                                                                                      const std::int64_t scopeId) {
    const Appointment* appointment = activeAppointmentFor(state, role, scopeType, scopeId);
    if (appointment == nullptr) {
        return std::nullopt;
    }

    const Person* person = findById(state.people, appointment->personId);
    if (person == nullptr) {
        return std::nullopt;
    }

    const AppointmentEffectProfile profile = appointmentEffectProfile(role);
    std::vector<AppointmentModifierBreakdownRow> breakdown;
    breakdown.reserve(8);

    const double primary = static_cast<double>(competencyValue(person->competencies, profile.primary)) * kAppointmentPrimaryCompetencyWeight;
    const double secondary = static_cast<double>(competencyValue(person->competencies, profile.secondary)) * kAppointmentSecondaryCompetencyWeight;
    const double seniority = static_cast<double>(person->seniorityLevel) * kAppointmentSeniorityWeight;
    const double successes = static_cast<double>(person->serviceRecord.successfulAssignments) * kAppointmentSuccessWeight;
    const double failures = static_cast<double>(person->serviceRecord.failedAssignments) * kAppointmentFailurePenalty;
    const double commendations = static_cast<double>(person->serviceRecord.commendations) * kAppointmentCommendationWeight;
    const double controversies = static_cast<double>(person->serviceRecord.controversies) * kAppointmentControversyPenalty;

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

    return AppointmentModifierDetails{
        .modifier = modifier,
        .personId = person->id,
        .personName = person->name,
        .breakdown = std::move(breakdown)
    };
}

[[nodiscard]] double appointmentModifierFor(const GameState& state,
                                            const AppointmentRole role,
                                            const AppointmentScopeType scopeType,
                                            const std::int64_t scopeId) {
    const std::optional<AppointmentModifierDetails> details = appointmentModifierDetailsFor(state, role, scopeType, scopeId);
    return details.has_value() ? details->modifier : 0.0;
}

[[nodiscard]] double effectiveShipyardCapacity(const GameState& state, const Colony& colony) {
    return std::max(0.0, colony.shipyardCapacity * (1.0 + appointmentModifierFor(
        state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value)));
}

[[nodiscard]] std::string burnPhaseName(const FleetOrder& order, const std::int64_t currentDay) {
    if (order.type != FleetOrderType::MoveToBody || order.arrivalDay <= order.departureDay) {
        return {};
    }

    const double total = static_cast<double>(order.arrivalDay - order.departureDay);
    const double elapsed = std::clamp(static_cast<double>(currentDay - order.departureDay), 0.0, total);
    const double fraction = elapsed / total;
    if (std::abs(fraction - 0.5) <= 0.05) {
        return "Flip";
    }
    return fraction < 0.5 ? "Accelerating" : "Decelerating";
}

[[nodiscard]] double effectiveFuelRange(const double currentFuel, const double fuelEfficiencyModifier) noexcept {
    const double costMultiplier = std::max(kFuelComparisonEpsilon, 1.0 - fuelEfficiencyModifier);
    return currentFuel / (kPrototypeFuelPerMapUnit * costMultiplier);
}

[[nodiscard]] std::string processingPolicyName(const ProcessingPolicy policy) {
    switch (policy) {
    case ProcessingPolicy::Balanced:
        return "Balanced";
    case ProcessingPolicy::ShipbuildingFocus:
        return "Shipbuilding Focus";
    case ProcessingPolicy::FuelFocus:
        return "Fuel Focus";
    case ProcessingPolicy::ElectronicsFocus:
        return "Electronics Focus";
    case ProcessingPolicy::StockpileRecovery:
        return "Stockpile Recovery";
    case ProcessingPolicy::Manual:
        return "Manual";
    }

    return "Unknown";
}

[[nodiscard]] std::string processedMaterialName(const ProcessedMaterial material) {
    return std::string{toString(material)};
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

[[nodiscard]] ProcessingShares processingWeightsForPolicy(
    const Colony& colony, const ProcessingPolicy policy,
    const ProcessingShares* manualOverride = nullptr) {
    // These weights mirror Simulation's policy presets for display. Keep this
    // table synchronized with simulation and forecast policy changes; a displayed
    // allocation percentage is not evidence that raw inputs can fund its output.
    ProcessingShares weights{};

    switch (policy) {
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
        if (manualOverride != nullptr) {
            weights = *manualOverride;
        } else {
            const ProcessingAllocationResult allocation = evaluateProcessingAllocations(
                colony.manualProcessingAllocations, true);
            if (!allocation.valid()) throw std::runtime_error{"Invalid live manual processing allocation"};
            weights = allocation.weights;
        }
        break;
    }

    return weights;
}

[[nodiscard]] std::vector<ProcessingAllocationSummary> summarizeProcessingWeights(
    const ProcessingShares& weights, const std::optional<ProcessingPolicy> activePolicy = std::nullopt) {
    std::vector<ProcessingAllocationSummary> summaries;
    summaries.reserve(weights.size());

    const ProcessingAllocationResult allocation = normalizeProcessingWeights(weights, false);
    if (!allocation.valid()) throw std::runtime_error{"Invalid live processing weights"};
    const ProcessingShares shares = activePolicy
        ? processingSharesForActivePolicy(allocation, *activePolicy) : allocation.shares;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        summaries.push_back(ProcessingAllocationSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .weight = allocation.weights[i],
            .normalizedPercent = shares[i] * 100.0
        });
    }

    return summaries;
}

[[nodiscard]] std::vector<ProcessingAllocationSummary> summarizeManualProcessingAllocations(const Colony& colony) {
    const ProcessingAllocationResult allocation = evaluateProcessingAllocations(
        colony.manualProcessingAllocations, colony.processingPolicy == ProcessingPolicy::Manual);
    if (!allocation.valid()) throw std::runtime_error{"Invalid stored manual processing allocation"};
    return summarizeProcessingWeights(allocation.weights);
}

[[nodiscard]] std::vector<ProcessedMaterialStockpileSummary> summarizeProcessedStockpiles(const Colony& colony) {
    std::vector<ProcessedMaterialStockpileSummary> summaries;
    summaries.reserve(processedMaterialCount());

    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        summaries.push_back(ProcessedMaterialStockpileSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .amount = colony.processedStockpile.amount[i]
        });
    }

    return summaries;
}

[[nodiscard]] std::vector<AppointmentModifierBreakdownRow> summarizeForecastModifierBreakdown(
    const std::vector<ForecastModifierBreakdownRow>& rows) {
    std::vector<AppointmentModifierBreakdownRow> summaries;
    summaries.reserve(rows.size());
    for (const ForecastModifierBreakdownRow& row : rows) {
        summaries.push_back(AppointmentModifierBreakdownRow{.label = row.label, .percent = row.percent});
    }
    return summaries;
}

[[nodiscard]] std::vector<ProcessedMaterialStockpileSummary> summarizeMaterialRequirements(const ProcessedMaterialSet& materials) {
    std::vector<ProcessedMaterialStockpileSummary> summaries;
    summaries.reserve(processedMaterialCount());

    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const double amount = materials.amount[i];
        if (amount <= kProcessedMaterialComparisonEpsilon) {
            continue;
        }

        const ProcessedMaterial material = static_cast<ProcessedMaterial>(i);
        summaries.push_back(ProcessedMaterialStockpileSummary{
            .material = material,
            .materialName = processedMaterialName(material),
            .amount = amount
        });
    }

    return summaries;
}

[[nodiscard]] std::string severityName(const EventSeverity severity) {
    switch (severity) {
    case EventSeverity::Info:
        return "Info";
    case EventSeverity::Warning:
        return "Warning";
    case EventSeverity::Critical:
        return "Critical";
    }

    return "Unknown";
}

[[nodiscard]] std::string idText(const std::int64_t value) {
    return std::to_string(value);
}


[[nodiscard]] double totalMinerals(const MineralSet& minerals) noexcept {
    double total = 0.0;
    for (const double amount : minerals.amount) {
        total += amount;
    }
    return total;
}

[[nodiscard]] double totalProcessedMaterials(const ProcessedMaterialSet& materials) noexcept {
    double total = 0.0;
    for (const double amount : materials.amount) {
        total += amount;
    }
    return total;
}

[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            return "mineral_extracted";
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            return "shipyard_order_created";
        } else if constexpr (std::is_same_v<Event, ShipClassRevisionCreatedEvent>) {
            return "ship_class_revision_created";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            return "ship_completed";
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            return "fleet_order_assigned";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            return "fleet_arrived";
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            return "resource_survey_completed";
        } else if constexpr (std::is_same_v<Event, SurveyProgramAuditEvent>) {
            return "survey_program";
        } else if constexpr (std::is_same_v<Event, FreightProgramAuditEvent>) {
            return "freight_program";
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            return "command_rejected";
        }
    }, payload);
}

[[nodiscard]] std::string eventMessage(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        std::ostringstream out;
        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            out << "Extracted " << event.amount << ' ' << toString(event.mineral)
                << " at body " << idText(event.bodyId.value);
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            out << "Created shipyard order " << idText(event.orderId.value)
                << " for " << event.quantity << " ship(s)";
        } else if constexpr (std::is_same_v<Event, ShipClassRevisionCreatedEvent>) {
            out << "Created ship class revision " << event.revision
                << " (class " << idText(event.shipClassId.value) << ')';
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            out << "Completed ship " << idText(event.shipId.value)
                << " for order " << idText(event.orderId.value);
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            out << "Assigned fleet " << idText(event.fleetId.value)
                << " to body " << idText(event.destinationBodyId.value)
                << " in " << event.daysRemaining << " day(s)";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            out << "Fleet " << idText(event.fleetId.value)
                << " arrived at body " << idText(event.destinationBodyId.value);
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            out << "Fleet " << idText(event.fleetId.value)
                << " surveyed body " << idText(event.bodyId.value) << "; ";
            if (event.depositsImproved == 0) {
                out << "No new information from this pass";
            } else {
                out << "improved " << event.depositsImproved << " deposit(s)"
                    << " from " << (event.averageConfidenceBefore * 100.0) << "% to "
                    << (event.averageConfidenceAfter * 100.0) << "% average confidence";
            }
        } else if constexpr (std::is_same_v<Event, SurveyProgramAuditEvent>) {
            out << "Survey program " << idText(event.programId.value) << ": " << event.detail;
        } else if constexpr (std::is_same_v<Event, FreightProgramAuditEvent>) {
            out << "Freight program " << idText(event.programId.value) << ": " << event.detail;
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            out << event.reason;
        }
        return out.str();
    }, payload);
}

[[nodiscard]] EventLogEntrySummary summarizeEvent(const SimEvent& event) {
    return EventLogEntrySummary{
        .id = event.id,
        .day = event.day,
        .severity = event.severity,
        .severityName = severityName(event.severity),
        .eventType = eventTypeName(event.payload),
        .message = eventMessage(event.payload)
    };
}

} // namespace

SimulationQueries::SimulationQueries(const SimulationService& service) noexcept
    : service_{service} {}

std::vector<ColonySummary> SimulationQueries::colonies() const {
    const GameState& state = service_.state();
    std::vector<ColonySummary> summaries;
    summaries.reserve(state.colonies.size());

    for (const Colony& colony : state.colonies) {
        summaries.push_back(ColonySummary{
            .id = colony.id,
            .bodyId = colony.bodyId,
            .name = colony.name,
            .bodyName = bodyName(state, colony.bodyId),
            .mines = colony.mines,
            .processorCapacity = colony.processorCapacity,
            .processingPolicy = colony.processingPolicy,
            .processingPolicyName = processingPolicyName(colony.processingPolicy),
            .ownerInstitutionId = colony.ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, colony.ownerInstitutionId),
            .manualProcessingAllocations = summarizeManualProcessingAllocations(colony),
            .effectiveProcessingAllocations = summarizeProcessingWeights(
                processingWeightsForPolicy(colony, colony.processingPolicy), colony.processingPolicy),
            .processedStockpiles = summarizeProcessedStockpiles(colony),
            .shipyardCapacity = colony.shipyardCapacity,
            .effectiveShipyardCapacity = effectiveShipyardCapacity(state, colony),
            .shipyardModifierPercent = appointmentModifierFor(
                state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value) * 100.0,
            .shipyardModifierBreakdown = appointmentModifierDetailsFor(
                state, AppointmentRole::ShipyardDirector, AppointmentScopeType::Colony, colony.id.value)
                .value_or(AppointmentModifierDetails{}).breakdown,
            .totalRawStockpile = totalMinerals(colony.stockpile),
            .totalProcessedStockpile = totalProcessedMaterials(colony.processedStockpile)
        });
    }

    return summaries;
}

std::optional<ColonyProcessingDraftPreview> SimulationQueries::previewColonyProcessingPolicy(
    const ColonyId colonyId, const ProcessingPolicy policy,
    const std::vector<ProcessingAllocation>& manualAllocations) const {
    const Colony* colony = findById(service_.state().colonies, colonyId);
    if (colony == nullptr) return std::nullopt;

    ColonyProcessingDraftPreview result{.colonyId = colonyId, .policy = policy,
                                        .policyName = processingPolicyName(policy),
                                        .valid = false, .validationMessage = {},
                                        .effectiveAllocations = {}};
    switch (policy) {
    case ProcessingPolicy::Balanced:
    case ProcessingPolicy::ShipbuildingFocus:
    case ProcessingPolicy::FuelFocus:
    case ProcessingPolicy::ElectronicsFocus:
    case ProcessingPolicy::StockpileRecovery:
    case ProcessingPolicy::Manual:
        break;
    default:
        result.validationMessage = "Choose a valid processing policy.";
        return result;
    }

    const ProcessingAllocationResult allocation = evaluateProcessingAllocations(
        manualAllocations, policy == ProcessingPolicy::Manual);
    if (!allocation.valid()) {
        switch (allocation.error) {
        case ProcessingAllocationError::InvalidMaterial:
            result.validationMessage = "Manual allocation contains an unknown material.";
            break;
        case ProcessingAllocationError::NonFiniteWeight:
            result.validationMessage = "Manual allocation weights must be finite.";
            break;
        case ProcessingAllocationError::NegativeWeight:
            result.validationMessage = "Manual allocation weights cannot be negative.";
            break;
        case ProcessingAllocationError::MaterialSubtotalOverflow:
        case ProcessingAllocationError::CombinedTotalOverflow:
        case ProcessingAllocationError::InvalidNormalizedShare:
            result.validationMessage = "Manual allocation total is too large.";
            break;
        case ProcessingAllocationError::InsufficientManualTotal:
            result.validationMessage = "Manual policy requires positive total weight.";
            break;
        case ProcessingAllocationError::None:
            break;
        }
        return result;
    }

    // Reuse this query layer's native policy projection. Actual output still
    // depends on available raw inputs and the authoritative command boundary.
    result.effectiveAllocations = summarizeProcessingWeights(
        processingWeightsForPolicy(*colony, policy, &allocation.weights), policy);
    result.valid = true;
    return result;
}

std::vector<ShipyardOrderSummary> SimulationQueries::shipyardOrders() const {
    const GameState& state = service_.state();
    std::vector<ShipyardOrderSummary> summaries;
    summaries.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        summaries.push_back(ShipyardOrderSummary{
            .id = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .quantityRequested = order.quantityRequested,
            .quantityCompleted = order.quantityCompleted,
            .accumulatedBuildPoints = order.accumulatedBuildPoints,
            .requiredBuildPoints = shipClassBuildPoints(state, order.shipClassId),
            .status = order.status,
            .statusName = statusName(order.status)
        });
    }

    return summaries;
}

std::vector<ProductionBacklogSummary> SimulationQueries::productionBacklog() const {
    const ForecastService forecasts{service_};
    const std::vector<ProductionBacklogForecast> backlog = forecasts.productionBacklog();

    std::vector<ProductionBacklogSummary> summaries;
    summaries.reserve(backlog.size());
    for (const ProductionBacklogForecast& row : backlog) {
        summaries.push_back(ProductionBacklogSummary{
            .orderId = row.orderId,
            .colonyId = row.colonyId,
            .shipClassId = row.shipClassId,
            .colonyName = row.colonyName,
            .shipClassName = row.shipClassName,
            .quantityRequested = row.quantityRequested,
            .quantityCompleted = row.quantityCompleted,
            .queuePosition = row.queuePosition,
            .shipsRemaining = row.shipsRemaining,
            .accumulatedBuildPoints = row.accumulatedBuildPoints,
            .buildPointsRemaining = row.buildPointsRemaining,
            .effectiveShipyardCapacity = row.effectiveShipyardCapacity,
            .shipyardModifierPercent = row.shipyardModifierPercent,
            .shipyardModifierBreakdown = summarizeForecastModifierBreakdown(row.shipyardModifierBreakdown),
            .requiredMaterialsRemaining = summarizeMaterialRequirements(row.requiredMaterialsRemaining),
            .etaDays = row.etaDays,
            .blockingMaterialName = row.blockingMaterialName,
            .statusName = row.statusName,
            .explanation = row.explanation
        });
    }

    return summaries;
}

std::vector<ShipClassSummary> SimulationQueries::shipClasses() const {
    const GameState& state = service_.state();
    std::vector<ShipClassSummary> summaries;
    summaries.reserve(state.shipClasses.size());

    for (const ShipClass& shipClass : state.shipClasses) {
        const ShipDesignEvaluation design = evaluateShipDesign(state.shipComponents, shipClass.components);
        summaries.push_back(ShipClassSummary{
            .id = shipClass.id,
            .name = shipClass.name,
            .revision = shipClass.revision,
            .role = shipClass.role,
            .roleName = shipRoleName(shipClass.role),
            .basedOnClassId = shipClass.basedOnClassId,
            .components = shipClass.components,
            .design = design,
            .buildPoints = design.buildPoints
        });
    }

    return summaries;
}

std::vector<ShipComponentSummary> SimulationQueries::shipComponents() const {
    std::vector<ShipComponentSummary> rows;
    for (const ShipComponentDefinition& component : service_.state().shipComponents) {
        rows.push_back(ShipComponentSummary{
            .id = component.id, .name = component.name, .kind = component.kind,
            .mass = component.mass, .volume = component.volume,
            .internalVolumeCapacity = component.internalVolumeCapacity,
            .powerGeneration = component.powerGeneration, .powerDemand = component.powerDemand,
            .propellantCapacity = component.propellantCapacity,
            .surveyCapability = component.surveyCapability,
            .cargoCapacity = component.cargoCapacity,
            .cargoHandlingPerDay = component.cargoHandlingPerDay,
            .buildCost = component.buildCost, .buildPoints = component.buildPoints
        });
    }
    return rows;
}

ShipDesignDraftPreview SimulationQueries::previewShipDesign(
    const std::vector<ShipComponentInstall>& components) const {
    ShipDesignDraftPreview preview;
    preview.design = evaluateShipDesign(service_.state().shipComponents, components);
    preview.warnings = preview.design.constraints;
    if (preview.design.powerMargin < 0.0) preview.warnings.push_back("Power deficit: mission systems are unavailable");
    if (preview.design.surveyCapability <= 0.0) preview.warnings.push_back("No survey capability installed");
    if (preview.design.propellantCapacity <= 0.0) preview.warnings.push_back("Zero propellant tankage");
    return preview;
}

std::vector<FleetSummary> SimulationQueries::fleets() const {
    const GameState& state = service_.state();
    std::vector<FleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const auto controller = controllingProgram(state, fleet.id);
        const bool hasActiveOrder = fleet.activeOrder.type != FleetOrderType::None;
        const std::int64_t currentDay = state.date.day;
        const int activeOrderEtaDays = hasActiveOrder
            ? std::max(0, static_cast<int>(fleet.activeOrder.arrivalDay - currentDay))
            : 0;
        std::int64_t nextStartDay = hasActiveOrder ? fleet.activeOrder.arrivalDay : currentDay;
        const FleetFuelTotals fuel = fleetFuelTotals(state, fleet);
        double projectedFuelRemaining = fuel.currentFuel;
        BodyId projectedOrigin = projectedQueueOrigin(fleet);

        std::vector<FleetQueuedOrderSummary> queuedOrders;
        queuedOrders.reserve(fleet.queuedOrders.size());
        for (std::size_t i = 0; i < fleet.queuedOrders.size(); ++i) {
            const QueuedFleetOrder& order = fleet.queuedOrders.at(i);
            const std::int64_t startDay = nextStartDay;
            const FleetOrder plan = order.targetBodyId.has_value()
                ? planFleetTransit(state, projectedOrigin, *order.targetBodyId, startDay)
                : FleetOrder{};
            const std::int64_t arrivalDay = plan.arrivalDay > startDay ? plan.arrivalDay : startDay;
            const double fuelCost = order.targetBodyId.has_value()
                ? adjustedFleetMoveFuelCost(state, fleet, projectedOrigin, *order.targetBodyId, startDay)
                : 0.0;
            projectedFuelRemaining -= fuelCost;

            queuedOrders.push_back(FleetQueuedOrderSummary{
                .queuePosition = i + 1U,
                .orderType = order.type,
                .orderName = fleetOrderName(order.type),
                .destinationBodyId = order.targetBodyId,
                .destinationBodyName = order.targetBodyId.has_value()
                    ? bodyName(state, *order.targetBodyId)
                    : std::string{},
                .etaDays = static_cast<int>(std::max<std::int64_t>(0, arrivalDay - startDay)),
                .transitDistanceKm = plan.transitDistanceKm,
                .burnAccelerationG = plan.burnAccelerationG,
                .routeVisualStyle = RouteVisualStyle::SustainedBurn,
                .routeVisualStyleName = routeVisualStyleName(RouteVisualStyle::SustainedBurn),
                .projectedStartDay = startDay,
                .projectedArrivalDay = arrivalDay,
                .fuelCost = fuelCost,
                .projectedFuelRemaining = std::max(0.0, projectedFuelRemaining),
                .fuelAffordable = projectedFuelRemaining + kFuelComparisonEpsilon >= 0.0
            });

            nextStartDay = arrivalDay;
            if (order.targetBodyId.has_value()) {
                projectedOrigin = *order.targetBodyId;
            }
        }

        const int totalRouteDurationDays = static_cast<int>(std::max<std::int64_t>(0, nextStartDay - currentDay));
        const std::int64_t activeArrivalDay = hasActiveOrder ? fleet.activeOrder.arrivalDay : currentDay;
        const std::optional<AppointmentModifierDetails> fleetCommanderModifier = appointmentModifierDetailsFor(
            state, AppointmentRole::FleetCommander, AppointmentScopeType::Fleet, fleet.id.value);
        const double fuelEfficiencyModifier = fleetCommanderModifier.has_value() ? fleetCommanderModifier->modifier : 0.0;

        summaries.push_back(FleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .currentBodyName = bodyName(state, fleet.currentBodyId),
            .destinationBodyId = fleet.destinationBodyId,
            .destinationBodyName = fleet.destinationBodyId.has_value()
                ? bodyName(state, *fleet.destinationBodyId)
                : std::string{},
            .shipCount = fleet.shipIds.size(),
            .ownerInstitutionId = fleet.ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, fleet.ownerInstitutionId),
            .controllingProgram = controller,
            .controllingProgramLabel = controller ? programControllerLabel(state, *controller) : std::string{},
            .activeOrderType = fleet.activeOrder.type,
            .activeOrderName = fleetOrderName(fleet.activeOrder.type),
            .hasActiveOrder = hasActiveOrder,
            .daysRemaining = fleet.activeOrder.daysRemaining,
            .currentFuel = fuel.currentFuel,
            .fuelCapacity = fuel.fuelCapacity,
            .fuelPercent = fuel.fuelCapacity <= kFuelComparisonEpsilon ? 0.0 : (fuel.currentFuel * 100.0 / fuel.fuelCapacity),
            .currentRange = effectiveFuelRange(fuel.currentFuel, fuelEfficiencyModifier),
            .fuelEfficiencyModifierPercent = fuelEfficiencyModifier * 100.0,
            .fuelModifierBreakdown = fleetCommanderModifier.has_value()
                ? fleetCommanderModifier->breakdown
                : std::vector<AppointmentModifierBreakdownRow>{},
            .activeOrderEtaDays = hasActiveOrder ? std::optional<int>{activeOrderEtaDays} : std::optional<int>{},
            .totalRouteDurationDays = totalRouteDurationDays,
            .activeOrderProjectedArrivalDay = activeArrivalDay,
            .activeOrderTransitDistanceKm = hasActiveOrder ? fleet.activeOrder.transitDistanceKm : 0.0,
            .activeOrderBurnAccelerationG = hasActiveOrder ? fleet.activeOrder.burnAccelerationG : 0.0,
            .activeOrderRouteVisualStyle = RouteVisualStyle::SustainedBurn,
            .activeOrderRouteVisualStyleName = hasActiveOrder
                ? routeVisualStyleName(RouteVisualStyle::SustainedBurn)
                : std::string{},
            .activeOrderBurnPhase = hasActiveOrder ? burnPhaseName(fleet.activeOrder, currentDay) : std::string{},
            .queuedOrders = std::move(queuedOrders)
        });
    }

    return summaries;
}


std::vector<PersonSummary> SimulationQueries::personnel() const {
    const GameState& state = service_.state();
    std::vector<PersonSummary> summaries;
    summaries.reserve(state.people.size());

    for (const Person& person : state.people) {
        summaries.push_back(PersonSummary{
            .id = person.id,
            .name = person.name,
            .institutionId = person.institutionId,
            .institutionName = institutionName(state, person.institutionId),
            .competencies = person.competencies,
            .seniorityLevel = person.seniorityLevel,
            .serviceRecord = person.serviceRecord,
            .surveyPlanningApproach = person.surveyPlanningApproach,
            .surveyPlanningApproachName = surveyApproachName(person.surveyPlanningApproach)
        });
    }

    return summaries;
}


std::vector<AppointmentSummary> SimulationQueries::appointments() const {
    const GameState& state = service_.state();
    std::vector<AppointmentSummary> summaries;
    summaries.reserve(state.appointments.size());

    for (const Appointment& appointment : state.appointments) {
        const Person* person = findById(state.people, appointment.personId);
        const InstitutionId institutionId = person == nullptr ? InstitutionId{} : person->institutionId;
        summaries.push_back(AppointmentSummary{
            .role = appointment.role,
            .roleName = appointmentRoleName(appointment.role),
            .scopeType = appointment.scopeType,
            .scopeTypeName = appointmentScopeTypeName(appointment.scopeType),
            .scopeId = appointment.scopeId,
            .scopeName = appointmentScopeName(state, appointment.scopeType, appointment.scopeId),
            .personId = appointment.personId,
            .personName = personName(state, appointment.personId),
            .personInstitutionId = institutionId,
            .personInstitutionName = institutionId ? institutionName(state, institutionId) : std::string{"<unknown institution>"},
            .appointedDay = appointment.appointedDay
        });
    }

    return summaries;
}



std::vector<AppointmentCandidateScore> SimulationQueries::appointmentCandidatesFor(
    const AppointmentRole role,
    const AppointmentScopeType scopeType,
    const std::int64_t scopeId) const {
    const GameState& state = service_.state();
    if (!appointmentScopeExists(state, scopeType, scopeId)) {
        return {};
    }

    const std::optional<InstitutionId> ownerInstitutionId = appointmentOwnerInstitution(state, scopeType, scopeId);
    std::vector<AppointmentCandidateScore> candidates;
    candidates.reserve(state.people.size());

    for (const Person& person : state.people) {
        // Simulation validates institutional references when state is imported.
        // Retain this defensive guard so candidate display never dereferences a
        // missing lookup if the state-entry contract changes in the future.
        if (findById(state.institutions, person.institutionId) == nullptr) {
            continue;
        }
        candidates.push_back(scoreAppointmentCandidate(state, person, role, ownerInstitutionId));
    }

    std::sort(candidates.begin(), candidates.end(), [&state](const AppointmentCandidateScore& lhs, const AppointmentCandidateScore& rhs) {
        if (lhs.totalScore != rhs.totalScore) {
            return lhs.totalScore > rhs.totalScore;
        }

        const Person* lhsPerson = findById(state.people, lhs.personId);
        const Person* rhsPerson = findById(state.people, rhs.personId);
        const int lhsSeniority = lhsPerson == nullptr ? 0 : lhsPerson->seniorityLevel;
        const int rhsSeniority = rhsPerson == nullptr ? 0 : rhsPerson->seniorityLevel;
        if (lhsSeniority != rhsSeniority) {
            return lhsSeniority > rhsSeniority;
        }
        if (lhs.personName != rhs.personName) {
            return lhs.personName < rhs.personName;
        }
        return lhs.personId.value < rhs.personId.value;
    });

    return candidates;
}

std::vector<AppointmentOperationalEffectSummary> SimulationQueries::appointmentOperationalEffects() const {
    const GameState& state = service_.state();
    std::vector<AppointmentOperationalEffectSummary> summaries;

    const auto addEffect = [&](const Appointment& appointment, const std::string& operationName, const std::string& explanation) {
        const std::optional<AppointmentModifierDetails> details = appointmentModifierDetailsFor(
            state, appointment.role, appointment.scopeType, appointment.scopeId);
        if (!details.has_value()) {
            return;
        }

        summaries.push_back(AppointmentOperationalEffectSummary{
            .role = appointment.role,
            .roleName = appointmentRoleName(appointment.role),
            .scopeType = appointment.scopeType,
            .scopeTypeName = appointmentScopeTypeName(appointment.scopeType),
            .scopeId = appointment.scopeId,
            .scopeName = appointmentScopeName(state, appointment.scopeType, appointment.scopeId),
            .personId = details->personId,
            .personName = details->personName,
            .operationName = operationName,
            .modifierPercent = details->modifier * 100.0,
            .modifierBreakdown = details->breakdown,
            .explanation = explanation
        });
    };

    for (const Appointment& appointment : state.appointments) {
        if (appointment.role == AppointmentRole::ShipyardDirector && appointment.scopeType == AppointmentScopeType::Colony) {
            addEffect(appointment, "Shipyard BP/day", "Modifier applies to effective colony shipyard build points per day.");
        } else if (appointment.role == AppointmentRole::FleetCommander && appointment.scopeType == AppointmentScopeType::Fleet) {
            addEffect(appointment, "Fleet fuel cost", "Positive modifier reduces v1 fleet movement fuel cost.");
        } else if (appointment.role == AppointmentRole::SurveyChief) {
            addEffect(appointment, "Survey duration estimate", "Positive modifier is reserved for survey-duration estimates once survey operations exist.");
        }
    }

    return summaries;
}

std::string SimulationQueries::institutionDisplayName(const InstitutionId id) const {
    return institutionName(service_.state(), id);
}

std::optional<FleetSummary> SimulationQueries::fleet(const FleetId id) const {
    const std::vector<FleetSummary> summaries = fleets();
    const auto it = std::find_if(summaries.begin(), summaries.end(), [id](const FleetSummary& summary) {
        return summary.id == id;
    });
    return it == summaries.end() ? std::optional<FleetSummary>{} : std::optional<FleetSummary>{*it};
}


std::optional<FleetMovePreview> SimulationQueries::fleetMovePreview(const FleetId fleetId, const BodyId destinationBodyId) const {
    const GameState& state = service_.state();
    const Fleet* fleet = findById(state.fleets, fleetId);
    const Body* destination = bodyById(state, destinationBodyId);
    if (fleet == nullptr || destination == nullptr) {
        return std::nullopt;
    }

    FleetFuelTotals fuel = fleetFuelTotals(state, *fleet);
    BodyId projectedOrigin = projectedQueueOrigin(*fleet);
    std::int64_t projectedStartDay = fleet->activeOrder.type == FleetOrderType::MoveToBody
        ? fleet->activeOrder.arrivalDay
        : state.date.day;
    double queuedFuelRequired = 0.0;

    for (const QueuedFleetOrder& queuedOrder : fleet->queuedOrders) {
        if (queuedOrder.type != FleetOrderType::MoveToBody || !queuedOrder.targetBodyId.has_value()) {
            return std::nullopt;
        }
        const FleetOrder queuedPlan = planFleetTransit(state, projectedOrigin, *queuedOrder.targetBodyId, projectedStartDay);
        queuedFuelRequired += adjustedFleetMoveFuelCost(state, *fleet, projectedOrigin, *queuedOrder.targetBodyId, projectedStartDay);
        projectedStartDay = queuedPlan.arrivalDay;
        projectedOrigin = *queuedOrder.targetBodyId;
    }

    const std::optional<AppointmentModifierDetails> fleetCommanderModifier = appointmentModifierDetailsFor(
        state, AppointmentRole::FleetCommander, AppointmentScopeType::Fleet, fleet->id.value);
    const double fuelEfficiencyModifier = fleetCommanderModifier.has_value() ? fleetCommanderModifier->modifier : 0.0;
    const FleetOrder newMovePlan = planFleetTransit(state, projectedOrigin, destinationBodyId, projectedStartDay);
    const double newMoveCost = adjustedFleetMoveFuelCost(state, *fleet, projectedOrigin, destinationBodyId, projectedStartDay);
    queuedFuelRequired += newMoveCost;
    const double projectedRemaining = fuel.currentFuel - queuedFuelRequired;
    const bool canAfford = projectedRemaining + kFuelComparisonEpsilon >= 0.0;

    std::string warning;
    if (!canAfford) {
        warning = "Insufficient fuel for queued route.";
    } else if (fuel.fuelCapacity <= kFuelComparisonEpsilon) {
        warning = "Fleet has no fuel capacity.";
    }

    return FleetMovePreview{
        .fleetId = fleetId,
        .destinationBodyId = destinationBodyId,
        .destinationBodyName = destination->name,
        .fuelAvailable = fuel.currentFuel,
        .queuedFuelRequired = queuedFuelRequired,
        .newMoveFuelCost = newMoveCost,
        .transitDistanceKm = newMovePlan.transitDistanceKm,
        .etaDays = newMovePlan.daysRemaining,
        .burnAccelerationG = newMovePlan.burnAccelerationG,
        .routeVisualStyle = RouteVisualStyle::SustainedBurn,
        .routeVisualStyleName = routeVisualStyleName(RouteVisualStyle::SustainedBurn),
        .projectedArrivalX = newMovePlan.projectedArrivalPosition.x,
        .projectedArrivalY = newMovePlan.projectedArrivalPosition.y,
        .routeControlX = newMovePlan.routeCurveControlPoint.x,
        .routeControlY = newMovePlan.routeCurveControlPoint.y,
        .fuelEfficiencyModifierPercent = fuelEfficiencyModifier * 100.0,
        .projectedFuelRemaining = std::max(0.0, projectedRemaining),
        .canAfford = canAfford,
        .warningText = std::move(warning)
    };
}

std::optional<ResourceSurveyPreview> SimulationQueries::resourceSurveyPreview(const FleetId fleetId, const BodyId bodyId) const {
    const GameState& state = service_.state();
    const Fleet* fleet = findById(state.fleets, fleetId);
    const Body* body = bodyById(state, bodyId);
    if (fleet == nullptr || body == nullptr) {
        return std::nullopt;
    }

    ResourceSurveyPreview preview{
        .fleetId = fleetId,
        .bodyId = bodyId,
        .bodyName = body->name,
        .surveyableDepositCount = 0,
        .averageConfidenceBefore = 0.0,
        .projectedAverageConfidenceAfter = 0.0,
        .canSurvey = false,
        .warningText = {}
    };

    for (const MineralDeposit& deposit : state.mineralDeposits) {
        if (deposit.bodyId != bodyId || isDepositKnown(deposit)) {
            continue;
        }
        preview.averageConfidenceBefore += deposit.confidence;
        preview.projectedAverageConfidenceAfter += surveyedDepositConfidence(deposit);
        ++preview.surveyableDepositCount;
    }

    if (preview.surveyableDepositCount > 0U) {
        const double count = static_cast<double>(preview.surveyableDepositCount);
        preview.averageConfidenceBefore /= count;
        preview.projectedAverageConfidenceAfter /= count;
    }

    if (fleet->activeOrder.type != FleetOrderType::None || fleet->destinationBodyId.has_value()) {
        preview.warningText = "Fleet must be stationary to survey.";
    } else if (fleet->currentBodyId != bodyId) {
        preview.warningText = "Fleet must be at the selected body.";
    } else if (const FleetSurveyEvaluation capability = evaluateFleetSurvey(state, *fleet);
               capability.operationalCapability <= 0.0) {
        preview.warningText = capability.installedCapability > 0.0
            ? "Survey equipment is unavailable due to power deficit."
            : "Fleet has no installed survey capability.";
    } else {
        preview.canSurvey = true;
        if (preview.surveyableDepositCount == 0U) {
            // A physically valid survey can finish without changing confidence.
            // This advice is informational; the button remains enabled.
            preview.warningText = "No confidence improvement is projected for this pass.";
        }
    }

    return preview;
}

std::vector<BodySystemSummary> SimulationQueries::bodySystemOverview() const {
    const GameState& state = service_.state();
    std::vector<BodySystemSummary> summaries;
    summaries.reserve(state.bodies.size());

    for (const Body& body : state.bodies) {
        const auto onBody = [body](const auto& item) {
            return item.bodyId == body.id;
        };
        const auto fleetAtBody = [body](const Fleet& fleet) {
            return fleet.currentBodyId == body.id;
        };

        std::size_t knownDepositCount = 0;
        std::size_t estimatedDepositCount = 0;
        std::size_t unknownDepositCount = 0;
        double confirmedDepositTotal = 0.0;
        double estimatedDepositTotal = 0.0;
        double uncertainDepositTotal = 0.0;
        for (const MineralDeposit& deposit : state.mineralDeposits) {
            if (deposit.bodyId != body.id) {
                continue;
            }

            switch (depositSurveyState(deposit)) {
            case DepositSurveyState::Known:
                ++knownDepositCount;
                break;
            case DepositSurveyState::Estimated:
                ++estimatedDepositCount;
                break;
            case DepositSurveyState::Unknown:
                ++unknownDepositCount;
                break;
            }
            confirmedDepositTotal += confirmedDepositQuantity(deposit);
            estimatedDepositTotal += estimatedDepositQuantity(deposit);
            uncertainDepositTotal += uncertainDepositQuantity(deposit);
        }

        // Counts are derived here rather than in the UI so the Bodies/System
        // panel remains a read-only projection over stable app DTOs.
        const std::optional<InstitutionId> ownerInstitutionId = bodyOwnerInstitutionId(state, body.id);
        summaries.push_back(BodySystemSummary{
            .id = body.id,
            .name = body.name,
            .type = body.type,
            .typeName = bodyTypeName(body.type),
            .strategicZone = body.strategicZone,
            .strategicZoneName = strategicZoneName(body.strategicZone),
            .ownerInstitutionId = ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, ownerInstitutionId),
            .parentBodyId = body.parentBodyId,
            .parentBodyName = body.parentBodyId.has_value() ? bodyName(state, *body.parentBodyId) : std::string{},
            .orbitalRadiusKm = body.orbitalRadiusKm,
            .orbitalPeriodDays = body.orbitalPeriodDays,
            .displayRadius = body.displayRadius,
            .colonyCount = static_cast<std::size_t>(std::count_if(state.colonies.begin(), state.colonies.end(), onBody)),
            .mineralDepositCount = static_cast<std::size_t>(std::count_if(state.mineralDeposits.begin(), state.mineralDeposits.end(), onBody)),
            .knownDepositCount = knownDepositCount,
            .estimatedDepositCount = estimatedDepositCount,
            .unknownDepositCount = unknownDepositCount,
            .confirmedDepositQuantity = confirmedDepositTotal,
            .estimatedDepositQuantity = estimatedDepositTotal,
            .uncertainDepositQuantity = uncertainDepositTotal,
            .fleetCount = static_cast<std::size_t>(std::count_if(state.fleets.begin(), state.fleets.end(), fleetAtBody))
        });
    }

    return summaries;
}

std::vector<BodyDepositSummary> SimulationQueries::bodyDeposits(const BodyId bodyId) const {
    const GameState& state = service_.state();
    const std::vector<MineralForecastCauseChain> mineralForecasts = ForecastService{service_}.mineralForecastCauseChains();
    std::vector<BodyDepositSummary> summaries;

    for (const MineralDeposit& deposit : state.mineralDeposits) {
        if (deposit.bodyId != bodyId) {
            continue;
        }

        const DepositSurveyState stateName = depositSurveyState(deposit);
        summaries.push_back(BodyDepositSummary{
            .bodyId = deposit.bodyId,
            .bodyName = bodyName(state, deposit.bodyId),
            .mineral = deposit.mineral,
            .mineralName = mineralDisplayName(deposit.mineral),
            .confidence = deposit.confidence,
            .surveyState = stateName,
            .surveyStateName = depositSurveyStateName(stateName),
            .confirmedQuantity = confirmedDepositQuantity(deposit),
            .estimatedQuantity = estimatedDepositQuantity(deposit),
            .uncertainQuantity = uncertainDepositQuantity(deposit),
            .accessibility = deposit.accessibility,
            .shortageRelevant = isMaterialShortageRelevant(mineralForecastFor(mineralForecasts, deposit.mineral)),
            .strategicRelevance = depositStrategicRelevance(deposit, mineralForecasts)
        });
    }

    return summaries;
}

ExplorationIntelligenceSummary SimulationQueries::explorationIntelligence() const {
    const GameState& state = service_.state();
    const std::vector<MineralForecastCauseChain> mineralForecasts = ForecastService{service_}.mineralForecastCauseChains();

    ExplorationIntelligenceSummary summary;

    for (const MineralDeposit& deposit : state.mineralDeposits) {
        if (isDepositKnown(deposit)) {
            continue;
        }

        const DepositSurveyState stateName = depositSurveyState(deposit);
        const MineralForecastCauseChain* forecast = mineralForecastFor(mineralForecasts, deposit.mineral);
        summary.lowConfidenceDeposits.push_back(ExplorationDepositIntelligenceRow{
            .bodyId = deposit.bodyId,
            .bodyName = bodyName(state, deposit.bodyId),
            .mineral = deposit.mineral,
            .mineralName = mineralDisplayName(deposit.mineral),
            .surveyState = stateName,
            .surveyStateName = depositSurveyStateName(stateName),
            .confidence = deposit.confidence,
            .confirmedQuantity = confirmedDepositQuantity(deposit),
            .estimatedQuantity = estimatedDepositQuantity(deposit),
            .unknownPotentialQuantity = stateName == DepositSurveyState::Unknown ? deposit.remaining : 0.0,
            .accessibility = deposit.accessibility,
            .shortageRelevant = isMaterialShortageRelevant(forecast),
            .strategicRelevance = depositStrategicRelevance(deposit, mineralForecasts)
        });
    }

    std::sort(summary.lowConfidenceDeposits.begin(),
              summary.lowConfidenceDeposits.end(),
              [](const ExplorationDepositIntelligenceRow& lhs, const ExplorationDepositIntelligenceRow& rhs) {
                  if (lhs.shortageRelevant != rhs.shortageRelevant) {
                      return lhs.shortageRelevant && !rhs.shortageRelevant;
                  }
                  if (lhs.confidence != rhs.confidence) {
                      return lhs.confidence < rhs.confidence;
                  }
                  if (lhs.bodyName != rhs.bodyName) {
                      return lhs.bodyName < rhs.bodyName;
                  }
                  return static_cast<std::size_t>(lhs.mineral) < static_cast<std::size_t>(rhs.mineral);
              });

    for (const SimEvent& event : state.eventLog) {
        const auto* survey = std::get_if<ResourceSurveyCompletedEvent>(&event.payload);
        if (survey == nullptr) {
            continue;
        }

        std::ostringstream out;
        out << bodyName(state, survey->bodyId) << ": ";
        if (survey->depositsImproved == 0) {
            out << "No new information from this pass";
        } else {
            out << "improved " << survey->depositsImproved
                << " deposit(s) from " << (survey->averageConfidenceBefore * 100.0)
                << "% to " << (survey->averageConfidenceAfter * 100.0) << "% average confidence.";
        }

        summary.recentSurveyResults.push_back(RecentSurveyResultSummary{
            .eventId = event.id,
            .day = event.day,
            .fleetId = survey->fleetId,
            .fleetName = fleetName(state, survey->fleetId),
            .bodyId = survey->bodyId,
            .bodyName = bodyName(state, survey->bodyId),
            .depositsImproved = survey->depositsImproved,
            .averageConfidenceBefore = survey->averageConfidenceBefore,
            .averageConfidenceAfter = survey->averageConfidenceAfter,
            .summary = out.str()
        });
    }

    std::sort(summary.recentSurveyResults.begin(),
              summary.recentSurveyResults.end(),
              [](const RecentSurveyResultSummary& lhs, const RecentSurveyResultSummary& rhs) {
                  if (lhs.day != rhs.day) {
                      return lhs.day > rhs.day;
                  }
                  return lhs.eventId.value > rhs.eventId.value;
              });

    for (const MineralForecastCauseChain& forecast : mineralForecasts) {
        if (!forecast.uncertaintyWarning.empty()) {
            summary.warnings.push_back(forecast.uncertaintyWarning);
        }
    }

    return summary;
}

std::vector<StrategicBodySummary> SimulationQueries::strategicBodies() const {
    const GameState& state = service_.state();
    std::vector<StrategicBodySummary> summaries;
    summaries.reserve(state.bodies.size());

    for (const Body& body : state.bodies) {
        const std::optional<InstitutionId> ownerInstitutionId = bodyOwnerInstitutionId(state, body.id);
        const MapPosition position = bodyPositionAtDay(state, body.id, state.date.day).value_or(fallbackBodyPosition(body));
        summaries.push_back(StrategicBodySummary{
            .id = body.id,
            .name = body.name,
            .type = body.type,
            .typeName = bodyTypeName(body.type),
            .strategicZone = body.strategicZone,
            .strategicZoneName = strategicZoneName(body.strategicZone),
            .ownerInstitutionId = ownerInstitutionId,
            .ownerInstitutionName = optionalInstitutionName(state, ownerInstitutionId),
            .parentBodyId = body.parentBodyId,
            .parentBodyName = body.parentBodyId.has_value() ? bodyName(state, *body.parentBodyId) : std::string{},
            .orbitalRadiusKm = body.orbitalRadiusKm,
            .orbitalPeriodDays = body.orbitalPeriodDays,
            .displayRadius = body.displayRadius,
            .x = position.x,
            .y = position.y
        });
    }

    return summaries;
}


std::optional<StrategicBodySummary> SimulationQueries::strategicBody(const BodyId id) const {
    const std::vector<StrategicBodySummary> summaries = strategicBodies();
    const auto it = std::find_if(summaries.begin(), summaries.end(), [id](const StrategicBodySummary& summary) {
        return summary.id == id;
    });
    return it == summaries.end() ? std::optional<StrategicBodySummary>{} : std::optional<StrategicBodySummary>{*it};
}

std::vector<StrategicFleetSummary> SimulationQueries::strategicFleets() const {
    const GameState& state = service_.state();
    std::vector<StrategicFleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        MapPosition position = bodyPositionAtDay(state, fleet.currentBodyId, state.date.day).value_or(MapPosition{});
        MapPosition destinationPosition = fleet.destinationBodyId.has_value()
            ? bodyPositionAtDay(state, *fleet.destinationBodyId, state.date.day).value_or(MapPosition{})
            : MapPosition{};
        MapPosition controlPosition{};
        MapPosition projectedArrival{};
        std::string phase;
        const bool moving = fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.destinationBodyId.has_value();
        if (moving) {
            const double total = static_cast<double>(std::max<std::int64_t>(1, fleet.activeOrder.arrivalDay - fleet.activeOrder.departureDay));
            const double elapsed = std::clamp(static_cast<double>(state.date.day - fleet.activeOrder.departureDay), 0.0, total);
            const double t = elapsed / total;
            const MapPosition p0 = fleet.activeOrder.departurePosition;
            const MapPosition p2 = fleet.activeOrder.projectedArrivalPosition;
            // Presentation-only compatibility: recompute the curve so old saves
            // use current visual styling while retaining saved endpoints, ETA,
            // and fuel accounting. Elapsed time is a curve parameter, not a
            // physical acceleration integration or a mutable fleet position.
            const MapPosition p1 = routeCurveControlPoint(p0, p2);
            position = MapPosition{
                .x = ((1.0 - t) * (1.0 - t) * p0.x) + (2.0 * (1.0 - t) * t * p1.x) + (t * t * p2.x),
                .y = ((1.0 - t) * (1.0 - t) * p0.y) + (2.0 * (1.0 - t) * t * p1.y) + (t * t * p2.y)
            };
            controlPosition = p1;
            projectedArrival = p2;
            phase = burnPhaseName(fleet.activeOrder, state.date.day);
        }

        summaries.push_back(StrategicFleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .x = position.x,
            .y = position.y,
            .departureX = moving ? fleet.activeOrder.departurePosition.x : position.x,
            .departureY = moving ? fleet.activeOrder.departurePosition.y : position.y,
            .destinationBodyId = fleet.destinationBodyId,
            .destinationX = destinationPosition.x,
            .destinationY = destinationPosition.y,
            .controlX = controlPosition.x,
            .controlY = controlPosition.y,
            .projectedArrivalX = projectedArrival.x,
            .projectedArrivalY = projectedArrival.y,
            .moving = moving,
            .daysRemaining = fleet.activeOrder.daysRemaining,
            .routeVisualStyle = RouteVisualStyle::SustainedBurn,
            .routeVisualStyleName = moving ? routeVisualStyleName(RouteVisualStyle::SustainedBurn) : std::string{},
            .burnPhase = std::move(phase)
        });
    }

    return summaries;
}

std::vector<SurveyTeamSummary> SimulationQueries::surveyTeams() const {
    const GameState& state = service_.state();
    std::vector<SurveyTeamSummary> rows;
    rows.reserve(state.surveyTeams.size());
    for (const SurveyTeam& team : state.surveyTeams) {
        const auto controlling = std::find_if(state.surveyPrograms.begin(), state.surveyPrograms.end(),
            [&team](const SurveyProgram& program) { return program.leasedTeamId == team.id; });
        const std::string location = surveyTeamLocationName(state, team);
        rows.push_back(SurveyTeamSummary{
            .id = team.id,
            .name = team.name,
            .locationKind = team.locationKind,
            .locationName = location,
            .controllingProgramId = controlling == state.surveyPrograms.end()
                ? std::optional<SurveyProgramId>{} : std::optional<SurveyProgramId>{controlling->id}
        });
    }
    return rows;
}

std::vector<SurveyProgramSummary> SimulationQueries::surveyPrograms() const {
    const GameState& state = service_.state();
    std::vector<SurveyProgramSummary> rows;
    rows.reserve(state.surveyPrograms.size());
    for (const SurveyProgram& program : state.surveyPrograms) {
        SurveyProgramSummary row;
        row.id = program.id;
        row.charter = program.charter;
        row.createdDay = program.createdDay;
        row.charterRevision = program.charterRevision;
        row.lifecycle = program.lifecycle;
        row.closure = program.closure;
        row.lifecycleName = surveyLifecycleName(program);
        row.condition = surveyProgramExecutionCondition(state, program);
        row.taskName = surveyTaskName(program.task);
        if (program.taskBodyId) row.taskName += " at " + bodyName(state, *program.taskBodyId);
        row.homeName = colonyName(state, program.charter.homeColonyId);
        row.pendingHomeColonyId = program.pendingHomeColonyId;
        row.pendingHomeName = program.pendingHomeColonyId
            ? colonyName(state, *program.pendingHomeColonyId) : std::string{};
        row.requestedFleetName = program.charter.requestedFleetId
            ? fleetName(state, *program.charter.requestedFleetId) : "Unassigned";
        row.requestedTeamName = program.charter.requestedTeamId
            ? surveyTeamName(state, *program.charter.requestedTeamId) : "Unassigned";
        row.leaderName = program.charter.requestedLeaderId
            ? personName(state, *program.charter.requestedLeaderId) : "Unassigned";
        if (program.charter.requestedLeaderId) {
            if (const Person* leader = findById(state.people, *program.charter.requestedLeaderId)) {
                row.leaderApproachName = surveyApproachName(leader->surveyPlanningApproach);
            }
        }
        row.leasedFleetId = program.leasedFleetId;
        row.leasedTeamId = program.leasedTeamId;
        row.leasedFleetName = program.leasedFleetId ? fleetName(state, *program.leasedFleetId) : "None";
        row.leasedTeamName = program.leasedTeamId ? surveyTeamName(state, *program.leasedTeamId) : "None";
        // After an amendment and safe lease release, requested IDs may no
        // longer name the assets that carried committed field work. Prefer the
        // task identities when presenting the physical cancellation outcome.
        const std::optional<SurveyTeamId> relevantTeam = program.leasedTeamId
            ? program.leasedTeamId
            : (program.taskTeamId ? program.taskTeamId : program.charter.requestedTeamId);
        if (relevantTeam) {
            if (const SurveyTeam* team = findById(state.surveyTeams, *relevantTeam)) {
                row.teamLocationName = surveyTeamLocationName(state, *team);
            }
        }
        row.taskBodyId = program.taskBodyId;
        row.taskFleetId = program.taskFleetId;
        row.taskTeamId = program.taskTeamId;
        row.taskLeaderId = program.taskLeaderId;
        row.taskFleetName = program.taskFleetId ? fleetName(state, *program.taskFleetId) : "None";
        row.taskTeamName = program.taskTeamId ? surveyTeamName(state, *program.taskTeamId) : "None";
        row.taskLeaderName = program.taskLeaderId ? personName(state, *program.taskLeaderId) : "None";
        row.taskPassNumber = program.taskPassNumber;
        row.workDaysCompleted = program.workDaysCompleted;
        row.historicalCompletedVisits = static_cast<int>(program.receipts.size());
        row.fuelLoaded = program.fuelLoaded;
        row.fuelBurned = program.fuelBurned;
        row.nextReportDay = program.nextReportDay;
        row.issueSignature = program.issue.signature;
        row.issueMessage = program.issue.message;
        row.issueAcknowledged = program.issue.acknowledged;
        row.targetChoiceReason = program.lastSelectionReason;

        const std::optional<FleetId> locationFleet = program.leasedFleetId
            ? program.leasedFleetId : program.taskFleetId;
        if (locationFleet) {
            if (const Fleet* fleet = findById(state.fleets, *locationFleet)) {
                row.currentLocationName = program.leasedFleetId ? "" : "Former task fleet currently at ";
                row.currentLocationName += bodyName(state, fleet->currentBodyId);
                if (fleet->activeOrder.type == FleetOrderType::MoveToBody && fleet->destinationBodyId) {
                    row.currentLocationName += " (in transit to " + bodyName(state, *fleet->destinationBodyId) + ")";
                }
            }
        }
        if (row.currentLocationName.empty()) {
            const bool fieldTeam = program.taskTeamId.has_value();
            row.currentLocationName = row.teamLocationName.empty()
                ? "No active fleet lease" : "No active fleet lease; " +
                    std::string{fieldTeam ? "task team " : "requested team "} + row.teamLocationName;
        }

        row.targets.reserve(program.charter.targets.size());
        SurveyPlanningInputs planning;
        if (program.charter.requestedLeaderId) {
            if (const Person* leader = findById(state.people, *program.charter.requestedLeaderId)) {
                planning.approach = leader->surveyPlanningApproach;
            }
        }
        for (std::size_t i = 0; i < program.charter.targets.size(); ++i) {
            const SurveyProgramTarget& target = program.charter.targets[i];
            const int completed = completedSurveyPasses(program, target.bodyId);
            row.requestedVisits += target.requestedPasses;
            row.completedVisits += std::min(completed, target.requestedPasses);
            row.targets.push_back(SurveyProgramTargetSummary{
                .bodyId = target.bodyId,
                .bodyName = bodyName(state, target.bodyId),
                .priority = target.priority,
                .requestedPasses = target.requestedPasses,
                .completedPasses = completed
            });
            planning.candidates.push_back(SurveyPlanningCandidate{
                .bodyId = target.bodyId,
                .playerPriority = target.priority,
                .requestedPasses = target.requestedPasses,
                .completedPasses = completed,
                .charterOrdinal = i,
                .knownFeasible = true,
                .waitingReason = {}
            });
            if (row.targetChoiceReason.empty() && program.taskBodyId == target.bodyId) {
                row.targetChoiceReason = "Current assignment uses charter row " + std::to_string(i + 1)
                    + " (priority " + std::to_string(target.priority) + ") under "
                    + (row.leaderApproachName.empty() ? "coverage-first" : row.leaderApproachName) + " planning.";
            }
        }
        if (row.targetChoiceReason.empty() && program.taskBodyId) {
            row.targetChoiceReason = "Committed task at " + bodyName(state, *program.taskBodyId)
                + " remains from an earlier charter scope.";
        }
        if (row.targetChoiceReason.empty()) {
            if (const auto choice = chooseSurveyTarget(planning)) {
                row.targetChoiceReason = "Priority order if otherwise feasible: " + choice->reason + ".";
            }
        }

        row.reports.reserve(program.reports.size());
        for (const SurveyProgramReport& report : program.reports) {
            std::string approach = surveyApproachName(report.approach);
            row.reports.push_back(SurveyProgramReportSummary{
                .startDay = report.startDay,
                .endDay = report.endDay,
                .isNinetyDayReview = report.isNinetyDayReview,
                .charterRevision = report.charterRevision,
                .leaderName = report.leaderId ? personName(state, *report.leaderId) : "Unassigned",
                .approachName = std::move(approach),
                .visitsCompleted = report.visitsCompleted,
                .workDays = report.workDays,
                .fuelLoaded = report.fuelLoaded,
                .fuelBurned = report.fuelBurned,
                .fleetName = report.fleetId ? fleetName(state, *report.fleetId) : "None",
                .teamName = report.teamId ? surveyTeamName(state, *report.teamId) : "None",
                .fleetBodyName = report.fleetBodyId ? bodyName(state, *report.fleetBodyId) : "None",
                .waitingReason = report.waitingReason
            });
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

SurveyProgramCharterPreview SimulationQueries::previewSurveyProgramCharter(
    const SurveyProgramCharter& charter, const std::optional<SurveyProgramId> amendingProgramId) const {
    const GameState& state = service_.state();
    SurveyProgramCharterPreview preview;
    if (const auto error = validateSurveyProgramCharter(state, charter)) {
        preview.validationMessage = *error;
        return preview;
    }
    preview.structurallyValid = true;
    preview.validationMessage = "Charter can be authorized. Readiness may change before the next simulated day.";
    SurveyProgram proposed;
    if (amendingProgramId) {
        if (const SurveyProgram* existing = findById(state.surveyPrograms, *amendingProgramId)) {
            proposed = *existing;
        }
    }
    proposed.charter = charter;
    if (amendingProgramId && proposed.task != SurveyProgramTask::None) {
        if (const SurveyProgram* existing = findById(state.surveyPrograms, *amendingProgramId)) {
            if (charter.homeColonyId != existing->charter.homeColonyId) {
                proposed.pendingHomeColonyId = charter.homeColonyId;
                proposed.charter.homeColonyId = existing->charter.homeColonyId;
            }
        }
    }
    preview.executionCondition = surveyProgramExecutionCondition(state, proposed);
    if (!charter.requestedFleetId) preview.waitingReasons.push_back("No fleet requested");
    if (!charter.requestedTeamId) preview.waitingReasons.push_back("No survey team requested");
    if (!charter.requestedLeaderId) preview.waitingReasons.push_back("No leader requested");
    if (charter.requestedFleetId) {
        const Fleet* fleet = findById(state.fleets, *charter.requestedFleetId);
        if (fleet != nullptr) {
            const bool heldByAmendedProgram = amendingProgramId && std::any_of(
                state.surveyPrograms.begin(), state.surveyPrograms.end(),
                [amendingProgramId, fleet](const SurveyProgram& program) {
                    return program.id == *amendingProgramId && program.leasedFleetId == fleet->id;
                });
            if (!heldByAmendedProgram &&
                (fleet->activeOrder.type != FleetOrderType::None || !fleet->queuedOrders.empty())) {
                preview.waitingReasons.push_back("Requested fleet has a manual order");
            }
            if (evaluateFleetSurvey(state, *fleet).operationalCapability <= 0.0) {
                preview.waitingReasons.push_back("Requested fleet lacks operational survey capability");
            }
            const Colony* home = findById(state.colonies, charter.homeColonyId);
            const double transferableAtHome = home == nullptr ? 0.0
                : std::max(0.0, home->processedStockpile.get(ProcessedMaterial::Propellant)
                    - charter.policy.homeStockFloor);
            double alreadyLoaded = 0.0;
            if (amendingProgramId) {
                if (const SurveyProgram* existing = findById(state.surveyPrograms, *amendingProgramId)) {
                    alreadyLoaded = existing->fuelLoaded;
                }
            }
            const bool budgetAllowsRefill = !charter.policy.maxAdditionalPropellant ||
                *charter.policy.maxAdditionalPropellant > alreadyLoaded + kFuelComparisonEpsilon;
            if (!heldByAmendedProgram && fleetFuelTotals(state, *fleet).currentFuel <= kFuelComparisonEpsilon &&
                (transferableAtHome <= kFuelComparisonEpsilon || !budgetAllowsRefill)) {
                preview.waitingReasons.push_back(budgetAllowsRefill
                    ? "Requested fleet has no onboard propellant and no available home refill"
                    : "Requested fleet has no onboard propellant and additional-fuel authorization is exhausted");
            }
            const auto owner = controllingProgram(state, fleet->id);
            if (owner && (!amendingProgramId || *owner != ProgramController{*amendingProgramId})) {
                preview.waitingReasons.push_back("Requested fleet is controlled by " + programControllerLabel(state, *owner));
            }
        }
    }
    if (charter.requestedTeamId) {
        const SurveyTeam* team = findById(state.surveyTeams, *charter.requestedTeamId);
        const Fleet* fleet = charter.requestedFleetId ? findById(state.fleets, *charter.requestedFleetId) : nullptr;
        const Colony* home = findById(state.colonies, charter.homeColonyId);
        if (team != nullptr && fleet != nullptr && home != nullptr) {
            const bool alreadyAboard = team->locationKind == SurveyTeamLocationKind::Fleet &&
                team->fleetId == fleet->id;
            const bool atHomeTogether = team->locationKind == SurveyTeamLocationKind::Colony &&
                team->colonyId == home->id && fleet->currentBodyId == home->bodyId &&
                fleet->activeOrder.type == FleetOrderType::None;
            if (!alreadyAboard && !atHomeTogether) {
                preview.waitingReasons.push_back("Requested team and fleet are not co-located for embarkation");
            }
        }
        const auto owner = std::find_if(state.surveyPrograms.begin(), state.surveyPrograms.end(),
            [&charter, amendingProgramId](const SurveyProgram& program) {
                return program.id != amendingProgramId && program.leasedTeamId == charter.requestedTeamId;
            });
        if (owner != state.surveyPrograms.end()) preview.waitingReasons.push_back("Requested team is controlled by another program");
    }
    SurveyPlanningInputs planning;
    if (charter.requestedLeaderId) {
        if (const Person* leader = findById(state.people, *charter.requestedLeaderId)) {
            planning.approach = leader->surveyPlanningApproach;
        }
    }
    for (std::size_t i = 0; i < charter.targets.size(); ++i) {
        const SurveyProgramTarget& target = charter.targets[i];
        planning.candidates.push_back(SurveyPlanningCandidate{
            .bodyId = target.bodyId,
            .playerPriority = target.priority,
            .requestedPasses = target.requestedPasses,
            .charterOrdinal = i,
            .knownFeasible = true,
            .waitingReason = {}
        });
    }
    if (const auto choice = chooseSurveyTarget(planning)) {
        preview.firstTargetChoiceReason = "Priority order if otherwise feasible: " + choice->reason + ".";
    }
    return preview;
}

std::vector<FreightProgramSummary> SimulationQueries::freightPrograms() const {
    const GameState& state = service_.state();
    std::vector<FreightProgramSummary> rows;
    for (const FreightProgram& program : state.freightPrograms) {
        FreightProgramSummary row;
        row.id = program.id;
        row.charter = program.charter;
        row.charterRevision = program.charterRevision;
        row.lifecycle = program.lifecycle;
        row.closure = program.closure;
        switch (program.lifecycle) {
        case FreightProgramLifecycle::Authorized: row.lifecycleName = "Authorized"; break;
        case FreightProgramLifecycle::Suspended: row.lifecycleName = "Suspended"; break;
        case FreightProgramLifecycle::Closing: row.lifecycleName = "Closing"; break;
        case FreightProgramLifecycle::Closed: row.lifecycleName = "Closed"; break;
        }
        if (program.closure == FreightProgramClosure::Completed) row.lifecycleName += " / Completed";
        if (program.closure == FreightProgramClosure::Cancelled) row.lifecycleName += " / Cancelled";
        switch (program.task) {
        case FreightProgramTask::None: row.taskName = "Planning"; break;
        case FreightProgramTask::Reposition: row.taskName = "Reposition to source"; break;
        case FreightProgramTask::Preparing: row.taskName = "Prepare operating fuel"; break;
        case FreightProgramTask::Loading: row.taskName = "Loading cargo"; break;
        case FreightProgramTask::Outbound: row.taskName = "Outbound"; break;
        case FreightProgramTask::Unloading: row.taskName = "Unload at destination"; break;
        case FreightProgramTask::Return: row.taskName = "Empty return to source"; break;
        case FreightProgramTask::ReturningCargo: row.taskName = "Return unshipped cargo to source stock"; break;
        }
        row.condition = freightProgramExecutionCondition(state, program);
        row.sourceName = colonyName(state, program.charter.sourceColonyId);
        row.destinationName = colonyName(state, program.charter.destinationColonyId);
        row.materialName = std::string{toString(program.charter.material)};
        row.requestedFleetName = program.charter.requestedFleetId ? fleetName(state, *program.charter.requestedFleetId) : "Unassigned";
        row.leasedFleetName = program.leasedFleetId ? fleetName(state, *program.leasedFleetId) : "None";
        row.taskFleetName = program.taskFleetId ? fleetName(state, *program.taskFleetId) : "None";
        row.leaderName = program.charter.requestedLeaderId ? personName(state, *program.charter.requestedLeaderId) : "Unassigned";
        row.leasedFleetId = program.leasedFleetId;
        row.taskFleetId = program.taskFleetId;
        row.pendingFleetChange = program.taskFleetId && program.taskFleetId != program.charter.requestedFleetId;
        row.shipment = program.shipment;
        row.shipmentLeaderName = program.shipment ? personName(state, program.shipment->leaderId) : "None";
        row.cargoLoaded = program.cargoLoaded;
        row.cargoDelivered = program.cargoDelivered;
        row.cargoReturned = program.cargoReturned;
        row.cargoAboard = freightCargoAboard(state, program.id);
        row.unpickedQuantity = freightUnpickedQuantity(state, program);
        row.committedQuantity = row.cargoAboard;
        if (program.shipment && program.closure != FreightProgramClosure::Cancelled) {
            row.committedQuantity += std::max(0.0, freightShipmentPlannedQuantity(*program.shipment)
                - freightShipmentLoadedQuantity(program));
        }
        row.commitmentAboveTarget = std::max(0.0, program.cargoDelivered + row.committedQuantity - program.charter.totalQuantity);
        row.fuelLoaded = program.fuelLoaded;
        row.fuelBurned = program.fuelBurned;
        if (program.charter.policy.maxAdditionalPropellant) row.fuelAllowanceRemaining = freightFuelAllowanceRemaining(program);
        if (program.lifecycle != FreightProgramLifecycle::Closed) row.nextReportDay = program.nextReportDay;
        row.closedDay = program.closedDay;
        row.issue = program.issue;
        // Every unfinished closing disposition retains the same decision surface.
        row.canAmend = program.lifecycle != FreightProgramLifecycle::Closed;
        row.canSuspend = row.canAmend && program.lifecycle != FreightProgramLifecycle::Suspended;
        row.canResume = program.lifecycle == FreightProgramLifecycle::Suspended;
        row.canCancel = row.canAmend;
        if (const Colony* source = findById(state.colonies, program.charter.sourceColonyId)) {
            row.sourceCargoStock = source->processedStockpile.get(program.charter.material);
            row.sourcePropellantStock = source->processedStockpile.get(ProcessedMaterial::Propellant);
        }
        if (const Colony* destination = findById(state.colonies, program.charter.destinationColonyId)) {
            row.destinationStock = destination->processedStockpile.get(program.charter.material);
        }
        const auto fleetId = program.lifecycle == FreightProgramLifecycle::Closed ? std::optional<FleetId>{}
            : program.taskFleetId ? program.taskFleetId
            : (program.leasedFleetId ? program.leasedFleetId : program.charter.requestedFleetId);
        row.locationName = program.lifecycle == FreightProgramLifecycle::Closed ? "No active cargo custody" : "No fleet assigned";
        if (const Fleet* fleet = fleetId ? findById(state.fleets, *fleetId) : nullptr) {
            row.locationName = bodyName(state, fleet->currentBodyId);
            if (fleet->activeOrder.type != FleetOrderType::None) {
                row.currentLegArrivalDay = fleet->activeOrder.arrivalDay;
                if (fleet->destinationBodyId) row.routeDestinationName = bodyName(state, *fleet->destinationBodyId);
            }
            for (const FreightHullCapability& capability : freightHullCapabilities(state, *fleet)) {
                const Ship* ship = findById(state.ships, capability.shipId);
                FreightHullSummary hull;
                hull.shipId = capability.shipId;
                hull.shipName = ship ? ship->name : "<unknown ship>";
                hull.cargoCapacity = capability.capacity;
                hull.operationalHandlingPerDay = capability.operationalHandlingPerDay;
                hull.cargoQuantity = capability.onboardQuantity;
                if (program.shipment) {
                    for (const FreightManifestRow& manifest : program.shipment->manifest) {
                        if (manifest.shipId == hull.shipId) hull.plannedQuantity = manifest.plannedQuantity;
                    }
                }
                if (ship && ship->cargo) {
                    hull.cargoProgramId = ship->cargo->programId;
                    hull.shipmentNumber = ship->cargo->shipmentNumber;
                    hull.materialName = std::string{toString(ship->cargo->material)};
                }
                row.hulls.push_back(std::move(hull));
            }
        }
        for (const FreightTransferReceipt& receipt : program.receipts) {
            std::string action;
            switch (receipt.kind) {
            case FreightTransferKind::Load: action = "Cargo loaded"; break;
            case FreightTransferKind::Delivery: action = "Delivered to destination"; break;
            case FreightTransferKind::SourceReturn: action = "Unshipped cargo returned"; break;
            case FreightTransferKind::OperatingFuel: action = "Operating fuel loaded"; break;
            }
            row.receipts.push_back({receipt, std::move(action), fleetName(state, receipt.fleetId),
                personName(state, receipt.leaderId), colonyName(state, receipt.colonyId), std::string{toString(receipt.material)}});
        }
        for (const FreightProgramReport& report : program.reports) {
            row.reports.push_back({report, report.fleetId ? fleetName(state, *report.fleetId) : "None",
                report.fleetBodyId ? bodyName(state, *report.fleetBodyId) : "None"});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

FreightProgramCharterPreview SimulationQueries::previewFreightProgramCharter(
    const FreightProgramCharter& charter, const std::optional<FreightProgramId> amendingProgramId) const {
    const GameState& state = service_.state();
    FreightProgramCharterPreview preview;
    FreightProgram proposed;
    if (amendingProgramId) {
        const FreightProgram* existing = findById(state.freightPrograms, *amendingProgramId);
        if (!existing || existing->lifecycle == FreightProgramLifecycle::Closed) {
            preview.validationMessage = "No editable freight program with that identity";
            return preview;
        }
        if (charter.sourceColonyId != existing->charter.sourceColonyId ||
            charter.destinationColonyId != existing->charter.destinationColonyId || charter.material != existing->charter.material) {
            preview.validationMessage = "Source, destination and material are fixed contract identity";
            return preview;
        }
        proposed = *existing;
    }
    if (const auto error = validateFreightProgramCharter(state, charter, amendingProgramId.has_value())) {
        preview.validationMessage = *error;
        return preview;
    }
    proposed.charter = charter;
    preview.structurallyValid = true;
    preview.validationMessage = "Valid intention. Authorization does not transfer stock or guarantee readiness.";
    preview.executionCondition = freightProgramExecutionCondition(state, proposed);
    if (!charter.requestedFleetId) preview.waitingReasons.push_back("No fleet requested");
    if (!charter.requestedLeaderId) preview.waitingReasons.push_back("No leader requested");
    const auto fleetId = proposed.taskFleetId ? proposed.taskFleetId : charter.requestedFleetId;
    if (const Fleet* fleet = fleetId ? findById(state.fleets, *fleetId) : nullptr) {
        const auto owner = controllingProgram(state, fleet->id);
        if (owner && (!amendingProgramId || *owner != ProgramController{*amendingProgramId})) {
            preview.waitingReasons.push_back("Fleet controlled by " + programControllerLabel(state, *owner));
        }
        // A draft cannot replace the committed shipment. Its live manifest is
        // shown unchanged; only a free planning boundary receives new candidate
        // dates, avoiding a fictitious second departure while already at work.
        const bool nextOpeningExists = state.date.day < std::numeric_limits<std::int64_t>::max();
        const FreightShipmentPlan plan = proposed.shipment || !nextOpeningExists ? FreightShipmentPlan{}
            : planFreightShipment(state, proposed, *fleet, nullptr, state.date.day + 1);
        if (!nextOpeningExists) preview.waitingReasons.push_back("No representable next opening day");
        preview.shipmentReady = plan.ready;
        preview.plannedQuantity = plan.quantity;
        preview.requiredOperatingFuel = plan.requiredFuel;
        preview.additionalOperatingFuel = plan.additionalFuel;
        preview.loadingDays = plan.loadingDays;
        preview.unloadingDays = plan.unloadingDays;
        if (plan.ready) {
            preview.projectedDepartureDay = plan.departureDay;
            preview.projectedReturnDepartureDay = plan.returnDepartureDay;
        } else if (!plan.waitingReason.empty()) {
            preview.waitingReasons.push_back(plan.waitingReason);
        }
        for (const FreightHullCapability& capability : freightHullCapabilities(state, *fleet)) {
            const Ship* ship = findById(state.ships, capability.shipId);
            FreightHullSummary hull;
            hull.shipId = capability.shipId;
            hull.shipName = ship ? ship->name : "<unknown ship>";
            hull.cargoCapacity = capability.capacity;
            hull.operationalHandlingPerDay = capability.operationalHandlingPerDay;
            hull.cargoQuantity = capability.onboardQuantity;
            const auto& manifestRows = proposed.shipment ? proposed.shipment->manifest : plan.manifest;
            for (const FreightManifestRow& manifest : manifestRows) {
                if (manifest.shipId == hull.shipId) hull.plannedQuantity = manifest.plannedQuantity;
            }
            if (ship && ship->cargo) {
                hull.cargoProgramId = ship->cargo->programId;
                hull.shipmentNumber = ship->cargo->shipmentNumber;
                hull.materialName = std::string{toString(ship->cargo->material)};
            }
            preview.hulls.push_back(std::move(hull));
        }
    }
    return preview;
}

SurveyTimeSummary SimulationQueries::surveyTime() const {
    const std::int64_t day = service_.state().date.day;
    const auto futureBoundary = [day](const std::int64_t interval) -> std::optional<std::int64_t> {
        try { return nextGlobalSurveyBoundary(day, interval); }
        catch (const std::overflow_error&) { return std::nullopt; }
    };
    const auto thirty = futureBoundary(30);
    const auto ninety = futureBoundary(90);
    const auto commandDays = [day](const std::optional<std::int64_t> boundary) -> std::optional<int> {
        if (!boundary) return std::nullopt;
        const std::int64_t count = *boundary - day;
        return count <= std::numeric_limits<int>::max() ? std::optional<int>{static_cast<int>(count)} : std::nullopt;
    };
    return SurveyTimeSummary{
        .day = day,
        .nextThirtyDay = thirty,
        .nextNinetyDay = ninety,
        .daysUntilThirty = commandDays(thirty),
        .daysUntilNinety = commandDays(ninety)
    };
}

std::vector<EventLogEntrySummary> SimulationQueries::recentEvents(const std::size_t limit) const {
    const std::vector<SimEvent>& events = service_.state().eventLog;
    if (limit == 0 || events.empty()) {
        return {};
    }

    // Keep chronological order inside the visible tail so append-only UI widgets
    // can render the returned rows without reversing them first.
    const std::size_t start = events.size() > limit ? events.size() - limit : 0;

    std::vector<EventLogEntrySummary> summaries;
    summaries.reserve(events.size() - start);
    for (std::size_t index = start; index < events.size(); ++index) {
        summaries.push_back(summarizeEvent(events[index]));
    }

    return summaries;
}

} // namespace deep
