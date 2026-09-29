// Ordered v15-only snapshots for service data. Every value is parameter-bound;
// dense material vectors and declared managed profiles must be complete on read.
#include "save/MaintenancePersistence.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace deep::save {
namespace {
void reuse(Statement& s) {
    s.reset();
    s.clearBindings();
}
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::int64_t ordinal(std::size_t n) {
    require(n <= static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()),
            "Service ordinal overflow");
    return static_cast<std::int64_t>(n);
}
int integer(const Statement& s, int c) {
    const auto value = s.columnInt64Strict(c);
    require(value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max(),
            "Service integer out of range");
    return static_cast<int>(value);
}
bool boolean(const Statement& s, int c) {
    const int value = integer(s, c);
    require(value == 0 || value == 1, "Service boolean invalid");
    return value != 0;
}
template <class T> void optional(Statement& s, int c, const std::optional<T>& value) {
    if (!value)
        s.bindNull(c);
    else if constexpr (requires { value->value; })
        s.bindInt64(c, value->value);
    else
        s.bindInt64(c, *value);
}
template <class T> std::optional<T> optionalId(const Statement& s, int c) {
    return s.columnIsNull(c) ? std::nullopt : std::optional{T{s.columnInt64Strict(c)}};
}
template <class T, class Id> T& byId(std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.id == id; });
    require(it != rows.end(), "Service child references unknown parent");
    return *it;
}
void next(const Statement& s, int c, std::size_t index) {
    require(s.columnInt64Strict(c) == ordinal(index), "Missing, duplicated or gapped service ordinal");
}
// These helpers accept only fixed SQL supplied below. Material identities must
// enumerate the complete existing ProcessedMaterial set, including zero rows.
void saveAmounts(Database& db, const char* sql, std::initializer_list<std::int64_t> keys,
                 const ProcessedMaterialSet& amounts) {
    Statement s(db, sql);
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        int col = 1;
        for (auto key : keys)
            s.bindInt64(col++, key);
        s.bindInt64(col++, ordinal(j));
        s.bindDouble(col, amounts.amount[j]);
        s.execute();
        reuse(s);
    }
}
void loadAmounts(Database& db, const char* sql, std::initializer_list<std::int64_t> keys,
                 ProcessedMaterialSet& amounts) {
    Statement s(db, sql);
    int col = 1;
    for (auto key : keys)
        s.bindInt64(col++, key);
    std::size_t index = 0;
    while (s.step()) {
        next(s, 0, index);
        require(index < processedMaterialCount(), "Extra service material");
        amounts.amount[index++] = s.columnDoubleStrict(1);
    }
    require(index == processedMaterialCount(), "Missing service material rows");
}
void savePolicy(Database& db, const char* sql, std::initializer_list<std::int64_t> keys,
                const MaintenanceSupplyPolicy& policy, const ProcessedMaterialSet& consumed) {
    Statement s(db, sql);
    for (std::size_t j = 0; j < processedMaterialCount(); ++j) {
        int col = 1;
        for (auto key : keys)
            s.bindInt64(col++, key);
        s.bindInt64(col++, ordinal(j));
        s.bindDouble(col++, policy.floors.amount[j]);
        if (policy.lifetimeAllowances)
            s.bindDouble(col++, policy.lifetimeAllowances->amount[j]);
        else
            s.bindNull(col++);
        s.bindDouble(col, consumed.amount[j]);
        s.execute();
        reuse(s);
    }
}
void loadPolicy(Database& db, const char* sql, std::initializer_list<std::int64_t> keys,
                MaintenanceSupplyPolicy& policy, ProcessedMaterialSet& consumed) {
    Statement s(db, sql);
    int col = 1;
    for (auto key : keys)
        s.bindInt64(col++, key);
    std::size_t index = 0;
    while (s.step()) {
        next(s, 0, index);
        require(index < processedMaterialCount(), "Extra policy material");
        policy.floors.amount[index] = s.columnDoubleStrict(1);
        require(s.columnIsNull(2) != policy.lifetimeAllowances.has_value(),
                "Allowance presence disagrees with declared policy");
        if (policy.lifetimeAllowances)
            policy.lifetimeAllowances->amount[index] = s.columnDoubleStrict(2);
        consumed.amount[index++] = s.columnDoubleStrict(3);
    }
    require(index == processedMaterialCount(), "Missing maintenance policy/material rows");
}
} // namespace

void saveMaintenanceState(Database& db, const GameState& state) {
    Statement family(db, "INSERT INTO equipment_families VALUES(?,?,?);");
    for (std::size_t i = 0; i < state.equipmentFamilies.size(); ++i) {
        const auto& r = state.equipmentFamilies[i];
        family.bindInt64(1, r.id.value);
        family.bindInt64(2, ordinal(i));
        family.bindText(3, r.name);
        family.execute();
        reuse(family);
    }
    Statement profile(db, "INSERT INTO equipment_service_profiles VALUES(?,?,?,?);");
    Statement workshop(db, "INSERT INTO workshop_family_rates VALUES(?,?,?,?);");
    for (const auto& component : state.shipComponents) {
        if (component.serviceProfile) {
            const auto& r = *component.serviceProfile;
            profile.bindInt64(1, component.id.value);
            profile.bindInt64(2, r.familyId.value);
            profile.bindDouble(3, r.dutyCapacity);
            profile.bindDouble(4, r.teamWorkdaysPerDuty);
            profile.execute();
            reuse(profile);
            saveAmounts(db, "INSERT INTO equipment_service_materials VALUES(?,?,?);", {component.id.value},
                        r.materialsPerDuty);
        }
        for (std::size_t i = 0; i < component.workshopRates.size(); ++i) {
            const auto& r = component.workshopRates[i];
            workshop.bindInt64(1, component.id.value);
            workshop.bindInt64(2, ordinal(i));
            workshop.bindInt64(3, r.familyId.value);
            workshop.bindDouble(4, r.teamWorkdaysPerDay);
            workshop.execute();
            reuse(workshop);
        }
    }
    Statement condition(db, "INSERT INTO ship_equipment_condition VALUES(?,?,?,?);");
    for (const auto& ship : state.ships)
        for (std::size_t i = 0; i < ship.equipmentCondition.size(); ++i) {
            const auto& r = ship.equipmentCondition[i];
            condition.bindInt64(1, ship.id.value);
            condition.bindInt64(2, ordinal(i));
            condition.bindInt64(3, r.componentId.value);
            condition.bindDouble(4, r.usedDuty);
            condition.execute();
            reuse(condition);
        }
    Statement team(db, "INSERT INTO maintenance_teams VALUES(?,?,?,?,?,?,?);");
    Statement qualification(db, "INSERT INTO maintenance_team_qualifications VALUES(?,?,?);");
    for (std::size_t i = 0; i < state.maintenanceTeams.size(); ++i) {
        const auto& r = state.maintenanceTeams[i];
        team.bindInt64(1, r.id.value);
        team.bindInt64(2, ordinal(i));
        team.bindText(3, r.name);
        team.bindDouble(4, r.workdaysPerDay);
        team.bindInt64(5, static_cast<int>(r.location));
        optional(team, 6, r.colonyId);
        optional(team, 7, r.fleetId);
        team.execute();
        reuse(team);
        for (std::size_t q = 0; q < r.qualifiedFamilies.size(); ++q) {
            qualification.bindInt64(1, r.id.value);
            qualification.bindInt64(2, ordinal(q));
            qualification.bindInt64(3, r.qualifiedFamilies[q].value);
            qualification.execute();
            reuse(qualification);
        }
    }
    Statement program(
        db, "INSERT INTO maintenance_programs VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement client(db, "INSERT INTO maintenance_program_clients VALUES(?,?,?);");
    Statement job(db, "INSERT INTO maintenance_jobs VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement target(db, "INSERT INTO maintenance_job_targets VALUES(?,?,?,?,?,?);");
    Statement receipt(db, "INSERT INTO maintenance_work_receipts VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    Statement report(db, "INSERT INTO maintenance_reports VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
    for (std::size_t i = 0; i < state.maintenancePrograms.size(); ++i) {
        const auto& p = state.maintenancePrograms[i];
        int c = 1;
        program.bindInt64(c++, p.id.value);
        program.bindInt64(c++, ordinal(i));
        program.bindText(c++, p.charter.name);
        program.bindInt64(c++, p.charter.serviceColonyId.value);
        optional(program, c++, p.charter.requestedTenderId);
        optional(program, c++, p.charter.requestedTeamId);
        optional(program, c++, p.charter.requestedLeaderId);
        program.bindInt64(c++, p.charter.policy.lifetimeAllowances ? 1 : 0);
        program.bindInt64(c++, p.createdDay);
        program.bindInt64(c++, p.charterRevision);
        program.bindInt64(c++, static_cast<int>(p.lifecycle));
        optional(program, c++, p.closedDay);
        optional(program, c++, p.leasedTenderId);
        optional(program, c++, p.leasedTeamId);
        program.bindInt64(c++, p.nextJobNumber);
        program.bindDouble(c++, p.restoredDuty);
        program.bindDouble(c++, p.teamWorkdays);
        program.bindInt64(c++, p.nextReportDay);
        program.bindInt64(c++, p.reportStartDay);
        program.bindText(c++, p.issue.signature);
        program.bindText(c++, p.issue.message);
        program.bindInt64(c++, p.issue.acknowledged ? 1 : 0);
        program.execute();
        reuse(program);
        for (std::size_t n = 0; n < p.charter.clients.size(); ++n) {
            client.bindInt64(1, p.id.value);
            client.bindInt64(2, ordinal(n));
            client.bindInt64(3, p.charter.clients[n].value);
            client.execute();
            reuse(client);
        }
        savePolicy(db, "INSERT INTO maintenance_program_materials VALUES(?,?,?,?,?);", {p.id.value},
                   p.charter.policy, p.consumed);
        for (std::size_t n = 0; n < p.jobs.size(); ++n) {
            const auto& r = p.jobs[n];
            c = 1;
            job.bindInt64(c++, p.id.value);
            job.bindInt64(c++, ordinal(n));
            job.bindInt64(c++, r.number);
            job.bindInt64(c++, r.surveyProgramId.value);
            job.bindInt64(c++, r.clientFleetId.value);
            job.bindInt64(c++, r.serviceColonyId.value);
            job.bindInt64(c++, r.tenderFleetId.value);
            job.bindInt64(c++, r.teamId.value);
            job.bindInt64(c++, r.leaderId.value);
            job.bindInt64(c++, r.providerRevision);
            job.bindInt64(c++, r.clientRevision);
            job.bindInt64(c++, r.startedDay);
            optional(job, c++, r.endedDay);
            job.bindInt64(c++, static_cast<int>(r.outcome));
            job.bindText(c++, r.endReason);
            optional(job, c++, r.activeTarget);
            optional(job, c++, r.workshopShipId);
            job.execute();
            reuse(job);
            for (std::size_t t = 0; t < r.targets.size(); ++t) {
                const auto& v = r.targets[t];
                target.bindInt64(1, p.id.value);
                target.bindInt64(2, r.number);
                target.bindInt64(3, ordinal(t));
                target.bindInt64(4, v.shipId.value);
                target.bindInt64(5, v.componentId.value);
                target.bindDouble(6, v.initialUsedDuty);
                target.execute();
                reuse(target);
            }
        }
        for (std::size_t n = 0; n < p.receipts.size(); ++n) {
            const auto& r = p.receipts[n];
            c = 1;
            receipt.bindInt64(c++, p.id.value);
            receipt.bindInt64(c++, ordinal(n));
            receipt.bindInt64(c++, r.sequence);
            receipt.bindInt64(c++, r.jobNumber);
            receipt.bindInt64(c++, r.day);
            receipt.bindInt64(c++, r.clientShipId.value);
            receipt.bindInt64(c++, r.componentId.value);
            receipt.bindInt64(c++, r.workshopShipId.value);
            receipt.bindInt64(c++, r.installationQuantity);
            receipt.bindDouble(c++, r.beforeUsedDuty);
            receipt.bindDouble(c++, r.afterUsedDuty);
            receipt.bindDouble(c++, r.restoredDuty);
            receipt.bindDouble(c++, r.teamWorkdays);
            receipt.bindInt64(c++, r.providerRevision);
            receipt.bindInt64(c++, r.clientRevision);
            receipt.execute();
            reuse(receipt);
            saveAmounts(db, "INSERT INTO maintenance_work_materials VALUES(?,?,?,?);",
                        {p.id.value, r.sequence}, r.consumed);
        }
        for (std::size_t n = 0; n < p.reports.size(); ++n) {
            const auto& r = p.reports[n];
            c = 1;
            report.bindInt64(c++, p.id.value);
            report.bindInt64(c++, ordinal(n));
            report.bindInt64(c++, r.startDay);
            report.bindInt64(c++, r.endDay);
            report.bindInt64(c++, r.isNinetyDayReview ? 1 : 0);
            report.bindInt64(c++, r.charterRevision);
            report.bindInt64(c++, r.jobsCompleted);
            report.bindInt64(c++, r.jobsWithdrawn);
            report.bindDouble(c++, r.restoredDuty);
            report.bindDouble(c++, r.teamWorkdays);
            report.bindInt64(c++, r.policy.lifetimeAllowances ? 1 : 0);
            report.bindDouble(c++, r.availableTeamRate);
            report.bindDouble(c++, r.compatibleWorkshopRate);
            report.bindText(c++, r.waitingReason);
            report.bindInt64(c++, r.auditThroughId);
            report.execute();
            reuse(report);
            savePolicy(db, "INSERT INTO maintenance_report_materials VALUES(?,?,?,?,?,?);",
                       {p.id.value, ordinal(n)}, r.policy, r.consumed);
        }
    }
    Statement support(db, "INSERT INTO survey_service_policy VALUES(?,?,?,?);");
    for (const auto& p : state.surveyPrograms) {
        support.bindInt64(1, p.id.value);
        optional(support, 2, p.charter.policy.maintenanceProgramId);
        support.bindDouble(3, p.charter.policy.remainingDutyTrigger);
        support.bindInt64(4, p.maintenanceReturn ? 1 : 0);
        support.execute();
        reuse(support);
    }
}

void loadMaintenanceState(Database& db, GameState& state) {
    Statement families(db, "SELECT id,ordinal,name FROM equipment_families ORDER BY ordinal;");
    while (families.step()) {
        next(families, 1, state.equipmentFamilies.size());
        state.equipmentFamilies.push_back(
            {EquipmentFamilyId{families.columnInt64Strict(0)}, families.columnText(2)});
    }
    Statement declared(db, "SELECT id,service_managed FROM ship_components ORDER BY ordinal;");
    while (declared.step()) {
        auto& component = byId(state.shipComponents, ShipComponentId{declared.columnInt64Strict(0)});
        Statement profile(db, "SELECT family_id,duty_capacity,team_work_per_duty FROM "
                              "equipment_service_profiles WHERE component_id=?;");
        profile.bindInt64(1, component.id.value);
        const bool exists = profile.step();
        require(exists == boolean(declared, 1), "Declared service profile is missing or unexpected");
        if (exists) {
            EquipmentServiceProfile p;
            p.familyId = EquipmentFamilyId{profile.columnInt64Strict(0)};
            p.dutyCapacity = profile.columnDoubleStrict(1);
            p.teamWorkdaysPerDuty = profile.columnDoubleStrict(2);
            require(!profile.step(), "Duplicate component service profile");
            loadAmounts(db,
                        "SELECT material,amount FROM equipment_service_materials WHERE component_id=? ORDER "
                        "BY material;",
                        {component.id.value}, p.materialsPerDuty);
            component.serviceProfile = p;
        }
        Statement rates(db, "SELECT ordinal,family_id,rate FROM workshop_family_rates WHERE component_id=? "
                            "ORDER BY ordinal;");
        rates.bindInt64(1, component.id.value);
        while (rates.step()) {
            next(rates, 0, component.workshopRates.size());
            component.workshopRates.push_back(
                {EquipmentFamilyId{rates.columnInt64Strict(1)}, rates.columnDoubleStrict(2)});
        }
    }
    for (auto& ship : state.ships) {
        Statement rows(db, "SELECT ordinal,component_id,used_duty FROM ship_equipment_condition WHERE "
                           "ship_id=? ORDER BY ordinal;");
        rows.bindInt64(1, ship.id.value);
        while (rows.step()) {
            next(rows, 0, ship.equipmentCondition.size());
            ship.equipmentCondition.push_back(
                {ShipComponentId{rows.columnInt64Strict(1)}, rows.columnDoubleStrict(2)});
        }
    }
    Statement teams(
        db,
        "SELECT id,ordinal,name,rate,location,colony_id,fleet_id FROM maintenance_teams ORDER BY ordinal;");
    while (teams.step()) {
        next(teams, 1, state.maintenanceTeams.size());
        MaintenanceTeam t;
        t.id = MaintenanceTeamId{teams.columnInt64Strict(0)};
        t.name = teams.columnText(2);
        t.workdaysPerDay = teams.columnDoubleStrict(3);
        t.location = static_cast<MaintenanceTeamLocation>(integer(teams, 4));
        t.colonyId = optionalId<ColonyId>(teams, 5);
        t.fleetId = optionalId<FleetId>(teams, 6);
        Statement q(db, "SELECT ordinal,family_id FROM maintenance_team_qualifications WHERE team_id=? ORDER "
                        "BY ordinal;");
        q.bindInt64(1, t.id.value);
        while (q.step()) {
            next(q, 0, t.qualifiedFamilies.size());
            t.qualifiedFamilies.push_back(EquipmentFamilyId{q.columnInt64Strict(1)});
        }
        state.maintenanceTeams.push_back(std::move(t));
    }
    Statement programs(
        db, "SELECT "
            "id,ordinal,name,service_colony_id,requested_tender_id,requested_team_id,requested_leader_id,has_"
            "allowances,created_day,charter_revision,lifecycle,closed_day,leased_tender_id,leased_team_id,"
            "next_job_number,restored_duty,team_work,next_report_day,report_start_day,issue_signature,issue_"
            "message,issue_acknowledged FROM maintenance_programs ORDER BY ordinal;");
    while (programs.step()) {
        next(programs, 1, state.maintenancePrograms.size());
        MaintenanceProgram p;
        p.id = MaintenanceProgramId{programs.columnInt64Strict(0)};
        p.charter.name = programs.columnText(2);
        p.charter.serviceColonyId = ColonyId{programs.columnInt64Strict(3)};
        p.charter.requestedTenderId = optionalId<FleetId>(programs, 4);
        p.charter.requestedTeamId = optionalId<MaintenanceTeamId>(programs, 5);
        p.charter.requestedLeaderId = optionalId<PersonId>(programs, 6);
        if (boolean(programs, 7))
            p.charter.policy.lifetimeAllowances = ProcessedMaterialSet{};
        p.createdDay = programs.columnInt64Strict(8);
        p.charterRevision = integer(programs, 9);
        p.lifecycle = static_cast<MaintenanceProgramLifecycle>(integer(programs, 10));
        if (!programs.columnIsNull(11))
            p.closedDay = programs.columnInt64Strict(11);
        p.leasedTenderId = optionalId<FleetId>(programs, 12);
        p.leasedTeamId = optionalId<MaintenanceTeamId>(programs, 13);
        p.nextJobNumber = integer(programs, 14);
        p.restoredDuty = programs.columnDoubleStrict(15);
        p.teamWorkdays = programs.columnDoubleStrict(16);
        p.nextReportDay = programs.columnInt64Strict(17);
        p.reportStartDay = programs.columnInt64Strict(18);
        p.issue = {programs.columnText(19), programs.columnText(20), boolean(programs, 21)};
        Statement clients(
            db,
            "SELECT ordinal,fleet_id FROM maintenance_program_clients WHERE program_id=? ORDER BY ordinal;");
        clients.bindInt64(1, p.id.value);
        while (clients.step()) {
            next(clients, 0, p.charter.clients.size());
            p.charter.clients.push_back(FleetId{clients.columnInt64Strict(1)});
        }
        loadPolicy(db,
                   "SELECT material,floor,allowance,consumed FROM maintenance_program_materials WHERE "
                   "program_id=? ORDER BY material;",
                   {p.id.value}, p.charter.policy, p.consumed);
        Statement jobs(
            db, "SELECT "
                "ordinal,number,survey_program_id,client_fleet_id,service_colony_id,tender_fleet_id,team_id,"
                "leader_id,provider_revision,client_revision,started_day,ended_day,outcome,end_reason,active_"
                "target,workshop_ship_id FROM maintenance_jobs WHERE program_id=? ORDER BY ordinal;");
        jobs.bindInt64(1, p.id.value);
        while (jobs.step()) {
            next(jobs, 0, p.jobs.size());
            ServiceJob j;
            j.number = integer(jobs, 1);
            j.surveyProgramId = SurveyProgramId{jobs.columnInt64Strict(2)};
            j.clientFleetId = FleetId{jobs.columnInt64Strict(3)};
            j.serviceColonyId = ColonyId{jobs.columnInt64Strict(4)};
            j.tenderFleetId = FleetId{jobs.columnInt64Strict(5)};
            j.teamId = MaintenanceTeamId{jobs.columnInt64Strict(6)};
            j.leaderId = PersonId{jobs.columnInt64Strict(7)};
            j.providerRevision = integer(jobs, 8);
            j.clientRevision = integer(jobs, 9);
            j.startedDay = jobs.columnInt64Strict(10);
            if (!jobs.columnIsNull(11))
                j.endedDay = jobs.columnInt64Strict(11);
            j.outcome = static_cast<ServiceJobOutcome>(integer(jobs, 12));
            j.endReason = jobs.columnText(13);
            if (!jobs.columnIsNull(14))
                j.activeTarget = integer(jobs, 14);
            j.workshopShipId = optionalId<ShipId>(jobs, 15);
            Statement targets(
                db, "SELECT ordinal,ship_id,component_id,initial_used_duty FROM maintenance_job_targets "
                    "WHERE program_id=? AND job_number=? ORDER BY ordinal;");
            targets.bindInt64(1, p.id.value);
            targets.bindInt64(2, j.number);
            while (targets.step()) {
                next(targets, 0, j.targets.size());
                j.targets.push_back({ShipId{targets.columnInt64Strict(1)},
                                     ShipComponentId{targets.columnInt64Strict(2)},
                                     targets.columnDoubleStrict(3)});
            }
            p.jobs.push_back(std::move(j));
        }
        Statement receipts(db,
                           "SELECT "
                           "ordinal,sequence,job_number,day,ship_id,component_id,workshop_ship_id,quantity,"
                           "before_duty,after_duty,restored_duty,team_work,provider_revision,client_revision "
                           "FROM maintenance_work_receipts WHERE program_id=? ORDER BY ordinal;");
        receipts.bindInt64(1, p.id.value);
        while (receipts.step()) {
            next(receipts, 0, p.receipts.size());
            MaintenanceWorkReceipt r;
            r.sequence = integer(receipts, 1);
            r.jobNumber = integer(receipts, 2);
            r.day = receipts.columnInt64Strict(3);
            r.clientShipId = ShipId{receipts.columnInt64Strict(4)};
            r.componentId = ShipComponentId{receipts.columnInt64Strict(5)};
            r.workshopShipId = ShipId{receipts.columnInt64Strict(6)};
            r.installationQuantity = integer(receipts, 7);
            r.beforeUsedDuty = receipts.columnDoubleStrict(8);
            r.afterUsedDuty = receipts.columnDoubleStrict(9);
            r.restoredDuty = receipts.columnDoubleStrict(10);
            r.teamWorkdays = receipts.columnDoubleStrict(11);
            r.providerRevision = integer(receipts, 12);
            r.clientRevision = integer(receipts, 13);
            loadAmounts(db,
                        "SELECT material,amount FROM maintenance_work_materials WHERE program_id=? AND "
                        "sequence=? ORDER BY material;",
                        {p.id.value, r.sequence}, r.consumed);
            p.receipts.push_back(std::move(r));
        }
        Statement reports(
            db,
            "SELECT "
            "ordinal,start_day,end_day,review,revision,jobs_completed,jobs_withdrawn,"
            "restored_duty,team_work,has_allowances,team_rate,workshop_rate,waiting_reason,audit_through_id "
            "FROM maintenance_reports WHERE program_id=? ORDER BY ordinal;");
        reports.bindInt64(1, p.id.value);
        while (reports.step()) {
            next(reports, 0, p.reports.size());
            MaintenanceProgramReport r;
            r.startDay = reports.columnInt64Strict(1);
            r.endDay = reports.columnInt64Strict(2);
            r.isNinetyDayReview = boolean(reports, 3);
            r.charterRevision = integer(reports, 4);
            r.jobsCompleted = integer(reports, 5);
            r.jobsWithdrawn = integer(reports, 6);
            r.restoredDuty = reports.columnDoubleStrict(7);
            r.teamWorkdays = reports.columnDoubleStrict(8);
            if (boolean(reports, 9))
                r.policy.lifetimeAllowances = ProcessedMaterialSet{};
            r.availableTeamRate = reports.columnDoubleStrict(10);
            r.compatibleWorkshopRate = reports.columnDoubleStrict(11);
            r.waitingReason = reports.columnText(12);
            r.auditThroughId = reports.columnInt64Strict(13);
            loadPolicy(db,
                       "SELECT material,floor,allowance,consumed FROM maintenance_report_materials WHERE "
                       "program_id=? AND report_ordinal=? ORDER BY material;",
                       {p.id.value, ordinal(p.reports.size())}, r.policy, r.consumed);
            p.reports.push_back(std::move(r));
        }
        state.maintenancePrograms.push_back(std::move(p));
    }
    for (auto& p : state.surveyPrograms) {
        Statement support(db, "SELECT provider_id,trigger_fraction,maintenance_return FROM "
                              "survey_service_policy WHERE program_id=?;");
        support.bindInt64(1, p.id.value);
        require(support.step(), "Missing survey support policy row");
        p.charter.policy.maintenanceProgramId = optionalId<MaintenanceProgramId>(support, 0);
        p.charter.policy.remainingDutyTrigger = support.columnDoubleStrict(1);
        p.maintenanceReturn = boolean(support, 2);
        require(!support.step(), "Duplicate survey support policy row");
    }
}
} // namespace deep::save
