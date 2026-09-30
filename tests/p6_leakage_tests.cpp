// Compares player-facing query, forecast and admission surfaces across worlds
// differing only in hidden geology or untested technical candidate truth.
#include "app/ForecastService.h"
#include "app/P6ProvingFixture.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace deep;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

CreateSiteDevelopmentCommand blindSite(const GameState& state) {
    CreateSiteDevelopmentCommand command;
    command.charter.supportColonyId = state.colonies.back().id;
    command.charter.package = referenceSitePackage();
    command.charter.assignments.name = "Uninvestigated physical investment";
    NewResourceSite site;
    site.bodyId = state.bodies.back().id;
    site.name = "Unknown ice prospect";
    command.newSite = site;
    return command;
}

void compareCommonPublicSurfaces(const SimulationService& left, const SimulationService& right) {
    SimulationQueries a(left), b(right);
    ForecastService fa(left), fb(right);
    require(a.bodySystemOverview() == b.bodySystemOverview(), "body overview revealed hidden-world rows");
    require(a.explorationIntelligence() == b.explorationIntelligence(),
            "exploration summary revealed unacquired truth");
    for (const auto& body : a.bodySystemOverview()) {
        require(a.bodyDeposits(body.id) == b.bodyDeposits(body.id),
                "declared mineral channels or assessments revealed truth");
        require(a.evidenceDossier(body.id).observations == b.evidenceDossier(body.id).observations,
                "raw scientific evidence differs before acquisition");
    }
    const auto processedA = fa.processedMaterialForecastCauseChains();
    const auto processedB = fb.processedMaterialForecastCauseChains();
    const bool processedEqual =
        processedA.size() == processedB.size() &&
        std::equal(
            processedA.begin(), processedA.end(), processedB.begin(), [](const auto& x, const auto& y) {
                return x.material == y.material && x.materialName == y.materialName &&
                       x.stockpile == y.stockpile && x.processingIncomePerDay == y.processingIncomePerDay &&
                       x.committedDemandPerDay == y.committedDemandPerDay && x.netPerDay == y.netPerDay &&
                       x.stockpileRunoutDays == y.stockpileRunoutDays && x.causes == y.causes;
            });
    require(fa.mineralIncomePerDay() == fb.mineralIncomePerDay() &&
                fa.mineralForecastCauseChains() == fb.mineralForecastCauseChains() && processedEqual &&
                fa.depositExhaustionEstimates() == fb.depositExhaustionEstimates(),
            "knowledge-limited economy or geology forecast revealed truth");
    require(a.shipyardOrders().size() == b.shipyardOrders().size() &&
                a.productionBacklog().size() == b.productionBacklog().size() &&
                a.surveyPrograms().size() == b.surveyPrograms().size() &&
                a.analysisPrograms().size() == b.analysisPrograms().size() &&
                a.freightPrograms().size() == b.freightPrograms().size() &&
                a.technicalDevelopments().size() == b.technicalDevelopments().size(),
            "pending or operating intentions differ before physical interaction");
    const auto classesA = a.shipClasses(), classesB = b.shipClasses();
    require(classesA.size() == classesB.size(), "public ship class catalog size differs");
    for (std::size_t index = 0; index < classesA.size(); ++index)
        require(classesA[index].id == classesB[index].id && classesA[index].name == classesB[index].name &&
                    classesA[index].design.buildPoints == classesB[index].design.buildPoints,
                "public class build consequence differs before interaction");
    const auto designA = a.previewShipDesign(referenceSurveyCutterComponents());
    const auto designB = b.previewShipDesign(referenceSurveyCutterComponents());
    require(designA.design.buildPoints == designB.design.buildPoints &&
                designA.design.buildCost.amount == designB.design.buildCost.amount &&
                designA.design.surveyCapability == designB.design.surveyCapability,
            "design preview read hidden geology or untested technical truth");
}

void blind_geology_has_equal_public_admission_and_advice() {
    auto useful = createP6EstablishedStartingWorld(true);
    auto absent = createP6EstablishedStartingWorld(false);
    const auto command = blindSite(useful);
    SimulationService a(useful), b(absent);
    compareCommonPublicSurfaces(a, b);
    SimulationQueries qa(a), qb(b);
    const auto previewA = qa.previewSiteDevelopment(command);
    const auto previewB = qb.previewSiteDevelopment(command);
    require(previewA.structurallyValid && previewB.structurallyValid &&
                previewA.validationMessage == previewB.validationMessage &&
                previewA.condition == previewB.condition &&
                previewA.package.cost.amount == previewB.package.cost.amount &&
                previewA.package.ratedExtraction == previewB.package.ratedExtraction,
            "blind site preview must not predict hidden outcome");
    const auto admittedA = a.execute(command), admittedB = b.execute(command);
    require(admittedA.ok && admittedB.ok && admittedA.message == admittedB.message,
            "same uninformed site intention has same admission outcome");
    compareCommonPublicSurfaces(a, b);
    const auto sitesA = qa.sites(), sitesB = qb.sites();
    require(sitesA.size() == 1 && sitesB.size() == 1 &&
                sitesA.front().condition == sitesB.front().condition &&
                sitesA.front().rawOccupancy == sitesB.front().rawOccupancy &&
                sitesA.front().site.installed.empty() && sitesB.front().site.installed.empty(),
            "registered cold site must not reveal geology or create capability");
    FreightProgramCharter freight;
    freight.name = "Blind site support";
    freight.source = useful.colonies.back().id;
    freight.destination = sitesA.front().site.id;
    freight.operatingBaseColonyId = useful.colonies.back().id;
    freight.commodity = ProcessedMaterial::ReactorFuel;
    freight.totalQuantity = 5;
    const auto freightA = qa.previewFreightProgramCharter(freight);
    const auto freightB = qb.previewFreightProgramCharter(freight);
    require(freightA.structurallyValid == freightB.structurallyValid &&
                freightA.validationMessage == freightB.validationMessage &&
                freightA.executionCondition == freightB.executionCondition &&
                freightA.waitingReasons == freightB.waitingReasons,
            "freight endpoint and readiness advice must be independent of hidden deposit");
}

void untested_technical_truth_has_equal_public_admission_and_advice() {
    auto good = createP6EstablishedStartingWorld();
    auto miss = good;
    good.technologyCandidateTruths.front().achievedDetectionThreshold = 6.0;
    miss.technologyCandidateTruths.front().achievedDetectionThreshold = 9.0;
    SimulationService a(good), b(miss);
    compareCommonPublicSurfaces(a, b);
    SimulationQueries qa(a), qb(b);
    const auto opportunitiesA = qa.technologyOpportunities();
    const auto opportunitiesB = qb.technologyOpportunities();
    require(opportunitiesA.size() == opportunitiesB.size() &&
                opportunitiesA.front().status == opportunitiesB.front().status &&
                !opportunitiesA.front().demonstratedThreshold &&
                !opportunitiesB.front().demonstratedThreshold &&
                opportunitiesA.front().opportunity.targetDetectionThreshold ==
                    opportunitiesB.front().opportunity.targetDetectionThreshold,
            "public opportunity target must not expose untested achieved threshold");
    const auto catalogA = qa.shipComponents(), catalogB = qb.shipComponents();
    require(catalogA.size() == catalogB.size(), "unearned component catalog differs");
    for (std::size_t index = 0; index < catalogA.size(); ++index)
        require(catalogA[index].id == catalogB[index].id && catalogA[index].name == catalogB[index].name &&
                    catalogA[index].buildCost.amount == catalogB[index].buildCost.amount &&
                    catalogA[index].measurementProfile == catalogB[index].measurementProfile &&
                    catalogA[index].demonstrated == catalogB[index].demonstrated,
                "catalog revealed untested candidate outcome");
    const auto facility = good.technicalFacilities.front();
    const auto team = std::find_if(good.maintenanceTeams.begin(), good.maintenanceTeams.end(),
                                   [](const auto& row) { return row.name == "Prototype Engineering Team"; });
    TechnicalDevelopmentCharter charter{"Blind optional development",
                                        good.technologyOpportunities.front().id,
                                        facility.colonyId,
                                        facility.id,
                                        team->id,
                                        good.people.front().id,
                                        TechnicalDevelopmentScope::DemonstratePrototype,
                                        {}};
    const auto previewA = qa.previewTechnicalDevelopment(charter);
    const auto previewB = qb.previewTechnicalDevelopment(charter);
    require(previewA.structurallyValid && previewB.structurallyValid &&
                previewA.condition == previewB.condition && previewA.requiredWork == previewB.requiredWork &&
                previewA.requiredMaterials.amount == previewB.requiredMaterials.amount,
            "technical work preview must not substitute hidden candidate truth");
    const auto admittedA = a.execute(CreateTechnicalDevelopmentCommand{charter});
    const auto admittedB = b.execute(CreateTechnicalDevelopmentCommand{charter});
    require(admittedA.ok && admittedB.ok && admittedA.message == admittedB.message,
            "technical intent admission must not depend on achieved threshold");
    compareCommonPublicSurfaces(a, b);
}
} // namespace

int main() {
    try {
        blind_geology_has_equal_public_admission_and_advice();
        untested_technical_truth_has_equal_public_admission_and_advice();
        std::cout << "P6 public projection isolation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P6 leakage proof failed: " << error.what() << '\n';
        return 1;
    }
}
