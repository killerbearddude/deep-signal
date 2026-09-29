#include "app/SiteDevelopmentFixture.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"
#include "sim/GameStateValidation.h"
#include <algorithm>
#include <stdexcept>
namespace deep {
namespace {
void command(Simulation& s, const SimCommand& c) {
    const auto r = s.execute(c);
    if (!r.ok)
        throw std::runtime_error("Site fixture command: " + r.message);
}
void acknowledge(Simulation& s) {
    for (const auto& p : s.state().siteDevelopmentPrograms)
        if (!p.issue.acknowledged)
            command(s, AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature});
    for (const auto& p : s.state().freightPrograms)
        if (!p.issue.acknowledged)
            command(s, AcknowledgeFreightProgramIssueCommand{p.id, p.issue.signature});
    for (const auto& at : s.state().resourceSites)
        if (!at.issue.acknowledged)
            command(s,
                    AcknowledgeSiteOperatingIssueCommand{at.id, at.issue.cause, at.issue.episodeStartedDay});
}
} // namespace
GameState earnSiteDevelopmentFixture(int throughDay, bool usefulIce) {
    if (throughDay < 1)
        throw std::invalid_argument("Site fixture requires at least one elapsed day");
    Simulation sim(createSiteDevelopmentScenario(usefulIce));
    const auto base = sim.state().colonies.back().id;
    const auto target = sim.state().bodies.back().id;
    const auto leader = sim.state().people.front().id;
    const auto team = sim.state().maintenanceTeams.back().id;
    command(sim, AssignShipyardBuildCommand{base, sim.state().shipClasses.at(2).id, 1});
    command(sim, AssignShipyardBuildCommand{base, sim.state().shipClasses.at(1).id, 5});
    if (sim.advanceDaysDetailed(1).advancedDays != 1 || sim.state().ships.size() != 6)
        throw std::runtime_error("Earned fixture hull construction failed");
    std::vector<FleetId> fleets;
    for (const auto& f : sim.state().fleets)
        fleets.push_back(f.id);
    SiteDevelopmentCharter development;
    development.supportColonyId = base;
    development.package = referenceSitePackage();
    development.assignments.name = "Unrestricted ice prospect development";
    development.assignments.builderId = fleets.front();
    development.assignments.teamId = team;
    development.assignments.leaderId = leader;
    NewResourceSite site;
    site.bodyId = target;
    site.name = "Uninvestigated ice working site";
    site.operatingPolicy.leaderId = leader;
    command(sim, CreateSiteDevelopmentCommand{development, site});
    const auto id = sim.state().resourceSites.front().id;
    const std::pair<ProcessedMaterial, double> deliveries[] = {{ProcessedMaterial::StructuralAlloys, 240},
                                                               {ProcessedMaterial::Electronics, 70},
                                                               {ProcessedMaterial::IndustrialComposites, 560},
                                                               {ProcessedMaterial::ReactorFuel, 500}};
    int index = 1;
    for (const auto& [material, quantity] : deliveries) {
        FreightProgramCharter c;
        c.name = "Site supply: " + std::string(toString(material));
        c.source = base;
        c.destination = id;
        c.operatingBaseColonyId = base;
        c.commodity = material;
        c.totalQuantity = quantity;
        c.requestedFleetId = fleets.at(static_cast<std::size_t>(index++));
        c.requestedLeaderId = leader;
        command(sim, CreateFreightProgramCommand{c});
    }
    FreightProgramCharter collect;
    collect.name = "Standing Water Ice collection";
    collect.source = id;
    collect.destination = base;
    collect.operatingBaseColonyId = base;
    collect.commodity = Mineral::WaterIce;
    collect.totalQuantity = 10000;
    collect.requestedFleetId = fleets.back();
    collect.requestedLeaderId = leader;
    command(sim, CreateFreightProgramCommand{collect});
    while (sim.state().date.day < throughDay) {
        // Inspection fixtures acknowledge disclosed decisions explicitly; no
        // budget, resource or trajectory is repaired to force a successful run.
        acknowledge(sim);
        const auto advanced = sim.advanceDaysDetailed(1);
        if (advanced.advancedDays != 1)
            throw std::runtime_error("Site fixture stalled: " + advanced.stopReason);
        validateGameState(sim.state());
    }
    return sim.state();
}
} // namespace deep
