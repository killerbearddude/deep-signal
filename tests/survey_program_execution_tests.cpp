#include "sim/EquipmentServiceRules.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/SurveyProgramExecution.h"
#include "sim/SurveyProgramRules.h"
#include "sim/TransitPlanning.h"

// Exercises the P3A runner through authoritative commands and daily ticks in
// the dedicated fixed-coordinate scenario. Each assertion checks durable
// physical state or a dated receipt rather than mirroring planner arithmetic.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void near(const double actual, const double expected, const std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-6) {
        throw std::runtime_error{std::string{message}};
    }
}

deep::PersonId personByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Person& person : state.people) {
        if (person.name == name) return person.id;
    }
    throw std::runtime_error{"fixture leader is missing"};
}

deep::SurveyProgramCharter charterFor(const deep::GameState& state,
                                      const std::string_view leaderName,
                                      const int passesPerTarget = 2) {
    // Fixture target rows are authored in public priority order, and both
    // leader variants use the same equipment, skills, policy, and geography.
    deep::SurveyProgramCharter charter;
    charter.name = "Delegated fixed-route survey";
    charter.homeColonyId = state.colonies.back().id;
    charter.requestedFleetId = state.fleets.back().id;
    charter.requestedLeaderId = personByName(state, leaderName);
    charter.requestedTeamId = state.surveyTeams.front().id;
    for (int i = 0; i < 3; ++i) {
        charter.targets.push_back(deep::SurveyProgramTarget{
            .bodyId = state.bodies.at(state.bodies.size() - 3U + static_cast<std::size_t>(i)).id,
            .priority = 3 - i,
            .requestedPasses = passesPerTarget
        });
    }
    return charter;
}

deep::SurveyProgramId authorize(deep::Simulation& sim, deep::SurveyProgramCharter charter) {
    const deep::SurveyProgramId id{sim.state().ids.nextSurveyProgramId};
    require(sim.execute(deep::CreateSurveyProgramCommand{.charter = std::move(charter)}).ok,
            "well-formed survey charter is authorized");
    require(sim.state().surveyPrograms.back().id == id, "authorization allocates one stable program ID");
    return id;
}

const deep::SurveyProgram& programById(const deep::GameState& state, const deep::SurveyProgramId id) {
    for (const deep::SurveyProgram& program : state.surveyPrograms) {
        if (program.id == id) return program;
    }
    throw std::runtime_error{"authorized program disappeared"};
}

std::vector<std::string> selectedReasons(const deep::GameState& state) {
    std::vector<std::string> reasons;
    for (const deep::SimEvent& event : state.eventLog) {
        if (const auto* audit = std::get_if<deep::SurveyProgramAuditEvent>(&event.payload);
            audit != nullptr && audit->kind == deep::SurveyProgramAuditKind::TaskSelected) {
            reasons.push_back(audit->detail);
        }
    }
    return reasons;
}

int issueRaisedCount(const deep::GameState& state) {
    int count = 0;
    for (const deep::SimEvent& event : state.eventLog) {
        if (const auto* audit = std::get_if<deep::SurveyProgramAuditEvent>(&event.payload);
            audit != nullptr && audit->kind == deep::SurveyProgramAuditKind::IssueRaised) ++count;
    }
    return count;
}

void test_automatic_visits_reporting_and_fuel_conservation() {
    // Six passes should occur without dispatch commands. Home stock-to-ship
    // loading is a transfer; only program departure fuel leaves the economy.
    deep::GameState state = deep::createDelegatedSurveyScenario();
    const double initialHome = state.colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant);
    const double initialShip = state.ships.back().fuel;
    deep::Simulation sim{std::move(state)};
    const deep::SurveyProgramId id = authorize(sim, charterFor(sim.state(), "Dr. Nia Okafor"));

    sim.advanceDays(90);
    const deep::SurveyProgram& program = programById(sim.state(), id);
    require(sim.state().date.day == 90, "ample-resource advance reaches the requested 90-day boundary");
    require(program.receipts.size() == 6U, "all six timed target passes completed automatically");
    require(program.totalWorkDays == 30, "each of six passes uses five qualifying workdays");
    require(program.lifecycle == deep::SurveyProgramLifecycle::Closed &&
                program.closure == deep::SurveyProgramClosure::Completed,
            "normal completion includes a real return to home support");
    require(sim.state().fleets.back().currentBodyId == sim.state().colonies.back().bodyId,
            "fleet ends at the actual home body");
    require(sim.state().surveyTeams.front().locationKind == deep::SurveyTeamLocationKind::Colony &&
                sim.state().surveyTeams.front().colonyId == sim.state().colonies.back().id,
            "team disembarks at its actual home colony after completion");
    require(program.fuelLoaded > 0.0 && program.fuelBurned > 0.0,
            "the program records real transfers and departure burns separately");
    near(initialHome + initialShip,
         sim.state().colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant) +
             sim.state().ships.back().fuel + program.fuelBurned,
         "home and ship propellant is conserved except for program travel burn");
    require(program.reports.size() == 3U && program.reports.at(0).endDay == 30 &&
                program.reports.at(1).endDay == 60 && program.reports.at(2).endDay == 90 &&
                program.reports.at(2).isNinetyDayReview,
            "durable reports occur on global 30/60/90-day boundaries, including after closure");
}

std::vector<deep::BodyId> completedOrder(const std::string_view leaderName) {
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    const deep::SurveyProgramId id = authorize(sim, charterFor(sim.state(), leaderName));
    sim.advanceDays(90);
    const deep::SurveyProgram& program = programById(sim.state(), id);
    require(program.receipts.size() == 6U, "leader-order fixture completes the same six passes");
    std::vector<deep::BodyId> order;
    for (const deep::SurveyVisitReceipt& receipt : program.receipts) order.push_back(receipt.bodyId);
    return order;
}

void test_equal_skill_leaders_choose_distinct_reproducible_orders() {
    // The only planning difference is the persisted approach, so pass order
    // proves the director matters without inventing a travel or sensor bonus.
    const deep::GameState fixture = deep::createDelegatedSurveyScenario();
    const deep::BodyId a = fixture.bodies.at(fixture.bodies.size() - 3U).id;
    const deep::BodyId b = fixture.bodies.at(fixture.bodies.size() - 2U).id;
    const deep::BodyId c = fixture.bodies.back().id;
    require(completedOrder("Dr. Nia Okafor") == std::vector<deep::BodyId>{a, b, c, a, b, c},
            "CoverageFirst distributes first passes before repeating targets");
    require(completedOrder("Dr. Tala Imani") == std::vector<deep::BodyId>{a, a, b, b, c, c},
            "PriorityFirst completes high-priority quotas before moving on");
}

void test_empty_home_stock_waits_and_recovers_with_same_program_id() {
    // A real shortage waits instead of rejecting the charter. Restoring stock
    // in a detached same-world snapshot lets the same durable intention run.
    deep::GameState state = deep::createDelegatedSurveyScenario();
    state.colonies.back().processedStockpile.set(deep::ProcessedMaterial::Propellant, 0.0);
    deep::Simulation waiting{std::move(state)};
    auto charter = charterFor(waiting.state(), "Dr. Nia Okafor", 1);
    charter.targets.resize(1);
    const deep::SurveyProgramId id = authorize(waiting, std::move(charter));
    waiting.advanceDays(10);
    const deep::SurveyProgram& paused = programById(waiting.state(), id);
    require(paused.lifecycle == deep::SurveyProgramLifecycle::Authorized && paused.receipts.empty() &&
                paused.fuelLoaded == 0.0 && paused.fuelBurned == 0.0,
            "zero home stock retains intent without phantom work, fuel, or departure");
    require(deep::surveyProgramExecutionCondition(waiting.state(), paused).find("stock") != std::string::npos,
            "query condition names the real home stock limit");

    deep::GameState replenished = waiting.state();
    replenished.colonies.back().processedStockpile.set(deep::ProcessedMaterial::Propellant, 20.0);
    deep::Simulation resumed{std::move(replenished)};
    resumed.advanceDays(30);
    const deep::SurveyProgram& completed = programById(resumed.state(), id);
    require(completed.receipts.size() == 1U && completed.id == id,
            "later real supply starts the original program without resubmission");
}

void test_arrival_boundary_and_mid_transit_suspension() {
    // Arrival occurs after the program opening phase. Suspending an outbound
    // leg keeps its stored transit and prevents survey work until explicit resume.
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
    charter.targets.resize(1);
    const deep::SurveyProgramId id = authorize(sim, std::move(charter));
    sim.advanceDays(1);
    require(sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "a home fuel transfer cannot also launch on the same day");
    sim.advanceDays(1);
    const int eta = sim.state().fleets.back().activeOrder.daysRemaining;
    require(eta > 0, "the next opening phase starts a real outbound transit");
    require(sim.execute(deep::SuspendSurveyProgramCommand{.programId = id}).ok,
            "mid-transit suspension is accepted");
    sim.advanceDays(eta);
    const deep::SurveyProgram& suspended = programById(sim.state(), id);
    require(sim.state().fleets.back().currentBodyId == suspended.taskBodyId &&
                sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
            "the committed outbound leg physically arrives despite suspension");
    require(!suspended.leasedFleetId.has_value() && suspended.workDaysCompleted == 0 &&
                suspended.receipts.empty(),
            "arrival releases control without doing survey work on arrival day");
    sim.advanceDays(3);
    require(programById(sim.state(), id).workDaysCompleted == 0,
            "suspended days do not advance a visit");
    require(sim.execute(deep::ResumeSurveyProgramCommand{.programId = id}).ok,
            "the preserved visit can be resumed");
    sim.advanceDays(1);
    require(programById(sim.state(), id).workDaysCompleted == 1,
            "the first qualifying workday follows actual arrival and explicit resume");
    sim.advanceDays(15);
    require(programById(sim.state(), id).receipts.size() == 1U,
            "resumed visit produces exactly one completed receipt");
}

deep::GameState twoHullLongSortieFixture() {
    // The long public route needs slightly more than the first hull's fuel.
    // This makes a partial refill cross the roster boundary without relying on
    // hidden deposits or changing the production movement implementation.
    deep::GameState state = deep::createDelegatedSurveyScenario();
    state.bodies.at(state.bodies.size() - 3U).x = 501.0;
    state.ships.back().fuel = 999.5;
    const deep::ShipId secondId{state.ids.nextShipId++};
    state.ships.push_back(deep::Ship{
        .id = secondId,
        .shipClassId = state.shipClasses.front().id,
        .name = "Second survey hull",
        .fleetId = state.fleets.back().id,
        .fuel = 0.0
    });
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.fleets.back().shipIds.push_back(secondId);
    return state;
}

void test_partial_refill_respects_roster_budget_and_stock_floor() {
    // Both policy limits can bound a real transfer. The first hull fills before
    // the second receives any fuel, and the colony loses exactly what ships gain.
    {
        deep::Simulation sim{twoHullLongSortieFixture()};
        auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
        charter.targets.resize(1);
        charter.policy.maxAdditionalPropellant = 1.25;
        const deep::SurveyProgramId id = authorize(sim, std::move(charter));
        const double sourceBefore = sim.state().colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant);
        sim.advanceDays(1);
        const deep::SurveyProgram& program = programById(sim.state(), id);
        near(program.fuelLoaded, 1.25, "finite authorization caps a partial home transfer");
        near(sim.state().ships.at(sim.state().ships.size() - 2U).fuel, 1000.0,
             "first roster hull fills to its derived tank capacity");
        near(sim.state().ships.back().fuel, 0.75,
             "second roster hull receives only the remaining authorized transfer");
        near(sourceBefore - sim.state().colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant),
             program.fuelLoaded, "home source debit equals hull credits");
        require(sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None,
                "a partial refill does not also start a departure");
    }
    {
        deep::Simulation sim{twoHullLongSortieFixture()};
        auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
        charter.targets.resize(1);
        const double sourceBefore = sim.state().colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant);
        charter.policy.homeStockFloor = sourceBefore - 1.5;
        const deep::SurveyProgramId id = authorize(sim, std::move(charter));
        sim.advanceDays(1);
        near(programById(sim.state(), id).fuelLoaded, 1.5,
             "home stock floor caps a partial transfer even without an authorization cap");
        near(sim.state().ships.at(sim.state().ships.size() - 2U).fuel, 1000.0,
             "stock-limited transfer fills first roster hull");
        near(sim.state().ships.back().fuel, 1.0,
             "stock-limited remainder reaches second roster hull");
        near(sim.state().colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant),
             sourceBefore - 1.5, "protected colony floor is retained exactly");
    }
}

void test_fuel_authorization_issue_stops_and_acknowledges_once() {
    // A first sortie consumes the authorized transfer; a later requested pass
    // then needs a genuinely new refill. The bulk runner stops on that day and
    // accepts keep-waiting without inventing fuel or prompting every day.
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
    charter.targets.resize(2);
    charter.policy.maxAdditionalPropellant = 2.0;
    const deep::SurveyProgramId id = authorize(sim, std::move(charter));

    const deep::AdvanceResult first = sim.advanceDaysDetailed(30);
    const deep::SurveyProgram& blocked = programById(sim.state(), id);
    require(first.requestedDays == 30 && first.advancedDays == 10 && first.interrupted &&
                first.issueProgramId == deep::ProgramController{id} && sim.state().date.day == 10,
            "bulk advance stops on the first return day where a new refill needs more authorization");
    require(!blocked.issue.signature.empty() && !blocked.issue.acknowledged &&
                blocked.issue.message.find("authorization") != std::string::npos,
            "the issued decision identifies the exhausted authorization");
    const std::string signature = blocked.issue.signature;
    const int raised = issueRaisedCount(sim.state());
    require(raised == 1, "one consequential issue is raised for the stable cause");
    require(sim.execute(deep::AcknowledgeSurveyProgramIssueCommand{.programId = id, .signature = signature}).ok,
            "player can keep waiting under the same binding fuel policy");

    const deep::AdvanceResult continued = sim.advanceDaysDetailed(5);
    const deep::SurveyProgram& stillWaiting = programById(sim.state(), id);
    require(continued.advancedDays == 5 && !continued.interrupted && sim.state().date.day == 15,
            "acknowledged unchanged issue no longer traps bulk time advancement");
    require(stillWaiting.issue.signature == signature && stillWaiting.issue.acknowledged &&
                issueRaisedCount(sim.state()) == raised,
            "same cause keeps its acknowledgment and produces no daily duplicate issue");
    near(stillWaiting.fuelLoaded, 2.0, "acknowledgment does not relax the fuel cap");
    require(stillWaiting.receipts.size() == 1U,
            "completed first visit remains durable while the second waits for authorization");
}

void test_known_small_cap_at_creation_waits_without_surprise_prompt() {
    // The player authorized this insufficient cap in the original charter.
    // A partial real transfer may occur, but its already-known limitation must
    // not be raised as a fresh day-one decision request.
    deep::Simulation sim{twoHullLongSortieFixture()};
    auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
    charter.targets.resize(1);
    charter.policy.maxAdditionalPropellant = 1.25;
    const deep::SurveyProgramId id = authorize(sim, std::move(charter));
    const deep::AdvanceResult result = sim.advanceDaysDetailed(5);
    const deep::SurveyProgram& program = programById(sim.state(), id);
    require(result.advancedDays == 5 && !result.interrupted && sim.state().date.day == 5,
            "accepted initial fuel shortage permits the world clock to continue");
    near(program.fuelLoaded, 1.25, "known cap still bounds the actual transfer");
    require(!program.issue.signature.empty() && program.issue.acknowledged &&
                issueRaisedCount(sim.state()) == 0,
            "known authorization shortfall retains acknowledgment without a surprise issue event");
}

void test_amended_known_cap_waits_but_new_return_cause_interrupts() {
    // Tightening the cap after a first sortie is an informed player decision.
    // A later independent loss of return fuel is a different physical problem
    // and must be allowed to interrupt despite the old acknowledged signature.
    deep::Simulation sim{deep::createDelegatedSurveyScenario()};
    auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
    charter.targets.resize(2);
    const deep::SurveyProgramId id = authorize(sim, std::move(charter));
    const deep::BodyId homeBodyId = sim.state().colonies.back().bodyId;
    for (int day = 0; day < 20; ++day) {
        const auto& current = programById(sim.state(), id);
        if (current.receipts.size() == 1U && current.task == deep::SurveyProgramTask::None &&
            sim.state().fleets.back().currentBodyId == homeBodyId) break;
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "first sortie advances to its home return");
    }
    const deep::SurveyProgram& afterFirst = programById(sim.state(), id);
    require(afterFirst.receipts.size() == 1U && afterFirst.task == deep::SurveyProgramTask::None,
            "first visit and return finish before amending the fuel commitment");
    auto reduced = afterFirst.charter;
    reduced.policy.maxAdditionalPropellant = afterFirst.fuelLoaded;
    require(sim.execute(deep::AmendSurveyProgramCommand{.programId = id, .charter = std::move(reduced)}).ok,
            "post-sortie cap amendment is recorded");
    const std::string acceptedSignature = programById(sim.state(), id).issue.signature;
    require(!acceptedSignature.empty() && programById(sim.state(), id).issue.acknowledged,
            "amendment records its currently known shortfall as acknowledged");
    const deep::AdvanceResult waiting = sim.advanceDaysDetailed(3);
    require(waiting.advancedDays == 3 && !waiting.interrupted &&
                programById(sim.state(), id).issue.signature == acceptedSignature,
            "accepted amended cap does not re-prompt on the next day");

    auto restored = programById(sim.state(), id).charter;
    restored.policy.maxAdditionalPropellant = programById(sim.state(), id).fuelLoaded + 100.0;
    require(sim.execute(deep::AmendSurveyProgramCommand{.programId = id, .charter = std::move(restored)}).ok,
            "later amendment allows the second sortie to proceed");
    for (int day = 0; day < 5 && sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::None; ++day) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "second sortie reaches outbound departure");
    }
    require(sim.state().fleets.back().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "second sortie has committed real outbound travel");
    deep::GameState changed = sim.state();
    changed.ships.back().fuel = 0.0;
    deep::Simulation withNewReturnShortage{std::move(changed)};
    const deep::AdvanceResult newIssue = withNewReturnShortage.advanceDaysDetailed(20);
    const deep::SurveyProgram& blocked = programById(withNewReturnShortage.state(), id);
    require(newIssue.interrupted && newIssue.advancedDays < 20 &&
                blocked.issue.signature.find("return:") == 0 && !blocked.issue.acknowledged,
            "a materially new return-fuel cause interrupts after its actual daily boundary");
}

void test_mid_visit_asset_amendment_switches_after_old_return() {
    // An amended fleet/team request is future intent. The active visit and
    // return stay with their original physical participants; a later sortie
    // acquires the new pair only after the old lease is safely released.
    deep::GameState state = deep::createDelegatedSurveyScenario();
    const deep::FleetId firstFleetId = state.fleets.back().id;
    const deep::SurveyTeamId firstTeamId = state.surveyTeams.front().id;
    const deep::FleetId secondFleetId{state.ids.nextFleetId++};
    const deep::ShipId secondShipId{state.ids.nextShipId++};
    const deep::SurveyTeamId secondTeamId{state.ids.nextSurveyTeamId++};
    const deep::BodyId homeBodyId = state.colonies.back().bodyId;
    const deep::ColonyId homeColonyId = state.colonies.back().id;
    state.ships.push_back(deep::Ship{
        .id = secondShipId, .shipClassId = state.shipClasses.front().id,
        .name = "Replacement survey cutter", .fleetId = secondFleetId, .fuel = 0.0
    });
    deep::initializeShipEquipmentCondition(state, state.ships.back());
    state.fleets.push_back(deep::Fleet{
        .id = secondFleetId, .name = "Replacement survey fleet",
        .currentBodyId = homeBodyId, .destinationBodyId = std::nullopt,
        .shipIds = {secondShipId}, .activeOrder = {}, .queuedOrders = {},
        .ownerInstitutionId = state.institutions.front().id
    });
    state.surveyTeams.push_back(deep::SurveyTeam{
        .id = secondTeamId, .name = "Replacement survey team",
        .locationKind = deep::SurveyTeamLocationKind::Colony,
        .colonyId = homeColonyId, .fleetId = std::nullopt
    });
    deep::Simulation sim{std::move(state)};
    auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
    charter.requestedFleetId = firstFleetId;
    charter.requestedTeamId = firstTeamId;
    charter.targets.resize(2);
    const deep::SurveyProgramId id = authorize(sim, std::move(charter));
    for (int day = 0; day < 12 && programById(sim.state(), id).workDaysCompleted == 0; ++day) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "first visit reaches partial fieldwork");
    }
    require(programById(sim.state(), id).workDaysCompleted == 1,
            "old fleet/team have begun the committed visit");
    auto amended = programById(sim.state(), id).charter;
    amended.requestedFleetId = secondFleetId;
    amended.requestedTeamId = secondTeamId;
    require(sim.execute(deep::AmendSurveyProgramCommand{.programId = id, .charter = std::move(amended)}).ok,
            "new fleet/team request is recorded during active fieldwork");
    require(programById(sim.state(), id).leasedFleetId == firstFleetId,
            "current visit retains the old lease immediately after amendment");
    sim.advanceDays(60);
    const auto& receipts = programById(sim.state(), id).receipts;
    require(receipts.size() == 2U && receipts.at(0).fleetId == firstFleetId &&
                receipts.at(0).teamId == firstTeamId && receipts.at(1).fleetId == secondFleetId &&
                receipts.at(1).teamId == secondTeamId,
            "old visit returns before the new fleet/team performs the next visit");
}

void test_hidden_deposits_do_not_change_target_decisions() {
    // These worlds have identical charter, geometry, fleet, people, and stock.
    // Removing one hidden deposit changes only the encountered result, never
    // pre-interaction advice or the known-data target decision sequence.
    deep::GameState withDeposit = deep::createDelegatedSurveyScenario();
    deep::GameState withoutDeposit = withDeposit;
    const deep::BodyId firstTarget = withDeposit.bodies.at(withDeposit.bodies.size() - 3U).id;
    withoutDeposit.mineralDeposits.erase(
        std::remove_if(withoutDeposit.mineralDeposits.begin(), withoutDeposit.mineralDeposits.end(),
                       [firstTarget](const deep::MineralDeposit& row) { return row.bodyId == firstTarget; }),
        withoutDeposit.mineralDeposits.end());
    deep::Simulation a{std::move(withDeposit)};
    deep::Simulation b{std::move(withoutDeposit)};
    auto charterA = charterFor(a.state(), "Dr. Nia Okafor", 1);
    auto charterB = charterFor(b.state(), "Dr. Nia Okafor", 1);
    const deep::SurveyProgramId idA = authorize(a, std::move(charterA));
    const deep::SurveyProgramId idB = authorize(b, std::move(charterB));
    require(deep::surveyProgramExecutionCondition(a.state(), programById(a.state(), idA)) ==
                deep::surveyProgramExecutionCondition(b.state(), programById(b.state(), idB)),
            "pre-interaction execution advice cannot reveal hidden deposit presence");

    a.advanceDays(90);
    b.advanceDays(90);
    const auto& receiptsA = programById(a.state(), idA).receipts;
    const auto& receiptsB = programById(b.state(), idB).receipts;
    require(receiptsA.size() == 3U && receiptsB.size() == 3U,
            "both public-equivalent worlds finish every requested pass");
    require(selectedReasons(a.state()) == selectedReasons(b.state()),
            "leader selection reasons and target order ignore hidden geology");
    for (std::size_t i = 0; i < receiptsA.size(); ++i) {
        require(receiptsA[i].bodyId == receiptsB[i].bodyId &&
                    receiptsA[i].passNumber == receiptsB[i].passNumber,
                "visit identities match across hidden-geology variants");
    }
    require(a.state().observations.front().instruments.front().channels != b.state().observations.front().instruments.front().channels,
            "only the encountered survey result differs when a hidden deposit is removed");
}

void test_barren_and_already_known_visits_have_zero_information_receipts() {
    // A completed visit records real timed work even when there is no deposit
    // row or no low-confidence row to improve. The result makes no barren claim.
    {
        deep::Simulation sim{deep::createDelegatedSurveyScenario()};
        auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
        const deep::SurveyProgramTarget barren = charter.targets.back();
        charter.targets = {barren};
        const deep::SurveyProgramId id = authorize(sim, std::move(charter));
        sim.advanceDays(30);
        const auto& receipts = programById(sim.state(), id).receipts;
        require(receipts.size() == 1U && receipts.front().bodyId == barren.bodyId &&
                    bool(receipts.front().observationBatchId) && receipts.front().workDays == 5,
                "barren target completes one five-day zero-information visit");
    }
    {
        deep::GameState state = deep::createDelegatedSurveyScenario();
        const deep::BodyId firstTarget = state.bodies.at(state.bodies.size() - 3U).id;
        for (deep::MineralDeposit& row : state.mineralDeposits) {
            if (row.bodyId == firstTarget) row.remaining = 0.0;
        }
        deep::Simulation sim{std::move(state)};
        auto charter = charterFor(sim.state(), "Dr. Nia Okafor", 1);
        charter.targets.resize(1);
        const deep::SurveyProgramId id = authorize(sim, std::move(charter));
        sim.advanceDays(30);
        const auto& receipts = programById(sim.state(), id).receipts;
        require(receipts.size() == 1U && bool(receipts.front().observationBatchId) &&
                    receipts.front().workDays == 5,
                "already-known target completes one timed zero-information visit");
    }
}

void compareDurableProgramState(const deep::GameState& bulk, const deep::GameState& daily,
                                const deep::SurveyProgramId id) {
    require(bulk.date.day == daily.date.day && bulk.ids.nextEventId == daily.ids.nextEventId &&
                bulk.ids.nextSurveyProgramId == daily.ids.nextSurveyProgramId,
            "bulk and daily advancement retain the same date and ID cursors");
    const deep::SurveyProgram& a = programById(bulk, id);
    const deep::SurveyProgram& b = programById(daily, id);
    require(a.lifecycle == b.lifecycle && a.closure == b.closure && a.task == b.task &&
                a.leasedFleetId == b.leasedFleetId && a.leasedTeamId == b.leasedTeamId &&
                a.receipts.size() == b.receipts.size() && a.reports.size() == b.reports.size() &&
                a.totalWorkDays == b.totalWorkDays && a.issue.signature == b.issue.signature,
            "bulk and daily advancement produce equivalent durable program lifecycle and work");
    near(a.fuelLoaded, b.fuelLoaded, "bulk and daily fuel transfers match");
    near(a.fuelBurned, b.fuelBurned, "bulk and daily travel burns match");
    for (std::size_t i = 0; i < a.receipts.size(); ++i) {
        const auto& left = a.receipts[i];
        const auto& right = b.receipts[i];
        require(left.bodyId == right.bodyId && left.passNumber == right.passNumber &&
                    left.firstWorkDay == right.firstWorkDay && left.completedDay == right.completedDay &&
                    left.observationBatchId == right.observationBatchId,
                "bulk and daily visit receipts retain the same identity and timing");
    }
    for (std::size_t i = 0; i < a.reports.size(); ++i) {
        const auto& left = a.reports[i];
        const auto& right = b.reports[i];
        require(left.startDay == right.startDay && left.endDay == right.endDay &&
                    left.isNinetyDayReview == right.isNinetyDayReview &&
                    left.visitsCompleted == right.visitsCompleted && left.workDays == right.workDays &&
                    left.waitingReason == right.waitingReason,
                "bulk and daily report snapshots retain the same intervals and work");
        near(left.fuelLoaded, right.fuelLoaded, "bulk and daily report transfer totals match");
        near(left.fuelBurned, right.fuelBurned, "bulk and daily report burn totals match");
    }
    require(bulk.fleets.back().currentBodyId == daily.fleets.back().currentBodyId &&
                bulk.fleets.back().activeOrder.type == daily.fleets.back().activeOrder.type &&
                bulk.surveyTeams.front().locationKind == daily.surveyTeams.front().locationKind &&
                bulk.surveyTeams.front().colonyId == daily.surveyTeams.front().colonyId,
            "bulk and daily physical fleet/team locations match");
    near(bulk.ships.back().fuel, daily.ships.back().fuel, "bulk and daily hull fuel matches");
    near(bulk.colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant),
         daily.colonies.back().processedStockpile.get(deep::ProcessedMaterial::Propellant),
         "bulk and daily home stock matches");
    require(bulk.eventLog.size() == daily.eventLog.size(), "bulk and daily audit lengths match");
    for (std::size_t i = 0; i < bulk.eventLog.size(); ++i) {
        const auto& left = bulk.eventLog[i];
        const auto& right = daily.eventLog[i];
        require(left.id == right.id && left.day == right.day &&
                    left.payload.index() == right.payload.index(),
                "bulk and daily event ID/day/type order matches");
        if (const auto* leftAudit = std::get_if<deep::SurveyProgramAuditEvent>(&left.payload)) {
            const auto* rightAudit = std::get_if<deep::SurveyProgramAuditEvent>(&right.payload);
            require(rightAudit != nullptr && leftAudit->kind == rightAudit->kind &&
                        leftAudit->detail == rightAudit->detail && leftAudit->bodyId == rightAudit->bodyId,
                    "bulk and daily program audit decisions match");
        }
    }
}

void test_bulk_and_daily_advancement_have_same_durable_result() {
    // Both public time paths invoke the same bounded daily phase sequence.
    // Comparing receipts, reports, inventories, and audit order catches a bulk
    // shortcut that silently bypasses decisions or repeats a completion.
    const deep::GameState fixture = deep::createDelegatedSurveyScenario();
    deep::Simulation bulk{fixture};
    deep::Simulation daily{fixture};
    const deep::SurveyProgramId id = authorize(bulk, charterFor(bulk.state(), "Dr. Nia Okafor"));
    authorize(daily, charterFor(daily.state(), "Dr. Nia Okafor"));
    const deep::AdvanceResult allAtOnce = bulk.advanceDaysDetailed(90);
    require(allAtOnce.advancedDays == 90 && !allAtOnce.interrupted,
            "ready bulk program reaches the 90-day boundary without mandatory routine prompts");
    for (int day = 0; day < 90; ++day) {
        const deep::AdvanceResult one = daily.advanceDaysDetailed(1);
        require(one.advancedDays == 1 && !one.interrupted,
                "equivalent one-day call advances exactly one day");
    }
    compareDurableProgramState(bulk.state(), daily.state(), id);
}

void test_return_budget_uses_projected_departure_date() {
    deep::GameState state = deep::createDelegatedSurveyScenario();
    const deep::BodyId homeId = state.colonies.back().bodyId;
    const auto homeBody = std::find_if(state.bodies.begin(), state.bodies.end(),
        [homeId](const deep::Body& body) { return body.id == homeId; });
    require(homeBody != state.bodies.end(), "fixed home exists for route-budget proof");
    homeBody->x = 50.0; // Offset from the orbit center so dates change distance.
    const auto terra = std::find_if(state.bodies.begin(), state.bodies.end(),
        [](const deep::Body& body) { return body.name == "Terra"; });
    require(terra != state.bodies.end(), "orbiting target exists");
    const auto& fleet = state.fleets.back();
    deep::SurveyProgram program;
    program.charter.policy.returnContingencyFraction = 0.25;
    constexpr std::int64_t departureDay = 5;
    const auto outbound = deep::planFleetTransit(state, homeId, terra->id, departureDay);
    require(outbound.type == deep::FleetOrderType::MoveToBody, "outbound route is finite");
    const std::int64_t projectedReturnDay = outbound.arrivalDay + deep::kP3ASurveyWorkDaysPerPass + 1;
    const double outward = deep::adjustedFleetMoveFuelCost(state, fleet, homeId, terra->id, departureDay);
    const double returnAtProjectedDay = deep::adjustedFleetMoveFuelCost(
        state, fleet, terra->id, homeId, projectedReturnDay);
    const auto budget = deep::projectedSurveySortieFuel(
        state, program, fleet, homeId, terra->id, departureDay);
    require(budget.has_value() && std::isfinite(*budget), "date-specific sortie budget is finite");
    near(*budget, outward + returnAtProjectedDay * 1.25,
         "sortie preview uses return fuel at earliest legal post-visit departure");
    require(std::abs(returnAtProjectedDay - outward) > 1.0e-4,
            "fixture rules out a twice-outbound-distance shortcut");
}

} // namespace

int main() {
    try {
        test_automatic_visits_reporting_and_fuel_conservation();
        test_equal_skill_leaders_choose_distinct_reproducible_orders();
        test_empty_home_stock_waits_and_recovers_with_same_program_id();
        test_arrival_boundary_and_mid_transit_suspension();
        test_partial_refill_respects_roster_budget_and_stock_floor();
        test_fuel_authorization_issue_stops_and_acknowledges_once();
        test_known_small_cap_at_creation_waits_without_surprise_prompt();
        test_amended_known_cap_waits_but_new_return_cause_interrupts();
        test_mid_visit_asset_amendment_switches_after_old_return();
        test_hidden_deposits_do_not_change_target_decisions();
        test_barren_and_already_known_visits_have_zero_information_receipts();
        test_bulk_and_daily_advancement_have_same_durable_result();
        test_return_budget_uses_projected_departure_date();
    } catch (const std::exception& ex) {
        std::cerr << "Survey program execution test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "All Deep Signal survey program execution tests passed.\n";
    return EXIT_SUCCESS;
}
