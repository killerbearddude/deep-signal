#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/EquipmentServiceRules.h"
#include "sim/ObservationRules.h"
#include <algorithm>

// Builds the deterministic mature home-system scenario used by new games and
// tests. The scenario is hand-authored so geography, ownership, and deposits
// can express design intent before procedural generation or logistics routes
// exist.

namespace deep {

namespace {

void addDeposit(GameState& state,
                const BodyId bodyId,
                const Mineral mineral,
                const double remaining,
                const double accessibility) {
    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = bodyId,
        .mineral = mineral,
        .remaining = remaining,
        .accessibility = accessibility
    });
}

} // namespace

GameState createHomeSystemScenario() {
    GameState state;
    state.date.day = 0;

    // IDs are allocated through the same counters the live simulation uses so
    // save/load preserves deterministic continuation behavior. Terra
    // and Mars intentionally remain the first two bodies because existing tests
    // and UI smoke workflows use them as the canonical short movement pair.
    const StarSystemId solId{state.ids.nextStarSystemId++};
    const BodyId terraId{state.ids.nextBodyId++};
    const BodyId marsId{state.ids.nextBodyId++};
    const BodyId lunaId{state.ids.nextBodyId++};
    const BodyId ceresId{state.ids.nextBodyId++};
    const BodyId vestaId{state.ids.nextBodyId++};
    const BodyId pallasId{state.ids.nextBodyId++};
    const BodyId titanId{state.ids.nextBodyId++};
    const BodyId frontierObjectId{state.ids.nextBodyId++};
    const BodyId sunId{state.ids.nextBodyId++};

    const ColonyId terraColonyId{state.ids.nextColonyId++};
    const ColonyId marsColonyId{state.ids.nextColonyId++};
    const ColonyId ceresColonyId{state.ids.nextColonyId++};
    const ColonyId titanColonyId{state.ids.nextColonyId++};

    const InstitutionId continuityOfficeId{state.ids.nextInstitutionId++};
    const InstitutionId navalBoardId{state.ids.nextInstitutionId++};
    const InstitutionId extractionCombineId{state.ids.nextInstitutionId++};
    const InstitutionId fuelTrustId{state.ids.nextInstitutionId++};
    const InstitutionId surveyOfficeId{state.ids.nextInstitutionId++};
    const InstitutionId haulerGuildId{state.ids.nextInstitutionId++};
    const InstitutionId developmentBureauId{state.ids.nextInstitutionId++};

    const PersonId continuityDirectorId{state.ids.nextPersonId++};
    const PersonId yardLiaisonId{state.ids.nextPersonId++};
    const PersonId surveyCoordinatorId{state.ids.nextPersonId++};
    const PersonId fuelPlannerId{state.ids.nextPersonId++};
    const ShipClassId surveyCutterId{state.ids.nextShipClassId++};

    state.starSystems.push_back(StarSystem{
        .id = solId,
        .name = "Sol"
    });

    // Starter institutions are identity anchors only. Ownership references let
    // assets carry institutional provenance before trust, requests, or access
    // mechanics exist.
    state.institutions.push_back(Institution{
        .id = continuityOfficeId,
        .name = "Strategic Continuity Office",
        .type = InstitutionType::ContinuityOffice
    });
    state.institutions.push_back(Institution{
        .id = navalBoardId,
        .name = "Naval Construction Board",
        .type = InstitutionType::NavalConstruction
    });
    state.institutions.push_back(Institution{
        .id = extractionCombineId,
        .name = "Belt Extraction Combine",
        .type = InstitutionType::ExtractionCombine
    });
    state.institutions.push_back(Institution{
        .id = fuelTrustId,
        .name = "Outer Fuel Trust",
        .type = InstitutionType::FuelTrust
    });
    state.institutions.push_back(Institution{
        .id = surveyOfficeId,
        .name = "Survey Office",
        .type = InstitutionType::SurveyOffice
    });
    state.institutions.push_back(Institution{
        .id = haulerGuildId,
        .name = "Private Hauler Guild",
        .type = InstitutionType::PrivateHauler
    });
    state.institutions.push_back(Institution{
        .id = developmentBureauId,
        .name = "Colonial Development Bureau",
        .type = InstitutionType::DevelopmentBureau
    });

    // Starter personnel are durable identity records. Their competencies feed
    // appointment merit and small operational modifiers, but not politics or
    // autonomous decision-making.
    state.people.push_back(Person{
        .id = continuityDirectorId,
        .name = "Director Mara Chen",
        .institutionId = continuityOfficeId,
        .competencies = PersonCompetencies{
            .logistics = 4,
            .industry = 3,
            .survey = 2,
            .command = 3,
            .administration = 5,
            .engineering = 2,
            .intelligence = 3,
            .crisisManagement = 5
        },
        .seniorityLevel = 5,
        .serviceRecord = PersonServiceRecord{
            .successfulAssignments = 12,
            .failedAssignments = 1,
            .commendations = 4,
            .controversies = 1
        }
    });
    state.people.push_back(Person{
        .id = yardLiaisonId,
        .name = "Commodore Elias Voss",
        .institutionId = navalBoardId,
        .competencies = PersonCompetencies{
            .logistics = 3,
            .industry = 5,
            .survey = 1,
            .command = 5,
            .administration = 3,
            .engineering = 4,
            .intelligence = 2,
            .crisisManagement = 3
        },
        .seniorityLevel = 4,
        .serviceRecord = PersonServiceRecord{
            .successfulAssignments = 9,
            .failedAssignments = 2,
            .commendations = 3,
            .controversies = 0
        }
    });
    state.people.push_back(Person{
        .id = surveyCoordinatorId,
        .name = "Dr. Nia Okafor",
        .institutionId = surveyOfficeId,
        .competencies = PersonCompetencies{
            .logistics = 2,
            .industry = 1,
            .survey = 5,
            .command = 2,
            .administration = 3,
            .engineering = 4,
            .intelligence = 5,
            .crisisManagement = 2
        },
        .seniorityLevel = 4,
        .serviceRecord = PersonServiceRecord{
            .successfulAssignments = 15,
            .failedAssignments = 1,
            .commendations = 5,
            .controversies = 0
        }
    });
    state.people.push_back(Person{
        .id = fuelPlannerId,
        .name = "Priya Raman",
        .institutionId = fuelTrustId,
        .competencies = PersonCompetencies{
            .logistics = 5,
            .industry = 3,
            .survey = 2,
            .command = 2,
            .administration = 4,
            .engineering = 3,
            .intelligence = 2,
            .crisisManagement = 4
        },
        .seniorityLevel = 3,
        .serviceRecord = PersonServiceRecord{
            .successfulAssignments = 7,
            .failedAssignments = 1,
            .commendations = 2,
            .controversies = 1
        }
    });

    // The mature home-system map is deliberately coarse but now uses simple
    // circular rails. Rail positions are computed from date; x/y remain fallback
    // positions for fixed objects and hand-authored display defaults.
    state.bodies.push_back(Body{
        .id = terraId,
        .systemId = solId,
        .name = "Terra",
        .type = BodyType::Terrestrial,
        .strategicZone = StrategicZone::InnerCore,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 149'600'000.0,
        .orbitalPeriodDays = 365.25,
        .phaseRadians = 0.0,
        .displayRadius = 10.0,
        .x = 149.6,
        .y = 0.0
    });
    state.bodies.push_back(Body{
        .id = marsId,
        .systemId = solId,
        .name = "Mars",
        .type = BodyType::Terrestrial,
        .strategicZone = StrategicZone::MilitaryIndustrial,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 227'900'000.0,
        .orbitalPeriodDays = 687.0,
        .phaseRadians = 1.32,
        .displayRadius = 8.0,
        .x = 56.6,
        .y = 220.7
    });
    state.bodies.push_back(Body{
        .id = lunaId,
        .systemId = solId,
        .name = "Luna Yard Complex",
        .type = BodyType::Moon,
        .strategicZone = StrategicZone::MilitaryIndustrial,
        .parentBodyId = terraId,
        .orbitalRadiusKm = 384'400.0,
        .orbitalPeriodDays = 27.3,
        .phaseRadians = 0.75,
        .displayRadius = 5.0,
        .x = 150.0,
        .y = 0.3
    });
    state.bodies.push_back(Body{
        .id = ceresId,
        .systemId = solId,
        .name = "Ceres Extraction Hub",
        .type = BodyType::Asteroid,
        .strategicZone = StrategicZone::BeltIndustrial,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 413'700'000.0,
        .orbitalPeriodDays = 1682.0,
        .phaseRadians = 2.05,
        .displayRadius = 5.0,
        .x = -190.1,
        .y = 367.4
    });
    state.bodies.push_back(Body{
        .id = vestaId,
        .systemId = solId,
        .name = "Vesta Refinery Claim",
        .type = BodyType::Asteroid,
        .strategicZone = StrategicZone::BeltIndustrial,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 353'400'000.0,
        .orbitalPeriodDays = 1325.0,
        .phaseRadians = 3.10,
        .displayRadius = 5.0,
        .x = -353.1,
        .y = 14.7
    });
    state.bodies.push_back(Body{
        .id = pallasId,
        .systemId = solId,
        .name = "Pallas Survey Claim",
        .type = BodyType::Asteroid,
        .strategicZone = StrategicZone::BeltIndustrial,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 414'500'000.0,
        .orbitalPeriodDays = 1686.0,
        .phaseRadians = 4.35,
        .displayRadius = 5.0,
        .x = -146.8,
        .y = -387.6
    });
    state.bodies.push_back(Body{
        .id = titanId,
        .systemId = solId,
        .name = "Titan Fuel Depot",
        .type = BodyType::Moon,
        .strategicZone = StrategicZone::OuterLogistics,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 1'433'500'000.0,
        .orbitalPeriodDays = 10'759.0,
        .phaseRadians = 5.20,
        .displayRadius = 6.0,
        .x = 672.0,
        .y = -1266.5
    });
    state.bodies.push_back(Body{
        .id = frontierObjectId,
        .systemId = solId,
        .name = "Helios Far Survey Object",
        .type = BodyType::Asteroid,
        .strategicZone = StrategicZone::DeepSurveyFrontier,
        .parentBodyId = sunId,
        .orbitalRadiusKm = 2'600'000'000.0,
        .orbitalPeriodDays = 20'000.0,
        .phaseRadians = 0.95,
        .displayRadius = 5.0,
        .x = 1510.0,
        .y = 2116.0
    });
    state.bodies.push_back(Body{
        .id = sunId,
        .systemId = solId,
        .name = "Sun",
        .type = BodyType::Star,
        .strategicZone = StrategicZone::InnerCore,
        .parentBodyId = std::nullopt,
        .orbitalRadiusKm = 0.0,
        .orbitalPeriodDays = 0.0,
        .phaseRadians = 0.0,
        .displayRadius = 14.0,
        .x = 0.0,
        .y = 0.0
    });

    MineralSet startingStockpile;
    startingStockpile.set(Mineral::Iron, 10'000.0);
    startingStockpile.set(Mineral::Nickel, 5'000.0);
    startingStockpile.set(Mineral::Titanium, 4'000.0);
    startingStockpile.set(Mineral::Aluminum, 8'000.0);
    startingStockpile.set(Mineral::Copper, 3'000.0);
    startingStockpile.set(Mineral::Silicon, 4'000.0);
    startingStockpile.set(Mineral::Lithium, 1'000.0);
    startingStockpile.set(Mineral::Uranium, 500.0);
    startingStockpile.set(Mineral::Thorium, 500.0);
    startingStockpile.set(Mineral::RareEarthElements, 700.0);
    startingStockpile.set(Mineral::PlatinumGroupMetals, 200.0);
    startingStockpile.set(Mineral::WaterIce, 50'000.0);
    startingStockpile.set(Mineral::CarbonCompounds, 10'000.0);
    startingStockpile.set(Mineral::Volatiles, 20'000.0);

    ProcessedMaterialSet startingProcessedStockpile;
    startingProcessedStockpile.set(ProcessedMaterial::StructuralAlloys, 1'500.0);
    startingProcessedStockpile.set(ProcessedMaterial::Electronics, 500.0);
    startingProcessedStockpile.set(ProcessedMaterial::Propellant, 2'000.0);
    startingProcessedStockpile.set(ProcessedMaterial::ReactorFuel, 200.0);
    startingProcessedStockpile.set(ProcessedMaterial::IndustrialComposites, 700.0);
    startingProcessedStockpile.set(ProcessedMaterial::OrdnanceMaterials, 300.0);

    MineralSet marsStockpile;
    marsStockpile.set(Mineral::Iron, 6'000.0);
    marsStockpile.set(Mineral::Titanium, 3'000.0);
    marsStockpile.set(Mineral::Aluminum, 4'000.0);
    marsStockpile.set(Mineral::WaterIce, 8'000.0);

    ProcessedMaterialSet marsProcessedStockpile;
    marsProcessedStockpile.set(ProcessedMaterial::StructuralAlloys, 900.0);
    marsProcessedStockpile.set(ProcessedMaterial::Electronics, 250.0);
    marsProcessedStockpile.set(ProcessedMaterial::Propellant, 600.0);
    marsProcessedStockpile.set(ProcessedMaterial::IndustrialComposites, 300.0);

    MineralSet ceresStockpile;
    ceresStockpile.set(Mineral::Iron, 12'000.0);
    ceresStockpile.set(Mineral::Nickel, 9'000.0);
    ceresStockpile.set(Mineral::WaterIce, 20'000.0);
    ceresStockpile.set(Mineral::CarbonCompounds, 4'000.0);

    ProcessedMaterialSet ceresProcessedStockpile;
    ceresProcessedStockpile.set(ProcessedMaterial::StructuralAlloys, 400.0);
    ceresProcessedStockpile.set(ProcessedMaterial::IndustrialComposites, 150.0);

    MineralSet titanStockpile;
    titanStockpile.set(Mineral::WaterIce, 80'000.0);
    titanStockpile.set(Mineral::Volatiles, 60'000.0);
    titanStockpile.set(Mineral::CarbonCompounds, 15'000.0);

    ProcessedMaterialSet titanProcessedStockpile;
    titanProcessedStockpile.set(ProcessedMaterial::Propellant, 4'000.0);
    titanProcessedStockpile.set(ProcessedMaterial::ReactorFuel, 100.0);

    state.colonies.push_back(Colony{
        .id = terraColonyId,
        .bodyId = terraId,
        .name = "Terra Directorate",
        .stockpile = startingStockpile,
        .processedStockpile = startingProcessedStockpile,
        .mines = 10.0,
        .processorCapacity = 50.0,
        .shipyardCapacity = 100.0,
        .processingPolicy = ProcessingPolicy::Balanced,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = continuityOfficeId
    });
    state.colonies.push_back(Colony{
        .id = marsColonyId,
        .bodyId = marsId,
        .name = "Mars Naval Yards",
        .stockpile = marsStockpile,
        .processedStockpile = marsProcessedStockpile,
        .mines = 4.0,
        .processorCapacity = 25.0,
        .shipyardCapacity = 60.0,
        .processingPolicy = ProcessingPolicy::ShipbuildingFocus,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = navalBoardId
    });
    state.colonies.push_back(Colony{
        .id = ceresColonyId,
        .bodyId = ceresId,
        .name = "Ceres Belt Works",
        .stockpile = ceresStockpile,
        .processedStockpile = ceresProcessedStockpile,
        .mines = 14.0,
        .processorCapacity = 30.0,
        .shipyardCapacity = 0.0,
        .processingPolicy = ProcessingPolicy::StockpileRecovery,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = extractionCombineId
    });
    state.colonies.push_back(Colony{
        .id = titanColonyId,
        .bodyId = titanId,
        .name = "Titan Fuel Depot",
        .stockpile = titanStockpile,
        .processedStockpile = titanProcessedStockpile,
        .mines = 6.0,
        .processorCapacity = 40.0,
        .shipyardCapacity = 0.0,
        .processingPolicy = ProcessingPolicy::FuelFocus,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = fuelTrustId
    });

    // Starter appointments name who is responsible for current operational
    // areas. Current effects are deliberately small deterministic modifiers.
    state.appointments.push_back(Appointment{
        .role = AppointmentRole::InstitutionHead,
        .scopeType = AppointmentScopeType::Institution,
        .scopeId = continuityOfficeId.value,
        .personId = continuityDirectorId,
        .appointedDay = state.date.day
    });
    state.appointments.push_back(Appointment{
        .role = AppointmentRole::ColonyAdministrator,
        .scopeType = AppointmentScopeType::Colony,
        .scopeId = terraColonyId.value,
        .personId = continuityDirectorId,
        .appointedDay = state.date.day
    });
    state.appointments.push_back(Appointment{
        .role = AppointmentRole::ShipyardDirector,
        .scopeType = AppointmentScopeType::Colony,
        .scopeId = terraColonyId.value,
        .personId = yardLiaisonId,
        .appointedDay = state.date.day
    });
    state.appointments.push_back(Appointment{
        .role = AppointmentRole::SurveyChief,
        .scopeType = AppointmentScopeType::Institution,
        .scopeId = surveyOfficeId.value,
        .personId = surveyCoordinatorId,
        .appointedDay = state.date.day
    });
    state.appointments.push_back(Appointment{
        .role = AppointmentRole::LogisticsCoordinator,
        .scopeType = AppointmentScopeType::Institution,
        .scopeId = fuelTrustId.value,
        .personId = fuelPlannerId,
        .appointedDay = state.date.day
    });

    // Deposits are distributed by strategic role: core bodies have legacy
    // industrial reserves, Mars has shipbuilding inputs, the belt has bulk ore,
    // Titan carries volatiles, and the frontier object contains unobserved
    // exploration targets for resource survey commands.
    addDeposit(state, terraId, Mineral::Iron, 1'000'000.0, 1.0);
    addDeposit(state, terraId, Mineral::Nickel, 600'000.0, 0.8);
    addDeposit(state, terraId, Mineral::Copper, 200'000.0, 0.6);
    addDeposit(state, terraId, Mineral::Silicon, 700'000.0, 0.9);
    addDeposit(state, terraId, Mineral::WaterIce, 2'000'000.0, 1.0);
    addDeposit(state, terraId, Mineral::CarbonCompounds, 500'000.0, 0.7);
    addDeposit(state, terraId, Mineral::Volatiles, 800'000.0, 0.75);

    addDeposit(state, marsId, Mineral::Iron, 800'000.0, 0.9);
    addDeposit(state, marsId, Mineral::Titanium, 250'000.0, 0.55);
    addDeposit(state, marsId, Mineral::Aluminum, 300'000.0, 0.65);
    addDeposit(state, marsId, Mineral::WaterIce, 350'000.0, 0.5);

    addDeposit(state, lunaId, Mineral::Aluminum, 120'000.0, 0.35);
    addDeposit(state, lunaId, Mineral::Silicon, 150'000.0, 0.4);
    addDeposit(state, lunaId, Mineral::PlatinumGroupMetals, 30'000.0, 0.25);

    addDeposit(state, ceresId, Mineral::Iron, 1'400'000.0, 0.95);
    addDeposit(state, ceresId, Mineral::Nickel, 900'000.0, 0.85);
    addDeposit(state, ceresId, Mineral::Copper, 180'000.0, 0.55);
    addDeposit(state, ceresId, Mineral::WaterIce, 1'800'000.0, 0.9);
    addDeposit(state, ceresId, Mineral::CarbonCompounds, 450'000.0, 0.75);

    addDeposit(state, vestaId, Mineral::Iron, 700'000.0, 0.8);
    addDeposit(state, vestaId, Mineral::Titanium, 600'000.0, 0.7);
    addDeposit(state, vestaId, Mineral::RareEarthElements, 90'000.0, 0.45);

    addDeposit(state, pallasId, Mineral::Uranium, 80'000.0, 0.35);
    addDeposit(state, pallasId, Mineral::Thorium, 95'000.0, 0.4);
    addDeposit(state, pallasId, Mineral::RareEarthElements, 120'000.0, 0.5);

    addDeposit(state, titanId, Mineral::WaterIce, 4'500'000.0, 0.95);
    addDeposit(state, titanId, Mineral::CarbonCompounds, 1'000'000.0, 0.75);
    addDeposit(state, titanId, Mineral::Volatiles, 3'200'000.0, 0.9);

    addDeposit(state, frontierObjectId, Mineral::Lithium, 160'000.0, 0.25);
    addDeposit(state, frontierObjectId, Mineral::RareEarthElements, 110'000.0, 0.2);
    addDeposit(state, frontierObjectId, Mineral::Volatiles, 600'000.0, 0.3);

    state.shipComponents = standardShipComponentCatalog();
    state.measurementProfiles=referenceMeasurementProfiles();
    state.ids.nextMeasurementProfileId=3;
    for (auto& c : state.shipComponents)
        if (c.surveyCapability > 0)
            c.measurementProfileId=MeasurementProfileId{c.id.value == 7 ? 2 : 1};
    // Only the authored home laboratory has throughput; other colonies wait.
    state.colonies.front().analysisCapacity=1.0;
    state.ids.nextShipComponentId = 11;
    state.equipmentFamilies = {{EquipmentFamilyId{1}, "Standard Survey Instruments"},
                              {EquipmentFamilyId{2}, "Specialist Survey Instruments"},
                              {EquipmentFamilyId{3}, "Field Construction"}};
    state.ids.nextEquipmentFamilyId = 4;
    state.siteConstructionFamilyId=EquipmentFamilyId{3};
    state.siteModuleCatalog=referenceSiteModuleCatalog();
    state.maintenanceTeams.push_back(MaintenanceTeam{
        .id = MaintenanceTeamId{state.ids.nextMaintenanceTeamId++}, .name = "Standard Engineering Team",
        .qualifiedFamilies = {EquipmentFamilyId{1}}, .workdaysPerDay = 1.0,
        .location = MaintenanceTeamLocation::Colony, .colonyId = terraColonyId, .fleetId = std::nullopt
    });
    state.shipClasses.push_back(ShipClass{
        .id = surveyCutterId,
        .name = "Survey Cutter",
        .role = ShipRole::Survey,
        .basedOnClassId = std::nullopt,
        .components = referenceSurveyCutterComponents(),
        .speedKmPerDay = 50.0
    });

    state.shipClasses.push_back(ShipClass{
        .id = ShipClassId{state.ids.nextShipClassId++}, .name = "Reference Freighter",
        .role = ShipRole::Freighter, .basedOnClassId = std::nullopt,
        .components = referenceFreighterComponents(), .speedKmPerDay = 50.0
    });

    state.shipClasses.push_back(ShipClass{
        .id=ShipClassId{state.ids.nextShipClassId++},.name="Reference Builder",.role=ShipRole::Builder,
        .basedOnClassId=std::nullopt,.components=referenceBuilderComponents(),.speedKmPerDay=50.0});
    state.maintenanceTeams.push_back(MaintenanceTeam{
        .id=MaintenanceTeamId{state.ids.nextMaintenanceTeamId++},.name="Field Construction Team",
        .qualifiedFamilies={*state.siteConstructionFamilyId},.workdaysPerDay=1.0,
        .location=MaintenanceTeamLocation::Colony,.colonyId=terraColonyId,.fleetId=std::nullopt});

    Person priorityLeader = state.people.at(2);
    priorityLeader.id = PersonId{state.ids.nextPersonId++};
    priorityLeader.name = "Dr. Tala Imani";
    priorityLeader.surveyPlanningApproach = SurveyPlanningApproach::PriorityFirst;
    state.people.push_back(std::move(priorityLeader));

    state.surveyTeams.push_back(SurveyTeam{
        .id = SurveyTeamId{state.ids.nextSurveyTeamId++},
        .name = "Home Survey Team",
        .locationKind = SurveyTeamLocationKind::Colony,
        .colonyId = terraColonyId,
        .fleetId = std::nullopt
    });

    return state;
}

GameState createDelegatedSurveyScenario() {
    GameState state = createHomeSystemScenario();
    const StarSystemId systemId = state.starSystems.front().id;
    const BodyId homeBodyId{state.ids.nextBodyId++};
    const ColonyId homeColonyId{state.ids.nextColonyId++};
    state.bodies.push_back(Body{
        .id = homeBodyId, .systemId = systemId, .name = "P3A Fixed Home",
        .type = BodyType::Asteroid, .strategicZone = StrategicZone::DeepSurveyFrontier,
        .parentBodyId = std::nullopt, .x = 0.0, .y = 0.0
    });
    ProcessedMaterialSet supportStock;
    supportStock.set(ProcessedMaterial::Propellant, 3'000.0);
    state.colonies.push_back(Colony{
        .id = homeColonyId, .bodyId = homeBodyId, .name = "P3A Survey Base",
        .stockpile = {}, .processedStockpile = supportStock,
        .mines = 0.0, .processorCapacity = 0.0, .shipyardCapacity = 0.0,
        .processingPolicy = ProcessingPolicy::Balanced,
        .manualProcessingAllocations = {},
        .ownerInstitutionId = state.institutions.front().id
    });
    state.surveyTeams.front().colonyId = homeColonyId;

    for (int i = 1; i <= 3; ++i) {
        state.bodies.push_back(Body{
            .id = BodyId{state.ids.nextBodyId++},
            .systemId = systemId,
            .name = "P3A Target " + std::to_string(i),
            .type = BodyType::Asteroid,
            .strategicZone = StrategicZone::DeepSurveyFrontier,
            .parentBodyId = std::nullopt,
            .x = static_cast<double>(i) * 0.25,
            .y = 0.0
        });
    }
    // The third body deliberately has no deposit rows. Its timed visit can
    // complete without claiming that the whole body is resource-free.
    addDeposit(state, state.bodies.at(state.bodies.size() - 3).id,
               Mineral::Iron, 100.0, 1.0);

    const FleetId fleetId{state.ids.nextFleetId++};
    const ShipId shipId{state.ids.nextShipId++};
    state.ships.push_back(Ship{
        .id = shipId,
        .shipClassId = state.shipClasses.front().id,
        .name = "P3A Survey Cutter",
        .fleetId = fleetId,
        .fuel = 0.0
    });
    state.fleets.push_back(Fleet{
        .id = fleetId, .name = "P3A Survey Fleet",
        .currentBodyId = homeBodyId, .destinationBodyId = std::nullopt,
        .shipIds = {shipId}, .activeOrder = {}, .queuedOrders = {},
        .ownerInstitutionId = state.institutions.front().id
    });
    initializeShipEquipmentCondition(state, state.ships.back());
    return state;
}

GameState createDelegatedFreightScenario() {
    // Reuse isolated fixed positions, leaving the main home economy intact.
    // The survey ship/team start at the receiving colony with no engine fuel.
    // Neither freight nor survey intent is pre-authorized by this fixture.
    GameState state = createDelegatedSurveyScenario();
    const ColonyId sourceId = state.colonies.back().id;
    Colony receiving = state.colonies.back();
    receiving.id = ColonyId{state.ids.nextColonyId++};
    receiving.bodyId = state.bodies.at(state.bodies.size() - 3).id;
    receiving.name = "P3B Receiving Colony";
    receiving.processedStockpile = {};
    state.colonies.push_back(receiving);
    state.fleets.back().currentBodyId = receiving.bodyId;
    state.surveyTeams.front().colonyId = receiving.id;
    for (auto& colony : state.colonies) if (colony.id == sourceId) {
        colony.name = "P3B Supply Colony";
        colony.processedStockpile.set(ProcessedMaterial::StructuralAlloys, 500.0);
    }
    const FleetId fleetId{state.ids.nextFleetId++};
    const ShipId shipId{state.ids.nextShipId++};
    state.ships.push_back(Ship{
        .id = shipId, .shipClassId = state.shipClasses.at(1).id,
        .name = "P3B Reference Freighter", .fleetId = fleetId, .fuel = 0.0
    });
    state.fleets.push_back(Fleet{
        .id = fleetId, .name = "P3B Freight Fleet",
        .currentBodyId = state.colonies.at(state.colonies.size() - 2).bodyId,
        .destinationBodyId = std::nullopt, .shipIds = {shipId},
        .activeOrder = {}, .queuedOrders = {},
        .ownerInstitutionId = state.institutions.front().id
    });
    initializeShipEquipmentCondition(state, state.ships.back());
    return state;
}

GameState createTenderMaintenanceScenario() {
    GameState state = createDelegatedSurveyScenario();
    for (auto& component : state.shipComponents) if (component.serviceProfile) component.serviceProfile->dutyCapacity = 10.0;
    auto& home = state.colonies.back();
    home.processedStockpile.set(ProcessedMaterial::Electronics, 100.0);
    home.processedStockpile.set(ProcessedMaterial::IndustrialComposites, 100.0);
    state.ships.back().fuel = 1000.0;
    state.maintenanceTeams.front().colonyId = home.id;
    const ShipClassId cls{state.ids.nextShipClassId++};
    state.shipClasses.push_back(ShipClass{.id=cls,.name="Reference Instrument Tender",.role=ShipRole::Freighter,
        .basedOnClassId=std::nullopt,.components=referenceTenderComponents(),.speedKmPerDay=50.0});
    const FleetId fleetId{state.ids.nextFleetId++};
    const ShipId shipId{state.ids.nextShipId++};
    state.ships.push_back(Ship{.id=shipId,.shipClassId=cls,.name="Colony Instrument Tender",.fleetId=fleetId,.fuel=1000.0});
    initializeShipEquipmentCondition(state,state.ships.back());
    state.fleets.push_back(Fleet{.id=fleetId,.name="Tender Support Fleet",.currentBodyId=home.bodyId,
        .destinationBodyId=std::nullopt,.shipIds={shipId},.activeOrder={},.queuedOrders={},
        .ownerInstitutionId=state.institutions.front().id});
    return state;
}

GameState createMaintenanceSupplyScenario() {
    GameState state=createTenderMaintenanceScenario();
    const BodyId sourceBody=state.bodies.at(state.bodies.size()-2).id;
    state.colonies.back().processedStockpile.set(ProcessedMaterial::Electronics,0.0);
    state.colonies.back().processedStockpile.set(ProcessedMaterial::IndustrialComposites,0.0);
    state.ships.front().equipmentCondition.front().usedDuty=10.0;
    Colony source=state.colonies.back(); source.id=ColonyId{state.ids.nextColonyId++};
    source.bodyId=sourceBody; source.name="Maintenance Parts Source"; source.processedStockpile={};
    source.processedStockpile.set(ProcessedMaterial::Electronics,20.0);
    source.processedStockpile.set(ProcessedMaterial::IndustrialComposites,20.0);
    source.processedStockpile.set(ProcessedMaterial::Propellant,1000.0);
    state.colonies.push_back(source);
    for(int i=0;i<2;++i) {
        const FleetId fleetId{state.ids.nextFleetId++}; const ShipId shipId{state.ids.nextShipId++};
        state.ships.push_back(Ship{.id=shipId,.shipClassId=state.shipClasses.at(1).id,
            .name=i==0?"Electronics Freighter":"Composites Freighter",.fleetId=fleetId,.fuel=1000.0});
        initializeShipEquipmentCondition(state,state.ships.back());
        state.fleets.push_back(Fleet{.id=fleetId,.name=i==0?"Electronics Delivery Fleet":"Composites Delivery Fleet",
            .currentBodyId=sourceBody,.destinationBodyId=std::nullopt,.shipIds={shipId},.activeOrder={},.queuedOrders={},
            .ownerInstitutionId=state.institutions.front().id});
    }
    // Keep home identity explicit: colony order is meaningful, so callers use
    // this fixture's second-last home and last source without sorting either.
    return state;
}

GameState createSiteDevelopmentScenario(bool usefulIce) {
    GameState s=createHomeSystemScenario();
    const BodyId home{s.ids.nextBodyId++},target{s.ids.nextBodyId++};
    s.bodies.push_back(Body{.id=home,.systemId=s.starSystems.front().id,.name="P4B Industrial Base",
        .type=BodyType::Asteroid,.strategicZone=StrategicZone::DeepSurveyFrontier,
        .parentBodyId=std::nullopt,.x=0,.y=0});
    s.bodies.push_back(Body{.id=target,.systemId=s.starSystems.front().id,.name="Uninvestigated Ice Prospect",
        .type=BodyType::Asteroid,.strategicZone=StrategicZone::DeepSurveyFrontier,
        .parentBodyId=std::nullopt,.x=100,.y=0});
    Colony c;c.id=ColonyId{s.ids.nextColonyId++};c.bodyId=home;c.name="P4B Supply Base";
    c.processedStockpile.amount.fill(10000);c.stockpile.set(Mineral::Volatiles,10000);
    c.processorCapacity=5;c.shipyardCapacity=5300;c.processingPolicy=ProcessingPolicy::Manual;
    c.manualProcessingAllocations={{ProcessedMaterial::Propellant,1}};
    c.ownerInstitutionId=s.institutions.front().id;s.colonies.push_back(c);
    for(auto& t:s.maintenanceTeams)if(std::find(t.qualifiedFamilies.begin(),t.qualifiedFamilies.end(),*s.siteConstructionFamilyId)!=t.qualifiedFamilies.end())t.colonyId=c.id;
    if(usefulIce)s.mineralDeposits.push_back({target,Mineral::WaterIce,100000,1});
    return s;
}

} // namespace deep
