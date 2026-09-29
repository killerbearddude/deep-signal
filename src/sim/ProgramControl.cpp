// Coordinates identity, stable dispatch order and phase-local spendable stock.
// It has no transfer executor and creates no additional inventory authority.
#include "sim/ProgramControl.h"
#include "sim/FreightProgramRules.h"
#include "sim/MaintenanceProgramRules.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace deep {

std::optional<ProgramController> controllingProgram(const GameState& state, const FleetId fleetId) {
    for (const auto& program : state.surveyPrograms) {
        if (program.leasedFleetId == fleetId) return program.id;
    }
    for (const auto& program : state.freightPrograms) {
        if (program.leasedFleetId == fleetId) return program.id;
    }
    for (const auto& program : state.maintenancePrograms) {
        if (program.leasedTenderId == fleetId) return program.id;
    }
    return std::nullopt;
}

std::optional<ProgramController> controllingScientificTeam(const GameState& s,SurveyTeamId id) {
    for (const auto& p : s.surveyPrograms) if (p.leasedTeamId==id) return p.id;
    for (const auto& p : s.analysisPrograms) if (p.leasedTeamId==id) return p.id;
    return std::nullopt;
}

std::string programControllerLabel(const GameState& state, const ProgramController& owner) {
    return std::visit([&](const auto id) {
        using IdType = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<IdType, SurveyProgramId>) {
            for (const auto& p : state.surveyPrograms) if (p.id == id) return "survey program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
            return std::string("survey program ") + std::to_string(id.value);
        } else if constexpr (std::is_same_v<IdType, FreightProgramId>) {
            for (const auto& p : state.freightPrograms) if (p.id == id) return "freight program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
            return std::string("freight program ") + std::to_string(id.value);
        } else if constexpr (std::is_same_v<IdType, MaintenanceProgramId>) {
            for (const auto& p : state.maintenancePrograms) if (p.id == id) return "maintenance program " + p.charter.name + " (#" + std::to_string(id.value) + ")";
            return std::string("maintenance program ") + std::to_string(id.value);
        } else {
            for (const auto& p : state.analysisPrograms) if (p.id==id) return "analysis program "+p.charter.name+" (#"+std::to_string(id.value)+")";
            return std::string("analysis program ")+std::to_string(id.value);
        }
    }, owner);
}

std::vector<ProgramController> programOpeningOrder(const GameState& state) {
    std::vector<ProgramController> result;
    result.reserve(state.surveyPrograms.size() + state.freightPrograms.size() + state.maintenancePrograms.size() + state.analysisPrograms.size());
    std::size_t s = 0, f = 0, m = 0, a = 0;
    while (s < state.surveyPrograms.size() || f < state.freightPrograms.size() || m < state.maintenancePrograms.size() || a < state.analysisPrograms.size()) {
        int selected = -1;
        std::int64_t day = 0;
        const auto consider = [&](int kind, std::int64_t createdDay) {
            // Strict comparison keeps Survey/Freight/Maintenance/Analysis tie order.
            if (selected == -1 || createdDay < day) { selected = kind; day = createdDay; }
        };
        if (s < state.surveyPrograms.size()) consider(0,state.surveyPrograms[s].createdDay);
        if (f < state.freightPrograms.size()) consider(1,state.freightPrograms[f].createdDay);
        if (m < state.maintenancePrograms.size()) consider(2,state.maintenancePrograms[m].createdDay);
        if (a < state.analysisPrograms.size()) consider(3,state.analysisPrograms[a].createdDay);
        if (selected == 0) result.emplace_back(state.surveyPrograms[s++].id);
        else if (selected == 1) result.emplace_back(state.freightPrograms[f++].id);
        else if (selected == 2) result.emplace_back(state.maintenancePrograms[m++].id);
        else result.emplace_back(state.analysisPrograms[a++].id);
    }
    return result;
}

std::optional<ProgramPendingIssue> pendingProgramIssue(const GameState& state) {
    for (const auto& owner : programOpeningOrder(state)) {
        const auto issue = std::visit([&](const auto id) -> std::optional<ProgramPendingIssue> {
            const auto inspect = [&](const auto& programs) -> std::optional<ProgramPendingIssue> {
                for (const auto& p : programs) {
                    if (p.id == id && !p.issue.signature.empty() && !p.issue.acknowledged) {
                        return ProgramPendingIssue{owner, p.issue.message};
                    }
                }
                return std::nullopt;
            };
            if constexpr (std::is_same_v<std::decay_t<decltype(id)>, SurveyProgramId>) return inspect(state.surveyPrograms);
            else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, FreightProgramId>) return inspect(state.freightPrograms);
            else if constexpr (std::is_same_v<std::decay_t<decltype(id)>, MaintenanceProgramId>) return inspect(state.maintenancePrograms);
            else return inspect(state.analysisPrograms);
        }, owner);
        if (issue) return issue;
    }
    return std::nullopt;
}

OpeningProgramContext::OpeningProgramContext(const GameState& state) {
    for (const auto& p : state.analysisPrograms)
        if (p.leasedTeamId) occupiedTeams.insert(p.leasedTeamId->value);
    for (const auto& b : state.observations)
        if (b.availableDay<=state.date.day) availableObservations.insert(b.id.value);
    for (const auto& c : state.colonies) analysisThroughput.push_back({c.id,c.analysisCapacity});
    stock.reserve(state.colonies.size());
    for (const auto& colony : state.colonies) stock.push_back({colony.id, colony.processedStockpile});
    for (const auto& p : state.surveyPrograms) {
        if (p.leasedFleetId) occupiedFleets.insert(p.leasedFleetId->value);
        if (p.leasedTeamId) occupiedTeams.insert(p.leasedTeamId->value);
    }
    for (const auto& p : state.freightPrograms) if (p.leasedFleetId) occupiedFleets.insert(p.leasedFleetId->value);
    for (const auto& p : state.maintenancePrograms) {
        if (p.leasedTenderId) occupiedFleets.insert(p.leasedTenderId->value);
        if (p.leasedTeamId) occupiedMaintenanceTeams.insert(p.leasedTeamId->value);
        if (const auto* job = activeServiceJob(p)) serviceHolds.insert(job->clientFleetId.value);
    }
    for (const auto& p : state.surveyPrograms) {
        if (eligibleServiceClient(state,p) && surveyServiceRequest(state,p).requested) {
            eligibleServiceClients.insert(p.id.value);
            serviceHolds.insert(p.leasedFleetId->value);
        }
    }
}

double OpeningProgramContext::available(const GameState& state, const ColonyId colonyId,
                                        const ProcessedMaterial material, const double floor) const {
    const auto colony = std::find_if(state.colonies.begin(), state.colonies.end(),
                                     [=](const auto& c) { return c.id == colonyId; });
    const auto budget = std::find_if(stock.begin(), stock.end(),
                                     [=](const auto& c) { return c.colonyId == colonyId; });
    if (colony == state.colonies.end() || budget == stock.end()) return 0.0;
    return std::max(0.0, std::min(colony->processedStockpile.get(material), budget->remaining.get(material)) - floor);
}

void OpeningProgramContext::debit(const ColonyId colonyId, const ProcessedMaterial material, const double amount) {
    for (auto& budget : stock) {
        if (budget.colonyId != colonyId) continue;
        const double remaining = budget.remaining.get(material) - amount;
        if (!std::isfinite(amount) || amount < 0.0 ||
            (remaining < 0.0 && !freightNearlyEqual(remaining, 0.0))) {
            throw std::logic_error("Program withdrawal exceeded opening stock budget");
        }
        budget.remaining.set(material, std::max(0.0, remaining));
        return;
    }
    throw std::logic_error("Program withdrawal references missing opening stock budget");
}

} // namespace deep
