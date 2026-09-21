#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"
#include "sim/TransitPlanning.h"

// Self-contained regression tests for app-layer read-only query DTOs.
// These tests protect the future UI boundary from drifting back toward direct
// raw GameState vector inspection.

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

void requireNear(const double actual, const double expected, const std::string_view message) {
    if (std::abs(actual - expected) > 1.0e-6) {
        throw TestFailure{message};
    }
}



deep::InstitutionId institutionIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Institution& institution : state.institutions) {
        if (institution.name == name) {
            return institution.id;
        }
    }
    throw TestFailure{"expected institution was not present in scenario"};
}

const deep::AppointmentCandidateScore& candidateByName(
    const std::vector<deep::AppointmentCandidateScore>& candidates,
    const std::string_view name) {
    for (const deep::AppointmentCandidateScore& candidate : candidates) {
        if (candidate.personName == name) {
            return candidate;
        }
    }
    throw TestFailure{"expected appointment candidate was not present"};
}

const deep::AppointmentScoreBreakdownRow& scoreRowByLabel(
    const deep::AppointmentCandidateScore& candidate,
    const std::string_view label) {
    for (const deep::AppointmentScoreBreakdownRow& row : candidate.scoreBreakdown) {
        if (row.label == label) {
            return row;
        }
    }
    throw TestFailure{"expected score breakdown row was not present"};
}


deep::BodyId bodyIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Body& body : state.bodies) {
        if (body.name == name) {
            return body.id;
        }
    }
    throw TestFailure{"expected body was not present in scenario"};
}

deep::FleetId addTestFleetAt(deep::GameState& state, const deep::BodyId bodyId) {
    // App-query survey tests need a fleet at a frontier body without requiring a
    // movement command or save fixture. Keep the fixture valid by adding the
    // matching ship and fleet records together.
    const deep::FleetId fleetId{state.ids.nextFleetId++};
    const deep::ShipId shipId{state.ids.nextShipId++};
    const deep::ShipClass& shipClass = state.shipClasses.front();

    state.fleets.push_back(deep::Fleet{
        .id = fleetId,
        .name = "Query Survey Fleet",
        .currentBodyId = bodyId,
        .destinationBodyId = std::nullopt,
        .shipIds = {shipId},
        .activeOrder = deep::FleetOrder{},
        .queuedOrders = {},
        .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(deep::Ship{
        .id = shipId,
        .shipClassId = shipClass.id,
        .name = "Query Survey Cutter",
        .fleetId = fleetId,
        .fuel = shipClass.fuelCapacity
    });

    return fleetId;
}

double sumModifierRows(const std::vector<deep::AppointmentModifierBreakdownRow>& rows) {
    double total = 0.0;
    for (const deep::AppointmentModifierBreakdownRow& row : rows) {
        total += row.percent;
    }
    return total;
}

void test_colony_summaries_resolve_body_context() {
    // Verifies that colony queries return UI-useful copies with resolved body
    // names. Prevents future panels from needing raw GameState::colonies access.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto colonies = queries.colonies();

    require(colonies.size() == 4, "home scenario exposes mature-system colony summaries");
    require(colonies.front().name == "Terra Directorate", "colony summary includes colony name");
    require(colonies.front().bodyName == "Terra", "colony summary resolves body name");
    require(colonies.front().ownerInstitutionId.has_value(), "colony summary exposes owner institution ID");
    require(colonies.front().ownerInstitutionName == "Strategic Continuity Office",
            "colony summary resolves owner institution name");
    require(colonies.front().mines == 10.0, "colony summary includes mine count");
    require(colonies.front().processorCapacity == 50.0, "colony summary includes processor capacity");
    require(colonies.front().shipyardCapacity == 100.0, "colony summary includes shipyard capacity");
    require(colonies.front().totalRawStockpile > 0.0, "colony summary includes raw stockpile total");
    require(colonies.front().totalProcessedStockpile > 0.0, "colony summary includes processed stockpile total");
}


void test_colony_summaries_include_processing_policy() {
    // Verifies colony queries expose processing allocation state through DTOs so
    // the Colony panel can render controls without reading raw GameState.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;

    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 75.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::ReactorFuel, .weight = 25.0}
        }
    }).ok, "manual processing policy is accepted before colony query");

    const deep::SimulationQueries queries{service};
    const auto colonies = queries.colonies();

    require(colonies.size() == 4, "home scenario still exposes mature-system colony summaries");
    require(colonies.front().processingPolicy == deep::ProcessingPolicy::Manual,
            "colony summary exposes processing policy enum");
    require(colonies.front().processingPolicyName == "Manual", "colony summary exposes processing policy display name");
    require(colonies.front().manualProcessingAllocations.size() == deep::processedMaterialCount(),
            "colony summary exposes one manual processing row per processed material");
    require(colonies.front().manualProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::Propellant)).materialName == "Propellant",
            "manual allocation summary resolves processed material display name");
    requireNear(colonies.front().manualProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::Propellant)).normalizedPercent,
                75.0,
                "manual allocation summary reports normalized effective percent");
    require(colonies.front().effectiveProcessingAllocations.size() == deep::processedMaterialCount(),
            "colony summary exposes policy-derived processing rows for UI previews");
    requireNear(colonies.front().effectiveProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::ReactorFuel)).normalizedPercent,
                25.0,
                "manual policy effective allocation is normalized from weights");
    require(colonies.front().processedStockpiles.size() == deep::processedMaterialCount(),
            "colony summary exposes processed stockpiles for policy preview math");
}

void test_shipyard_order_summaries_resolve_names() {
    // Verifies that accepted build orders can be shown without joining colony
    // and ship-class vectors in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 2
    }).ok, "build order is accepted before querying summaries");

    const deep::SimulationQueries queries{service};
    const auto orders = queries.shipyardOrders();

    require(orders.size() == 1, "one shipyard order summary is returned");
    require(orders.front().colonyName == "Terra Directorate", "order summary resolves colony name");
    require(orders.front().shipClassName == "Survey Cutter", "order summary resolves ship-class name");
    require(orders.front().quantityRequested == 2, "order summary includes requested quantity");
    require(orders.front().quantityCompleted == 0, "new order has no completions");
    require(orders.front().requiredBuildPoints == 500.0, "order summary includes required build points");
    require(orders.front().statusName == "Active", "new order summary reports active status");
}


void test_production_backlog_summaries_expose_queue_eta() {
    // Verifies the UI-facing backlog query includes FIFO queue position and
    // queue-aware ETA so the Shipyard panel does not recalculate production.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "first build order is accepted before backlog query");
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "second build order is accepted before backlog query");

    const deep::SimulationQueries queries{service};
    const auto backlog = queries.productionBacklog();

    require(backlog.size() == 2, "two production backlog summaries are returned");
    require(backlog.front().colonyName == "Terra Directorate", "backlog summary resolves colony name");
    require(backlog.front().shipClassName == "Survey Cutter", "backlog summary resolves ship class name");
    require(backlog.front().queuePosition == 1, "first backlog row has queue position one");
    require(backlog.at(1).queuePosition == 2, "second backlog row has queue position two");
    require(backlog.front().etaDays.has_value(), "first backlog row has ETA");
    require(backlog.at(1).etaDays.has_value(), "second backlog row has ETA");
    require(*backlog.front().etaDays == 5, "first backlog ETA uses direct capacity");
    require(*backlog.at(1).etaDays == 10, "second backlog ETA includes first order capacity use");
    require(backlog.front().shipsRemaining == 1, "backlog summary exposes ships remaining");
    require(!backlog.front().requiredMaterialsRemaining.empty(), "backlog summary exposes processed material requirements");
    require(backlog.front().requiredMaterialsRemaining.front().material == deep::ProcessedMaterial::StructuralAlloys,
            "first material requirement names structural alloys");
    requireNear(backlog.front().requiredMaterialsRemaining.front().amount, 250.0,
                "first material requirement preserves remaining structural alloy need");
    require(backlog.front().blockingMaterialName.empty(), "well-stocked order has no blocking material name");
    require(backlog.front().statusName == "Building", "well-stocked order reports building status");
}

void test_personnel_summaries_resolve_institution_context() {
    // Personnel queries expose durable person records with resolved institution
    // names so future UI can render staff lists without raw GameState access.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto personnel = queries.personnel();

    require(personnel.size() >= 4, "home scenario exposes starter personnel summaries");
    require(personnel.front().name == "Director Mara Chen", "personnel summary includes starter person name");
    require(personnel.front().institutionName == "Strategic Continuity Office",
            "personnel summary resolves institution name");
    require(personnel.front().competencies.crisisManagement == 5,
            "personnel summary includes competency values");
    require(personnel.front().seniorityLevel == 5, "personnel summary includes seniority level");
    require(personnel.front().serviceRecord.commendations == 4,
            "personnel summary includes service record counters");
}

void test_appointment_summaries_resolve_people_and_scopes() {
    // Appointment queries flatten current responsibility slots into resolved
    // display rows. This keeps future personnel/debug panels out of raw
    // GameState appointment vectors.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::PersonId replacementPersonId = service.state().people.at(2).id;

    require(service.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::ShipyardDirector,
        .scopeType = deep::AppointmentScopeType::Colony,
        .scopeId = colonyId.value,
        .personId = replacementPersonId
    }).ok, "appointment reassignment is accepted before query");

    const deep::SimulationQueries queries{service};
    const auto appointments = queries.appointments();

    require(appointments.size() >= 5, "home scenario exposes starter appointment summaries");

    bool foundShipyardDirector = false;
    bool foundInstitutionHead = false;
    for (const deep::AppointmentSummary& appointment : appointments) {
        if (appointment.role == deep::AppointmentRole::ShipyardDirector &&
            appointment.scopeType == deep::AppointmentScopeType::Colony &&
            appointment.scopeId == colonyId.value) {
            foundShipyardDirector = true;
            require(appointment.roleName == "Shipyard Director", "appointment summary resolves role name");
            require(appointment.scopeTypeName == "Colony", "appointment summary resolves scope type name");
            require(appointment.scopeName == "Terra Directorate", "appointment summary resolves colony scope name");
            require(appointment.personId == replacementPersonId, "appointment summary exposes assigned person ID");
            require(appointment.personName == "Dr. Nia Okafor", "appointment summary resolves assigned person name");
            require(appointment.personInstitutionName == "Survey Office",
                    "appointment summary resolves assigned person's institution");
        }
        if (appointment.role == deep::AppointmentRole::InstitutionHead &&
            appointment.scopeType == deep::AppointmentScopeType::Institution) {
            foundInstitutionHead = true;
            require(appointment.scopeName == "Strategic Continuity Office",
                    "appointment summary resolves institution scope name");
        }
    }

    require(foundShipyardDirector, "appointment summaries include reassigned shipyard director slot");
    require(foundInstitutionHead, "appointment summaries include starter institution-head slot");
}



void test_appointment_candidates_rank_matching_competency_first() {
    // Survey-chief scoring should favor the role's primary/secondary
    // competencies without mutating appointment state or auto-selecting anyone.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::InstitutionId surveyOfficeId = institutionIdByName(state, "Survey Office");
    const deep::SimulationService service{std::move(state)};
    const deep::SimulationQueries queries{service};

    const auto candidates = queries.appointmentCandidatesFor(
        deep::AppointmentRole::SurveyChief,
        deep::AppointmentScopeType::Institution,
        surveyOfficeId.value);

    require(!candidates.empty(), "survey-chief candidate query returns personnel rows");
    require(candidates.front().personName == "Dr. Nia Okafor",
            "best survey-chief candidate ranks first for matching competencies");
    require(candidates.front().role == deep::AppointmentRole::SurveyChief,
            "candidate score preserves requested role enum");
    require(candidates.front().roleName == "Survey Chief", "candidate score resolves role display name");
    require(!candidates.front().scoreBreakdown.empty(), "candidate score exposes explainable breakdown rows");
}

void test_appointment_candidate_service_risks_lower_score() {
    // A poor service record should reduce the same person's score and expose
    // visible risk notes rather than hiding penalties in a single opaque number.
    deep::GameState baselineState = deep::createHomeSystemScenario();
    const deep::InstitutionId surveyOfficeId = institutionIdByName(baselineState, "Survey Office");
    const deep::SimulationService baselineService{baselineState};
    const deep::SimulationQueries baselineQueries{baselineService};
    const double baselineScore = candidateByName(
        baselineQueries.appointmentCandidatesFor(
            deep::AppointmentRole::SurveyChief,
            deep::AppointmentScopeType::Institution,
            surveyOfficeId.value),
        "Dr. Nia Okafor").totalScore;

    for (deep::Person& person : baselineState.people) {
        if (person.name == "Dr. Nia Okafor") {
            person.serviceRecord.failedAssignments += 30;
            person.serviceRecord.controversies += 20;
        }
    }

    const deep::SimulationService riskService{std::move(baselineState)};
    const deep::SimulationQueries riskQueries{riskService};
    const auto riskyCandidates = riskQueries.appointmentCandidatesFor(
        deep::AppointmentRole::SurveyChief,
        deep::AppointmentScopeType::Institution,
        surveyOfficeId.value);
    const deep::AppointmentCandidateScore& riskyCandidate = candidateByName(riskyCandidates, "Dr. Nia Okafor");

    require(riskyCandidate.totalScore < baselineScore, "bad service record lowers candidate score");
    require(riskyCandidate.riskNotes.size() >= 2, "bad service record produces visible risk notes");
}

void test_appointment_candidate_owner_match_breakdown_visible() {
    // Institution fit is deliberately a visible score component so the UI can
    // explain why a local institutional candidate is preferred or penalized.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};
    const deep::ColonyId terraColonyId = service.state().colonies.front().id;

    const auto candidates = queries.appointmentCandidatesFor(
        deep::AppointmentRole::ColonyAdministrator,
        deep::AppointmentScopeType::Colony,
        terraColonyId.value);
    const deep::AppointmentCandidateScore& mara = candidateByName(candidates, "Director Mara Chen");
    const deep::AppointmentScoreBreakdownRow& ownerMatch = scoreRowByLabel(mara, "Institution owner match");

    requireNear(ownerMatch.value, 10.0, "owner institution match adds a visible score component");
}

void test_appointment_candidate_breakdown_sums_to_total() {
    // The breakdown must be auditable: summing the displayed rows should produce
    // the same total used for sorting candidates.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};
    const deep::InstitutionId continuityOfficeId = institutionIdByName(service.state(), "Strategic Continuity Office");

    const auto candidates = queries.appointmentCandidatesFor(
        deep::AppointmentRole::InstitutionHead,
        deep::AppointmentScopeType::Institution,
        continuityOfficeId.value);

    double displayedTotal = 0.0;
    for (const deep::AppointmentScoreBreakdownRow& row : candidates.front().scoreBreakdown) {
        displayedTotal += row.value;
    }

    requireNear(displayedTotal, candidates.front().totalScore, "score breakdown rows sum to total score");
}

void test_appointment_candidate_tie_ordering_is_deterministic() {
    // Candidates with equal totals sort by seniority first, then name and ID.
    // The junior candidate has an extra success to tie the total, so this test
    // would fail if sorting used only score and name.
    deep::GameState state;
    const deep::InstitutionId institutionId{state.ids.nextInstitutionId++};
    state.institutions.push_back(deep::Institution{
        .id = institutionId,
        .name = "Test Institution",
        .type = deep::InstitutionType::ContinuityOffice
    });

    state.people.push_back(deep::Person{
        .id = deep::PersonId{state.ids.nextPersonId++},
        .name = "Beta Senior",
        .institutionId = institutionId,
        .competencies = deep::PersonCompetencies{},
        .seniorityLevel = 2,
        .serviceRecord = deep::PersonServiceRecord{}
    });
    state.people.push_back(deep::Person{
        .id = deep::PersonId{state.ids.nextPersonId++},
        .name = "Alpha Senior",
        .institutionId = institutionId,
        .competencies = deep::PersonCompetencies{},
        .seniorityLevel = 2,
        .serviceRecord = deep::PersonServiceRecord{}
    });
    state.people.push_back(deep::Person{
        .id = deep::PersonId{state.ids.nextPersonId++},
        .name = "Aardvark Junior",
        .institutionId = institutionId,
        .competencies = deep::PersonCompetencies{},
        .seniorityLevel = 1,
        .serviceRecord = deep::PersonServiceRecord{.successfulAssignments = 1}
    });

    const deep::SimulationService service{std::move(state)};
    const deep::SimulationQueries queries{service};
    const auto candidates = queries.appointmentCandidatesFor(
        deep::AppointmentRole::InstitutionHead,
        deep::AppointmentScopeType::Institution,
        institutionId.value);

    require(candidates.size() == 3, "tie-ordering fixture returns all candidates");
    requireNear(candidates.at(0).totalScore, candidates.at(1).totalScore, "senior candidates tie on total score");
    requireNear(candidates.at(1).totalScore, candidates.at(2).totalScore, "junior candidate ties on total score");
    require(candidates.at(0).personName == "Alpha Senior", "equal seniority tie sorts by name");
    require(candidates.at(1).personName == "Beta Senior", "second senior candidate follows by name");
    require(candidates.at(2).personName == "Aardvark Junior", "lower seniority comes after higher seniority on tied score");
}

void test_appointment_operational_effects_are_visible_and_deterministic() {
    // Operational-effect queries expose the same small capped modifier the sim
    // uses, including rows that sum to the visible percentage for UI audit.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto effects = queries.appointmentOperationalEffects();

    bool foundShipyardDirector = false;
    bool foundSurveyChief = false;
    for (const deep::AppointmentOperationalEffectSummary& effect : effects) {
        if (effect.role == deep::AppointmentRole::ShipyardDirector) {
            foundShipyardDirector = true;
            require(effect.operationName == "Shipyard BP/day", "shipyard director effect names the affected operation");
            requireNear(effect.modifierPercent, 10.0, "strong shipyard director is capped at +10 percent");
            require(!effect.modifierBreakdown.empty(), "shipyard director effect exposes breakdown rows");
            requireNear(sumModifierRows(effect.modifierBreakdown), effect.modifierPercent,
                        "shipyard modifier breakdown rows sum to capped modifier");
        }
        if (effect.role == deep::AppointmentRole::SurveyChief) {
            foundSurveyChief = true;
            require(effect.operationName == "Survey duration estimate", "survey chief effect is visible for future survey estimates");
        }
    }

    require(foundShipyardDirector, "operational effects include the colony shipyard director");
    require(foundSurveyChief, "operational effects include the survey chief estimate hook");
}

void test_weak_appointee_can_apply_small_negative_modifier() {
    // Poorly matched service history should be visible as a small capped penalty,
    // not a hidden or unbounded leader malus.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::InstitutionId continuityOfficeId = institutionIdByName(state, "Strategic Continuity Office");
    const deep::PersonId weakPersonId{state.ids.nextPersonId++};
    state.people.push_back(deep::Person{
        .id = weakPersonId,
        .name = "Temporary Clerk",
        .institutionId = continuityOfficeId,
        .competencies = deep::PersonCompetencies{},
        .seniorityLevel = 0,
        .serviceRecord = deep::PersonServiceRecord{
            .successfulAssignments = 0,
            .failedAssignments = 5,
            .commendations = 0,
            .controversies = 4
        }
    });

    deep::SimulationService service{std::move(state)};
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    require(service.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::ShipyardDirector,
        .scopeType = deep::AppointmentScopeType::Colony,
        .scopeId = colonyId.value,
        .personId = weakPersonId
    }).ok, "weak shipyard director appointment is accepted before query");

    const deep::SimulationQueries queries{service};
    const auto colonies = queries.colonies();

    requireNear(colonies.front().shipyardModifierPercent, -5.0, "weak appointee penalty is capped at -5 percent");
    requireNear(colonies.front().effectiveShipyardCapacity, 95.0, "negative modifier lowers effective shipyard capacity slightly");
    requireNear(sumModifierRows(colonies.front().shipyardModifierBreakdown), -5.0,
                "negative modifier breakdown rows sum to capped penalty");
}

void test_ship_class_summaries_expose_build_targets() {
    // Verifies that UI production panels can discover buildable ship classes
    // through query DTOs instead of reading GameState::shipClasses directly.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto shipClasses = queries.shipClasses();

    require(shipClasses.size() == 1, "home scenario exposes one buildable ship class summary");
    require(shipClasses.front().name == "Survey Cutter", "ship class summary includes Survey Cutter");
    require(shipClasses.front().roleName == "Survey", "ship class summary exposes display role name");
    require(shipClasses.front().buildPoints == 500.0, "ship class summary exposes build points");
}

void test_fleet_summaries_resolve_location_and_order() {
    // Verifies fleet summaries after ship completion and movement assignment.
    // This protects future map/fleet panels from duplicating movement joins.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before fleet summary test");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;

    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before fleet summary query");

    const deep::SimulationQueries queries{service};
    const auto fleets = queries.fleets();

    require(fleets.size() == 1, "one fleet summary is returned");
    require(fleets.front().currentBodyName == "Terra", "fleet summary resolves current body");
    require(fleets.front().destinationBodyName == "Mars", "fleet summary resolves destination body");
    require(fleets.front().shipCount == 1, "fleet summary includes ship count");
    requireNear(fleets.front().fuelCapacity, 1000.0, "fleet summary includes total fuel capacity");
    const double expectedFuel = 1000.0 - fleets.front().activeOrderTransitDistanceKm / deep::kKilometersPerMapUnit;
    requireNear(fleets.front().currentFuel, expectedFuel, "fleet summary includes fuel after starting movement");
    requireNear(fleets.front().currentRange, expectedFuel, "fleet summary exposes current fuel range");
    require(fleets.front().activeOrderName == "MoveToBody", "fleet summary includes active order type");
    require(fleets.front().hasActiveOrder, "fleet summary marks active movement orders");
    require(fleets.front().daysRemaining > 0, "fleet summary includes remaining movement days");
    require(fleets.front().activeOrderEtaDays.has_value(), "active movement exposes an ETA");
    require(*fleets.front().activeOrderEtaDays == fleets.front().daysRemaining, "active movement ETA uses remaining movement days");
    require(fleets.front().activeOrderProjectedArrivalDay == service.state().date.day + fleets.front().daysRemaining,
            "active movement exposes projected arrival day");
    require(fleets.front().activeOrderTransitDistanceKm > 0.0, "active movement exposes transit distance");
    require(fleets.front().activeOrderBurnAccelerationG > 0.0, "active movement exposes burn acceleration");
    require(!fleets.front().activeOrderBurnPhase.empty(), "active movement exposes burn phase");
    require(fleets.front().totalRouteDurationDays == fleets.front().daysRemaining, "single active order route duration matches ETA");
    require(fleets.front().queuedOrders.empty(), "fleet summary exposes an empty queue for immediate movement");
}

void test_fleet_summaries_include_queued_orders() {
    // Verifies that command panels can display future fleet intent without
    // reading raw Fleet::queuedOrders from GameState.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId terraId = service.state().bodies.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before queued fleet summary test");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;

    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "first queued move starts before queued summary test");
    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = terraId
    }).ok, "follow-up move remains queued before summary test");

    const deep::SimulationQueries queries{service};
    const auto fleet = queries.fleet(fleetId);

    require(fleet.has_value(), "existing fleet ID returns queued fleet summary");
    require(fleet->hasActiveOrder, "summary keeps current order separate from queued orders");
    require(fleet->queuedOrders.size() == 1, "summary includes one queued order");
    require(fleet->queuedOrders.front().queuePosition == 1, "queued order summary has one-based position");
    require(fleet->queuedOrders.front().orderName == "MoveToBody", "queued order summary resolves order name");
    require(fleet->queuedOrders.front().destinationBodyId == terraId, "queued order summary includes destination ID");
    require(fleet->queuedOrders.front().destinationBodyName == "Terra", "queued order summary resolves destination name");
    require(fleet->queuedOrders.front().etaDays > 0,
            "queued order summary exposes ETA for the queued move");
    require(fleet->queuedOrders.front().projectedStartDay == fleet->activeOrderProjectedArrivalDay,
            "queued order summary exposes projected start after active order arrival");
    require(fleet->queuedOrders.front().projectedArrivalDay ==
                fleet->queuedOrders.front().projectedStartDay + fleet->queuedOrders.front().etaDays,
            "queued order summary exposes projected arrival after queued move duration");
    require(fleet->queuedOrders.front().transitDistanceKm > 0.0,
            "queued order summary includes projected transit distance");
    require(fleet->queuedOrders.front().burnAccelerationG > 0.0,
            "queued order summary includes burn acceleration");
    requireNear(fleet->queuedOrders.front().fuelCost,
                fleet->queuedOrders.front().transitDistanceKm / deep::kKilometersPerMapUnit,
                "queued order summary includes fuel cost from projected origin");
    requireNear(fleet->queuedOrders.front().projectedFuelRemaining,
                fleet->currentFuel - fleet->queuedOrders.front().fuelCost,
                "queued order summary includes projected fuel after queued move");
    require(fleet->queuedOrders.front().fuelAffordable,
            "queued order summary marks affordable queued movement");
    require(fleet->totalRouteDurationDays == fleet->daysRemaining + fleet->queuedOrders.front().etaDays,
            "fleet summary exposes total route duration through the queue");

    const auto preview = queries.fleetMovePreview(fleetId, marsId);
    require(preview.has_value(), "fleet move preview is available for valid fleet and destination");
    requireNear(preview->fuelAvailable, fleet->currentFuel, "move preview uses current remaining fleet fuel");
    require(preview->transitDistanceKm > 0.0, "move preview exposes planned transit distance");
    require(preview->etaDays > 0, "move preview exposes sustained-burn ETA");
    require(preview->burnAccelerationG > 0.0, "move preview exposes burn acceleration");
    requireNear(preview->queuedFuelRequired, fleet->queuedOrders.front().fuelCost + preview->newMoveFuelCost,
                "move preview accounts for existing queued moves plus the new move");
    requireNear(preview->projectedFuelRemaining, preview->fuelAvailable - preview->queuedFuelRequired,
                "move preview exposes remaining fuel after queued route");
    require(preview->canAfford, "move preview marks affordable route as queueable");
}



void test_fleet_move_preview_matches_authoritative_transit_plan() {
    // The UI preview must be generated from the same transit planner used by
    // command execution. This catches future drift between SimulationQueries and
    // Simulation when rail projection, ETA, or fuel-cost rules change.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId terraId = service.state().bodies.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before transit-preview consistency test");
    static_cast<void>(service.advanceDays(5));

    const deep::FleetId fleetId = service.state().fleets.front().id;
    const deep::FleetOrder expectedPlan = deep::planFleetTransit(service.state(), terraId, marsId, service.state().date.day);
    const double expectedFuelCost = deep::moveFuelCost(service.state(), terraId, marsId, service.state().date.day);

    const deep::SimulationQueries queriesBeforeMove{service};
    const auto preview = queriesBeforeMove.fleetMovePreview(fleetId, marsId);
    require(preview.has_value(), "move preview exists before immediate move");
    require(preview->etaDays == expectedPlan.daysRemaining, "preview ETA comes from authoritative transit plan");
    requireNear(preview->transitDistanceKm, expectedPlan.transitDistanceKm,
                "preview distance comes from authoritative transit plan");
    requireNear(preview->newMoveFuelCost, expectedFuelCost,
                "preview fuel cost comes from authoritative transit fuel rule");

    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted after preview consistency check");

    const deep::SimulationQueries queriesAfterMove{service};
    const auto fleet = queriesAfterMove.fleet(fleetId);
    require(fleet.has_value(), "fleet summary exists after immediate move");
    require(fleet->activeOrderEtaDays.has_value(), "active move summary exposes ETA");
    require(*fleet->activeOrderEtaDays == expectedPlan.daysRemaining,
            "active move summary keeps the same ETA as the previewed plan");
    require(fleet->activeOrderProjectedArrivalDay == expectedPlan.arrivalDay,
            "active move summary keeps the same arrival day as the previewed plan");
    requireNear(fleet->activeOrderTransitDistanceKm, expectedPlan.transitDistanceKm,
                "active move summary keeps the same distance as the previewed plan");
}

void test_fleet_summaries_report_empty_timeline_for_idle_fleet() {
    // Verifies that the UI can warn about a fleet with no active or queued
    // orders without inferring state from raw Fleet records.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before idle fleet timeline test");

    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto fleet = queries.fleet(service.state().fleets.front().id);

    require(fleet.has_value(), "completed starter ship creates a fleet summary");
    requireNear(fleet->currentFuel, 1000.0, "newly completed fleet starts with full fuel");
    requireNear(fleet->fuelCapacity, 1000.0, "idle fleet summary includes fuel capacity");
    require(!fleet->hasActiveOrder, "new fleet starts idle");
    require(!fleet->activeOrderEtaDays.has_value(), "idle fleet has no active-order ETA");
    require(fleet->totalRouteDurationDays == 0, "idle fleet has zero route duration");
    require(fleet->activeOrderProjectedArrivalDay == service.state().date.day,
            "idle fleet projected arrival defaults to the current day");
    require(fleet->queuedOrders.empty(), "idle fleet has no queued-order timeline rows");
}

void test_single_record_queries_return_matching_summaries() {
    // Verifies the small lookup helpers used by command-oriented UI controls.
    // This prevents panels from rebuilding their own raw GameState lookups.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before single-record query test");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const deep::FleetId fleetId = service.state().fleets.front().id;
    const deep::BodyId terraId = service.state().bodies.front().id;

    const auto fleet = queries.fleet(fleetId);
    const auto body = queries.strategicBody(terraId);
    const auto missingFleet = queries.fleet(deep::FleetId{9999});
    const auto missingBody = queries.strategicBody(deep::BodyId{9999});

    require(fleet.has_value(), "existing fleet ID returns a fleet summary");
    require(fleet->id == fleetId, "fleet lookup preserves the requested ID");
    require(fleet->ownerInstitutionName == "Strategic Continuity Office",
            "fleet summary resolves inherited owner institution name");
    require(queries.institutionDisplayName(*fleet->ownerInstitutionId) == "Strategic Continuity Office",
            "institution display-name helper resolves known institution IDs");
    require(queries.institutionDisplayName(deep::InstitutionId{9999}) == "<unknown institution>",
            "institution display-name helper marks unknown institution IDs");
    require(body.has_value(), "existing body ID returns a strategic body summary");
    require(body->name == "Terra", "body lookup resolves Terra");
    require(!missingFleet.has_value(), "missing fleet ID returns no summary");
    require(!missingBody.has_value(), "missing body ID returns no summary");
}


void test_body_system_overview_exposes_counts() {
    // Verifies the Bodies/System panel can show body-level context without
    // scanning raw GameState bodies, colonies, deposits, or fleets in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before body overview query");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto bodies = queries.bodySystemOverview();

    require(bodies.size() == 9, "home scenario exposes mature-system body overview rows including the Sun");
    require(bodies.front().name == "Terra", "first body overview row resolves Terra");
    require(bodies.front().typeName == "Terrestrial", "body overview resolves body type name");
    require(bodies.front().strategicZoneName == "Inner Core", "body overview resolves strategic zone name");
    require(bodies.front().ownerInstitutionName == "Strategic Continuity Office",
            "body overview resolves colony owner institution where available");
    require(bodies.front().colonyCount == 1, "Terra body overview counts the colony");
    require(bodies.front().mineralDepositCount == 7, "Terra body overview counts mineral deposits");
    require(bodies.front().fleetCount == 1, "Terra body overview counts the newly completed fleet");
    require(bodies.at(1).name == "Mars", "second body overview row resolves Mars");
    require(bodies.at(1).strategicZoneName == "Military Industrial", "Mars body overview resolves military-industrial zone");
    require(bodies.at(1).ownerInstitutionName == "Naval Construction Board", "Mars body overview resolves yard owner");
    require(bodies.at(1).colonyCount == 1, "Mars body overview counts the naval yard colony");
    require(bodies.at(1).mineralDepositCount == 4, "Mars body overview counts mineral deposits");
    require(bodies.at(1).fleetCount == 0, "Mars body overview has no fleets before movement");
    require(bodies.at(7).strategicZoneName == "Deep Survey Frontier",
            "remote body overview exposes the survey-frontier zone");
    require(bodies.back().name == "Sun", "body overview includes the fixed central star");
}

void test_strategic_map_summaries_resolve_positions() {
    // Verifies that the map can draw bodies and fleets from DTOs instead of
    // reading raw GameState body/fleet vectors in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before strategic map query");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto bodies = queries.strategicBodies();
    const auto fleets = queries.strategicFleets();

    require(bodies.size() == 9, "home scenario exposes mature-system strategic body summaries including the Sun");
    require(bodies.front().name == "Terra", "strategic body summary includes body name");
    require(bodies.front().typeName == "Terrestrial", "strategic body summary includes body type name");
    require(bodies.front().strategicZoneName == "Inner Core", "strategic body summary includes zone name");
    require(bodies.front().ownerInstitutionName == "Strategic Continuity Office",
            "strategic body summary includes owner institution name where available");
    require(bodies.front().x != 0.0 || bodies.front().y != 0.0,
            "strategic body summary computes Terra's current rail position");
    require(bodies.front().parentBodyName == "Sun", "strategic body summary resolves orbital parent name");
    require(bodies.front().orbitalRadiusKm > 0.0, "strategic body summary exposes orbital radius");
    require(bodies.at(1).name == "Mars", "second strategic body summary includes Mars");
    require(bodies.at(1).orbitalPeriodDays > bodies.front().orbitalPeriodDays,
            "Mars strategic body summary exposes slower orbital rail");
    require(bodies.at(7).strategicZoneName == "Deep Survey Frontier",
            "strategic body summary includes survey-frontier body metadata");
    require(bodies.back().name == "Sun", "strategic body summary includes central star");

    require(fleets.size() == 1, "completed ship creates one strategic fleet summary");
    require(fleets.front().name.find("Survey Cutter Fleet") != std::string::npos, "strategic fleet summary includes fleet name");
    require(fleets.front().x == bodies.front().x && fleets.front().y == bodies.front().y,
            "fleet marker resolves current body rail position");
}

void test_body_deposit_queries_expose_confidence_status() {
    // Body deposit rows separate confirmed and estimated reserves so UI panels
    // can show exploration uncertainty without reaching into raw GameState.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};
    const auto bodies = queries.bodySystemOverview();

    const auto frontierBody = std::find_if(bodies.begin(), bodies.end(), [](const deep::BodySystemSummary& body) {
        return body.name == "Helios Far Survey Object";
    });
    require(frontierBody != bodies.end(), "frontier body appears in body overview");
    require(frontierBody->estimatedDepositCount == 2, "frontier body counts estimated deposits");
    require(frontierBody->unknownDepositCount == 1, "frontier body counts hidden unknown deposits");
    require(frontierBody->knownDepositCount == 0, "frontier body has no fully known deposits");
    require(frontierBody->confirmedDepositQuantity < frontierBody->estimatedDepositQuantity,
            "frontier overview separates confirmed supply from reserve estimates");

    const auto deposits = queries.bodyDeposits(frontierBody->id);
    require(deposits.size() == 3, "frontier body exposes deposit detail rows");
    require(deposits.front().surveyStateName == "Estimated", "low-confidence deposits are displayed as estimates");
    require(deposits.front().confidence > 0.0 && deposits.front().confidence < 1.0,
            "deposit detail exposes partial confidence");
    require(deposits.front().confirmedQuantity < deposits.front().estimatedQuantity,
            "deposit detail separates confirmed and estimated quantities");
    require(!deposits.front().strategicRelevance.empty(),
            "deposit detail explains survey or shortage relevance");
}

void test_exploration_intelligence_lists_survey_targets() {
    // The exploration summary is the app-layer bridge from deposit confidence to
    // strategy: it lists uncertain reserves before any future request/AI system.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};
    const deep::ExplorationIntelligenceSummary intelligence = queries.explorationIntelligence();

    require(!intelligence.lowConfidenceDeposits.empty(), "exploration intelligence lists low-confidence deposits");
    require(intelligence.lowConfidenceDeposits.front().confidence <= intelligence.lowConfidenceDeposits.back().confidence,
            "survey targets are sorted by low confidence first when relevance ties allow it");

    const auto frontierRareEarth = std::find_if(
        intelligence.lowConfidenceDeposits.begin(),
        intelligence.lowConfidenceDeposits.end(),
        [](const deep::ExplorationDepositIntelligenceRow& row) {
            return row.bodyName == "Helios Far Survey Object" && row.mineral == deep::Mineral::RareEarthElements;
        });
    require(frontierRareEarth != intelligence.lowConfidenceDeposits.end(),
            "exploration intelligence includes hidden frontier rare-earth potential");
    require(frontierRareEarth->surveyStateName == "Unknown", "hidden deposit is marked unknown in the intelligence summary");
    require(frontierRareEarth->unknownPotentialQuantity > 0.0, "unknown deposit exposes potential instead of confirmed supply");
    require(!frontierRareEarth->strategicRelevance.empty(), "survey target includes strategic relevance text");
}

void test_resource_survey_preview_and_queries_update_after_survey() {
    // Verifies the Fleet Orders panel can preview survey eligibility through the
    // app layer and that body deposit query rows reflect the accepted command.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId frontierId = bodyIdByName(state, "Helios Far Survey Object");
    const deep::FleetId fleetId = addTestFleetAt(state, frontierId);
    deep::SimulationService service{std::move(state)};
    deep::SimulationQueries queries{service};

    const std::optional<deep::ResourceSurveyPreview> before = queries.resourceSurveyPreview(fleetId, frontierId);
    require(before.has_value(), "survey preview exists for valid fleet/body IDs");
    require(before->canSurvey, "survey preview allows fleet at low-confidence body");
    require(before->surveyableDepositCount == 3, "survey preview counts all non-known deposits");
    require(before->projectedAverageConfidenceAfter > before->averageConfidenceBefore,
            "survey preview explains the projected confidence gain");

    require(service.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = frontierId
    }).ok, "resource survey command is accepted through service");

    const auto deposits = queries.bodyDeposits(frontierId);
    const auto hiddenDeposit = std::find_if(deposits.begin(), deposits.end(), [](const deep::BodyDepositSummary& deposit) {
        return deposit.mineral == deep::Mineral::RareEarthElements;
    });
    require(hiddenDeposit != deposits.end(), "surveyed body still exposes rare-earth deposit row");
    require(hiddenDeposit->surveyStateName == "Estimated", "formerly hidden deposit becomes an estimate after survey");
    require(hiddenDeposit->confidence >= deep::kResourceSurveyMinimumRevealedConfidence,
            "surveyed hidden deposit receives visible confidence");

    const auto overview = queries.bodySystemOverview();
    const auto body = std::find_if(overview.begin(), overview.end(), [frontierId](const deep::BodySystemSummary& row) {
        return row.id == frontierId;
    });
    require(body != overview.end(), "surveyed frontier body remains in body overview");
    require(body->unknownDepositCount == 0, "survey clears unknown deposit count for the target body");
    require(body->estimatedDepositCount == 3, "surveyed deposits remain visible estimates until fully known");

    const deep::ExplorationIntelligenceSummary intelligence = queries.explorationIntelligence();
    require(!intelligence.recentSurveyResults.empty(), "exploration intelligence reports recent survey results");
    require(intelligence.recentSurveyResults.front().bodyName == "Helios Far Survey Object",
            "recent survey result resolves body name");
    require(intelligence.recentSurveyResults.front().depositsImproved == 3,
            "recent survey result reports changed deposit count");
}

void test_sustained_burn_route_visualization_is_shallow_projected_intercept() {
    // Verifies the strategic map receives a direct sustained-burn route preview:
    // it targets the destination's projected arrival position, keeps the curve
    // shallow, and bends away from the Sun instead of reading as a transfer orbit.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before route-visualization query");
    static_cast<void>(service.advanceDays(5));

    const deep::FleetId fleetId = service.state().fleets.front().id;
    const deep::SimulationQueries previewQueries{service};
    const auto preview = previewQueries.fleetMovePreview(fleetId, marsId);
    require(preview.has_value(), "move preview exists before route-visualization test");
    require(preview->routeVisualStyle == deep::RouteVisualStyle::SustainedBurn,
            "move preview exposes sustained-burn route style");
    require(preview->routeVisualStyleName == "Sustained burn",
            "move preview exposes display name for route style");

    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before strategic route query");

    const deep::SimulationQueries movingQueries{service};
    const auto fleets = movingQueries.strategicFleets();
    require(!fleets.empty(), "moving strategic fleet summary exists");
    const deep::StrategicFleetSummary& fleet = fleets.front();
    require(fleet.moving, "strategic fleet summary marks active movement");
    require(fleet.routeVisualStyle == deep::RouteVisualStyle::SustainedBurn,
            "strategic fleet route style remains sustained burn");
    require(fleet.routeVisualStyleName == "Sustained burn",
            "strategic fleet route style has UI display text");

    const double currentDestinationSeparation = std::hypot(
        fleet.projectedArrivalX - fleet.destinationX,
        fleet.projectedArrivalY - fleet.destinationY);
    require(currentDestinationSeparation > 1.0e-6,
            "route endpoint uses projected arrival position instead of current body position");

    const double chordX = fleet.projectedArrivalX - fleet.departureX;
    const double chordY = fleet.projectedArrivalY - fleet.departureY;
    const double routeLength = std::hypot(chordX, chordY);
    const double midpointX = (fleet.departureX + fleet.projectedArrivalX) * 0.5;
    const double midpointY = (fleet.departureY + fleet.projectedArrivalY) * 0.5;
    const double offset = std::hypot(fleet.controlX - midpointX, fleet.controlY - midpointY);
    const double maximumOffset = std::min(routeLength * deep::kSustainedBurnRouteCurveFraction,
                                          deep::kSustainedBurnRouteCurveMaxMapUnits);
    require(offset <= maximumOffset + 1.0e-6,
            "sustained-burn route control point is shallow and capped");

    const double midpointSunDistance = std::hypot(midpointX, midpointY);
    const double controlSunDistance = std::hypot(fleet.controlX, fleet.controlY);
    require(controlSunDistance + 1.0e-6 >= midpointSunDistance,
            "sustained-burn route curve bows away from the Sun when possible");
}

void test_recent_events_returns_limited_chronological_tail() {
    // Verifies that recentEvents(limit) returns the newest audit entries but
    // preserves log order inside that returned window. Routine mining telemetry
    // must not be required for event-log query coverage.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before recent event query");
    static_cast<void>(service.advanceDays(5));
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = service.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before recent event query");

    const deep::SimulationQueries queries{service};
    const auto allEvents = queries.recentEvents(100);
    const auto recentTwo = queries.recentEvents(2);
    const auto none = queries.recentEvents(0);

    require(allEvents.size() == 3, "setup creates three queryable audit event summaries");
    require(recentTwo.size() == 2, "recentEvents applies the requested limit");
    require(none.empty(), "recentEvents with zero limit is empty");
    require(recentTwo.front().id == allEvents.at(allEvents.size() - 2).id,
            "recentEvents returns the chronological tail window");
    require(recentTwo.front().id.value < recentTwo.back().id.value,
            "recentEvents preserves chronological order within the tail");
    require(recentTwo.back().eventType == "fleet_order_assigned", "event summary exposes flattened event type");
    require(!recentTwo.back().message.empty(), "event summary exposes display message text");
}

} // namespace

int main() {
    try {
        test_colony_summaries_resolve_body_context();
        test_colony_summaries_include_processing_policy();
        test_shipyard_order_summaries_resolve_names();
        test_production_backlog_summaries_expose_queue_eta();
        test_personnel_summaries_resolve_institution_context();
        test_appointment_summaries_resolve_people_and_scopes();
        test_appointment_candidates_rank_matching_competency_first();
        test_appointment_candidate_service_risks_lower_score();
        test_appointment_candidate_owner_match_breakdown_visible();
        test_appointment_candidate_breakdown_sums_to_total();
        test_appointment_candidate_tie_ordering_is_deterministic();
        test_appointment_operational_effects_are_visible_and_deterministic();
        test_weak_appointee_can_apply_small_negative_modifier();
        test_ship_class_summaries_expose_build_targets();
        test_fleet_summaries_resolve_location_and_order();
        test_fleet_summaries_include_queued_orders();
        test_fleet_move_preview_matches_authoritative_transit_plan();
        test_fleet_summaries_report_empty_timeline_for_idle_fleet();
        test_single_record_queries_return_matching_summaries();
        test_body_system_overview_exposes_counts();
        test_strategic_map_summaries_resolve_positions();
        test_sustained_burn_route_visualization_is_shallow_projected_intercept();
        test_body_deposit_queries_expose_confidence_status();
        test_exploration_intelligence_lists_survey_targets();
        test_resource_survey_preview_and_queries_update_after_survey();
        test_recent_events_returns_limited_chronological_tail();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal app query tests passed.\n";
    return EXIT_SUCCESS;
}
