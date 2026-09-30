// Explicit v18-only P5 tables. Technical evidence and physical prototype state
// are normalized separately from ordinary ship-component/profile catalogs.
#include "save/TechnicalPersistence.h"

namespace deep::save {

void createTechnicalSchema(Database& db) {
    db.execute(R"sql(
CREATE TABLE technology_opportunities(
 ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0),
 id INTEGER PRIMARY KEY CHECK(typeof(id)='integer' AND id>0), name TEXT NOT NULL,
 baseline_component_id INTEGER NOT NULL REFERENCES ship_components(id), target REAL NOT NULL CHECK(target>0),
 access INTEGER NOT NULL CHECK(access IN(0,1)), objective TEXT NOT NULL, tradeoff TEXT NOT NULL);
CREATE TABLE technology_candidate_truth(
 ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0),
 opportunity_id INTEGER PRIMARY KEY REFERENCES technology_opportunities(id), achieved REAL NOT NULL CHECK(achieved>0));
CREATE TABLE technical_facilities(
 ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal)='integer' AND ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 colony_id INTEGER NOT NULL REFERENCES colonies(id), name TEXT NOT NULL, rate REAL NOT NULL CHECK(rate>=0),
 capability INTEGER NOT NULL CHECK(capability=0));
CREATE TABLE maintenance_team_engineering_qualifications(
 team_id INTEGER NOT NULL REFERENCES maintenance_teams(id), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 qualification INTEGER NOT NULL CHECK(qualification=0), PRIMARY KEY(team_id,ordinal), UNIQUE(team_id,qualification));
CREATE TABLE technical_development_programs(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0), name TEXT NOT NULL,
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), colony_id INTEGER NOT NULL REFERENCES colonies(id),
 requested_facility_id INTEGER REFERENCES technical_facilities(id), requested_team_id INTEGER REFERENCES maintenance_teams(id),
 requested_leader_id INTEGER REFERENCES people(id), scope INTEGER NOT NULL CHECK(scope BETWEEN 0 AND 2),
 created_day INTEGER NOT NULL CHECK(created_day>=0), revision INTEGER NOT NULL CHECK(revision>0),
 lifecycle INTEGER NOT NULL CHECK(lifecycle BETWEEN 0 AND 2), closure INTEGER NOT NULL CHECK(closure BETWEEN 0 AND 2),
 closed_day INTEGER, stage INTEGER NOT NULL CHECK(stage BETWEEN 0 AND 5), stage_work REAL NOT NULL CHECK(stage_work>=0),
 leased_team_id INTEGER REFERENCES maintenance_teams(id), report_start INTEGER NOT NULL CHECK(report_start>=0),
 next_report INTEGER NOT NULL CHECK(next_report>0), issue_signature TEXT NOT NULL, issue_message TEXT NOT NULL,
 issue_ack INTEGER NOT NULL CHECK(issue_ack IN(0,1)));
CREATE TABLE technical_program_materials(
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), category INTEGER NOT NULL CHECK(category BETWEEN 0 AND 2),
 material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5), amount REAL,
 PRIMARY KEY(program_id,category,material));
CREATE TABLE technical_work_receipts(
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 sequence INTEGER NOT NULL CHECK(sequence>0), day INTEGER NOT NULL CHECK(day>=0), revision INTEGER NOT NULL CHECK(revision>0),
 stage INTEGER NOT NULL CHECK(stage BETWEEN 0 AND 4), facility_id INTEGER NOT NULL REFERENCES technical_facilities(id),
 team_id INTEGER NOT NULL REFERENCES maintenance_teams(id), leader_id INTEGER NOT NULL REFERENCES people(id),
 work REAL NOT NULL CHECK(work>0), PRIMARY KEY(program_id,ordinal), UNIQUE(program_id,sequence));
CREATE TABLE technical_work_materials(
 program_id INTEGER NOT NULL, receipt_ordinal INTEGER NOT NULL, material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
 amount REAL NOT NULL CHECK(amount>=0), PRIMARY KEY(program_id,receipt_ordinal,material),
 FOREIGN KEY(program_id,receipt_ordinal) REFERENCES technical_work_receipts(program_id,ordinal));
CREATE TABLE technical_development_reports(
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 start_day INTEGER NOT NULL, end_day INTEGER NOT NULL, review INTEGER NOT NULL CHECK(review IN(0,1)),
 revision INTEGER NOT NULL, scope INTEGER NOT NULL CHECK(scope BETWEEN 0 AND 2), stage INTEGER NOT NULL CHECK(stage BETWEEN 0 AND 5),
 facility_id INTEGER REFERENCES technical_facilities(id), team_id INTEGER REFERENCES maintenance_teams(id), leader_id INTEGER REFERENCES people(id),
 period_work REAL NOT NULL CHECK(period_work>=0), lifetime_work REAL NOT NULL CHECK(lifetime_work>=0),
 prototype_id INTEGER, test_count INTEGER NOT NULL CHECK(test_count>=0), demonstrated_threshold REAL,
 component_id INTEGER REFERENCES ship_components(id), production_ready INTEGER NOT NULL CHECK(production_ready IN(0,1)),
 support_ready INTEGER NOT NULL CHECK(support_ready IN(0,1)), waiting TEXT NOT NULL, audit_through INTEGER NOT NULL CHECK(audit_through>=0),
 PRIMARY KEY(program_id,ordinal));
CREATE TABLE technical_report_materials(
 program_id INTEGER NOT NULL, report_ordinal INTEGER NOT NULL, category INTEGER NOT NULL CHECK(category BETWEEN 0 AND 1),
 material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5), amount REAL NOT NULL CHECK(amount>=0),
 PRIMARY KEY(program_id,report_ordinal,category,material),
 FOREIGN KEY(program_id,report_ordinal) REFERENCES technical_development_reports(program_id,ordinal));
CREATE TABLE prototype_designs(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), program_id INTEGER NOT NULL REFERENCES technical_development_programs(id),
 created_day INTEGER NOT NULL, name TEXT NOT NULL, mass REAL NOT NULL, volume REAL NOT NULL, power REAL NOT NULL,
 survey REAL NOT NULL, bp REAL NOT NULL, family_id INTEGER NOT NULL REFERENCES equipment_families(id), duty REAL NOT NULL,
 labor REAL NOT NULL);
CREATE TABLE prototype_design_materials(
 design_id INTEGER NOT NULL REFERENCES prototype_designs(id), category INTEGER NOT NULL CHECK(category BETWEEN 0 AND 1),
 material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5), amount REAL NOT NULL CHECK(amount>=0),
 PRIMARY KEY(design_id,category,material));
CREATE TABLE prototype_component_units(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), design_id INTEGER NOT NULL REFERENCES prototype_designs(id),
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), colony_id INTEGER NOT NULL REFERENCES colonies(id),
 fabrication_day INTEGER NOT NULL, state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 2), component_id INTEGER REFERENCES ship_components(id),
 available_day INTEGER, reserved_order_id INTEGER REFERENCES shipyard_orders(id), reserved_hull INTEGER,
 consumed_ship_id INTEGER REFERENCES ships(id));
CREATE TABLE technical_test_records(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), prototype_id INTEGER NOT NULL REFERENCES prototype_component_units(id),
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), facility_id INTEGER NOT NULL REFERENCES technical_facilities(id),
 team_id INTEGER NOT NULL REFERENCES maintenance_teams(id), leader_id INTEGER NOT NULL REFERENCES people(id),
 sequence INTEGER NOT NULL CHECK(sequence>0), day INTEGER NOT NULL, measured REAL NOT NULL CHECK(measured>0),
 target REAL NOT NULL CHECK(target>0), meets INTEGER NOT NULL CHECK(meets IN(0,1)), limitation TEXT NOT NULL);
CREATE TABLE developed_component_revisions(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), design_id INTEGER NOT NULL REFERENCES prototype_designs(id),
 prototype_id INTEGER NOT NULL REFERENCES prototype_component_units(id), profile_id INTEGER NOT NULL REFERENCES measurement_profiles(id),
 component_id INTEGER NOT NULL UNIQUE REFERENCES ship_components(id), demonstrated_day INTEGER NOT NULL, available_day INTEGER NOT NULL);
CREATE TABLE developed_component_tests(
 revision_id INTEGER NOT NULL REFERENCES developed_component_revisions(id), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 test_id INTEGER NOT NULL UNIQUE REFERENCES technical_test_records(id), PRIMARY KEY(revision_id,ordinal));
CREATE TABLE component_production_capabilities(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 component_id INTEGER NOT NULL REFERENCES ship_components(id), opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id),
 colony_id INTEGER NOT NULL REFERENCES colonies(id), facility_id INTEGER NOT NULL REFERENCES technical_facilities(id),
 program_id INTEGER NOT NULL REFERENCES technical_development_programs(id), qualified_day INTEGER NOT NULL, available_day INTEGER NOT NULL,
 UNIQUE(component_id,colony_id));
CREATE TABLE support_qualifications(
 ordinal INTEGER NOT NULL UNIQUE CHECK(ordinal>=0), id INTEGER PRIMARY KEY CHECK(id>0),
 opportunity_id INTEGER NOT NULL REFERENCES technology_opportunities(id), team_id INTEGER NOT NULL REFERENCES maintenance_teams(id),
 family_id INTEGER NOT NULL REFERENCES equipment_families(id), program_id INTEGER NOT NULL REFERENCES technical_development_programs(id),
 qualified_day INTEGER NOT NULL, available_day INTEGER NOT NULL, UNIQUE(team_id,family_id));
CREATE TABLE shipyard_current_supply_plans(
 order_id INTEGER PRIMARY KEY REFERENCES shipyard_orders(id), class_id INTEGER NOT NULL REFERENCES ship_classes(id),
 hull_number INTEGER NOT NULL CHECK(hull_number>0), effective_bp REAL NOT NULL CHECK(effective_bp>0), bound_day INTEGER NOT NULL);
CREATE TABLE shipyard_supply_plan_costs(
 order_id INTEGER NOT NULL REFERENCES shipyard_current_supply_plans(order_id), material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
 amount REAL NOT NULL CHECK(amount>=0), PRIMARY KEY(order_id,material));
CREATE TABLE shipyard_developed_supplies(
 order_id INTEGER NOT NULL REFERENCES shipyard_current_supply_plans(order_id), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 component_id INTEGER NOT NULL REFERENCES ship_components(id), quantity INTEGER NOT NULL CHECK(quantity>0),
 kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 1), PRIMARY KEY(order_id,ordinal));
CREATE TABLE shipyard_supply_prototypes(
 order_id INTEGER NOT NULL, supply_ordinal INTEGER NOT NULL, ordinal INTEGER NOT NULL CHECK(ordinal>=0),
 prototype_id INTEGER NOT NULL UNIQUE REFERENCES prototype_component_units(id), PRIMARY KEY(order_id,supply_ordinal,ordinal),
 FOREIGN KEY(order_id,supply_ordinal) REFERENCES shipyard_developed_supplies(order_id,ordinal));
CREATE TABLE prototype_integration_receipts(
 ordinal INTEGER PRIMARY KEY CHECK(ordinal>=0), prototype_id INTEGER NOT NULL UNIQUE REFERENCES prototype_component_units(id),
 order_id INTEGER NOT NULL REFERENCES shipyard_orders(id), hull_number INTEGER NOT NULL CHECK(hull_number>0),
 ship_id INTEGER NOT NULL REFERENCES ships(id), day INTEGER NOT NULL);
)sql");
}

void clearTechnicalState(Database& db) {
    db.execute(R"sql(
DELETE FROM prototype_integration_receipts;
DELETE FROM shipyard_supply_prototypes;
DELETE FROM shipyard_developed_supplies;
DELETE FROM shipyard_supply_plan_costs;
DELETE FROM shipyard_current_supply_plans;
DELETE FROM support_qualifications;
DELETE FROM component_production_capabilities;
DELETE FROM developed_component_tests;
DELETE FROM developed_component_revisions;
DELETE FROM technical_test_records;
DELETE FROM prototype_component_units;
DELETE FROM prototype_design_materials;
DELETE FROM prototype_designs;
DELETE FROM technical_report_materials;
DELETE FROM technical_development_reports;
DELETE FROM technical_work_materials;
DELETE FROM technical_work_receipts;
DELETE FROM technical_program_materials;
DELETE FROM technical_development_programs;
DELETE FROM maintenance_team_engineering_qualifications;
DELETE FROM technical_facilities;
DELETE FROM technology_candidate_truth;
DELETE FROM technology_opportunities;
)sql");
}

} // namespace deep::save
