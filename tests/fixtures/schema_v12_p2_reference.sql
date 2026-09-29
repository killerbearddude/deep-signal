-- Valid v12 home-system save created with pinned 2f1385c0250c4a0b9b17a0c6280e777174ae58a1 save/sim libraries.
-- Includes real scenario rows; retained only to test clean unsupported-version rejection.
PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE appointments (
            ordinal INTEGER PRIMARY KEY NOT NULL CHECK(ordinal >= 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 5),
            scope_type INTEGER NOT NULL CHECK(scope_type BETWEEN 0 AND 2),
            scope_id INTEGER NOT NULL CHECK(scope_id > 0),
            person_id INTEGER NOT NULL CHECK(person_id > 0),
            appointed_day INTEGER NOT NULL CHECK(appointed_day >= 0),
            UNIQUE(role, scope_type, scope_id),
            FOREIGN KEY(person_id) REFERENCES people(id)
        );
INSERT INTO "appointments" VALUES(0,5,2,1,1,0);
INSERT INTO "appointments" VALUES(1,1,1,1,1,0);
INSERT INTO "appointments" VALUES(2,2,1,1,2,0);
INSERT INTO "appointments" VALUES(3,3,2,5,3,0);
INSERT INTO "appointments" VALUES(4,4,2,4,4,0);
CREATE TABLE bodies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            system_id INTEGER NOT NULL CHECK(system_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            body_type INTEGER NOT NULL CHECK(body_type BETWEEN 0 AND 4),
            strategic_zone INTEGER NOT NULL CHECK(strategic_zone BETWEEN 0 AND 4),
            parent_body_id INTEGER NULL CHECK(parent_body_id IS NULL OR parent_body_id > 0),
            orbital_radius_km REAL NOT NULL CHECK(orbital_radius_km >= 0.0),
            orbital_period_days REAL NOT NULL CHECK(orbital_period_days >= 0.0),
            phase_radians REAL NOT NULL,
            display_radius REAL NOT NULL CHECK(display_radius > 0.0),
            x REAL NOT NULL,
            y REAL NOT NULL,
            CHECK(parent_body_id IS NULL OR parent_body_id != id),
            FOREIGN KEY(system_id) REFERENCES star_systems(id)
        );
INSERT INTO "bodies" VALUES(1,0,1,'Terra',1,0,9,149600000.0,365.25,0.0,10.0,149.6,0.0);
INSERT INTO "bodies" VALUES(2,1,1,'Mars',1,1,9,227900000.0,687.0,1.32,8.0,56.6,220.7);
INSERT INTO "bodies" VALUES(3,2,1,'Luna Yard Complex',3,1,1,384400.0,27.3,0.75,5.0,150.0,0.3);
INSERT INTO "bodies" VALUES(4,3,1,'Ceres Extraction Hub',4,2,9,413700000.0,1682.0,2.05,5.0,-190.1,367.4);
INSERT INTO "bodies" VALUES(5,4,1,'Vesta Refinery Claim',4,2,9,353400000.0,1325.0,3.1,5.0,-353.1,14.7);
INSERT INTO "bodies" VALUES(6,5,1,'Pallas Survey Claim',4,2,9,414500000.0,1686.0,4.35,5.0,-146.8,-387.6);
INSERT INTO "bodies" VALUES(7,6,1,'Titan Fuel Depot',3,3,9,1433500000.0,10759.0,5.2,6.0,672.0,-1266.5);
INSERT INTO "bodies" VALUES(8,7,1,'Helios Far Survey Object',4,4,9,2600000000.0,20000.0,0.95,5.0,1510.0,2116.0);
INSERT INTO "bodies" VALUES(9,8,1,'Sun',0,0,NULL,0.0,0.0,0.0,14.0,0.0,0.0);
CREATE TABLE colonies (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            owner_institution_id INTEGER NULL CHECK(owner_institution_id IS NULL OR owner_institution_id > 0),
            mines REAL NOT NULL CHECK(mines >= 0.0),
            processor_capacity REAL NOT NULL CHECK(processor_capacity >= 0.0),
            shipyard_capacity REAL NOT NULL CHECK(shipyard_capacity >= 0.0),
            processing_policy INTEGER NOT NULL CHECK(processing_policy BETWEEN 0 AND 5),
            FOREIGN KEY(body_id) REFERENCES bodies(id),
            FOREIGN KEY(owner_institution_id) REFERENCES institutions(id)
        );
INSERT INTO "colonies" VALUES(1,0,1,'Terra Directorate',1,10.0,50.0,100.0,0);
INSERT INTO "colonies" VALUES(2,1,2,'Mars Naval Yards',2,4.0,25.0,60.0,1);
INSERT INTO "colonies" VALUES(3,2,4,'Ceres Belt Works',3,14.0,30.0,0.0,4);
INSERT INTO "colonies" VALUES(4,3,7,'Titan Fuel Depot',4,6.0,40.0,0.0,2);
CREATE TABLE colony_materials (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, material),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );
INSERT INTO "colony_materials" VALUES(1,0,1500.0);
INSERT INTO "colony_materials" VALUES(1,1,500.0);
INSERT INTO "colony_materials" VALUES(1,2,2000.0);
INSERT INTO "colony_materials" VALUES(1,3,200.0);
INSERT INTO "colony_materials" VALUES(1,4,700.0);
INSERT INTO "colony_materials" VALUES(1,5,300.0);
INSERT INTO "colony_materials" VALUES(2,0,900.0);
INSERT INTO "colony_materials" VALUES(2,1,250.0);
INSERT INTO "colony_materials" VALUES(2,2,600.0);
INSERT INTO "colony_materials" VALUES(2,3,0.0);
INSERT INTO "colony_materials" VALUES(2,4,300.0);
INSERT INTO "colony_materials" VALUES(2,5,0.0);
INSERT INTO "colony_materials" VALUES(3,0,400.0);
INSERT INTO "colony_materials" VALUES(3,1,0.0);
INSERT INTO "colony_materials" VALUES(3,2,0.0);
INSERT INTO "colony_materials" VALUES(3,3,0.0);
INSERT INTO "colony_materials" VALUES(3,4,150.0);
INSERT INTO "colony_materials" VALUES(3,5,0.0);
INSERT INTO "colony_materials" VALUES(4,0,0.0);
INSERT INTO "colony_materials" VALUES(4,1,0.0);
INSERT INTO "colony_materials" VALUES(4,2,4000.0);
INSERT INTO "colony_materials" VALUES(4,3,100.0);
INSERT INTO "colony_materials" VALUES(4,4,0.0);
INSERT INTO "colony_materials" VALUES(4,5,0.0);
CREATE TABLE colony_minerals (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(colony_id, mineral),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );
INSERT INTO "colony_minerals" VALUES(1,0,10000.0);
INSERT INTO "colony_minerals" VALUES(1,1,5000.0);
INSERT INTO "colony_minerals" VALUES(1,2,4000.0);
INSERT INTO "colony_minerals" VALUES(1,3,8000.0);
INSERT INTO "colony_minerals" VALUES(1,4,3000.0);
INSERT INTO "colony_minerals" VALUES(1,5,4000.0);
INSERT INTO "colony_minerals" VALUES(1,6,1000.0);
INSERT INTO "colony_minerals" VALUES(1,7,500.0);
INSERT INTO "colony_minerals" VALUES(1,8,500.0);
INSERT INTO "colony_minerals" VALUES(1,9,700.0);
INSERT INTO "colony_minerals" VALUES(1,10,200.0);
INSERT INTO "colony_minerals" VALUES(1,11,50000.0);
INSERT INTO "colony_minerals" VALUES(1,12,10000.0);
INSERT INTO "colony_minerals" VALUES(1,13,20000.0);
INSERT INTO "colony_minerals" VALUES(2,0,6000.0);
INSERT INTO "colony_minerals" VALUES(2,1,0.0);
INSERT INTO "colony_minerals" VALUES(2,2,3000.0);
INSERT INTO "colony_minerals" VALUES(2,3,4000.0);
INSERT INTO "colony_minerals" VALUES(2,4,0.0);
INSERT INTO "colony_minerals" VALUES(2,5,0.0);
INSERT INTO "colony_minerals" VALUES(2,6,0.0);
INSERT INTO "colony_minerals" VALUES(2,7,0.0);
INSERT INTO "colony_minerals" VALUES(2,8,0.0);
INSERT INTO "colony_minerals" VALUES(2,9,0.0);
INSERT INTO "colony_minerals" VALUES(2,10,0.0);
INSERT INTO "colony_minerals" VALUES(2,11,8000.0);
INSERT INTO "colony_minerals" VALUES(2,12,0.0);
INSERT INTO "colony_minerals" VALUES(2,13,0.0);
INSERT INTO "colony_minerals" VALUES(3,0,12000.0);
INSERT INTO "colony_minerals" VALUES(3,1,9000.0);
INSERT INTO "colony_minerals" VALUES(3,2,0.0);
INSERT INTO "colony_minerals" VALUES(3,3,0.0);
INSERT INTO "colony_minerals" VALUES(3,4,0.0);
INSERT INTO "colony_minerals" VALUES(3,5,0.0);
INSERT INTO "colony_minerals" VALUES(3,6,0.0);
INSERT INTO "colony_minerals" VALUES(3,7,0.0);
INSERT INTO "colony_minerals" VALUES(3,8,0.0);
INSERT INTO "colony_minerals" VALUES(3,9,0.0);
INSERT INTO "colony_minerals" VALUES(3,10,0.0);
INSERT INTO "colony_minerals" VALUES(3,11,20000.0);
INSERT INTO "colony_minerals" VALUES(3,12,4000.0);
INSERT INTO "colony_minerals" VALUES(3,13,0.0);
INSERT INTO "colony_minerals" VALUES(4,0,0.0);
INSERT INTO "colony_minerals" VALUES(4,1,0.0);
INSERT INTO "colony_minerals" VALUES(4,2,0.0);
INSERT INTO "colony_minerals" VALUES(4,3,0.0);
INSERT INTO "colony_minerals" VALUES(4,4,0.0);
INSERT INTO "colony_minerals" VALUES(4,5,0.0);
INSERT INTO "colony_minerals" VALUES(4,6,0.0);
INSERT INTO "colony_minerals" VALUES(4,7,0.0);
INSERT INTO "colony_minerals" VALUES(4,8,0.0);
INSERT INTO "colony_minerals" VALUES(4,9,0.0);
INSERT INTO "colony_minerals" VALUES(4,10,0.0);
INSERT INTO "colony_minerals" VALUES(4,11,80000.0);
INSERT INTO "colony_minerals" VALUES(4,12,15000.0);
INSERT INTO "colony_minerals" VALUES(4,13,60000.0);
CREATE TABLE colony_processing_allocations (
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            weight REAL NOT NULL CHECK(weight >= 0.0),
            PRIMARY KEY(colony_id, ordinal),
            FOREIGN KEY(colony_id) REFERENCES colonies(id)
        );
CREATE TABLE event_log (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            day INTEGER NOT NULL CHECK(day >= 0),
            severity INTEGER NOT NULL CHECK(severity BETWEEN 0 AND 2),
            event_type TEXT NOT NULL CHECK(length(event_type) > 0),
            payload_json TEXT NOT NULL CHECK(length(payload_json) > 0)
        );
CREATE TABLE fleet_order_queue (
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            order_type INTEGER NOT NULL CHECK(order_type = 1),
            target_body_id INTEGER NOT NULL CHECK(target_body_id > 0),
            PRIMARY KEY(fleet_id, ordinal),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id),
            FOREIGN KEY(target_body_id) REFERENCES bodies(id)
        );
CREATE TABLE fleets (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            owner_institution_id INTEGER NULL CHECK(owner_institution_id IS NULL OR owner_institution_id > 0),
            current_body_id INTEGER NOT NULL CHECK(current_body_id > 0),
            destination_body_id INTEGER NULL CHECK(destination_body_id IS NULL OR destination_body_id > 0),
            order_type INTEGER NOT NULL CHECK(order_type BETWEEN 0 AND 1),
            order_target_body_id INTEGER NULL CHECK(order_target_body_id IS NULL OR order_target_body_id > 0),
            order_days_remaining INTEGER NOT NULL CHECK(order_days_remaining >= 0),
            order_departure_body_id INTEGER NULL CHECK(order_departure_body_id IS NULL OR order_departure_body_id > 0),
            order_departure_day INTEGER NOT NULL CHECK(order_departure_day >= 0),
            order_arrival_day INTEGER NOT NULL CHECK(order_arrival_day >= 0),
            order_departure_x REAL NOT NULL,
            order_departure_y REAL NOT NULL,
            order_projected_arrival_x REAL NOT NULL,
            order_projected_arrival_y REAL NOT NULL,
            order_transit_distance_km REAL NOT NULL CHECK(order_transit_distance_km >= 0.0),
            order_burn_acceleration_g REAL NOT NULL CHECK(order_burn_acceleration_g >= 0.0),
            order_curve_control_x REAL NOT NULL,
            order_curve_control_y REAL NOT NULL,
            CHECK(
                (order_type = 0 AND destination_body_id IS NULL AND
                 order_target_body_id IS NULL AND order_days_remaining = 0 AND
                 order_departure_body_id IS NULL AND order_departure_day = 0 AND
                 order_arrival_day = 0 AND order_transit_distance_km = 0.0 AND
                 order_burn_acceleration_g = 0.0)
                OR
                (order_type = 1 AND destination_body_id IS NOT NULL AND
                 order_target_body_id IS NOT NULL AND
                 order_departure_body_id IS NOT NULL AND
                 destination_body_id = order_target_body_id AND
                 destination_body_id != current_body_id AND
                 order_days_remaining > 0 AND
                 order_arrival_day > order_departure_day AND
                 order_transit_distance_km > 0.0 AND
                 order_burn_acceleration_g > 0.0)
            ),
            FOREIGN KEY(owner_institution_id) REFERENCES institutions(id),
            FOREIGN KEY(current_body_id) REFERENCES bodies(id),
            FOREIGN KEY(destination_body_id) REFERENCES bodies(id),
            FOREIGN KEY(order_target_body_id) REFERENCES bodies(id)
        );
CREATE TABLE game_meta (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value TEXT NOT NULL
        );
INSERT INTO "game_meta" VALUES('current_day','0');
CREATE TABLE id_counters (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(key) > 0),
            value INTEGER NOT NULL CHECK(value > 0)
        );
INSERT INTO "id_counters" VALUES('next_star_system_id',2);
INSERT INTO "id_counters" VALUES('next_body_id',10);
INSERT INTO "id_counters" VALUES('next_colony_id',5);
INSERT INTO "id_counters" VALUES('next_institution_id',8);
INSERT INTO "id_counters" VALUES('next_person_id',5);
INSERT INTO "id_counters" VALUES('next_ship_class_id',2);
INSERT INTO "id_counters" VALUES('next_ship_component_id',6);
INSERT INTO "id_counters" VALUES('next_shipyard_order_id',1);
INSERT INTO "id_counters" VALUES('next_ship_id',1);
INSERT INTO "id_counters" VALUES('next_fleet_id',1);
INSERT INTO "id_counters" VALUES('next_event_id',1);
CREATE TABLE institutions (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            institution_type INTEGER NOT NULL CHECK(institution_type BETWEEN 0 AND 7)
        );
INSERT INTO "institutions" VALUES(1,0,'Strategic Continuity Office',7);
INSERT INTO "institutions" VALUES(2,1,'Naval Construction Board',1);
INSERT INTO "institutions" VALUES(3,2,'Belt Extraction Combine',2);
INSERT INTO "institutions" VALUES(4,3,'Outer Fuel Trust',3);
INSERT INTO "institutions" VALUES(5,4,'Survey Office',4);
INSERT INTO "institutions" VALUES(6,5,'Private Hauler Guild',5);
INSERT INTO "institutions" VALUES(7,6,'Colonial Development Bureau',6);
CREATE TABLE mineral_deposits (
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            body_id INTEGER NOT NULL CHECK(body_id > 0),
            mineral INTEGER NOT NULL CHECK(mineral BETWEEN 0 AND 13),
            remaining REAL NOT NULL CHECK(remaining >= 0.0),
            accessibility REAL NOT NULL CHECK(accessibility >= 0.0),
            confidence REAL NOT NULL CHECK(confidence >= 0.0 AND confidence <= 1.0),
            PRIMARY KEY(body_id, mineral),
            FOREIGN KEY(body_id) REFERENCES bodies(id)
        );
INSERT INTO "mineral_deposits" VALUES(0,1,0,1000000.0,1.0,1.0);
INSERT INTO "mineral_deposits" VALUES(1,1,1,600000.0,0.8,1.0);
INSERT INTO "mineral_deposits" VALUES(2,1,4,200000.0,0.6,1.0);
INSERT INTO "mineral_deposits" VALUES(3,1,5,700000.0,0.9,1.0);
INSERT INTO "mineral_deposits" VALUES(4,1,11,2000000.0,1.0,1.0);
INSERT INTO "mineral_deposits" VALUES(5,1,12,500000.0,0.7,1.0);
INSERT INTO "mineral_deposits" VALUES(6,1,13,800000.0,0.75,1.0);
INSERT INTO "mineral_deposits" VALUES(7,2,0,800000.0,0.9,1.0);
INSERT INTO "mineral_deposits" VALUES(8,2,2,250000.0,0.55,1.0);
INSERT INTO "mineral_deposits" VALUES(9,2,3,300000.0,0.65,1.0);
INSERT INTO "mineral_deposits" VALUES(10,2,11,350000.0,0.5,1.0);
INSERT INTO "mineral_deposits" VALUES(11,3,3,120000.0,0.35,1.0);
INSERT INTO "mineral_deposits" VALUES(12,3,5,150000.0,0.4,1.0);
INSERT INTO "mineral_deposits" VALUES(13,3,10,30000.0,0.25,0.7);
INSERT INTO "mineral_deposits" VALUES(14,4,0,1400000.0,0.95,1.0);
INSERT INTO "mineral_deposits" VALUES(15,4,1,900000.0,0.85,1.0);
INSERT INTO "mineral_deposits" VALUES(16,4,4,180000.0,0.55,1.0);
INSERT INTO "mineral_deposits" VALUES(17,4,11,1800000.0,0.9,1.0);
INSERT INTO "mineral_deposits" VALUES(18,4,12,450000.0,0.75,1.0);
INSERT INTO "mineral_deposits" VALUES(19,5,0,700000.0,0.8,1.0);
INSERT INTO "mineral_deposits" VALUES(20,5,2,600000.0,0.7,1.0);
INSERT INTO "mineral_deposits" VALUES(21,5,9,90000.0,0.45,0.65);
INSERT INTO "mineral_deposits" VALUES(22,6,7,80000.0,0.35,0.45);
INSERT INTO "mineral_deposits" VALUES(23,6,8,95000.0,0.4,0.4);
INSERT INTO "mineral_deposits" VALUES(24,6,9,120000.0,0.5,0.35);
INSERT INTO "mineral_deposits" VALUES(25,7,11,4500000.0,0.95,1.0);
INSERT INTO "mineral_deposits" VALUES(26,7,12,1000000.0,0.75,1.0);
INSERT INTO "mineral_deposits" VALUES(27,7,13,3200000.0,0.9,1.0);
INSERT INTO "mineral_deposits" VALUES(28,8,6,160000.0,0.25,0.15);
INSERT INTO "mineral_deposits" VALUES(29,8,9,110000.0,0.2,0.0);
INSERT INTO "mineral_deposits" VALUES(30,8,13,600000.0,0.3,0.2);
CREATE TABLE people (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            institution_id INTEGER NOT NULL CHECK(institution_id > 0),
            logistics INTEGER NOT NULL CHECK(logistics >= 0),
            industry INTEGER NOT NULL CHECK(industry >= 0),
            survey INTEGER NOT NULL CHECK(survey >= 0),
            command INTEGER NOT NULL CHECK(command >= 0),
            administration INTEGER NOT NULL CHECK(administration >= 0),
            engineering INTEGER NOT NULL CHECK(engineering >= 0),
            intelligence INTEGER NOT NULL CHECK(intelligence >= 0),
            crisis_management INTEGER NOT NULL CHECK(crisis_management >= 0),
            seniority_level INTEGER NOT NULL CHECK(seniority_level >= 0),
            successful_assignments INTEGER NOT NULL CHECK(successful_assignments >= 0),
            failed_assignments INTEGER NOT NULL CHECK(failed_assignments >= 0),
            commendations INTEGER NOT NULL CHECK(commendations >= 0),
            controversies INTEGER NOT NULL CHECK(controversies >= 0),
            FOREIGN KEY(institution_id) REFERENCES institutions(id)
        );
INSERT INTO "people" VALUES(1,0,'Director Mara Chen',1,4,3,2,3,5,2,3,5,5,12,1,4,1);
INSERT INTO "people" VALUES(2,1,'Commodore Elias Voss',2,3,5,1,5,3,4,2,3,4,9,2,3,0);
INSERT INTO "people" VALUES(3,2,'Dr. Nia Okafor',5,2,1,5,2,3,4,5,2,4,15,1,5,0);
INSERT INTO "people" VALUES(4,3,'Priya Raman',4,5,3,2,2,4,3,2,4,3,7,1,2,1);
CREATE TABLE schema_version (
            id INTEGER PRIMARY KEY CHECK(id = 1),
            version INTEGER NOT NULL CHECK(version = 12)
        );
INSERT INTO "schema_version" VALUES(1,12);
CREATE TABLE ship_class_installs (
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            ordinal INTEGER NOT NULL CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            component_id INTEGER NOT NULL CHECK(component_id > 0),
            quantity INTEGER NOT NULL CHECK(quantity > 0),
            PRIMARY KEY(ship_class_id, ordinal),
            UNIQUE(ship_class_id, component_id),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id),
            FOREIGN KEY(component_id) REFERENCES ship_components(id)
        );
INSERT INTO "ship_class_installs" VALUES(1,0,1,1);
INSERT INTO "ship_class_installs" VALUES(1,1,2,1);
INSERT INTO "ship_class_installs" VALUES(1,2,3,1);
INSERT INTO "ship_class_installs" VALUES(1,3,4,1);
INSERT INTO "ship_class_installs" VALUES(1,4,5,1);
CREATE TABLE ship_classes (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            role INTEGER NOT NULL CHECK(role BETWEEN 0 AND 2),
            speed_km_per_day REAL NOT NULL CHECK(speed_km_per_day >= 0.0),
            revision INTEGER NOT NULL CHECK(revision > 0),
            based_on_class_id INTEGER NULL CHECK(based_on_class_id IS NULL OR based_on_class_id > 0),
            FOREIGN KEY(based_on_class_id) REFERENCES ship_classes(id) DEFERRABLE INITIALLY DEFERRED
        );
INSERT INTO "ship_classes" VALUES(1,0,'Survey Cutter',0,50.0,1,NULL);
CREATE TABLE ship_component_material_costs (
            component_id INTEGER NOT NULL CHECK(component_id > 0),
            material INTEGER NOT NULL CHECK(material BETWEEN 0 AND 5),
            amount REAL NOT NULL CHECK(amount >= 0.0),
            PRIMARY KEY(component_id, material),
            FOREIGN KEY(component_id) REFERENCES ship_components(id)
        );
INSERT INTO "ship_component_material_costs" VALUES(1,0,200.0);
INSERT INTO "ship_component_material_costs" VALUES(1,1,0.0);
INSERT INTO "ship_component_material_costs" VALUES(1,2,0.0);
INSERT INTO "ship_component_material_costs" VALUES(1,3,0.0);
INSERT INTO "ship_component_material_costs" VALUES(1,4,20.0);
INSERT INTO "ship_component_material_costs" VALUES(1,5,0.0);
INSERT INTO "ship_component_material_costs" VALUES(2,0,0.0);
INSERT INTO "ship_component_material_costs" VALUES(2,1,30.0);
INSERT INTO "ship_component_material_costs" VALUES(2,2,0.0);
INSERT INTO "ship_component_material_costs" VALUES(2,3,20.0);
INSERT INTO "ship_component_material_costs" VALUES(2,4,0.0);
INSERT INTO "ship_component_material_costs" VALUES(2,5,0.0);
INSERT INTO "ship_component_material_costs" VALUES(3,0,30.0);
INSERT INTO "ship_component_material_costs" VALUES(3,1,0.0);
INSERT INTO "ship_component_material_costs" VALUES(3,2,0.0);
INSERT INTO "ship_component_material_costs" VALUES(3,3,0.0);
INSERT INTO "ship_component_material_costs" VALUES(3,4,10.0);
INSERT INTO "ship_component_material_costs" VALUES(3,5,0.0);
INSERT INTO "ship_component_material_costs" VALUES(4,0,0.0);
INSERT INTO "ship_component_material_costs" VALUES(4,1,40.0);
INSERT INTO "ship_component_material_costs" VALUES(4,2,0.0);
INSERT INTO "ship_component_material_costs" VALUES(4,3,0.0);
INSERT INTO "ship_component_material_costs" VALUES(4,4,0.0);
INSERT INTO "ship_component_material_costs" VALUES(4,5,0.0);
INSERT INTO "ship_component_material_costs" VALUES(5,0,20.0);
INSERT INTO "ship_component_material_costs" VALUES(5,1,10.0);
INSERT INTO "ship_component_material_costs" VALUES(5,2,0.0);
INSERT INTO "ship_component_material_costs" VALUES(5,3,0.0);
INSERT INTO "ship_component_material_costs" VALUES(5,4,20.0);
INSERT INTO "ship_component_material_costs" VALUES(5,5,0.0);
CREATE TABLE ship_components (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 4),
            mass REAL NOT NULL CHECK(mass >= 0.0),
            volume REAL NOT NULL CHECK(volume >= 0.0),
            internal_volume_capacity REAL NOT NULL CHECK(internal_volume_capacity >= 0.0),
            power_generation REAL NOT NULL CHECK(power_generation >= 0.0),
            power_demand REAL NOT NULL CHECK(power_demand >= 0.0),
            propellant_capacity REAL NOT NULL CHECK(propellant_capacity >= 0.0),
            survey_capability REAL NOT NULL CHECK(survey_capability >= 0.0),
            build_points REAL NOT NULL CHECK(build_points >= 0.0)
        );
INSERT INTO "ship_components" VALUES(1,0,'Compact Survey Hull',0,300.0,0.0,1000.0,0.0,0.0,0.0,0.0,200.0);
INSERT INTO "ship_components" VALUES(2,1,'Standard R-60 Reactor',1,80.0,120.0,0.0,120.0,0.0,0.0,0.0,100.0);
INSERT INTO "ship_components" VALUES(3,2,'Standard Propellant Tank',2,50.0,100.0,0.0,0.0,0.0,1000.0,0.0,80.0);
INSERT INTO "ship_components" VALUES(4,3,'Wide-Area Survey Array',3,30.0,80.0,0.0,0.0,40.0,0.0,1.0,70.0);
INSERT INTO "ship_components" VALUES(5,4,'General Ship Systems',4,40.0,50.0,0.0,0.0,20.0,0.0,0.0,50.0);
CREATE TABLE ships (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            fleet_id INTEGER NOT NULL CHECK(fleet_id > 0),
            fleet_ordinal INTEGER NOT NULL CHECK(typeof(fleet_ordinal) = 'integer' AND fleet_ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0),
            fuel REAL NOT NULL CHECK(fuel >= 0.0),
            UNIQUE(fleet_id, fleet_ordinal),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id),
            FOREIGN KEY(fleet_id) REFERENCES fleets(id)
        );
CREATE TABLE shipyard_orders (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            colony_id INTEGER NOT NULL CHECK(colony_id > 0),
            ship_class_id INTEGER NOT NULL CHECK(ship_class_id > 0),
            quantity_requested INTEGER NOT NULL CHECK(quantity_requested > 0),
            quantity_completed INTEGER NOT NULL CHECK(quantity_completed >= 0),
            accumulated_build_points REAL NOT NULL CHECK(accumulated_build_points >= 0.0),
            status INTEGER NOT NULL CHECK(status BETWEEN 0 AND 1),
            CHECK(quantity_completed <= quantity_requested),
            CHECK(
                (status = 0 AND quantity_completed < quantity_requested)
                OR
                (status = 1 AND quantity_completed = quantity_requested AND accumulated_build_points = 0.0)
            ),
            FOREIGN KEY(colony_id) REFERENCES colonies(id),
            FOREIGN KEY(ship_class_id) REFERENCES ship_classes(id)
        );
CREATE TABLE star_systems (
            id INTEGER PRIMARY KEY NOT NULL CHECK(id > 0),
            ordinal INTEGER NOT NULL UNIQUE CHECK(typeof(ordinal) = 'integer' AND ordinal >= 0),
            name TEXT NOT NULL CHECK(length(name) > 0)
        );
INSERT INTO "star_systems" VALUES(1,0,'Sol');
CREATE INDEX idx_people_institution_id ON people(institution_id);
CREATE INDEX idx_appointments_person_id ON appointments(person_id);
CREATE INDEX idx_bodies_system_id ON bodies(system_id);
CREATE INDEX idx_colonies_body_id ON colonies(body_id);
CREATE INDEX idx_colonies_owner_institution_id ON colonies(owner_institution_id);
CREATE INDEX idx_colony_processing_allocations_colony_id
            ON colony_processing_allocations(colony_id);
CREATE INDEX idx_fleets_owner_institution_id ON fleets(owner_institution_id);
CREATE INDEX idx_fleet_order_queue_fleet_id ON fleet_order_queue(fleet_id);
CREATE INDEX idx_ships_fleet_id ON ships(fleet_id);
CREATE INDEX idx_events_day ON event_log(day);
COMMIT;
PRAGMA foreign_keys=ON;
