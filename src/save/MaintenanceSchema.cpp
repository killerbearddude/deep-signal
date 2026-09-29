// v15-only relational shape for profiles, physical condition and service jobs.
// Parents are written before children; source files are never migrated/repaired.
#include "save/MaintenancePersistence.h"
namespace deep::save {
void createMaintenanceSchema(Database& db) {
    db.execute(R"sql(
CREATE TABLE equipment_families(
    id INTEGER PRIMARY KEY NOT NULL CHECK(id>0),
     ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     name TEXT NOT NULL CHECK(length(name)>0)
);
CREATE TABLE equipment_service_profiles(
    component_id INTEGER PRIMARY KEY NOT NULL REFERENCES ship_components(id),
     family_id INTEGER NOT NULL REFERENCES equipment_families(id),
     duty_capacity REAL NOT NULL CHECK(duty_capacity>0),
     team_work_per_duty REAL NOT NULL CHECK(team_work_per_duty>0)
);
CREATE TABLE equipment_service_materials(
    component_id INTEGER NOT NULL REFERENCES equipment_service_profiles(component_id),
     material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
     amount REAL NOT NULL CHECK(amount>=0),
     PRIMARY KEY(component_id,material)
);
CREATE TABLE workshop_family_rates(
    component_id INTEGER NOT NULL REFERENCES ship_components(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     family_id INTEGER NOT NULL REFERENCES equipment_families(id),
     rate REAL NOT NULL CHECK(rate>=0),
     PRIMARY KEY(component_id,ordinal),
     UNIQUE(component_id,family_id)
);
CREATE TABLE ship_equipment_condition(
    ship_id INTEGER NOT NULL REFERENCES ships(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     component_id INTEGER NOT NULL REFERENCES equipment_service_profiles(component_id),
     used_duty REAL NOT NULL CHECK(used_duty>=0),
     PRIMARY KEY(ship_id,ordinal),
     UNIQUE(ship_id,component_id)
);
CREATE TABLE maintenance_teams(
    id INTEGER PRIMARY KEY NOT NULL CHECK(id>0),
     ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     name TEXT NOT NULL CHECK(length(name)>0),
     rate REAL NOT NULL CHECK(rate>=0),
     location INTEGER NOT NULL CHECK(location IN(0,1)),
     colony_id INTEGER REFERENCES colonies(id),
     fleet_id INTEGER REFERENCES fleets(id),
     CHECK((location=0 AND colony_id IS NOT NULL AND fleet_id IS NULL) OR (location=1 AND colony_id IS NULL AND fleet_id IS NOT NULL))
);
CREATE TABLE maintenance_team_qualifications(
    team_id INTEGER NOT NULL REFERENCES maintenance_teams(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     family_id INTEGER NOT NULL REFERENCES equipment_families(id),
     PRIMARY KEY(team_id,ordinal),
     UNIQUE(team_id,family_id)
);
CREATE TABLE maintenance_programs(

 id INTEGER PRIMARY KEY NOT NULL CHECK(id>0),
     ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0),

 name TEXT NOT NULL CHECK(length(name)>0),
     service_colony_id INTEGER NOT NULL REFERENCES colonies(id),

 requested_tender_id INTEGER REFERENCES fleets(id),
     requested_team_id INTEGER REFERENCES maintenance_teams(id),
     requested_leader_id INTEGER REFERENCES people(id),

 has_allowances INTEGER NOT NULL CHECK(has_allowances IN(0,1)),
     created_day INTEGER NOT NULL CHECK(created_day>=0),
     charter_revision INTEGER NOT NULL CHECK(charter_revision>0),

 lifecycle INTEGER NOT NULL CHECK(lifecycle BETWEEN 0 AND 2),
     closed_day INTEGER CHECK(closed_day>=0),

 leased_tender_id INTEGER REFERENCES fleets(id),
     leased_team_id INTEGER REFERENCES maintenance_teams(id),
     next_job_number INTEGER NOT NULL CHECK(next_job_number>0),

 restored_duty REAL NOT NULL CHECK(restored_duty>=0),
     team_work REAL NOT NULL CHECK(team_work>=0),
     next_report_day INTEGER NOT NULL CHECK(next_report_day>0),
     report_start_day INTEGER NOT NULL CHECK(report_start_day>=0),

 issue_signature TEXT NOT NULL,
     issue_message TEXT NOT NULL,
     issue_acknowledged INTEGER NOT NULL CHECK(issue_acknowledged IN(0,1)),

 CHECK((leased_tender_id IS NULL)=(leased_team_id IS NULL))
);
CREATE TABLE maintenance_program_clients(
    program_id INTEGER NOT NULL REFERENCES maintenance_programs(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     fleet_id INTEGER NOT NULL REFERENCES fleets(id),
     PRIMARY KEY(program_id,ordinal),
     UNIQUE(program_id,fleet_id)
);
CREATE TABLE maintenance_program_materials(
    program_id INTEGER NOT NULL REFERENCES maintenance_programs(id),
     material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
     floor REAL NOT NULL CHECK(floor>=0),
     allowance REAL CHECK(allowance>=0),
     consumed REAL NOT NULL CHECK(consumed>=0),
     PRIMARY KEY(program_id,material)
);
CREATE TABLE survey_service_policy(
    program_id INTEGER PRIMARY KEY NOT NULL REFERENCES survey_programs(id),
     provider_id INTEGER REFERENCES maintenance_programs(id),
     trigger_fraction REAL NOT NULL CHECK(trigger_fraction BETWEEN 0 AND 1),
     maintenance_return INTEGER NOT NULL CHECK(maintenance_return IN(0,1))
);
CREATE TABLE maintenance_jobs(

 program_id INTEGER NOT NULL REFERENCES maintenance_programs(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     number INTEGER NOT NULL CHECK(number>0),

 survey_program_id INTEGER NOT NULL REFERENCES survey_programs(id),
     client_fleet_id INTEGER NOT NULL REFERENCES fleets(id),
     service_colony_id INTEGER NOT NULL REFERENCES colonies(id),
     tender_fleet_id INTEGER NOT NULL REFERENCES fleets(id),
     team_id INTEGER NOT NULL REFERENCES maintenance_teams(id),
     leader_id INTEGER NOT NULL REFERENCES people(id),

 provider_revision INTEGER NOT NULL CHECK(provider_revision>0),
     client_revision INTEGER NOT NULL CHECK(client_revision>0),
     started_day INTEGER NOT NULL CHECK(started_day>=0),
     ended_day INTEGER CHECK(ended_day>=0),
     outcome INTEGER NOT NULL CHECK(outcome BETWEEN 0 AND 2),
     end_reason TEXT NOT NULL,

 active_target INTEGER CHECK(active_target>=0),
     workshop_ship_id INTEGER REFERENCES ships(id),
     PRIMARY KEY(program_id,number),
     UNIQUE(program_id,ordinal),
     CHECK((active_target IS NULL)=(workshop_ship_id IS NULL))
);
CREATE TABLE maintenance_job_targets(
    program_id INTEGER NOT NULL,
     job_number INTEGER NOT NULL,
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     ship_id INTEGER NOT NULL REFERENCES ships(id),
     component_id INTEGER NOT NULL REFERENCES equipment_service_profiles(component_id),
     initial_used_duty REAL NOT NULL CHECK(initial_used_duty>0),
     PRIMARY KEY(program_id,job_number,ordinal),
     UNIQUE(program_id,job_number,ship_id,component_id),
     FOREIGN KEY(program_id,job_number) REFERENCES maintenance_jobs(program_id,number)
);
CREATE TABLE maintenance_work_receipts(

 program_id INTEGER NOT NULL REFERENCES maintenance_programs(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     sequence INTEGER NOT NULL CHECK(sequence>0),
     job_number INTEGER NOT NULL,
     day INTEGER NOT NULL CHECK(day>=0),

 ship_id INTEGER NOT NULL REFERENCES ships(id),
     component_id INTEGER NOT NULL REFERENCES equipment_service_profiles(component_id),
     workshop_ship_id INTEGER NOT NULL REFERENCES ships(id),
     quantity INTEGER NOT NULL CHECK(quantity>0),

 before_duty REAL NOT NULL CHECK(before_duty>0),
     after_duty REAL NOT NULL CHECK(after_duty>=0),
     restored_duty REAL NOT NULL CHECK(restored_duty>0),
     team_work REAL NOT NULL CHECK(team_work>0),
     provider_revision INTEGER NOT NULL CHECK(provider_revision>0),
     client_revision INTEGER NOT NULL CHECK(client_revision>0),

 PRIMARY KEY(program_id,sequence),
     UNIQUE(program_id,ordinal),
     FOREIGN KEY(program_id,job_number) REFERENCES maintenance_jobs(program_id,number)
);
CREATE TABLE maintenance_work_materials(
    program_id INTEGER NOT NULL,
     sequence INTEGER NOT NULL,
     material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
     amount REAL NOT NULL CHECK(amount>=0),
     PRIMARY KEY(program_id,sequence,material),
     FOREIGN KEY(program_id,sequence) REFERENCES maintenance_work_receipts(program_id,sequence)
);
CREATE TABLE maintenance_reports(

 program_id INTEGER NOT NULL REFERENCES maintenance_programs(id),
     ordinal INTEGER NOT NULL CHECK(typeof(ordinal)='integer' AND ordinal>=0),
     start_day INTEGER NOT NULL CHECK(start_day>=0),
     end_day INTEGER NOT NULL CHECK(end_day>=start_day),
     review INTEGER NOT NULL CHECK(review IN(0,1)),
     revision INTEGER NOT NULL CHECK(revision>0),
     jobs_completed INTEGER NOT NULL CHECK(jobs_completed>=0),
     jobs_withdrawn INTEGER NOT NULL CHECK(jobs_withdrawn>=0),
     restored_duty REAL NOT NULL CHECK(restored_duty>=0),
     team_work REAL NOT NULL CHECK(team_work>=0),
     has_allowances INTEGER NOT NULL CHECK(has_allowances IN(0,1)),
     team_rate REAL NOT NULL CHECK(team_rate>=0),
     workshop_rate REAL NOT NULL CHECK(workshop_rate>=0),
     waiting_reason TEXT NOT NULL,
     audit_through_id INTEGER NOT NULL CHECK(audit_through_id>=0),
     PRIMARY KEY(program_id,ordinal)
);
CREATE TABLE maintenance_report_materials(
    program_id INTEGER NOT NULL,
     report_ordinal INTEGER NOT NULL,
     material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
     floor REAL NOT NULL CHECK(floor>=0),
     allowance REAL CHECK(allowance>=0),
     consumed REAL NOT NULL CHECK(consumed>=0),
     PRIMARY KEY(program_id,report_ordinal,material),
     FOREIGN KEY(program_id,report_ordinal) REFERENCES maintenance_reports(program_id,ordinal)
);
    )sql");
}
void clearMaintenanceState(Database& db) {
    db.execute(R"sql(
DELETE FROM maintenance_report_materials;
DELETE FROM maintenance_reports;
DELETE FROM maintenance_work_materials;
DELETE FROM maintenance_work_receipts;
DELETE FROM maintenance_job_targets;
DELETE FROM maintenance_jobs;
DELETE FROM survey_service_policy;
DELETE FROM maintenance_program_materials;
DELETE FROM maintenance_program_clients;
DELETE FROM maintenance_programs;
DELETE FROM maintenance_team_qualifications;
DELETE FROM maintenance_teams;
DELETE FROM ship_equipment_condition;
DELETE FROM workshop_family_rates;
DELETE FROM equipment_service_materials;
DELETE FROM equipment_service_profiles;
DELETE FROM equipment_families;
    )sql");
}
} // namespace deep::save
