// Independent P4B construction lifecycle proof. Fixtures provision real finite
// hulls/engineers and sealed inputs; commands and elapsed work earn every module.
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/SiteDevelopmentExecution.h"
#include "sim/SiteDevelopmentValidation.h"
#include "sim/GameStateValidation.h"
#include "sim/ShipDesignRules.h"
#include "sim/SiteOperationRules.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b, const char* why) {
    require(std::abs(a - b) < 1e-8, why);
}
// Give this isolated test an explicit construction family and hull; runtime code
// must use the typed binding rather than guessing a catalog ID or role label.
GameState fixture(bool sameBody = false) {
    auto s = createTenderMaintenanceScenario();
    const EquipmentFamilyId family{s.ids.nextEquipmentFamilyId++};
    s.equipmentFamilies.push_back({family, "Test field engineering"});
    s.siteConstructionFamilyId = family;
    s.siteModuleCatalog = referenceSiteModuleCatalog();
    ShipComponentDefinition workshop;
    workshop.id = ShipComponentId{s.ids.nextShipComponentId++};
    workshop.name = "Test construction workshop";
    workshop.kind = ShipComponentKind::Workshop;
    workshop.mass = 100;
    workshop.volume = 150;
    workshop.powerDemand = 30;
    workshop.buildPoints = 100;
    workshop.workshopRates = {{family, 1}};
    s.shipComponents.push_back(workshop);
    auto& cls = s.shipClasses.back();
    cls.components.erase(
        std::remove_if(cls.components.begin(), cls.components.end(),
                       [&](auto row) {
                           auto i = std::find_if(s.shipComponents.begin(), s.shipComponents.end(),
                                                 [&](auto c) { return c.id == row.componentId; });
                           return i != s.shipComponents.end() && i->kind == ShipComponentKind::Workshop;
                       }),
        cls.components.end());
    cls.components.push_back({workshop.id, 1});
    MaintenanceTeam team;
    team.id = MaintenanceTeamId{s.ids.nextMaintenanceTeamId++};
    team.name = "Field team";
    team.qualifiedFamilies = {family};
    team.colonyId = s.colonies.back().id;
    s.maintenanceTeams.push_back(team);
    ResourceSite site;
    site.id = SiteId{s.ids.nextSiteId++};
    site.name = "Uninvestigated test staging";
    site.bodyId = sameBody ? s.colonies.back().bodyId : s.bodies.back().id;
    site.operatingPolicy.enabled = false;
    site.processedStock.set(ProcessedMaterial::StructuralAlloys, 240);
    site.processedStock.set(ProcessedMaterial::Electronics, 70);
    site.processedStock.set(ProcessedMaterial::IndustrialComposites, 60);
    s.resourceSites.push_back(site);
    return s;
}
SiteDevelopmentCharter charter(const GameState& s) {
    SiteDevelopmentCharter c;
    c.siteId = s.resourceSites.back().id;
    c.supportColonyId = s.colonies.back().id;
    c.package = referenceSitePackage();
    c.assignments.name = "Earned construction";
    c.assignments.builderId = s.fleets.back().id;
    c.assignments.teamId = s.maintenanceTeams.back().id;
    c.assignments.leaderId = s.people.front().id;
    return c;
}
void step(Simulation& sim) {
    const auto result = sim.advanceDaysDetailed(1);
    require(result.advancedDays == 1, "one physical day advances");
    validateGameState(sim.state());
    validateSiteDevelopmentState(sim.state());
}
template <class P> void until(Simulation& sim, P predicate) {
    for (int i = 0; i < 150 && !predicate(sim.state()); ++i) {
        const auto& p = sim.state().siteDevelopmentPrograms.front();
        if (!p.issue.acknowledged)
            require(sim.execute(AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature}).ok,
                    "acknowledge stable constraint");
        step(sim);
    }
    if (!predicate(sim.state())) {
        const auto& p = sim.state().siteDevelopmentPrograms.front();
        for (const auto& row : p.rows)
            std::cerr << "work " << std::setprecision(17) << row.workCompleted << " materials "
                      << row.consumed.amount[0] << "," << row.consumed.amount[1] << ","
                      << row.consumed.amount[2] << "\n";
        throw std::runtime_error(
            "expected bounded construction phase reached; day=" + std::to_string(sim.state().date.day) +
            "; task=" + std::to_string(static_cast<int>(p.task)) + "; issue=" + p.issue.message);
    }
}
void earned_reference() {
    Simulation sim(fixture());
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize coherent package");
    const auto builder = sim.state().fleets.back().id;
    step(sim);
    require(sim.state().siteDevelopmentPrograms.front().task == SiteDevelopmentTask::Preparing,
            "embark is its own action");
    require(sim.state().maintenanceTeams.back().fleetId == builder, "engineer physically aboard");
    require(sim.state().siteDevelopmentPrograms.front().workReceipts.empty(), "embark cannot perform work");
    until(sim, [](const auto& s) {
        return s.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed;
    });
    const auto& s = sim.state();
    const auto& p = s.siteDevelopmentPrograms.front();
    require(p.closure == SiteDevelopmentClosure::Completed && p.workReceipts.size() == 16,
            "fourteen assembly plus two commissioning workdays");
    double assembly = 0, commission = 0;
    ProcessedMaterialSet paid;
    for (const auto& r : p.workReceipts) {
        if (r.kind == SiteDevelopmentWorkKind::Assembly)
            assembly += r.work;
        else
            commission += r.work;
        paid.addSet(r.consumed);
    }
    near(assembly, 14, "literal assembly labor");
    near(commission, 2, "literal commissioning labor");
    near(paid.get(ProcessedMaterial::StructuralAlloys), 240, "literal alloys consumption");
    near(paid.get(ProcessedMaterial::Electronics), 70, "literal electronics consumption");
    near(paid.get(ProcessedMaterial::IndustrialComposites), 60, "literal composites consumption");
    require(s.resourceSites.back().installed.size() == 5, "earned lasting groups installed exactly once");
    require(s.maintenanceTeams.back().colonyId == s.colonies.back().id && !p.leasedBuilderId,
            "physical home disembark releases builders");
    require(p.fuelBurned > 0, "both physical trips burn actual fuel");
    require(s.observations.empty(), "construction does not invent scientific evidence");
}
void independent_rows_and_opening_snapshot() {
    auto s = fixture(true);
    s.resourceSites.back().processedStock.set(ProcessedMaterial::Electronics, 0);
    Simulation sim(s);
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize shortage package");
    until(sim, [](const auto& s) { return !s.siteDevelopmentPrograms.front().workReceipts.empty(); });
    const auto& p = sim.state().siteDevelopmentPrograms.front();
    require(p.workReceipts.front().packageRow == 3,
            "stock-blocked first row must not block independent storage row");
    auto snapshot = sim.state();
    ++snapshot.date.day;
    auto& project = snapshot.siteDevelopmentPrograms.front();
    OpeningProgramContext opening(snapshot);
    snapshot.resourceSites.back().processedStock.set(ProcessedMaterial::Electronics, 70);
    const auto before = project.rows.front().workCompleted;
    runSiteDevelopmentOpeningDay(snapshot, project, opening, {});
    near(project.rows.front().workCompleted, before,
         "later incoming electronics cannot be spent in same opening");
}
void cancel_remote_retains_custody() {
    Simulation sim(fixture());
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize cancellable package");
    until(sim, [](const auto& s) { return !s.siteDevelopmentPrograms.front().workReceipts.empty(); });
    const auto id = sim.state().siteDevelopmentPrograms.front().id;
    const double sunk = sim.state().siteDevelopmentPrograms.front().rows.front().workCompleted;
    require(sim.execute(CancelSiteDevelopmentCommand{id}).ok, "cancel remote project");
    require(sim.state().siteDevelopmentPrograms.front().leasedTeamId.has_value(),
            "cancel retains remote engineer lease");
    require(sim.execute(SuspendSiteDevelopmentCommand{id}).ok, "closing return remains suspendable");
    require(sim.execute(ResumeSiteDevelopmentCommand{id}).ok, "closing return resumes");
    require(sim.state().siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closing,
            "resume cancellation cannot reauthorize assembly");
    until(sim, [](const auto& s) {
        return s.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed;
    });
    near(sim.state().siteDevelopmentPrograms.front().rows.front().workCompleted, sunk,
         "cancel retains sunk partial work");
    require(sim.state().resourceSites.back().installed.empty(),
            "partial assembly grants no installed capacity");
}
// Requested changes cannot replace the actual engineer halfway through a trip,
// and completion remains physically actionable until home release.
void retained_participants_and_completion_controls() {
    auto initial = fixture();
    auto second = initial.maintenanceTeams.back();
    second.id = MaintenanceTeamId{initial.ids.nextMaintenanceTeamId++};
    second.name = "Replacement engineer";
    initial.maintenanceTeams.insert(initial.maintenanceTeams.end() - 1, second);
    Simulation sim(initial);
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize amendable project");
    until(sim, [](const auto& s) { return !s.siteDevelopmentPrograms.front().workReceipts.empty(); });
    const auto id = sim.state().siteDevelopmentPrograms.front().id;
    const auto actual = sim.state().siteDevelopmentPrograms.front().taskTeamId;
    auto amendment = sim.state().siteDevelopmentPrograms.front().charter.assignments;
    amendment.teamId = second.id;
    require(sim.execute(AmendSiteDevelopmentCommand{id, amendment}).ok,
            "future engineering assignment can change remotely");
    require(sim.state().siteDevelopmentPrograms.front().taskTeamId == actual,
            "actual engineer retained across amendment");
    until(sim, [](const auto& s) { return s.siteDevelopmentPrograms.front().commissionedDay.has_value(); });
    require(sim.execute(SuspendSiteDevelopmentCommand{id}).ok, "completed package return can suspend");
    require(sim.execute(AmendSiteDevelopmentCommand{id, amendment}).ok, "completed package return can amend");
    require(sim.execute(ResumeSiteDevelopmentCommand{id}).ok, "completed package return can resume");
    require(sim.execute(CancelSiteDevelopmentCommand{id}).ok,
            "completed return remains cancellation-actionable");
    require(sim.state().siteDevelopmentPrograms.front().closure == SiteDevelopmentClosure::Completed,
            "cancellation does not erase earned Completed disposition");
    until(sim, [](const auto& s) {
        return s.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed;
    });
    require(sim.state().resourceSites.back().installed.size() == 5,
            "completion controls never duplicate/uninstall earned assets");
}
void distinct_fuel_action_and_validation() {
    auto s = fixture();
    s.ships.back().fuel = 0;
    s.colonies.back().processedStockpile.set(ProcessedMaterial::Propellant, 1000);
    Simulation sim(s);
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize unfueled builder");
    step(sim);
    require(sim.state().ships.back().fuel == 0, "embark cannot refuel");
    step(sim);
    require(sim.state().ships.back().fuel > 0 && !sim.state().fleets.back().destinationBodyId,
            "refuel cannot depart");
    const double loaded = sim.state().siteDevelopmentPrograms.front().fuelLoaded;
    near(1000 - sim.state().colonies.back().processedStockpile.get(ProcessedMaterial::Propellant), loaded,
         "real stock pays engine fuel");
    step(sim);
    require(sim.state().siteDevelopmentPrograms.front().fuelBurned > 0,
            "departure independently burns engine fuel");
    until(sim, [](const auto& state) { return !state.siteDevelopmentPrograms.front().workReceipts.empty(); });
    auto bad = sim.state();
    bad.siteDevelopmentPrograms.front().rows.front().workCompleted += 0.5;
    bool rejected = false;
    try {
        validateSiteDevelopmentState(bad);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "forged free work is rejected by snapshot validator");
    bad = sim.state();
    bad.siteDevelopmentPrograms.front().workReceipts.front().consumed.amount.front() += 1;
    rejected = false;
    try {
        validateSiteDevelopmentState(bad);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "forged receipt material is rejected by snapshot validator");
}
// Accepted missing resources do not stop delegated time. A later observed
// interruption retains its acknowledgment through a remote pause/resume.
void accepted_wait_and_acknowledged_pause() {
    auto initial = fixture();
    auto c = charter(initial);
    c.assignments.builderId.reset();
    c.assignments.teamId.reset();
    c.assignments.leaderId.reset();
    Simulation waiting(initial);
    require(waiting.execute(CreateSiteDevelopmentCommand{c, {}}).ok,
            "missing assets authorize waiting intent");
    const auto elapsed = waiting.advanceDaysDetailed(30);
    require(elapsed.advancedDays == 30 && !elapsed.interrupted &&
                waiting.state().siteDevelopmentPrograms.front().issue.acknowledged,
            "known initial resource waits do not interrupt");
    validateGameState(waiting.state());
    auto corrupt = waiting.state();
    corrupt.siteDevelopmentPrograms.front().reports.front().policy.homePropellantFloor = -1;
    bool invalid = false;
    try {
        validateSiteDevelopmentState(corrupt);
    } catch (const std::exception&) {
        invalid = true;
    }
    require(invalid, "negative historical report fuel floor is rejected");
    corrupt = waiting.state();
    corrupt.siteDevelopmentPrograms.front().reports.front().policy.materialAllowances =
        ProcessedMaterialSet{};
    corrupt.siteDevelopmentPrograms.front().reports.front().policy.materialAllowances->amount.front() = -1;
    invalid = false;
    try {
        validateSiteDevelopmentState(corrupt);
    } catch (const std::exception&) {
        invalid = true;
    }
    require(invalid, "negative historical material allowance is rejected");
    Simulation building(initial);
    require(building.execute(CreateSiteDevelopmentCommand{charter(initial), {}}).ok,
            "authorize later regression test");
    until(building,
          [](const auto& state) { return !state.siteDevelopmentPrograms.front().workReceipts.empty(); });
    auto changed = building.state();
    changed.resourceSites.back().processedStock = {};
    Simulation shortage(changed);
    auto stopped = shortage.advanceDaysDetailed(1);
    require(stopped.interrupted, "loss of material after actual work raises one decision");
    const auto id = shortage.state().siteDevelopmentPrograms.front().id;
    const auto signature = shortage.state().siteDevelopmentPrograms.front().issue.signature;
    require(shortage.execute(AcknowledgeSiteDevelopmentIssueCommand{id, signature}).ok,
            "acknowledge physical shortage");
    require(shortage.execute(SuspendSiteDevelopmentCommand{id}).ok, "suspend accepted shortage");
    require(shortage.advanceDaysDetailed(1).advancedDays == 1, "paused shortage allows time");
    require(shortage.execute(ResumeSiteDevelopmentCommand{id}).ok, "resume accepted shortage");
    stopped = shortage.advanceDaysDetailed(1);
    require(!stopped.interrupted &&
                shortage.state().siteDevelopmentPrograms.front().issue.signature == signature &&
                shortage.state().siteDevelopmentPrograms.front().issue.acknowledged,
            "pause does not rediscover the same acknowledged episode");
}
// Event/fuel numeric limits are checked before actual registration or movement.
void conservative_numeric_preflight() {
    auto initial = fixture();
    initial.ids.nextEventId = std::numeric_limits<std::int64_t>::max();
    Simulation exhausted(initial);
    const auto sites = exhausted.state().resourceSites.size();
    require(!exhausted.execute(CreateSiteDevelopmentCommand{charter(initial), {}}).ok &&
                exhausted.state().siteDevelopmentPrograms.empty() &&
                exhausted.state().resourceSites.size() == sites,
            "event identity exhaustion rejects before accepted mutation");
    Simulation sim(fixture());
    require(sim.execute(CreateSiteDevelopmentCommand{charter(sim.state()), {}}).ok,
            "authorize numeric departure proof");
    step(sim);
    auto extreme = sim.state();
    ++extreme.date.day;
    auto& p = extreme.siteDevelopmentPrograms.front();
    p.fuelBurned = 1e30;
    OpeningProgramContext opening(extreme);
    bool moved = false;
    const double fuel = extreme.ships.back().fuel;
    SiteDevelopmentExecutionHooks hooks;
    hooks.startProgramMove = [&](auto, auto, auto, double&) {
        moved = true;
        return true;
    };
    runSiteDevelopmentOpeningDay(extreme, p, opening, hooks);
    require(!moved && extreme.ships.back().fuel == fuel && p.task == SiteDevelopmentTask::Preparing,
            "unrepresentable cumulative burn cannot start or pay a movement");
}
void deficient_package_and_workshop_constraints() {
    auto initial = fixture(true);
    initial.resourceSites.front().operatingPolicy.enabled = true;
    initial.resourceSites.front().operatingPolicy.leaderId = initial.people.front().id;
    Simulation sim(initial);
    auto c = charter(sim.state());
    c.package = {{SiteModuleKind::BulkStorage, 1}};
    require(sim.execute(CreateSiteDevelopmentCommand{c, {}}).ok,
            "storage-only package is valid construction intent");
    until(sim, [](const auto& s) {
        return s.siteDevelopmentPrograms.front().lifecycle == SiteDevelopmentLifecycle::Closed;
    });
    const auto& site = sim.state().resourceSites.front();
    require(site.installed.size() == 1 &&
                siteCapabilities(sim.state(), site, sim.state().date.day + 1).equipment.rawStorage == 200,
            "deficient package physically earns its passive storage");
    require(site.dutyReceipts.empty() &&
                siteDutyPreview(sim.state(), site, sim.state().date.day + 1).cause == SiteWorkCause::NoPower,
            "commissioned deficient design truthfully waits for power without synthetic support");
    for (bool incompatible : {false, true}) {
        auto state = fixture(true);
        auto& cls = state.shipClasses.back();
        if (incompatible)
            state.maintenanceTeams.back().qualifiedFamilies = {EquipmentFamilyId{1}};
        else
            std::erase_if(cls.components, [&](const auto& install) {
                return std::any_of(
                    state.shipComponents.begin(), state.shipComponents.end(), [&](const auto& d) {
                        return d.id == install.componentId && d.kind == ShipComponentKind::Reactor;
                    });
            });
        Simulation waiting(state);
        const auto proposal = charter(state);
        require(waiting.execute(CreateSiteDevelopmentCommand{proposal, {}}).ok,
                "unpowered/incompatible assets preserve valid development intent");
        require(waiting.advanceDaysDetailed(3).advancedDays == 3,
                "known workshop limitation waits without surprise interruption");
        require(!waiting.state().siteDevelopmentPrograms.front().leasedBuilderId &&
                    waiting.state().siteDevelopmentPrograms.front().workReceipts.empty(),
                "classification cannot bypass unpowered workshop or engineering family mismatch");
    }
}
void creation_and_expansion() {
    Simulation sim(fixture(true));
    auto c = charter(sim.state());
    c.siteId = {};
    NewResourceSite n;
    n.bodyId = sim.state().bodies.back().id;
    n.name = "Cold registered location";
    n.operatingPolicy.enabled = false;
    auto bad = c;
    bad.package.push_back(bad.package.front());
    const auto sites = sim.state().resourceSites.size();
    require(!sim.execute(CreateSiteDevelopmentCommand{bad, n}).ok &&
                sim.state().resourceSites.size() == sites,
            "invalid package registers no partial site");
    require(sim.execute(CreateSiteDevelopmentCommand{c, n}).ok, "new site and project admitted atomically");
    const auto& site = sim.state().resourceSites.back();
    require(site.installed.empty() && site.processedStock.get(ProcessedMaterial::StructuralAlloys) == 0,
            "registration creates no equipment or supplies");
    auto edit = sim.state().siteDevelopmentPrograms.back().charter.assignments;
    edit.builderId.reset();
    require(sim.execute(AmendSiteDevelopmentCommand{sim.state().siteDevelopmentPrograms.back().id, edit}).ok,
            "missing builder remains valid waiting authority");
}
} // namespace
int main() {
    try {
        earned_reference();
        independent_rows_and_opening_snapshot();
        cancel_remote_retains_custody();
        creation_and_expansion();
        deficient_package_and_workshop_constraints();
        retained_participants_and_completion_controls();
        distinct_fuel_action_and_validation();
        accepted_wait_and_acknowledged_pause();
        conservative_numeric_preflight();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Development execution checks passed\n";
}
