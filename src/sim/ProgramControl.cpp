// Coordinates identity, stable dispatch order and phase-local spendable stock.
// It has no transfer executor and creates no additional inventory authority.
#include "sim/ProgramControl.h"
#include "sim/FreightProgramRules.h"
#include "sim/MaintenanceProgramRules.h"
#include "sim/StockAccess.h"
#include "sim/SiteOperationRules.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace deep {

std::optional<ProgramController> controllingProgram(const GameState& state, const FleetId fleetId) {
    for (const auto& program : state.surveyPrograms) {
        if (program.leasedFleetId == fleetId)
            return program.id;
    }
    for (const auto& program : state.freightPrograms) {
        if (program.leasedFleetId == fleetId)
            return program.id;
    }
    for (const auto& program : state.maintenancePrograms) {
        if (program.leasedTenderId == fleetId)
            return program.id;
    }
    for (const auto& p : state.siteDevelopmentPrograms)
        if (p.leasedBuilderId == fleetId)
            return p.id;
    return std::nullopt;
}

std::optional<ProgramController> controllingScientificTeam(const GameState& s, SurveyTeamId id) {
    for (const auto& p : s.surveyPrograms)
        if (p.leasedTeamId == id)
            return p.id;
    for (const auto& p : s.analysisPrograms)
        if (p.leasedTeamId == id)
            return p.id;
    return std::nullopt;
}

std::optional<ProgramController> controllingEngineeringTeam(const GameState& s, MaintenanceTeamId id) {
    for (const auto& p : s.maintenancePrograms)
        if (p.leasedTeamId == id)
            return p.id;
    for (const auto& p : s.siteDevelopmentPrograms)
        if (p.leasedTeamId == id)
            return p.id;
    for (const auto& p : s.technicalDevelopmentPrograms)
        if (p.leasedTeamId == id)
            return p.id;
    return std::nullopt;
}
std::optional<SiteDevelopmentProgramId> controllingSiteConstruction(const GameState& s, SiteId id) {
    for (const auto& p : s.siteDevelopmentPrograms)
        if (p.holdsSiteConstruction && p.charter.siteId == id)
            return p.id;
    return std::nullopt;
}

std::string programControllerLabel(const GameState& state, const ProgramController& owner) {
    return std::visit(
        [&](const auto id) {
            using IdType = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<IdType, SurveyProgramId>) {
                for (const auto& p : state.surveyPrograms)
                    if (p.id == id)
                        return "survey program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
                return std::string("survey program ") + std::to_string(id.value);
            } else if constexpr (std::is_same_v<IdType, FreightProgramId>) {
                for (const auto& p : state.freightPrograms)
                    if (p.id == id)
                        return "freight program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
                return std::string("freight program ") + std::to_string(id.value);
            } else if constexpr (std::is_same_v<IdType, MaintenanceProgramId>) {
                for (const auto& p : state.maintenancePrograms)
                    if (p.id == id)
                        return "maintenance program " + p.charter.name + " (#" + std::to_string(id.value) +
                               ")";
                return std::string("maintenance program ") + std::to_string(id.value);
            } else if constexpr (std::is_same_v<IdType, AnalysisProgramId>) {
                for (const auto& p : state.analysisPrograms)
                    if (p.id == id)
                        return "analysis program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
                return std::string("analysis program ") + std::to_string(id.value);
            } else if constexpr (std::is_same_v<IdType, SiteDevelopmentProgramId>) {
                for (const auto& p : state.siteDevelopmentPrograms)
                    if (p.id == id)
                        return "site development program " + p.charter.assignments.name + " (#" +
                               std::to_string(id.value) + ")";
                return std::string("site development program ") + std::to_string(id.value);
            } else {
                for (const auto& p : state.technicalDevelopmentPrograms)
                    if (p.id == id)
                        return "technical development program " + p.charter.name + " (#" +
                               std::to_string(id.value) + ")";
                return std::string("technical development program ") + std::to_string(id.value);
            }
        },
        owner);
}

std::vector<ProgramController> programOpeningOrder(const GameState& state) {
    std::vector<ProgramController> result;
    result.reserve(state.surveyPrograms.size() + state.freightPrograms.size() +
                   state.maintenancePrograms.size() + state.analysisPrograms.size() +
                   state.siteDevelopmentPrograms.size() + state.technicalDevelopmentPrograms.size());
    std::size_t s = 0, f = 0, m = 0, a = 0, d = 0, t = 0;
    while (s < state.surveyPrograms.size() || f < state.freightPrograms.size() ||
           m < state.maintenancePrograms.size() || a < state.analysisPrograms.size() ||
           d < state.siteDevelopmentPrograms.size() || t < state.technicalDevelopmentPrograms.size()) {
        int selected = -1;
        std::int64_t day = 0;
        const auto consider = [&](int kind, std::int64_t createdDay) {
            // Strict comparison keeps the declared six-kind tie order.
            if (selected == -1 || createdDay < day) {
                selected = kind;
                day = createdDay;
            }
        };
        if (s < state.surveyPrograms.size())
            consider(0, state.surveyPrograms[s].createdDay);
        if (f < state.freightPrograms.size())
            consider(1, state.freightPrograms[f].createdDay);
        if (m < state.maintenancePrograms.size())
            consider(2, state.maintenancePrograms[m].createdDay);
        if (a < state.analysisPrograms.size())
            consider(3, state.analysisPrograms[a].createdDay);
        if (d < state.siteDevelopmentPrograms.size())
            consider(4, state.siteDevelopmentPrograms[d].createdDay);
        if (t < state.technicalDevelopmentPrograms.size())
            consider(5, state.technicalDevelopmentPrograms[t].createdDay);
        if (selected == 0)
            result.emplace_back(state.surveyPrograms[s++].id);
        else if (selected == 1)
            result.emplace_back(state.freightPrograms[f++].id);
        else if (selected == 2)
            result.emplace_back(state.maintenancePrograms[m++].id);
        else if (selected == 3)
            result.emplace_back(state.analysisPrograms[a++].id);
        else if (selected == 4)
            result.emplace_back(state.siteDevelopmentPrograms[d++].id);
        else
            result.emplace_back(state.technicalDevelopmentPrograms[t++].id);
    }
    return result;
}

std::optional<ProgramPendingIssue> pendingProgramIssue(const GameState& state) {
    for (const auto& owner : programOpeningOrder(state)) {
        const auto issue = std::visit(
            [&](const auto id) -> std::optional<ProgramPendingIssue> {
                const auto inspect = [&](const auto& programs) -> std::optional<ProgramPendingIssue> {
                    for (const auto& p : programs) {
                        if (p.id == id && !p.issue.signature.empty() && !p.issue.acknowledged) {
                            return ProgramPendingIssue{owner, p.issue.message};
                        }
                    }
                    return std::nullopt;
                };
                if constexpr (std::is_same_v<std::decay_t<decltype(id)>, SurveyProgramId>)
                    return inspect(state.surveyPrograms);
                else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, FreightProgramId>)
                    return inspect(state.freightPrograms);
                else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, MaintenanceProgramId>)
                    return inspect(state.maintenancePrograms);
                else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, AnalysisProgramId>)
                    return inspect(state.analysisPrograms);
                else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, SiteDevelopmentProgramId>)
                    return inspect(state.siteDevelopmentPrograms);
                else
                    return inspect(state.technicalDevelopmentPrograms);
            },
            owner);
        if (issue)
            return issue;
    }
    return std::nullopt;
}

OpeningProgramContext::OpeningProgramContext(const GameState& state) {
    for (const auto& p : state.analysisPrograms)
        if (p.leasedTeamId)
            occupiedTeams.insert(p.leasedTeamId->value);
    for (const auto& b : state.observations)
        if (b.availableDay <= state.date.day)
            availableObservations.insert(b.id.value);
    for (const auto& c : state.colonies)
        analysisThroughput.push_back({c.id, c.analysisCapacity});
    for (const auto& facility : state.technicalFacilities)
        technicalFacilityThroughput.push_back({facility.id, facility.engineeringWorkdaysPerDay});
    stock.reserve(state.colonies.size() + state.resourceSites.size());
    for (const auto& c : state.colonies)
        stock.push_back({c.id, c.processedStockpile, c.stockpile});
    for (const auto& site : state.resourceSites) {
        stock.push_back({site.id, site.processedStock, site.rawStock});
        const auto cap = siteCapabilities(state, site, state.date.day);
        sites.push_back({site.id, cap.installedGroupCutoff, 0, 0,
                         std::max(0.0, cap.equipment.rawStorage - siteRawOccupied(site)),
                         cap.equipment.ratedExtraction, cap.equipment.rawStorage});
    }
    for (const auto& p : state.siteDevelopmentPrograms) {
        if (p.leasedBuilderId)
            occupiedFleets.insert(p.leasedBuilderId->value);
        if (p.leasedTeamId)
            occupiedMaintenanceTeams.insert(p.leasedTeamId->value);
        if (p.holdsSiteConstruction)
            occupiedConstructionSites.insert(p.charter.siteId.value);
    }
    for (const auto& p : state.technicalDevelopmentPrograms)
        if (p.leasedTeamId)
            occupiedMaintenanceTeams.insert(p.leasedTeamId->value);
    for (const auto& p : state.surveyPrograms) {
        if (p.leasedFleetId)
            occupiedFleets.insert(p.leasedFleetId->value);
        if (p.leasedTeamId)
            occupiedTeams.insert(p.leasedTeamId->value);
    }
    for (const auto& p : state.freightPrograms)
        if (p.leasedFleetId)
            occupiedFleets.insert(p.leasedFleetId->value);
    for (const auto& p : state.maintenancePrograms) {
        if (p.leasedTenderId)
            occupiedFleets.insert(p.leasedTenderId->value);
        if (p.leasedTeamId)
            occupiedMaintenanceTeams.insert(p.leasedTeamId->value);
        if (const auto* job = activeServiceJob(p))
            serviceHolds.insert(job->clientFleetId.value);
    }
    for (const auto& p : state.surveyPrograms) {
        if (eligibleServiceClient(state, p) && surveyServiceRequest(state, p).requested) {
            eligibleServiceClients.insert(p.id.value);
            serviceHolds.insert(p.leasedFleetId->value);
        }
    }
}

namespace {
double& budgetQuantity(StockOpeningBudget& b, const Commodity& c) {
    if (const auto* p = std::get_if<ProcessedMaterial>(&c))
        return b.processed.amount.at(processedMaterialIndex(*p));
    return b.raw.amount.at(mineralIndex(std::get<Mineral>(c)));
}
void subtractBudget(double& balance, double amount) {
    const double remaining = balance - amount;
    if (!std::isfinite(amount) || amount < 0 || (remaining < 0 && !freightNearlyEqual(remaining, 0)))
        throw std::logic_error("Program withdrawal exceeded opening budget");
    balance = std::max(0.0, remaining);
}
} // namespace
double OpeningProgramContext::available(const GameState& state, StockLocation location, Commodity material,
                                        double floor) const {
    for (const auto& b : stock)
        if (b.location == location) {
            const double opening = std::visit(
                [&](auto m) {
                    if constexpr (std::is_same_v<decltype(m), ProcessedMaterial>)
                        return b.processed.get(m);
                    else
                        return b.raw.get(m);
                },
                material);
            return std::max(0.0, std::min(stockQuantity(state, location, material), opening) - floor);
        }
    return 0;
}
void OpeningProgramContext::debit(StockLocation location, Commodity material, double amount) {
    for (auto& b : stock)
        if (b.location == location) {
            subtractBudget(budgetQuantity(b, material), amount);
            return;
        }
    throw std::logic_error("Program withdrawal references missing opening stock budget");
}
double OpeningProgramContext::availableSiteRawHandling(SiteId id) const {
    for (const auto& s : sites)
        if (s.siteId == id)
            return s.remainingHandling;
    return 0;
}
double OpeningProgramContext::availableSiteRawRoom(SiteId id) const {
    for (const auto& s : sites)
        if (s.siteId == id)
            return s.receivingRoom;
    return 0;
}
void OpeningProgramContext::debitSiteRawHandling(SiteId id, double amount) {
    for (auto& s : sites)
        if (s.siteId == id) {
            subtractBudget(s.remainingHandling, amount);
            return;
        }
    throw std::logic_error("Missing opening site handling");
}
void OpeningProgramContext::debitSiteRawRoom(SiteId id, double amount) {
    for (auto& s : sites)
        if (s.siteId == id) {
            subtractBudget(s.receivingRoom, amount);
            return;
        }
    throw std::logic_error("Missing opening site raw room");
}

double OpeningProgramContext::availableTechnicalFacility(TechnicalFacilityId id) const {
    const auto it = std::find_if(technicalFacilityThroughput.begin(), technicalFacilityThroughput.end(),
                                 [&](const auto& row) { return row.first == id; });
    return it == technicalFacilityThroughput.end() ? 0.0 : it->second;
}

void OpeningProgramContext::debitTechnicalFacility(TechnicalFacilityId id, double amount) {
    const auto it = std::find_if(technicalFacilityThroughput.begin(), technicalFacilityThroughput.end(),
                                 [&](const auto& row) { return row.first == id; });
    if (it == technicalFacilityThroughput.end() || !std::isfinite(amount) || amount < 0.0 ||
        it->second + 1e-9 < amount)
        throw std::logic_error("Technical-development work exceeded opening facility budget");
    it->second = std::max(0.0, it->second - amount);
}
std::optional<PendingDecision> pendingDecision(const GameState& state) {
    if (const auto issue = pendingProgramIssue(state))
        return PendingDecision{issue->controller, issue->message};
    for (const auto& s : state.resourceSites)
        if (s.issue.cause != SiteOperatingIssueCause::None && !s.issue.acknowledged)
            return PendingDecision{s.id, s.issue.message};
    return std::nullopt;
}
std::string decisionSourceLabel(const GameState& state, const DecisionSource& source) {
    if (const auto* p = std::get_if<ProgramController>(&source))
        return programControllerLabel(state, *p);
    return "site " + stockLocationName(state, std::get<SiteId>(source));
}
} // namespace deep
