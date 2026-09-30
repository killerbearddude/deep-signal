// PR14 revision regressions distinguish durable completed test evidence from
// sunk fractional work and requested future assignments from actual provenance.
// All artifacts are earned by commands/ticks; Save/Load exercises both blockers.
#include "save/SaveGameRepository.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/GameStateValidation.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/TechnicalDevelopmentRules.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) {
    require(std::isfinite(a) && std::abs(a - b) < 1e-8, why);
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "technical-revision-XXXXXX").string();
        const auto dir = mkdtemp(pattern.data());
        if (!dir)
            throw std::runtime_error("unique temporary directory failed");
        path = dir;
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct Fixture {
    GameState state;
    TechnicalDevelopmentCharter charter;
    TechnicalFacilityId otherFacility;
    MaintenanceTeamId otherTeam;
    PersonId otherLeader;
};
Fixture fixture(double rate = 1.0,
                TechnicalDevelopmentScope scope = TechnicalDevelopmentScope::DemonstratePrototype) {
    Fixture f;
    f.state = createHomeSystemScenario();
    const auto original = std::find_if(f.state.maintenanceTeams.begin(), f.state.maintenanceTeams.end(),
                                       [](const auto& t) { return t.name == "Prototype Engineering Team"; });
    require(original != f.state.maintenanceTeams.end(), "authored prototype engineer exists");
    f.charter = {"Revision evidence",
                 f.state.technologyOpportunities.front().id,
                 f.state.technicalFacilities.front().colonyId,
                 f.state.technicalFacilities.front().id,
                 original->id,
                 f.state.people.front().id,
                 scope,
                 {}};
    auto team = *original;
    team.id = {f.state.ids.nextMaintenanceTeamId++};
    team.name = "Alternate real prototype engineer";
    f.otherTeam = team.id;
    f.state.maintenanceTeams.push_back(team);
    f.state.technicalFacilities.front().engineeringWorkdaysPerDay = rate;
    auto facility = f.state.technicalFacilities.front();
    facility.id = {f.state.ids.nextTechnicalFacilityId++};
    facility.name = "Alternate physical test facility";
    f.otherFacility = facility.id;
    f.state.technicalFacilities.push_back(facility);
    const auto leader = std::find_if(f.state.people.begin(), f.state.people.end(),
                                     [&](const auto& p) { return p.id != f.charter.requestedLeaderId; });
    require(leader != f.state.people.end(), "fixture has distinct leaders");
    f.otherLeader = leader->id;
    for (auto& c : f.state.colonies)
        if (c.id == f.charter.developmentColonyId) {
            c.processedStockpile.amount.fill(2000);
            c.processorCapacity = 0;
        }
    validateGameState(f.state);
    return f;
}
void command(Simulation& sim, const SimCommand& command) {
    const auto r = sim.execute(command);
    if (!r.ok)
        throw std::runtime_error("technical revision command: " + r.message);
}
void step(Simulation& sim) {
    const auto r = sim.advanceDaysDetailed(1);
    if (r.advancedDays != 1)
        throw std::runtime_error("technical revision wait: " + r.stopReason);
    validateGameState(sim.state());
}
template <class Predicate> void until(Simulation& sim, Predicate predicate) {
    for (int day = 0; day < 200 && !predicate(); ++day)
        step(sim);
    require(predicate(), "bounded technical revision phase reached");
}
GameState roundTrip(const GameState& state, const TemporaryDirectory& temp, const char* name) {
    validateGameState(state);
    const auto path = temp.path / name;
    save::SaveGameRepository::save(path, state);
    auto loaded = save::SaveGameRepository::load(path);
    validateGameState(loaded);
    return loaded;
}
double electronics(const GameState& s, ColonyId id) {
    for (const auto& c : s.colonies)
        if (c.id == id)
            return c.processedStockpile.get(ProcessedMaterial::Electronics);
    throw std::runtime_error("missing fixture colony");
}
double work(const TechnicalDevelopmentProgram& p, TechnicalDevelopmentStage stage) {
    double sum = 0;
    for (const auto& r : p.receipts)
        if (r.stage == stage)
            sum += r.work;
    return sum;
}
double cost(const TechnicalDevelopmentProgram& p, TechnicalDevelopmentStage stage,
            ProcessedMaterial material) {
    double sum = 0;
    for (const auto& r : p.receipts)
        if (r.stage == stage)
            sum += r.consumed.get(material);
    return sum;
}
const TechnicalDevelopmentProgram& current(const Simulation& sim) {
    return sim.state().technicalDevelopmentPrograms.back();
}
void assertTestSame(const TechnicalTestRecord& a, const TechnicalTestRecord& b) {
    require(a.id == b.id && a.programId == b.programId && a.prototypeId == b.prototypeId &&
                a.facilityId == b.facilityId && a.teamId == b.teamId && a.leaderId == b.leaderId &&
                a.sequence == b.sequence && a.day == b.day &&
                a.measuredDetectionThreshold == b.measuredDetectionThreshold,
            "historical test provenance and measurement remain immutable");
}
void completed_tests_survive_cancel_and_load() {
    for (const double paid : {1.0, 2.0, 1.5}) {
        TemporaryDirectory temp;
        auto f = fixture(paid == 1.5 ? .5 : 1);
        Simulation sim{f.state};
        const auto originalProfiles = f.state.measurementProfiles.size(),
                   originalComponents = f.state.shipComponents.size();
        command(sim, CreateTechnicalDevelopmentCommand{f.charter});
        until(sim, [&] {
            return current(sim).stage == TechnicalDevelopmentStage::PrototypeTesting &&
                   current(sim).stageWork == paid;
        });
        const auto completed = static_cast<std::size_t>(std::floor(paid));
        require(sim.state().technicalTestRecords.size() == completed,
                "only complete paid test-workdays become reusable evidence");
        const auto oldTests = sim.state().technicalTestRecords;
        const auto sunkWork = current(sim).stageWork;
        const auto sunkCost = current(sim).stageConsumed;
        command(sim, CancelTechnicalDevelopmentCommand{current(sim).id});
        auto cancelled = sim.state();
        // Storage ordinal is durable order, not implicit test sequence. A valid
        // reversed predecessor must continue without rewriting its saved vector.
        if (completed == 2)
            std::reverse(cancelled.technicalTestRecords.begin(), cancelled.technicalTestRecords.end());
        Simulation resumed{roundTrip(cancelled, temp, "cancelled.sqlite")};
        SimulationService previewService{resumed.state()};
        SimulationQueries queries{previewService};
        const auto preview = queries.previewTechnicalDevelopment(f.charter);
        require(preview.structurallyValid &&
                    preview.startingStage == TechnicalDevelopmentStage::PrototypeTesting,
                "successor preview starts at missing test evidence");
        near(preview.requiredWork, 3 - static_cast<double>(completed),
             "successor preview shows only missing complete test work");
        near(preview.requiredMaterials.get(ProcessedMaterial::Electronics),
             (3 - static_cast<double>(completed)) * 5,
             "successor preview prices only missing test Electronics");
        near(preview.requiredMaterials.get(ProcessedMaterial::IndustrialComposites),
             (3 - static_cast<double>(completed)) * 2,
             "successor preview prices only missing test Composites");
        command(resumed, CreateTechnicalDevelopmentCommand{f.charter});
        require(current(resumed).stage == TechnicalDevelopmentStage::PrototypeTesting,
                "missing testing remains next stage");
        near(current(resumed).stageWork, 0, "successor inherits no paid or fractional stage work");
        near(current(resumed).stageConsumed.get(ProcessedMaterial::Electronics), 0,
             "successor inherits no material expenditure");
        Simulation loaded{roundTrip(resumed.state(), temp, "continued.sqlite")};
        const double beforeElectronics = electronics(loaded.state(), f.charter.developmentColonyId);
        until(loaded, [&] { return current(loaded).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
        const double remaining = 3 - static_cast<double>(completed);
        near(beforeElectronics - electronics(loaded.state(), f.charter.developmentColonyId), remaining * 5,
             "successor debits actual colony Electronics only for missing tests");
        near(work(current(loaded), TechnicalDevelopmentStage::PrototypeTesting), remaining,
             "only missing complete tests require new paid work");
        near(cost(current(loaded), TechnicalDevelopmentStage::PrototypeTesting,
                  ProcessedMaterial::Electronics),
             remaining * 5, "new tests debit literal5 Electronics each");
        near(cost(current(loaded), TechnicalDevelopmentStage::PrototypeTesting,
                  ProcessedMaterial::IndustrialComposites),
             remaining * 2, "new tests debit literal2 Composites each");
        near(loaded.state().technicalDevelopmentPrograms.front().stageWork, sunkWork,
             "cancelled fractional work remains sunk on predecessor");
        require(loaded.state().technicalDevelopmentPrograms.front().stageConsumed.amount == sunkCost.amount,
                "predecessor costs are not refunded or moved");
        require(loaded.state().technicalTestRecords.size() == 3 &&
                    loaded.state().measurementProfiles.size() == originalProfiles + 1 &&
                    loaded.state().shipComponents.size() == originalComponents + 1 &&
                    loaded.state().developedComponentRevisions.size() == 1,
                "cross-program completion publishes exactly3 tests and one profile component revision");
        for (const auto& old : oldTests) {
            const auto retained = std::find_if(loaded.state().technicalTestRecords.begin(),
                                               loaded.state().technicalTestRecords.end(),
                                               [&](const auto& test) { return test.id == old.id; });
            require(retained != loaded.state().technicalTestRecords.end(), "prior test identity retained");
            assertTestSame(old, *retained);
        }
        const auto& links = loaded.state().developedComponentRevisions.front().testIds;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto record = std::find_if(loaded.state().technicalTestRecords.begin(),
                                             loaded.state().technicalTestRecords.end(),
                                             [&](const auto& t) { return t.id == links[i]; });
            require(record != loaded.state().technicalTestRecords.end() &&
                        record->sequence == static_cast<int>(i) + 1,
                    "demonstration gathers canonical sequence1..3 without a fourth test");
        }
        if (completed == 2)
            require(loaded.state().technicalTestRecords[0].sequence == 2 &&
                        loaded.state().technicalTestRecords[1].sequence == 1,
                    "continuation preserves reversed predecessor storage ordinals");
        Simulation finished{roundTrip(loaded.state(), temp, "demonstrated.sqlite")};
        for (int day = 0; day < 4; ++day)
            step(finished);
        require(finished.state().technicalTestRecords.size() == 3 &&
                    finished.state().developedComponentRevisions.size() == 1,
                "closed successor never repeats demonstration");
    }
}
void test_participants_and_reports_survive_amendment() {
    // Exercise each ordinary edit independently, then all edits against a
    // published report; no current charter may rewrite prior test participants.
    for (int edit = 0; edit < 4; ++edit) {
        TemporaryDirectory temp;
        auto f = fixture();
        Simulation sim{f.state};
        command(sim, CreateTechnicalDevelopmentCommand{f.charter});
        until(sim, [&] { return sim.state().technicalTestRecords.size() == 1; });
        const auto acquired = sim.state().technicalTestRecords.front();
        std::optional<TechnicalDevelopmentReport> report;
        if (edit == 3) {
            command(sim, SuspendTechnicalDevelopmentCommand{current(sim).id});
            until(sim, [&] { return sim.state().date.day >= 30; });
            report = current(sim).reports.front();
        }
        auto amended = current(sim).charter;
        if (edit == 0 || edit == 3)
            amended.requestedLeaderId = f.otherLeader;
        if (edit == 1 || edit == 3)
            amended.requestedTeamId = f.otherTeam;
        if (edit == 2 || edit == 3)
            amended.requestedFacilityId = f.otherFacility;
        command(sim, AmendTechnicalDevelopmentCommand{current(sim).id, amended});
        Simulation loaded{roundTrip(sim.state(), temp, "amended-test.sqlite")};
        assertTestSame(acquired, loaded.state().technicalTestRecords.front());
        if (edit == 3)
            command(loaded, ResumeTechnicalDevelopmentCommand{current(loaded).id});
        until(loaded, [&] { return current(loaded).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
        require(loaded.state().technicalTestRecords.size() == 3,
                "amended testing completes one bounded demonstration");
        const auto& later = loaded.state().technicalTestRecords[1];
        require(later.facilityId == *amended.requestedFacilityId &&
                    later.teamId == *amended.requestedTeamId && later.leaderId == *amended.requestedLeaderId,
                "future tests use newly requested actual participants");
        if (report) {
            const auto& retained = current(loaded).reports.front();
            require(retained.facilityId == report->facilityId && retained.teamId == report->teamId &&
                        retained.leaderId == report->leaderId &&
                        retained.charterRevision == report->charterRevision &&
                        retained.periodWork == report->periodWork &&
                        retained.periodConsumed.amount == report->periodConsumed.amount &&
                        retained.auditThroughId == report->auditThroughId,
                    "published report participants work and cutoff remain unchanged after amendment");
        }
        static_cast<void>(roundTrip(loaded.state(), temp, "amended-complete.sqlite"));
    }
}
void qualification_progress_stays_with_actual_participant() {
    for (const bool support : {false, true}) {
        TemporaryDirectory temp;
        auto f = fixture(1, support ? TechnicalDevelopmentScope::ProductionAndSupportReady
                                    : TechnicalDevelopmentScope::ProductionReady);
        Simulation sim{f.state};
        command(sim, CreateTechnicalDevelopmentCommand{f.charter});
        const auto stage = support ? TechnicalDevelopmentStage::SupportQualification
                                   : TechnicalDevelopmentStage::ProductionQualification;
        const double partial = support ? 1 : 2;
        until(sim, [&] { return current(sim).stage == stage && current(sim).stageWork == partial; });
        auto amended = current(sim).charter;
        if (support)
            amended.requestedTeamId = f.otherTeam;
        else
            amended.requestedFacilityId = f.otherFacility;
        command(sim, AmendTechnicalDevelopmentCommand{current(sim).id, amended});
        Simulation loaded{roundTrip(sim.state(), temp, "pinned-amendment.sqlite")};
        SimulationService service{loaded.state()};
        SimulationQueries queries{service};
        const auto row = queries.technicalDevelopments().back();
        if (support) {
            const auto original =
                std::find_if(f.state.maintenanceTeams.begin(), f.state.maintenanceTeams.end(),
                             [&](const auto& t) { return t.id == f.charter.requestedTeamId; });
            require(row.teamName == original->name &&
                        row.requestedTeamName == "Alternate real prototype engineer",
                    "support DTO distinguishes active pinned teamA from requested teamB");
        } else
            require(row.facilityName == f.state.technicalFacilities.front().name &&
                        row.requestedFacilityName == "Alternate physical test facility",
                    "production DTO distinguishes active pinned facilityA from requested facilityB");
        until(loaded, [&] { return current(loaded).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
        for (const auto& receipt : current(loaded).receipts)
            if (receipt.stage == stage)
                require(support ? receipt.teamId == *f.charter.requestedTeamId
                                : receipt.facilityId == *f.charter.requestedFacilityId,
                        "in-progress qualification work stays with first actual participant");
        near(work(current(loaded), stage), support ? 2 : 4,
             "pinned qualification pays its exact full required work");
        if (support) {
            require(loaded.state().supportQualificationRecords.size() == 1 &&
                        loaded.state().supportQualificationRecords.front().teamId ==
                            *f.charter.requestedTeamId,
                    "only exact trained teamA earns qualification");
            const auto family = loaded.state().supportQualificationRecords.front().familyId;
            const auto b =
                std::find_if(loaded.state().maintenanceTeams.begin(), loaded.state().maintenanceTeams.end(),
                             [&](const auto& t) { return t.id == f.otherTeam; });
            require(std::find(b->qualifiedFamilies.begin(), b->qualifiedFamilies.end(), family) ==
                        b->qualifiedFamilies.end(),
                    "requested teamB receives no transferred training");
        } else
            require(loaded.state().componentProductionCapabilities.size() == 1 &&
                        loaded.state().componentProductionCapabilities.front().facilityId ==
                            *f.charter.requestedFacilityId,
                    "four process days qualify actual pinned facilityA not requestedB");
        static_cast<void>(roundTrip(loaded.state(), temp, "pinned-complete.sqlite"));
    }
}
void batched_complete_tests_and_fractional_forgery() {
    // One finite high-throughput receipt may cross two completed tests; the
    // leftover half-workday still cannot create a third record by itself.
    auto f = fixture(2.5);
    for (auto& t : f.state.maintenanceTeams)
        if (t.id == f.charter.requestedTeamId)
            t.workdaysPerDay = 2.5;
    Simulation fast{f.state};
    command(fast, CreateTechnicalDevelopmentCommand{f.charter});
    until(fast, [&] {
        return current(fast).stage == TechnicalDevelopmentStage::PrototypeTesting &&
               current(fast).stageWork == 2.5;
    });
    require(fast.state().technicalTestRecords.size() == 2 &&
                fast.state().technicalTestRecords[0].day == fast.state().technicalTestRecords[1].day,
            "single paid2.5 receipt establishes exactlytwo completed tests");
    step(fast);
    require(fast.state().technicalTestRecords.size() == 3 &&
                current(fast).stage == TechnicalDevelopmentStage::Complete,
            "remaining halfwork completes thirdtest once");
    auto partial = fixture(.5);
    Simulation half{partial.state};
    command(half, CreateTechnicalDevelopmentCommand{partial.charter});
    until(half, [&] {
        return current(half).stage == TechnicalDevelopmentStage::PrototypeTesting &&
               current(half).stageWork == .5;
    });
    auto forged = half.state();
    auto test = fast.state().technicalTestRecords.front();
    test.id = {forged.ids.nextTechnicalTestId++};
    test.opportunityId = partial.charter.opportunityId;
    test.prototypeId = forged.prototypeComponentUnits.front().id;
    test.programId = current(half).id;
    test.facilityId = *partial.charter.requestedFacilityId;
    test.teamId = *partial.charter.requestedTeamId;
    test.leaderId = *partial.charter.requestedLeaderId;
    test.sequence = 1;
    test.day = forged.date.day;
    forged.technicalTestRecords.push_back(test);
    bool rejected = false;
    try {
        validateGameState(forged);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "half-paid testing receipt cannot justify fabricated complete test evidence");
}
void actionable_completion_and_scope_reexpansion() {
    // Completing the currently requested artifact is distinct from administrative
    // closure. An accepted extension at that boundary must schedule new work.
    TemporaryDirectory temp;
    auto demo = fixture();
    Simulation first{demo.state};
    command(first, CreateTechnicalDevelopmentCommand{demo.charter});
    until(first, [&] { return current(first).stage == TechnicalDevelopmentStage::Complete; });
    require(current(first).lifecycle == TechnicalDevelopmentLifecycle::Authorized,
            "artifact completion retains its pre-close amendment boundary");
    auto extended = current(first).charter;
    extended.scope = TechnicalDevelopmentScope::ProductionReady;
    command(first, AmendTechnicalDevelopmentCommand{current(first).id, extended});
    require(current(first).stage == TechnicalDevelopmentStage::ProductionQualification,
            "scope extension before admin close selects missing production stage");
    Simulation production{roundTrip(first.state(), temp, "completion-extension.sqlite")};
    until(production, [&] { return current(production).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
    require(production.state().technicalDevelopmentPrograms.size() == 1 &&
                production.state().componentProductionCapabilities.size() == 1 &&
                production.state().technicalTestRecords.size() == 3,
            "same extended program completes production without repeating demonstration");
    near(work(current(production), TechnicalDevelopmentStage::ProductionQualification), 4,
         "extended production pays exactlyfour process workdays");

    for (const bool support : {false, true}) {
        auto f = fixture(1, support ? TechnicalDevelopmentScope::ProductionAndSupportReady
                                    : TechnicalDevelopmentScope::ProductionReady);
        Simulation sim{f.state};
        command(sim, CreateTechnicalDevelopmentCommand{f.charter});
        const auto stage = support ? TechnicalDevelopmentStage::SupportQualification
                                   : TechnicalDevelopmentStage::ProductionQualification;
        const double partial = support ? 1 : 2;
        until(sim, [&] { return current(sim).stage == stage && current(sim).stageWork == partial; });
        const auto paid = current(sim).stageConsumed;
        const double before = electronics(sim.state(), f.charter.developmentColonyId);
        if (support)
            command(sim, SuspendTechnicalDevelopmentCommand{current(sim).id});
        auto narrowed = current(sim).charter;
        narrowed.scope = support ? TechnicalDevelopmentScope::ProductionReady
                                 : TechnicalDevelopmentScope::DemonstratePrototype;
        command(sim, AmendTechnicalDevelopmentCommand{current(sim).id, narrowed});
        require(current(sim).stage == TechnicalDevelopmentStage::Complete,
                "narrowed scope is fulfilled while partial qualification remains paid history");
        Simulation reopened{
            roundTrip(sim.state(), temp, support ? "narrowed-support.sqlite" : "narrowed-process.sqlite")};
        auto restore = current(reopened).charter;
        restore.scope = f.charter.scope;
        if (support)
            restore.requestedTeamId = f.otherTeam;
        else
            restore.requestedFacilityId = f.otherFacility;
        command(reopened, AmendTechnicalDevelopmentCommand{current(reopened).id, restore});
        require(current(reopened).stage == stage,
                "reexpanded authority selects its prior partially paid stage");
        near(current(reopened).stageWork, partial, "same program restores its own paid qualification work");
        require(current(reopened).stageConsumed.amount == paid.amount,
                "scope restoration retains exact prior paid materials");
        near(electronics(reopened.state(), f.charter.developmentColonyId), before,
             "scope edits and SaveLoad neither refund nor recharge prior inputs");
        Simulation loaded{roundTrip(reopened.state(), temp,
                                    support ? "restored-support.sqlite" : "restored-process.sqlite")};
        if (support)
            command(loaded, ResumeTechnicalDevelopmentCommand{current(loaded).id});
        until(loaded, [&] { return current(loaded).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
        near(work(current(loaded), stage), support ? 2 : 4,
             "restored stage finishes once with original paid work included");
        near(cost(current(loaded), stage, ProcessedMaterial::Electronics), support ? 10 : 30,
             "restored stage does not double charge Electronics");
        near(cost(current(loaded), stage, ProcessedMaterial::IndustrialComposites), support ? 10 : 20,
             "restored stage does not double charge Composites");
        near(before - electronics(loaded.state(), f.charter.developmentColonyId), support ? 5 : 15,
             "only unpaid remaining qualification debits actual stock");
        for (const auto& receipt : current(loaded).receipts)
            if (receipt.stage == stage)
                require(support ? receipt.teamId == *f.charter.requestedTeamId
                                : receipt.facilityId == *f.charter.requestedFacilityId,
                        "scope narrowing and expansion cannot transfer original qualification pin");
        if (support)
            require(loaded.state().supportQualificationRecords.size() == 1 &&
                        loaded.state().supportQualificationRecords.front().teamId ==
                            *f.charter.requestedTeamId,
                    "reexpanded support qualifies pinnedA exactlyonce");
        else
            require(loaded.state().componentProductionCapabilities.size() == 1 &&
                        loaded.state().componentProductionCapabilities.front().facilityId ==
                            *f.charter.requestedFacilityId,
                    "reexpanded production qualifies pinnedA exactlyonce");
        static_cast<void>(
            roundTrip(loaded.state(), temp,
                      support ? "restored-support-complete.sqlite" : "restored-process-complete.sqlite"));
    }
}
void cancelled_testing_keeps_physical_prototype_local() {
    // Completed evidence is portable knowledge; the fabricated prototype is a
    // physical local object. A new remote charter must not teleport that object.
    TemporaryDirectory temp;
    auto f = fixture();
    Simulation first{f.state};
    command(first, CreateTechnicalDevelopmentCommand{f.charter});
    until(first, [&] { return first.state().technicalTestRecords.size() == 2; });
    command(first, CancelTechnicalDevelopmentCommand{current(first).id});
    auto remote = roundTrip(first.state(), temp, "local-prototype-cancelled.sqlite");
    const auto originalColony = remote.prototypeComponentUnits.front().colonyId;
    auto other = std::find_if(remote.colonies.begin(), remote.colonies.end(),
                              [&](const auto& c) { return c.id != originalColony; });
    require(other != remote.colonies.end(), "locality fixture has another existing colony");
    const auto otherColony = other->id;
    other->processedStockpile.amount.fill(2000);
    other->processorCapacity = 0;
    auto facility = std::find_if(remote.technicalFacilities.begin(), remote.technicalFacilities.end(),
                                 [&](const auto& row) { return row.id == f.otherFacility; });
    facility->colonyId = otherColony;
    auto team = std::find_if(remote.maintenanceTeams.begin(), remote.maintenanceTeams.end(),
                             [&](const auto& row) { return row.id == f.otherTeam; });
    team->colonyId = otherColony;
    auto charter = f.charter;
    charter.developmentColonyId = otherColony;
    charter.requestedFacilityId = f.otherFacility;
    charter.requestedTeamId = f.otherTeam;
    SimulationService previewService{remote};
    SimulationQueries queries{previewService};
    const auto preview = queries.previewTechnicalDevelopment(charter);
    const std::string reason =
        "Waiting for the physical prototype at the development colony; prototype transport is unavailable";
    require(preview.structurallyValid &&
                preview.startingStage == TechnicalDevelopmentStage::PrototypeTesting &&
                preview.condition == reason,
            "remote successor remains admitted intent with explicit physical prototype wait");
    Simulation sim{remote};
    command(sim, CreateTechnicalDevelopmentCommand{charter});
    require(technicalDevelopmentCondition(sim.state(), current(sim)) == reason,
            "actual successor readiness identifies missing local prototype");
    const auto initialStock = other->processedStockpile.amount;
    Simulation loaded{roundTrip(sim.state(), temp, "remote-prototype-wait.sqlite")};
    for (int day = 0; day < 4; ++day)
        step(loaded);
    near(current(loaded).stageWork, 0, "remote tests cannot acquire work without their physical prototype");
    require(current(loaded).receipts.empty() && loaded.state().technicalTestRecords.size() == 2 &&
                loaded.state().developedComponentRevisions.empty(),
            "waiting remote successor creates no test costs evidence or demonstration");
    const auto stock = std::find_if(loaded.state().colonies.begin(), loaded.state().colonies.end(),
                                    [&](const auto& c) { return c.id == otherColony; });
    require(stock->processedStockpile.amount == initialStock,
            "remote physical wait consumes no local supplies");
    require(loaded.state().prototypeComponentUnits.front().colonyId == originalColony,
            "cancellation and remote authority never move prototype custody");
    static_cast<void>(roundTrip(loaded.state(), temp, "remote-prototype-still-waiting.sqlite"));
    // Once the original physical prototype finishes testing at home, a separate
    // local production process may be qualified elsewhere without moving it.
    Simulation home{first.state()};
    command(home, CreateTechnicalDevelopmentCommand{f.charter});
    until(home, [&] { return current(home).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
    auto demonstrated = home.state();
    for (auto& row : demonstrated.technicalFacilities)
        if (row.id == f.otherFacility)
            row.colonyId = otherColony;
    for (auto& row : demonstrated.maintenanceTeams)
        if (row.id == f.otherTeam)
            row.colonyId = otherColony;
    for (auto& row : demonstrated.colonies)
        if (row.id == otherColony) {
            row.processedStockpile.amount.fill(2000);
            row.processorCapacity = 0;
        }
    Simulation process{demonstrated};
    charter.scope = TechnicalDevelopmentScope::ProductionReady;
    command(process, CreateTechnicalDevelopmentCommand{charter});
    require(current(process).stage == TechnicalDevelopmentStage::ProductionQualification,
            "demonstrated knowledge permits separate remote process qualification");
    until(process, [&] { return current(process).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
    require(process.state().componentProductionCapabilities.size() == 1 &&
                process.state().componentProductionCapabilities.front().colonyId == otherColony &&
                process.state().prototypeComponentUnits.front().colonyId == originalColony,
            "remote production is legal while prototype remains physically at original colony");
}
void malformed_evidence_remains_rejected() {
    auto f = fixture();
    Simulation sim{f.state};
    command(sim, CreateTechnicalDevelopmentCommand{f.charter});
    until(sim, [&] { return current(sim).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
    const auto valid = sim.state();
    // Earn a second actual prototype so mixed-prototype corruption references
    // an existing physical object, rather than merely a dangling identifier.
    auto extended = valid;
    auto opportunity = extended.technologyOpportunities.front();
    opportunity.id = {extended.ids.nextTechnologyOpportunityId++};
    opportunity.name = "Independent evidence fixture";
    extended.technologyOpportunities.push_back(opportunity);
    extended.technologyCandidateTruths.push_back({opportunity.id, 6});
    auto other = f.charter;
    other.opportunityId = opportunity.id;
    Simulation second{extended};
    command(second, CreateTechnicalDevelopmentCommand{other});
    until(second, [&] { return current(second).lifecycle == TechnicalDevelopmentLifecycle::Closed; });
    auto mixed = second.state();
    mixed.technicalTestRecords.front().prototypeId = mixed.technicalTestRecords.back().prototypeId;
    bool mixedRejected = false;
    try {
        validateGameState(mixed);
    } catch (const std::exception&) {
        mixedRejected = true;
    }
    require(mixedRejected, "test evidence from two actual prototypes cannot be mixed");
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto corrupt = valid;
        if (mutation == 0)
            corrupt.technicalTestRecords[1].sequence = 1;
        if (mutation == 1)
            corrupt.technicalTestRecords[2].sequence = 4;
        if (mutation == 2)
            corrupt.technicalTestRecords[1].measuredDetectionThreshold += .5;
        if (mutation == 3) {
            auto& receipts = corrupt.technicalDevelopmentPrograms.front().receipts;
            const auto receipt = std::find_if(receipts.begin(), receipts.end(), [](const auto& r) {
                return r.stage == TechnicalDevelopmentStage::PrototypeTesting;
            });
            receipts.erase(receipt);
        }
        if (mutation == 4) {
            auto duplicate = corrupt.developedComponentRevisions.front();
            duplicate.id = {corrupt.ids.nextDevelopedComponentRevisionId++};
            corrupt.developedComponentRevisions.push_back(duplicate);
        }
        if (mutation == 5)
            corrupt.technicalTestRecords[1].prototypeId = {corrupt.ids.nextPrototypeComponentUnitId};
        bool rejected = false;
        try {
            validateGameState(corrupt);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "malformed duplicate/out-of-range/mismatched/unpaid technical evidence rejected");
    }
}
} // namespace
int main() {
    int failures = 0;
    const std::pair<const char*, void (*)()> cases[] = {
        {"completed test continuation", completed_tests_survive_cancel_and_load},
        {"historical participants", test_participants_and_reports_survive_amendment},
        {"qualification pins", qualification_progress_stays_with_actual_participant},
        {"batched and fractional tests", batched_complete_tests_and_fractional_forgery},
        {"completion and scope reexpansion", actionable_completion_and_scope_reexpansion},
        {"physical prototype locality", cancelled_testing_keeps_physical_prototype_local},
        {"malformed evidence", malformed_evidence_remains_rejected}};
    for (const auto& [name, test] : cases)
        try {
            test();
            std::cout << name << ": passed\n";
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << name << ": " << e.what() << '\n';
        }
    return failures == 0 ? 0 : 1;
}
