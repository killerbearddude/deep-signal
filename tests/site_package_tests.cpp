// Independent P4B specification arithmetic and typed identity checks. A draft
// may lack useful equipment; only malformed structure/numbers are rejected.
#include "sim/SitePackageRules.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/StockTypes.h"
#include "sim/StockAccess.h"
#include "sim/SiteWorkRules.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace deep;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, const char* why) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(why);
}
void literal_package_totals() {
    const auto definitions = referenceSiteModuleCatalog();
    const auto package = referenceSitePackage();
    const auto p = evaluateSitePackage(definitions, 1, package);
    check(p.cost.get(ProcessedMaterial::StructuralAlloys) == 240 &&
              p.cost.get(ProcessedMaterial::Electronics) == 70 &&
              p.cost.get(ProcessedMaterial::IndustrialComposites) == 60,
          "Reference assembly bill must be 240/70/60");
    check(p.cost.get(ProcessedMaterial::Propellant) == 0 && p.cost.get(ProcessedMaterial::ReactorFuel) == 0,
          "Operating fuel is not charged in the installation bill");
    check(p.assemblyWorkdays == 14 && p.commissioningWorkdays == 2,
          "Reference assembly/commissioning must be 14+2");
    check(p.powerGeneration == 30 && p.powerDemand == 20 && p.powerMargin == 10 && p.ratedExtraction == 10 &&
              p.rawHandling == 50 && p.rawStorage == 200 && p.reactorFuelPerDuty == 1 &&
              p.compositesPerDuty == 1,
          "Reference nominal machinery and support values differ from handoff");
    const std::vector<SiteModuleInstall> storage{{SiteModuleKind::BulkStorage, 2}};
    const auto expansion = evaluateSitePackage(definitions, 1, storage);
    check(expansion.rawStorage == 400 && expansion.assemblyWorkdays == 4 &&
              expansion.commissioningWorkdays == 2 && expansion.ratedExtraction == 0 &&
              expansion.powerGeneration == 0,
          "Passive storage is a valid but incomplete package");
    auto deficient = package;
    deficient[0].quantity = 5;
    const auto overloaded = evaluateSitePackage(definitions, 1, deficient);
    check(overloaded.powerMargin < 0 && overloaded.ratedExtraction == 10,
          "Power deficit and limited automation do not invalidate a draft");
    check(package == referenceSitePackage(), "Read-only evaluation mutated the package");
}
void reference_builder_totals() {
    const auto state = createHomeSystemScenario();
    const auto builder = evaluateShipDesign(state.shipComponents, referenceBuilderComponents());
    check(builder.constructible && builder.dryMass == 570 && builder.usedVolume == 420 &&
              builder.powerGeneration == 120 && builder.powerDemand == 50 &&
              builder.propellantCapacity == 1000 && builder.buildPoints == 530,
          "literal builder physical totals");
    check(builder.buildCost.get(ProcessedMaterial::StructuralAlloys) == 310 &&
              builder.buildCost.get(ProcessedMaterial::Electronics) == 60 &&
              builder.buildCost.get(ProcessedMaterial::ReactorFuel) == 20 &&
              builder.buildCost.get(ProcessedMaterial::IndustrialComposites) == 60,
          "literal builder material bill");
    check(builder.workshopRates.size() == 1 &&
              builder.workshopRates.front().familyId == state.siteConstructionFamilyId &&
              builder.workshopRates.front().teamWorkdaysPerDay == 1,
          "construction family binding and rate");
    const auto cutter = evaluateShipDesign(state.shipComponents, referenceSurveyCutterComponents());
    const auto freight = evaluateShipDesign(state.shipComponents, referenceFreighterComponents());
    check(cutter.buildPoints == 500 && cutter.workshopRates.empty() && freight.cargoCapacity == 200 &&
              freight.cargoHandlingPerDay == 50 && freight.workshopRates.empty(),
          "predecessor designs unchanged");
    check(state.ships.empty() && state.fleets.empty() && state.resourceSites.empty(),
          "reference design grants no physical assets");
}
void malformed_definitions_and_rows() {
    const auto catalog = referenceSiteModuleCatalog();
    for (double value :
         {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto malformed = catalog;
        malformed[0].powerDemand = value;
        rejects([&] { (void)evaluateSitePackage(malformed, 1, referenceSitePackage()); },
                "Malformed numeric catalog data accepted");
    }
    auto overflow = catalog;
    overflow[0].assemblyWorkdays = std::numeric_limits<double>::max();
    rejects(
        [&] {
            (void)evaluateSitePackage(overflow, 1,
                                      std::vector<SiteModuleInstall>{{SiteModuleKind::IceExtraction, 2}});
        },
        "Overflowing derived work accepted");
    for (const auto& rows :
         std::vector<std::vector<SiteModuleInstall>>{{},
                                                     {{SiteModuleKind::Power, 0}},
                                                     {{SiteModuleKind::Power, -1}},
                                                     {{SiteModuleKind::Power, 1}, {SiteModuleKind::Power, 2}},
                                                     {{static_cast<SiteModuleKind>(99), 1}}})
        rejects([&] { (void)evaluateSitePackage(catalog, 1, rows); }, "Invalid package structure accepted");
    rejects([&] { (void)evaluateSitePackage(catalog, 2, referenceSitePackage()); },
            "Missing frozen catalog version accepted");
}
void proportional_work_and_supported_duty() {
    SiteAssemblyInputs work;
    work.requiredWork = 3;
    work.teamRate = 1;
    work.workshopRate = 1;
    work.totalCost = referenceSiteModuleCatalog()[1].cost;
    work.currentStock = work.totalCost;
    for (int day = 0; day < 3; ++day) {
        work.openingStock = work.currentStock;
        const auto step = prepareSiteAssembly(work);
        check(step.canWork && step.work == 1,
              "Three-day assembly must not stall on fractional recipe rounding");
        work.completedWork += step.work;
        for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
            work.currentStock.amount[i] -= step.consumed.amount[i];
            work.lifetimeConsumed.amount[i] += step.consumed.amount[i];
        }
    }
    check(work.completedWork == 3 &&
              std::abs(work.lifetimeConsumed.get(ProcessedMaterial::Electronics) - 20) < 1e-12,
          "Actual proportional work/material accounting differs from the row bill");
    work.completedWork = 0;
    work.currentStock = {};
    work.openingStock = {};
    work.lifetimeConsumed = {};
    check(!prepareSiteAssembly(work).canWork, "Zero stock cannot produce assembly work");
    work.currentStock = work.totalCost;
    work.openingStock = work.totalCost;
    work.floors = work.totalCost;
    check(prepareSiteAssembly(work).cause == SiteWorkCause::MaterialFloor,
          "Protected input must explain its floor");
    work.floors = {};
    work.lifetimeAllowances = ProcessedMaterialSet{};
    check(prepareSiteAssembly(work).cause == SiteWorkCause::MaterialAllowance,
          "Zero material authority is a retained wait");
    SiteDutyInputs duty;
    duty.hasLeader = true;
    duty.installed = evaluateSitePackage(referenceSiteModuleCatalog(), 1, referenceSitePackage());
    duty.currentStock.set(ProcessedMaterial::ReactorFuel, .5);
    duty.currentStock.set(ProcessedMaterial::IndustrialComposites, 1);
    duty.openingStock = duty.currentStock;
    const auto half = prepareSiteDuty(duty);
    check(half.canWork && half.work == .5 && half.consumed.get(ProcessedMaterial::ReactorFuel) == .5 &&
              half.consumed.get(ProcessedMaterial::IndustrialComposites) == .5,
          "Half supported duty requires both actual supply debits");
    duty.openingStock = {};
    check(!prepareSiteDuty(duty).canWork, "Same-opening inbound support cannot power site operation");
    duty.openingStock = duty.currentStock;
    duty.lifetimeDutyAllowance = 0;
    check(prepareSiteDuty(duty).cause == SiteWorkCause::DutyAllowance,
          "Zero operating authority permits no duty");
    duty.lifetimeDutyAllowance.reset();
    duty.installed.rawStorage = 0;
    duty.installed.ratedExtraction = 0;
    check(prepareSiteDuty(duty).work == .5,
          "Supported machinery costs time/supply independently of output or storage");
}

void typed_inventory_access() {
    // This detached accessor fixture isolates identity selection from transfer
    // permission. The eventual executor still supplies capacity/handling checks.
    GameState state;
    Colony colony;
    colony.id = ColonyId{1};
    colony.bodyId = BodyId{1};
    colony.name = "Base";
    colony.stockpile.set(Mineral::Iron, 11);
    colony.processedStockpile.set(ProcessedMaterial::StructuralAlloys, 22);
    ResourceSite site;
    site.id = SiteId{1};
    site.bodyId = BodyId{1};
    site.name = "Distinct staging location";
    check(site.installed.empty() && site.rawStock.get(Mineral::Iron) == 0 &&
              site.processedStock.get(ProcessedMaterial::StructuralAlloys) == 0,
          "A site record must not create hardware or stocks");
    site.rawStock.set(Mineral::Iron, 33);
    site.processedStock.set(ProcessedMaterial::StructuralAlloys, 44);
    state.colonies.push_back(colony);
    state.resourceSites.push_back(site);
    check(stockQuantity(state, ColonyId{1}, Mineral::Iron) == 11 &&
              stockQuantity(state, ColonyId{1}, ProcessedMaterial::StructuralAlloys) == 22 &&
              stockQuantity(state, SiteId{1}, Mineral::Iron) == 33 &&
              stockQuantity(state, SiteId{1}, ProcessedMaterial::StructuralAlloys) == 44,
          "Raw/processed and colony/site identities must select four distinct stores");
    stockQuantity(state, SiteId{1}, Mineral::Iron) += 5;
    check(state.resourceSites.front().rawStock.get(Mineral::Iron) == 38 &&
              state.colonies.front().stockpile.get(Mineral::Iron) == 11,
          "Accessor must reference actual inventory rather than a copied stock registry");
    check(stockLocationBody(state, ColonyId{1}) == stockLocationBody(state, SiteId{1}) &&
              stockLocationName(state, SiteId{1}) == site.name,
          "Distinct same-body stores retain public metadata");
    rejects([&] { (void)stockQuantity(state, SiteId{99}, Mineral::Iron); },
            "Missing stock identity silently defaulted");
}

void distinct_stock_identities() {
    check(commodityFromStored(0, 0) != commodityFromStored(1, 0),
          "Stored commodity tags must survive equal numeric values");
    check(stockLocationFromStored(0, 1) != stockLocationFromStored(1, 1),
          "Stored endpoint tags must preserve namespace");
    rejects([] { (void)commodityFromStored(0, 11); }, "Raw Water Ice ordinal accepted as processed material");
    rejects([] { (void)commodityFromStored(2, 0); }, "Unsupported commodity tag accepted");
    rejects([] { (void)stockLocationFromStored(2, 1); }, "Unsupported endpoint tag accepted");
    rejects([] { (void)stockLocationFromStored(1, 0); }, "Zero endpoint ID accepted");
    const Commodity processed = ProcessedMaterial::StructuralAlloys, raw = Mineral::Iron;
    check(processed != raw && validCommodity(processed) && validCommodity(raw),
          "Equal numeric commodity ordinals aliased");
    check(StockLocation{ColonyId{1}} != StockLocation{SiteId{1}}, "Equal colony/site IDs aliased");
    check(!validStockLocation(StockLocation{SiteId{0}}) && !validCommodity(Commodity{Mineral::Count}),
          "Malformed identity accepted");
    check(commodityName(Commodity{Mineral::WaterIce}) == "Water Ice" &&
              commodityName(Commodity{ProcessedMaterial::Propellant}) == "Propellant",
          "Commodity labels lost their type");
}
} // namespace
int main() {
    try {
        literal_package_totals();
        reference_builder_totals();
        malformed_definitions_and_rows();
        proportional_work_and_supported_duty();
        distinct_stock_identities();
        typed_inventory_access();
        std::cout << "Site package and stock identity tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
