// v15 authoritative-field round trips, partial-work continuation and malformed
// rejection. New tests own unique temporary directories to avoid suite clashes.
#include "app/SimulationService.h"
#include "save/Database.h"
#include "save/EventJson.h"
#include "save/SaveGameRepository.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/ScenarioFactory.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace deep;
using namespace deep::save;
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Temp {
    std::filesystem::path path;
    Temp() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int n = 0; n < 100; ++n) {
            auto p = std::filesystem::temp_directory_path() /
                     ("deep_signal_maintenance_" + std::to_string(stamp) + "_" + std::to_string(n));
            if (std::filesystem::create_directory(p)) {
                path = p;
                return;
            }
        }
        throw std::runtime_error("Could not allocate unique maintenance test directory");
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
template <class Id> std::int64_t id(std::optional<Id> value) { return value ? value->value : 0; }
void amounts(std::ostream& out, const ProcessedMaterialSet& values) {
    for (double value : values.amount)
        out << ':' << value;
}
void policy(std::ostream& out, const MaintenanceSupplyPolicy& p) {
    amounts(out, p.floors);
    out << ':' << bool(p.lifetimeAllowances);
    if (p.lifetimeAllowances)
        amounts(out, *p.lifetimeAllowances);
}
// Directly enumerate each new field before involving SQL. A load-save identity
// check alone could miss a column dropped by both sides of the same mapping.
std::string fingerprint(const GameState& s) {
    std::ostringstream o;
    o << std::setprecision(17) << s.date.day << ':' << s.ids.nextEquipmentFamilyId << ':'
      << s.ids.nextMaintenanceTeamId << ':' << s.ids.nextMaintenanceProgramId;
    for (const auto& f : s.equipmentFamilies)
        o << "\nfamily " << f.id.value << ':' << f.name;
    for (const auto& c : s.shipComponents) {
        o << "\ncomponent " << c.id.value << ':' << bool(c.serviceProfile);
        if (c.serviceProfile) {
            const auto& p = *c.serviceProfile;
            o << ':' << p.familyId.value << ':' << p.dutyCapacity << ':' << p.teamWorkdaysPerDuty;
            amounts(o, p.materialsPerDuty);
        }
        for (const auto& r : c.workshopRates)
            o << " rate " << r.familyId.value << ':' << r.teamWorkdaysPerDay;
    }
    for (const auto& ship : s.ships) {
        o << "\nship " << ship.id.value << ':' << ship.shipClassId.value << ':' << ship.fleetId.value << ':'
          << ship.fuel;
        for (const auto& r : ship.equipmentCondition)
            o << " duty " << r.componentId.value << ':' << r.usedDuty;
    }
    for (const auto& p : s.surveyPrograms)
        o << "\nsurvey " << p.id.value << ':' << id(p.charter.policy.maintenanceProgramId) << ':'
          << p.charter.policy.remainingDutyTrigger << ':' << p.maintenanceReturn << ':' << p.workDaysCompleted
          << ':' << p.firstWorkDay;
    for (const auto& t : s.maintenanceTeams) {
        o << "\nteam " << t.id.value << ':' << t.name << ':' << t.workdaysPerDay << ':'
          << static_cast<int>(t.location) << ':' << id(t.colonyId) << ':' << id(t.fleetId);
        for (auto f : t.qualifiedFamilies)
            o << ':' << f.value;
    }
    for (const auto& p : s.maintenancePrograms) {
        o << "\nprovider " << p.id.value << ':' << p.charter.name << ':' << p.charter.serviceColonyId.value
          << ':' << id(p.charter.requestedTenderId) << ':' << id(p.charter.requestedTeamId) << ':'
          << id(p.charter.requestedLeaderId);
        for (auto f : p.charter.clients)
            o << " client " << f.value;
        policy(o, p.charter.policy);
        o << ':' << p.createdDay << ':' << p.charterRevision << ':' << static_cast<int>(p.lifecycle) << ':'
          << p.closedDay.value_or(-1) << ':' << id(p.leasedTenderId) << ':' << id(p.leasedTeamId) << ':'
          << p.nextJobNumber;
        amounts(o, p.consumed);
        o << ':' << p.restoredDuty << ':' << p.teamWorkdays << ':' << p.nextReportDay << ':'
          << p.reportStartDay << ':' << p.issue.signature << ':' << p.issue.message << ':'
          << p.issue.acknowledged;
        for (const auto& j : p.jobs) {
            o << "\njob " << j.number << ':' << j.surveyProgramId.value << ':' << j.clientFleetId.value << ':'
              << j.serviceColonyId.value << ':' << j.tenderFleetId.value << ':' << j.teamId.value << ':'
              << j.leaderId.value << ':' << j.providerRevision << ':' << j.clientRevision << ':'
              << j.startedDay << ':' << j.endedDay.value_or(-1) << ':' << static_cast<int>(j.outcome) << ':'
              << j.endReason << ':' << j.activeTarget.value_or(-1) << ':' << id(j.workshopShipId);
            for (const auto& t : j.targets)
                o << " target " << t.shipId.value << ':' << t.componentId.value << ':' << t.initialUsedDuty;
        }
        for (const auto& r : p.receipts) {
            o << "\nwork " << r.sequence << ':' << r.jobNumber << ':' << r.day << ':' << r.clientShipId.value
              << ':' << r.componentId.value << ':' << r.workshopShipId.value << ':' << r.installationQuantity
              << ':' << r.beforeUsedDuty << ':' << r.afterUsedDuty << ':' << r.restoredDuty << ':'
              << r.teamWorkdays << ':' << r.providerRevision << ':' << r.clientRevision;
            amounts(o, r.consumed);
        }
        for (const auto& r : p.reports) {
            o << "\nreport " << r.startDay << ':' << r.endDay << ':' << r.isNinetyDayReview << ':'
              << r.charterRevision << ':' << r.jobsCompleted << ':' << r.jobsWithdrawn << ':'
              << r.restoredDuty << ':' << r.teamWorkdays;
            amounts(o, r.consumed);
            policy(o, r.policy);
            o << ':' << r.availableTeamRate << ':' << r.compatibleWorkshopRate << ':' << r.waitingReason
              << ':' << r.auditThroughId;
        }
    }
    for (const auto& c : s.colonies) {
        o << "\nstock " << c.id.value;
        amounts(o, c.processedStockpile);
    }
    for (const auto& f : s.fleets)
        o << "\nfleet " << f.id.value << ':' << f.currentBodyId.value << ':' << id(f.destinationBodyId) << ':'
          << f.activeOrder.departureDay << ':' << f.activeOrder.arrivalDay;
    for (const auto& e : s.eventLog)
        o << "\nevent " << e.id.value << ':' << e.day << ':' << eventTypeName(e.payload) << ':'
          << eventPayloadToJson(e.payload);
    return o.str();
}
Simulation started(bool worn = false, std::int64_t startDay = 0) {
    auto s = createTenderMaintenanceScenario();
    s.date.day = startDay;
    if (worn)
        s.ships.front().equipmentCondition.front().usedDuty = 10;
    Simulation sim(s);
    MaintenanceProgramCharter p;
    p.name = "Persisted provider";
    p.serviceColonyId = s.colonies.back().id;
    p.requestedTenderId = s.fleets.back().id;
    p.requestedTeamId = s.maintenanceTeams.front().id;
    p.requestedLeaderId = s.people.front().id;
    p.clients = {s.fleets.front().id};
    p.policy.floors.set(ProcessedMaterial::Electronics, 1.25);
    p.policy.lifetimeAllowances = ProcessedMaterialSet{};
    p.policy.lifetimeAllowances->set(ProcessedMaterial::Electronics, 50);
    p.policy.lifetimeAllowances->set(ProcessedMaterial::IndustrialComposites, 50);
    require(sim.execute(CreateMaintenanceProgramCommand{p}).ok, "persistence provider accepted");
    SurveyProgramCharter c;
    c.name = "Persisted client";
    c.homeColonyId = p.serviceColonyId;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, 6}};
    c.policy.maintenanceProgramId = sim.state().maintenancePrograms.front().id;
    require(sim.execute(CreateSurveyProgramCommand{c}).ok, "persistence client accepted");
    return sim;
}
std::string databaseFingerprint(const std::filesystem::path& path) {
    Database db(path, Database::OpenMode::ReadOnly);
    std::ostringstream out;
    Statement tables(
        db, "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT GLOB 'sqlite_*' ORDER BY name;");
    const auto quote = [](const std::string& name) {
        std::string result = "\"";
        for (char c : name) {
            result += c;
            if (c == '\"')
                result += c;
        }
        return result + '\"';
    };
    while (tables.step()) {
        const auto table = tables.columnText(0);
        Statement columns(db, "PRAGMA table_xinfo(" + quote(table) + ");");
        std::vector<std::string> names;
        while (columns.step())
            names.push_back(columns.columnText(1));
        std::string sql = "SELECT ";
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i)
                sql += ',';
            sql += "quote(" + quote(names[i]) + ")";
        }
        sql += " FROM " + quote(table) + " ORDER BY rowid;";
        Statement rows(db, sql);
        while (rows.step()) {
            out << table;
            for (std::size_t i = 0; i < names.size(); ++i) {
                const auto cell = rows.columnText(static_cast<int>(i));
                out << ':' << cell.size() << ':' << cell;
            }
            out << '\n';
        }
    }
    return out.str();
}
void roundTrip(const GameState& state, int days, Temp& temp) {
    const auto path = temp.path / "roundtrip.sqlite";
    SaveGameRepository::save(path, state);
    const auto restored = SaveGameRepository::load(path);
    require(fingerprint(state) == fingerprint(restored), "all new authoritative fields round-trip exactly");
    Simulation direct(state), loaded(restored);
    const auto a = direct.advanceDaysDetailed(days), b = loaded.advanceDaysDetailed(days);
    require(a.advancedDays == b.advancedDays && a.issueProgramId == b.issueProgramId &&
                fingerprint(direct.state()) == fingerprint(loaded.state()),
            "same-input continuation preserves physical state, policy, authority and audit order");
    SaveGameRepository::save(temp.path / "continued.sqlite", loaded.state());
    SaveGameRepository::save(temp.path / "direct.sqlite", direct.state());
    require(databaseFingerprint(temp.path / "continued.sqlite") ==
                databaseFingerprint(temp.path / "direct.sqlite"),
            "complete durable world and all predecessor rows match after equivalent continuation");
}
void physical_checkpoints_and_policy_history() {
    Temp temp;
    auto sim = started();
    roundTrip(sim.state(), 100, temp);
    bool wore = false, partial = false, complete = false;
    for (int d = 0; d < 130; ++d) {
        require(sim.advanceDaysDetailed(1).advancedDays == 1, "normal service advances");
        const auto& p = sim.state().maintenancePrograms.front();
        if (!wore && sim.state().ships.front().equipmentCondition.front().usedDuty > 0) {
            roundTrip(sim.state(), 100, temp);
            wore = true;
        }
        if (!partial && activeServiceJob(p) && !p.receipts.empty()) {
            roundTrip(sim.state(), 100, temp);
            partial = true;
        }
        if (!complete && !p.jobs.empty() && p.jobs.front().outcome == ServiceJobOutcome::Completed) {
            roundTrip(sim.state(), 100, temp);
            complete = true;
        }
    }
    require(wore && partial && complete,
            "actual wear, first partial service and full completion were captured");
    const auto oldPolicy = sim.state().maintenancePrograms.front().reports.front().policy;
    auto a = maintenanceAmendmentFromCharter(sim.state().maintenancePrograms.front().charter);
    a.policy.lifetimeAllowances.reset();
    a.policy.floors.set(ProcessedMaterial::Electronics, 4.5);
    require(sim.execute(AmendMaintenanceProgramCommand{MaintenanceProgramId{1}, a}).ok,
            "report policy amendment accepted");
    sim.advanceDays(30);
    roundTrip(sim.state(), 30, temp);
    require(sim.state().maintenancePrograms.front().reports.front().policy.floors.amount ==
                oldPolicy.floors.amount,
            "later limits do not rewrite past report policy");
    require(sim.execute(CancelMaintenanceProgramCommand{MaintenanceProgramId{1}}).ok,
            "closed provider remains persistable");
    roundTrip(sim.state(), 90, temp);
}
void stopped_jobs_issues_and_detours() {
    Temp temp;
    auto sim = started(true);
    sim.advanceDays(2);
    require(activeServiceJob(sim.state().maintenancePrograms.front()), "partial repair checkpoint active");
    require(sim.execute(SuspendMaintenanceProgramCommand{MaintenanceProgramId{1}}).ok,
            "provider suspend withdraws claim");
    roundTrip(sim.state(), 10, temp);
    require(sim.execute(ResumeMaintenanceProgramCommand{MaintenanceProgramId{1}}).ok,
            "provider resumes actual remaining condition");
    roundTrip(sim.state(), 60, temp);
    auto limited = started(true);
    auto change = maintenanceAmendmentFromCharter(limited.state().maintenancePrograms.front().charter);
    change.policy.lifetimeAllowances->set(ProcessedMaterial::Electronics, 2.5);
    require(limited.execute(AmendMaintenanceProgramCommand{MaintenanceProgramId{1}, change}).ok,
            "finite allowance accepted");
    const auto interrupted = limited.advanceDaysDetailed(30);
    require(interrupted.interrupted &&
                std::holds_alternative<MaintenanceProgramId>(*interrupted.issueProgramId),
            "maintenance issue interrupts with typed identity");
    roundTrip(limited.state(), 20, temp);
    require(limited
                .execute(AcknowledgeMaintenanceIssueCommand{
                    MaintenanceProgramId{1}, limited.state().maintenancePrograms.front().issue.signature})
                .ok,
            "issue ack accepted");
    roundTrip(limited.state(), 10, temp);
    auto state = started().state();
    state.ships.front().equipmentCondition.front().usedDuty = 8;
    state.surveyPrograms.front().charter.policy.maintenanceProgramId.reset();
    Simulation detour(state);
    require(detour.advanceDaysDetailed(100).interrupted, "unassisted partial visit exhausts duty");
    roundTrip(detour.state(), 5, temp);
    auto survey = detour.state().surveyPrograms.front().charter;
    survey.policy.maintenanceProgramId = MaintenanceProgramId{1};
    require(detour.execute(AmendSurveyProgramCommand{SurveyProgramId{1}, survey}).ok,
            "later support enables recovery");
    detour.advanceDays(1);
    require(detour.state().surveyPrograms.front().maintenanceReturn, "actual maintenance return retained");
    roundTrip(detour.state(), 100, temp);
}
void malformed_and_ordered_current_data() {
    Temp temp;
    auto sim = started(true);
    sim.advanceDays(2);
    const auto base = temp.path / "valid.sqlite";
    SaveGameRepository::save(base, sim.state());
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"missing-condition", "DELETE FROM ship_equipment_condition;"},
        {"condition-range", "UPDATE ship_equipment_condition SET used_duty=1000;"},
        {"condition-type", "UPDATE ship_equipment_condition SET used_duty='bad';"},
        {"profile-missing", "PRAGMA foreign_keys=OFF; DELETE FROM equipment_service_materials; DELETE FROM "
                            "equipment_service_profiles;"},
        {"profile-nonfinite", "UPDATE equipment_service_profiles SET duty_capacity=1e999;"},
        {"recipe-missing", "DELETE FROM equipment_service_materials WHERE material=1;"},
        {"qualification-gap", "UPDATE maintenance_team_qualifications SET ordinal=ordinal+2;"},
        {"target-gap", "UPDATE maintenance_job_targets SET ordinal=ordinal+2;"},
        {"target-debt", "UPDATE maintenance_job_targets SET initial_used_duty=9;"},
        {"work-quantity", "UPDATE maintenance_work_receipts SET quantity=2;"},
        {"work-material", "UPDATE maintenance_work_materials SET amount=amount+1 WHERE material=1;"},
        {"work-revision", "UPDATE maintenance_work_receipts SET provider_revision=999;"},
        {"counter", "UPDATE maintenance_programs SET team_work=200;"},
        {"released-job",
         "UPDATE maintenance_programs SET lifecycle=1,leased_tender_id=NULL,leased_team_id=NULL;"},
        {"client-release", "UPDATE survey_programs SET leased_fleet_id=NULL,leased_team_id=NULL;"},
        {"support-missing", "DELETE FROM survey_service_policy;"},
        {"ordinal-type", "PRAGMA ignore_check_constraints=ON; UPDATE maintenance_programs SET ordinal=0.5;"}};
    for (const auto& [label, sql] : cases) {
        const auto file = temp.path / (label + ".sqlite");
        std::filesystem::copy_file(base, file);
        {
            Database db(file);
            db.execute(sql);
        }
        SimulationService service(sim.state());
        const auto before = fingerprint(service.state());
        require(!service.loadGame(file).ok, "malformed current save rejected: " + label);
        require(fingerprint(service.state()) == before,
                "failed load preserves current service world: " + label);
    }
    auto ordered = sim.state();
    std::reverse(ordered.equipmentFamilies.begin(), ordered.equipmentFamilies.end());
    std::reverse(ordered.shipComponents.begin(), ordered.shipComponents.end());
    ordered.maintenanceTeams.front().qualifiedFamilies = {EquipmentFamilyId{2}, EquipmentFamilyId{1}};
    roundTrip(ordered, 80, temp);
}

void multiple_groups_and_mixed_delivery_checkpoints() {
    Temp temp;
    auto state = started(true).state();
    state.shipClasses.front().components.push_back({ShipComponentId{7}, 1});
    state.ships.front().equipmentCondition.clear();
    initializeShipEquipmentCondition(state, state.ships.front());
    state.ships.front().equipmentCondition[0].usedDuty = 1;
    state.ships.front().equipmentCondition[1].usedDuty = 10;
    state.shipClasses.back().components.push_back({ShipComponentId{9}, 1});
    state.maintenanceTeams.front().qualifiedFamilies = {EquipmentFamilyId{2}, EquipmentFamilyId{1}};
    Simulation groups(state);
    groups.advanceDays(2);
    require(activeServiceJob(groups.state().maintenancePrograms.front()) &&
                !groups.state().maintenancePrograms.front().jobs.back().activeTarget,
            "completed first group retains unfinished full job without a pinned next workshop");
    roundTrip(groups.state(), 120, temp);
    groups.advanceDays(1);
    roundTrip(groups.state(), 120, temp);

    Simulation supply(createMaintenanceSupplyScenario());
    const auto& s = supply.state();
    const auto home = s.colonies.at(s.colonies.size() - 2).id;
    MaintenanceProgramCharter p;
    p.name = "Supply continuation";
    p.serviceColonyId = home;
    p.requestedTenderId = s.fleets.at(1).id;
    p.requestedTeamId = s.maintenanceTeams.front().id;
    p.requestedLeaderId = s.people.front().id;
    p.clients = {s.fleets.front().id};
    require(supply.execute(CreateMaintenanceProgramCommand{p}).ok, "supply provider accepted");
    SurveyProgramCharter c;
    c.name = "Supply client";
    c.homeColonyId = home;
    c.requestedFleetId = s.fleets.front().id;
    c.requestedTeamId = s.surveyTeams.front().id;
    c.requestedLeaderId = s.people.front().id;
    c.targets = {{s.bodies.back().id, 0, 6}};
    c.policy.maintenanceProgramId = MaintenanceProgramId{1};
    require(supply.execute(CreateSurveyProgramCommand{c}).ok, "supply client accepted");
    for (int i = 0; i < 2; ++i) {
        FreightProgramCharter f;
        f.name = "Parts continuation";
        f.sourceColonyId = s.colonies.back().id;
        f.destinationColonyId = home;
        f.totalQuantity = 20;
        f.material = i == 0 ? ProcessedMaterial::Electronics : ProcessedMaterial::IndustrialComposites;
        f.requestedFleetId = s.fleets.at(static_cast<std::size_t>(i) + 2).id;
        f.requestedLeaderId = s.people.front().id;
        require(supply.execute(CreateFreightProgramCommand{f}).ok, "supply freight accepted");
    }
    supply.advanceDays(2);
    require(supply.state().maintenancePrograms.front().receipts.empty(),
            "missing parts hold current service");
    roundTrip(supply.state(), 120, temp);
    for (int i = 0; i < 60 && supply.state().freightPrograms.front().cargoDelivered == 0; ++i)
        supply.advanceDays(1);
    require(supply.state().freightPrograms.front().cargoDelivered > 0 &&
                supply.state().maintenancePrograms.front().receipts.empty(),
            "capture actual incoming stock before next-opening repair");
    roundTrip(supply.state(), 120, temp);
}
void report_boundary_commands_keep_published_history() {
    // A command after the day-30 publication still carries integer day30. Its
    // withdrawal belongs to the next audit interval, not a rewritten report.
    Temp temp;
    auto sim = started(true, 28);
    sim.advanceDays(2);
    const auto first = sim.state().maintenancePrograms.front().reports.front();
    require(first.endDay == 30 && first.jobsWithdrawn == 0 &&
                activeServiceJob(sim.state().maintenancePrograms.front()),
            "partial service and due report share day30");
    require(sim.execute(SuspendMaintenanceProgramCommand{MaintenanceProgramId{1}}).ok,
            "post-publication command can withdraw active service");
    require(sim.state().maintenancePrograms.front().reports.front().jobsWithdrawn == 0,
            "published report remains unchanged after later same-date command");
    roundTrip(sim.state(), 30, temp);
    sim.advanceDays(30);
    const auto& reports = sim.state().maintenancePrograms.front().reports;
    require(reports.size() == 2 && reports.back().jobsWithdrawn == 1 &&
                reports.front().auditThroughId == first.auditThroughId,
            "next report accounts for the later withdrawal exactly once through audit cutoff");
    roundTrip(sim.state(), 30, temp);
    const auto path = temp.path / "bad-cutoff.sqlite";
    SaveGameRepository::save(path, sim.state());
    {
        Database db(path);
        db.execute("UPDATE maintenance_reports SET audit_through_id=0 WHERE ordinal=0;");
    }
    bool rejected = false;
    try {
        (void)SaveGameRepository::load(path);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "malformed publication cutoff cannot erase historical outcomes");
}
} // namespace
int main() {
    try {
        physical_checkpoints_and_policy_history();
        stopped_jobs_issues_and_detours();
        malformed_and_ordered_current_data();
        multiple_groups_and_mixed_delivery_checkpoints();
        report_boundary_commands_keep_published_history();
        std::cout << "Maintenance save/continuation tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
