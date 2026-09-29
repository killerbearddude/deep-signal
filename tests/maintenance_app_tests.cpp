// Owned projections and all application time paths consume the same physical
// condition, support authority and typed issue rules as authoritative commands.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/ScenarioFactory.h"
#include <iostream>
#include <stdexcept>
namespace {
using namespace deep;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void manual_preview_and_owned_condition() {
    SimulationService service(createTenderMaintenanceScenario());
    SimulationQueries queries(service);
    const auto fleet = service.state().fleets.front().id;
    const auto body = service.state().fleets.front().currentBodyId;
    const auto original = queries.equipmentConditions(fleet);
    require(original.size() == 1 && original.front().remainingDuty == 10 &&
                original.front().nominalCapability == 1,
            "owned physical DTO exposes duty separately from nominal class");
    require(queries.resourceSurveyPreview(fleet, body)->canSurvey,
            "healthy equipment supports immediate five-duty action");
    require(service.execute(ResourceSurveyCommand{fleet, body}).ok, "manual five-duty pass accepted");
    require(original.front().remainingDuty == 10 &&
                queries.equipmentConditions(fleet).front().remainingDuty == 5,
            "previous DTO remains owned across mutation");
    require(service.execute(ResourceSurveyCommand{fleet, body}).ok,
            "second full pass consumes remainingfive");
    const auto preview = queries.resourceSurveyPreview(fleet, body);
    require(!preview->canSurvey && preview->warningText.find("duty") != std::string::npos &&
                !service.execute(ResourceSurveyCommand{fleet, body}).ok,
            "preview and command agree on exhausted manual capability");
    require(queries.shipClasses().front().design.surveyCapability == 1,
            "physical condition does not erase nominal design capability");
    const auto catalog = queries.shipComponents();
    require(catalog.at(3).serviceProfile && catalog.at(7).workshopRates.size() == 1,
            "design catalog exposes service life and family workshops");
}
void provider_intent_history_and_issues() {
    auto state = createTenderMaintenanceScenario();
    state.ships.front().equipmentCondition.front().usedDuty = 10;
    SimulationService service(state);
    SimulationQueries queries(service);
    MaintenanceProgramCharter p;
    p.name = "App support";
    p.serviceColonyId = state.colonies.back().id;
    require(queries.previewMaintenanceCharter(p).structurallyValid,
            "absent tender/team/leader does not reject preview intent");
    require(service.execute(CreateMaintenanceProgramCommand{p}).ok, "unready support accepted");
    const auto id = service.state().maintenancePrograms.front().id;
    auto a = maintenanceAmendmentFromCharter(p);
    a.requestedTenderId = state.fleets.back().id;
    a.requestedTeamId = state.maintenanceTeams.front().id;
    a.requestedLeaderId = state.people.front().id;
    a.clients = {state.fleets.front().id};
    a.policy.lifetimeAllowances = ProcessedMaterialSet{};
    a.policy.lifetimeAllowances->set(ProcessedMaterial::Electronics, 2.5);
    a.policy.lifetimeAllowances->set(ProcessedMaterial::IndustrialComposites, 20);
    require(service.execute(AmendMaintenanceProgramCommand{id, a}).ok,
            "actual assets assigned without resource consumption");
    SurveyProgramCharter c;
    c.name = "Held app client";
    c.homeColonyId = p.serviceColonyId;
    c.requestedFleetId = state.fleets.front().id;
    c.requestedTeamId = state.surveyTeams.front().id;
    c.requestedLeaderId = state.people.front().id;
    c.targets = {{state.bodies.back().id, 0, 1}};
    c.policy.maintenanceProgramId = id;
    require(service.execute(CreateSurveyProgramCommand{c}).ok, "client policy accepted");
    const auto result = service.advanceDaysDetailed(30);
    require(result.interrupted && result.advancedDays == 2 && result.issueProgramId == ProgramController{id},
            "maintenance issue returns exact completed-day boundary and typed identity");
    const auto snapshot = queries.maintenancePrograms().front();
    require(snapshot.program.receipts.size() == 1 &&
                snapshot.nextWork.condition.find("allowance") != std::string::npos,
            "owned service history and real allowance blocker exposed");
    require(service.advanceDays(10).empty() &&
                service.execute(AdvanceDaysCommand{10}).message.find("Advanced 0") != std::string::npos,
            "legacy app and command time paths cannot bypass maintenance issue");
    require(queries.fleet(*a.requestedTenderId)->controllingProgram == ProgramController{id},
            "fleet view names maintenance controller kind");
    require(queries.fleet(*c.requestedFleetId)->controllingProgram == ProgramController{SurveyProgramId{1}},
            "held client retains survey movement ownership");
    require(service.execute(AcknowledgeMaintenanceIssueCommand{id, snapshot.program.issue.signature}).ok,
            "acknowledgment leaves limits unchanged");
    require(service.advanceDaysDetailed(3).advancedDays == 3 &&
                queries.equipmentConditions(*c.requestedFleetId).front().usedDuty == 5,
            "ack does not fabricate materials or restore duty");
    require(snapshot.program.receipts.size() == 1, "owned history survives later service calls");
    require(service.execute(SuspendMaintenanceProgramCommand{id}).ok &&
            service.execute(ResumeMaintenanceProgramCommand{id}).ok,
            "accepted limit survives an explicit provider stop and reacquisition");
    require(service.advanceDaysDetailed(40).advancedDays == 40,
            "unchanged acknowledged allowance does not reinterrupt after an idle/reacquisition gap");
}
} // namespace
int main() {
    try {
        manual_preview_and_owned_condition();
        provider_intent_history_and_issues();
        std::cout << "Maintenance app tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
