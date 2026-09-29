// Site screens consume copied records and the same pure construction/support
// rules as execution. Changing physical geology cannot change these previews.
#include "app/SimulationQueries.h"
#include "sim/SiteOperationRules.h"
#include "sim/SiteOperationExecution.h"
#include "sim/SiteDevelopmentRules.h"
#include "sim/StockAccess.h"
#include <algorithm>
#include <limits>
namespace deep {
std::vector<StockLocationSummary> SimulationQueries::stockLocations() const {
    std::vector<StockLocationSummary> result;
    for (const auto& c : service_.state().colonies)
        result.push_back({c.id, c.bodyId, "Colony: " + c.name});
    for (const auto& s : service_.state().resourceSites)
        result.push_back({s.id, s.bodyId, "Site: " + s.name});
    return result;
}
std::vector<SiteModuleDefinition> SimulationQueries::siteModuleCatalog() const {
    return service_.state().siteModuleCatalog;
}
std::vector<SiteSummary> SimulationQueries::sites() const {
    const auto& s = service_.state();
    std::vector<SiteSummary> result;
    for (const auto& at : s.resourceSites) {
        SiteSummary r;
        r.site = at;
        r.installed =
            siteCapabilities(
                s, at, (s.date.day == std::numeric_limits<std::int64_t>::max() ? s.date.day : s.date.day + 1))
                .equipment;
        r.condition = siteOperatingCondition(s, at);
        r.rawOccupancy = siteRawOccupied(at);
        r.dutySpent = siteDutySpent(at);
        const auto duty = siteDutyPreview(
            s, at, (s.date.day == std::numeric_limits<std::int64_t>::max() ? s.date.day : s.date.day + 1));
        r.supportedDuty = duty.work;
        r.rawHandling = siteRawHandlingPreview(
            s, at.id, (s.date.day == std::numeric_limits<std::int64_t>::max() ? s.date.day : s.date.day + 1));
        r.constructionOwner = controllingSiteConstruction(s, at.id);
        for (const auto& b : s.bodies)
            if (b.id == at.bodyId)
                r.bodyName = b.name;
        for (const auto& p : s.freightPrograms)
            if (p.charter.source == StockLocation{at.id} || p.charter.destination == StockLocation{at.id})
                r.relatedFreight.push_back(p.id);
        result.push_back(std::move(r));
    }
    return result;
}
std::vector<SiteDevelopmentSummary> SimulationQueries::siteDevelopments() const {
    const auto& s = service_.state();
    std::vector<SiteDevelopmentSummary> result;
    for (const auto& p : s.siteDevelopmentPrograms) {
        SiteDevelopmentSummary r;
        r.program = p;
        r.condition = siteDevelopmentExecutionCondition(s, p);
        r.siteName = stockLocationName(s, p.charter.siteId);
        r.supportName = stockLocationName(s, p.charter.supportColonyId);
        r.package = evaluateSitePackage(s.siteModuleCatalog, p.charter.catalogVersion, p.charter.package);
        for (const auto& row : p.rows)
            r.assemblyWork += row.workCompleted;
        const auto builder = p.taskBuilderId ? p.taskBuilderId : p.charter.assignments.builderId;
        for (const auto& f : s.fleets)
            if (builder == f.id)
                r.builderName = f.name;
        const auto team = p.taskTeamId ? p.taskTeamId : p.charter.assignments.teamId;
        for (const auto& t : maintenanceTeams())
            if (team == t.team.id) {
                r.teamName = t.team.name;
                r.teamLocation = t.locationName;
            }
        for (const auto& f : s.freightPrograms)
            if (f.charter.source == StockLocation{p.charter.siteId} ||
                f.charter.destination == StockLocation{p.charter.siteId})
                r.relatedFreight.push_back(f.id);
        result.push_back(std::move(r));
    }
    return result;
}
SiteDevelopmentPreview
SimulationQueries::previewSiteDevelopment(const CreateSiteDevelopmentCommand& command) const {
    SiteDevelopmentPreview r;
    const auto& s = service_.state();
    auto charter = command.charter;
    if (command.newSite) {
        if (charter.siteId.value != 0) {
            r.validationMessage = "New site registration requires an unassigned site ID";
            return r;
        }
        if (command.newSite->institutionId &&
            std::none_of(s.institutions.begin(), s.institutions.end(),
                         [&](const auto& i) { return i.id == *command.newSite->institutionId; })) {
            r.validationMessage = "Site institution does not exist";
            return r;
        }
        if (std::none_of(s.bodies.begin(), s.bodies.end(),
                         [&](const auto& b) { return b.id == command.newSite->bodyId; })) {
            r.validationMessage = "Choose an existing public body";
            return r;
        }
        if (command.newSite->name.find_first_not_of(" \t\r\n") == std::string::npos) {
            r.validationMessage = "Site name is required";
            return r;
        }
        if (const auto error = validateSiteOperatingPolicy(s, command.newSite->operatingPolicy)) {
            r.validationMessage = *error;
            return r;
        }
        charter.siteId = SiteId{s.ids.nextSiteId};
    }
    if (const auto error = validateSiteDevelopmentCharter(s, charter, command.newSite.has_value())) {
        r.validationMessage = *error;
        return r;
    }
    r.structurallyValid = true;
    r.package = evaluateSitePackage(s.siteModuleCatalog, charter.catalogVersion, charter.package);
    r.validationMessage = "Valid intent; registration grants no stock or operating capability";
    if (command.newSite)
        r.condition = "New site will wait for physical delivery and field construction";
    else {
        SiteDevelopmentProgram p;
        p.charter = charter;
        p.rows.resize(charter.package.size());
        r.condition = siteDevelopmentExecutionCondition(s, p);
    }
    return r;
}
} // namespace deep
