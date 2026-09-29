// A compact owned-data workflow for cold-site registration, physical building
// and independently lasting operation. Scientific and operating records remain
// visibly separate; geological truth never enters authoring or button gating.
#include "ui_imgui/SiteDevelopmentPanel.h"
#include "ui_imgui/OperationalWindow.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
namespace deep::ui_imgui {
namespace {
constexpr std::array<const char*, siteModuleKindCount> moduleNames{
    "Ice extraction", "Site power", "Bulk handling", "Bulk storage", "Automation / support"};
template <class Id> long long number(Id id) {
    return static_cast<long long>(id.value);
}
template <class Id> long long number(const std::optional<Id>& id) {
    return id ? number(*id) : 0;
}
template <class Id>
void selectOptional(const char* label, std::optional<Id>& value,
                    const std::vector<std::pair<Id, std::string>>& choices) {
    std::string current = "Unassigned";
    for (const auto& [id, name] : choices)
        if (value == id)
            current = name + " (#" + std::to_string(id.value) + ")";
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("Unassigned", !value))
            value.reset();
        for (const auto& [id, name] : choices) {
            const auto title = name + " (#" + std::to_string(id.value) + ")";
            if (ImGui::Selectable(title.c_str(), value == id))
                value = id;
        }
        ImGui::EndCombo();
    }
}
template <class Id>
void selectRequired(const char* label, Id& value, const std::vector<std::pair<Id, std::string>>& choices) {
    std::optional<Id> selected = value ? std::optional{value} : std::nullopt;
    selectOptional(label, selected, choices);
    value = selected.value_or(Id{});
}
void materials(const char* title, const ProcessedMaterialSet& set) {
    ImGui::TextUnformatted(title);
    for (std::size_t i = 0; i < processedMaterialCount(); ++i)
        if (set.amount[i] != 0)
            ImGui::BulletText("%s: %.6g", std::string(toString(static_cast<ProcessedMaterial>(i))).c_str(),
                              set.amount[i]);
}
void allowance(const char* label, std::optional<double>& amount) {
    bool limited = amount.has_value();
    if (ImGui::Checkbox(label, &limited))
        amount = limited ? std::optional{0.0} : std::nullopt;
    if (amount) {
        ImGui::PushID(label);
        ImGui::InputDouble("Lifetime amount", &*amount, 1, 10, "%.3f");
        ImGui::PopID();
    }
}
void operatingEditor(SiteOperatingPolicy& p, const SimulationQueries& q) {
    std::vector<std::pair<PersonId, std::string>> leaders;
    for (const auto& person : q.personnel())
        leaders.push_back({person.id, person.name});
    selectOptional("Operating leader", p.leaderId, leaders);
    ImGui::Checkbox("Enable standing site operation", &p.enabled);
    ImGui::InputDouble("Requested Water Ice / day", &p.requestedIcePerDay, 1, 10, "%.3f");
    ImGui::InputDouble("Operating ReactorFuel stock floor", &p.reactorFuelFloor, 1, 10, "%.3f");
    ImGui::InputDouble("Operating composites stock floor", &p.compositesFloor, 1, 10, "%.3f");
    allowance("Limit lifetime supported duty", p.lifetimeDutyAllowance);
    ImGui::TextWrapped("Supported duty is measured in equivalent full-duty site-days. Zero allowance permits "
                       "no duty. A zero extraction target can retain supported raw handling.");
}
void copyName(std::array<char, 128>& out, const std::string& in) {
    out.fill('\0');
    std::memcpy(out.data(), in.data(), std::min(in.size(), out.size() - 1));
}
const char* lifecycle(const SiteDevelopmentProgram& p) {
    switch (p.lifecycle) {
    case SiteDevelopmentLifecycle::Authorized:
        return "Authorized";
    case SiteDevelopmentLifecycle::Suspended:
        return "Suspended";
    case SiteDevelopmentLifecycle::Closing:
        return p.closure == SiteDevelopmentClosure::Completed ? "Closing / Completed" : "Closing / Cancelled";
    case SiteDevelopmentLifecycle::Closed:
        return p.closure == SiteDevelopmentClosure::Completed ? "Closed / Completed" : "Closed / Cancelled";
    }
    return "Unknown";
}
} // namespace
void SiteDevelopmentPanel::resetWorldState() {
    draft_ = {};
    name_.fill('\0');
    siteName_.fill('\0');
    quantities_ = {1, 1, 1, 1, 1};
    registerSite_ = true;
    selectedDevelopment_.reset();
    editingDevelopment_.reset();
    selectedSite_.reset();
    editingOperation_.reset();
    operationDraft_ = {};
    notice_ = "Ready";
}
void SiteDevelopmentPanel::render(const SimulationQueries& queries, SimulationService& service,
                                  bool& visible) {
    if (!visible)
        return;
    if (!beginOperationalWindow("Sites / Development", &visible)) {
        ImGui::End();
        return;
    }
    const auto sites = queries.sites();
    const auto developments = queries.siteDevelopments();
    std::size_t commissioned = 0, building = 0;
    for (const auto& s : sites) {
        if (!s.site.installed.empty())
            ++commissioned;
        if (s.constructionOwner)
            ++building;
    }
    ImGui::Text("Registered sites: %zu | Under construction: %zu | Commissioned: %zu", sites.size(), building,
                commissioned);
    ImGui::TextWrapped(
        "A registered site is a body-associated working location. Landing terrain and environmental "
        "feasibility are abstracted. Registration grants no colony, mines, power, stock, or bulk storage.");
    if (ImGui::CollapsingHeader("Authorize or amend development", ImGuiTreeNodeFlags_DefaultOpen))
        renderEditor(queries, service);
    ImGui::SeparatorText("Development commitments");
    if (!developments.empty() && std::none_of(developments.begin(), developments.end(), [&](const auto& p) {
            return selectedDevelopment_ == p.program.id;
        }))
        selectedDevelopment_ = developments.front().program.id;
    for (const auto& row : developments) {
        const auto label = row.program.charter.assignments.name + " (#" +
                           std::to_string(row.program.id.value) + ") - " + lifecycle(row.program);
        if (ImGui::Selectable(label.c_str(), selectedDevelopment_ == row.program.id)) {
            selectedDevelopment_ = row.program.id;
            selectedSite_ = row.program.charter.siteId;
        }
    }
    for (const auto& row : developments)
        if (selectedDevelopment_ == row.program.id)
            renderDevelopment(row, queries, service);
    ImGui::SeparatorText("Lasting sites and operating responsibility");
    if (!sites.empty() &&
        std::none_of(sites.begin(), sites.end(), [&](const auto& s) { return selectedSite_ == s.site.id; }))
        selectedSite_ = sites.front().site.id;
    for (const auto& row : sites) {
        const auto label = row.site.name + " (#" + std::to_string(row.site.id.value) + ")";
        if (ImGui::Selectable(label.c_str(), selectedSite_ == row.site.id))
            selectedSite_ = row.site.id;
    }
    for (const auto& row : sites)
        if (selectedSite_ == row.site.id)
            renderSite(row, queries, service);
    ImGui::TextWrapped("Last action: %s", notice_.c_str());
    ImGui::End();
}
void SiteDevelopmentPanel::renderEditor(const SimulationQueries& q, SimulationService& service) {
    ImGui::InputText("Development name", name_.data(), name_.size());
    draft_.charter.assignments.name = name_.data();
    ImGui::BeginDisabled(editingDevelopment_.has_value());
    ImGui::Checkbox("Register a new site", &registerSite_);
    if (registerSite_) {
        if (!draft_.newSite)
            draft_.newSite = NewResourceSite{};
        draft_.charter.siteId = {};
        ImGui::InputText("Site name", siteName_.data(), siteName_.size());
        draft_.newSite->name = siteName_.data();
        std::vector<std::pair<BodyId, std::string>> bodies;
        for (const auto& body : q.bodySystemOverview())
            bodies.push_back({body.id, body.name});
        selectRequired("Known body", draft_.newSite->bodyId, bodies);
    } else {
        draft_.newSite.reset();
        std::vector<std::pair<SiteId, std::string>> sites;
        for (const auto& site : q.sites())
            sites.push_back({site.site.id, site.site.name});
        selectRequired("Existing site for additive expansion", draft_.charter.siteId, sites);
    }
    std::vector<std::pair<ColonyId, std::string>> colonies;
    for (const auto& colony : q.colonies())
        colonies.push_back({colony.id, colony.name});
    selectRequired("Fixed support colony", draft_.charter.supportColonyId, colonies);
    if (ImGui::Button("Reference package: one of each"))
        quantities_ = {1, 1, 1, 1, 1};
    for (std::size_t i = 0; i < quantities_.size(); ++i)
        ImGui::InputInt(moduleNames[i], &quantities_[i]);
    if (!editingDevelopment_) {
        draft_.charter.package.clear();
        for (std::size_t i = 0; i < quantities_.size(); ++i)
            if (quantities_[i] != 0)
                draft_.charter.package.push_back({static_cast<SiteModuleKind>(i), quantities_[i]});
    }
    ImGui::EndDisabled();
    if (editingDevelopment_)
        ImGui::TextUnformatted(
            "Package and route are frozen. Additional equipment requires a new expansion.");
    std::vector<std::pair<FleetId, std::string>> fleets;
    for (const auto& fleet : q.fleets())
        fleets.push_back({fleet.id, fleet.name});
    selectOptional("Requested builder fleet", draft_.charter.assignments.builderId, fleets);
    std::vector<std::pair<MaintenanceTeamId, std::string>> teams;
    for (const auto& team : q.maintenanceTeams())
        teams.push_back({team.team.id, team.team.name + " at " + team.locationName});
    selectOptional("Requested engineering team", draft_.charter.assignments.teamId, teams);
    std::vector<std::pair<PersonId, std::string>> leaders;
    for (const auto& person : q.personnel())
        leaders.push_back({person.id, person.name});
    selectOptional("Responsible construction leader", draft_.charter.assignments.leaderId, leaders);
    if (ImGui::TreeNode("Construction stock floors and lifetime limits")) {
        auto& p = draft_.charter.assignments.policy;
        ImGui::InputDouble("Home Propellant floor", &p.homePropellantFloor, 1, 10, "%.3f");
        allowance("Limit additional engine Propellant", p.additionalPropellantAllowance);
        ImGui::InputDouble("Return fuel contingency fraction", &p.returnContingencyFraction, 0.1, 1, "%.3f");
        bool capped = p.materialAllowances.has_value();
        if (ImGui::Checkbox("Limit lifetime construction materials", &capped))
            p.materialAllowances = capped ? std::optional{ProcessedMaterialSet{}} : std::nullopt;
        for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(std::string(toString(static_cast<ProcessedMaterial>(i))).c_str());
            ImGui::InputDouble("Site stock floor", &p.constructionFloors.amount[i], 1, 10, "%.3f");
            if (p.materialAllowances)
                ImGui::InputDouble("Lifetime material amount", &p.materialAllowances->amount[i], 1, 10,
                                   "%.3f");
            ImGui::PopID();
        }
        ImGui::TextUnformatted(
            "Zero allowance means zero permission; absent allowance means no program cap.");
        ImGui::TreePop();
    }
    if (draft_.newSite && ImGui::TreeNode("Initial independent operating policy")) {
        operatingEditor(draft_.newSite->operatingPolicy, q);
        ImGui::TreePop();
    }
    const auto preview = q.previewSiteDevelopment(draft_);
    ImGui::TextWrapped("%s", preview.structurallyValid ? preview.condition.c_str()
                                                       : preview.validationMessage.c_str());
    if (preview.structurallyValid) {
        const auto& e = preview.package;
        materials("Required delivered construction inputs", e.cost);
        ImGui::Text("Engineering labor %.3f + %.3f commissioning team-workdays", e.assemblyWorkdays,
                    e.commissioningWorkdays);
        ImGui::Text("Power %.3f generation / %.3f demand (margin %.3f)", e.powerGeneration, e.powerDemand,
                    e.powerMargin);
        ImGui::Text("Rated Ice %.3f/day | Shared raw handling %.3f/day | Raw storage %.3f", e.ratedExtraction,
                    e.rawHandling, e.rawStorage);
        ImGui::Text("Full duty support: ReactorFuel %.3f/day; composites %.3f/day", e.reactorFuelPerDuty,
                    e.compositesPerDuty);
    }
    ImGui::TextWrapped(
        "Yield and reserve lifetime are not established. Rated output assumes useful accessible Water Ice. "
        "Evidence, a builder, engineer, leader, fuel, and delivered supplies are not admission gates.");
    ImGui::BeginDisabled(!preview.structurallyValid);
    if (ImGui::Button(editingDevelopment_ ? "Save development amendment" : "Authorize site development")) {
        const auto result = editingDevelopment_ ? service.execute(AmendSiteDevelopmentCommand{
                                                      *editingDevelopment_, draft_.charter.assignments})
                                                : service.execute(draft_);
        notice_ = result.message;
        if (result.ok)
            editingDevelopment_.reset();
    }
    ImGui::EndDisabled();
    if (editingDevelopment_) {
        ImGui::SameLine();
        if (ImGui::Button("New development draft")) {
            editingDevelopment_.reset();
            draft_ = {};
            registerSite_ = true;
            name_.fill('\0');
            siteName_.fill('\0');
            quantities_ = {1, 1, 1, 1, 1};
        }
    }
}
void SiteDevelopmentPanel::renderDevelopment(const SiteDevelopmentSummary& row, const SimulationQueries& q,
                                             SimulationService& service) {
    const auto& p = row.program;
    ImGui::PushID(std::to_string(p.id.value).c_str());
    ImGui::Text("%s | %s -> %s", lifecycle(p), row.supportName.c_str(), row.siteName.c_str());
    ImGui::TextWrapped("%s", row.condition.c_str());
    ImGui::Text("Requested builder #%lld / engineer #%lld / leader #%lld",
                number(p.charter.assignments.builderId), number(p.charter.assignments.teamId),
                number(p.charter.assignments.leaderId));
    ImGui::Text("Actual leased builder #%lld / engineer #%lld | Site lease: %s", number(p.leasedBuilderId),
                number(p.leasedTeamId), p.holdsSiteConstruction ? "held" : "not held");
    ImGui::Text("Retained engineer: %s at %s", row.teamName.c_str(), row.teamLocation.c_str());
    for (const auto& fleet : q.fleets())
        if (fleet.id == p.taskBuilderId || fleet.id == p.charter.assignments.builderId) {
            ImGui::Text("Builder %s at %s | Engine Propellant %.3f / %.3f", fleet.name.c_str(),
                        fleet.currentBodyName.c_str(), fleet.currentFuel, fleet.fuelCapacity);
            if (fleet.destinationBodyId)
                ImGui::Text("Committed transit to %s; arrival day %lld", fleet.destinationBodyName.c_str(),
                            static_cast<long long>(fleet.activeOrderProjectedArrivalDay));
            if (fleet.controllingProgram)
                ImGui::TextWrapped("Builder controller: %s", fleet.controllingProgramLabel.c_str());
        }
    for (const auto& team : q.maintenanceTeams())
        if (team.team.id == p.charter.assignments.teamId && team.controller)
            ImGui::TextWrapped("Requested engineer controller: %s", team.controllerName.c_str());
    if (p.closure != SiteDevelopmentClosure::None && p.lifecycle != SiteDevelopmentLifecycle::Closed)
        ImGui::TextUnformatted(
            "Physical obligation: return original builders to support colony and disembark.");
    if (p.lifecycle != SiteDevelopmentLifecycle::Closed) {
        if (ImGui::Button("Amend development")) {
            editingDevelopment_ = p.id;
            draft_.charter = p.charter;
            draft_.newSite.reset();
            registerSite_ = false;
            copyName(name_, p.charter.assignments.name);
            quantities_.fill(0);
            for (const auto& module : p.charter.package)
                quantities_[static_cast<std::size_t>(module.kind)] = module.quantity;
        }
        ImGui::SameLine();
        if (p.lifecycle == SiteDevelopmentLifecycle::Suspended) {
            if (ImGui::Button("Resume development"))
                notice_ = service.execute(ResumeSiteDevelopmentCommand{p.id}).message;
        } else if (ImGui::Button("Suspend development"))
            notice_ = service.execute(SuspendSiteDevelopmentCommand{p.id}).message;
        ImGui::SameLine();
        if (ImGui::Button("Cancel development"))
            notice_ = service.execute(CancelSiteDevelopmentCommand{p.id}).message;
    }
    if (!p.issue.acknowledged) {
        ImGui::TextWrapped("Construction issue: %s", p.issue.message.c_str());
        if (ImGui::Button("Acknowledge construction issue"))
            notice_ =
                service.execute(AcknowledgeSiteDevelopmentIssueCommand{p.id, p.issue.signature}).message;
    }
    ImGui::Text("Assembly %.6g / %.6g; commissioning %.6g / 2 team-workdays", row.assemblyWork,
                row.package.assemblyWorkdays, p.commissioningWork);
    ImGui::Text("Engine Propellant loaded %.6g; burned %.6g", p.fuelLoaded, p.fuelBurned);
    if (p.commissionedDay)
        ImGui::Text("Commissioned day %lld; capabilities eligible from following opening",
                    static_cast<long long>(*p.commissionedDay));
    if (ImGui::TreeNode("Frozen package rows and paid inputs")) {
        const auto catalog = q.siteModuleCatalog();
        for (std::size_t i = 0; i < p.charter.package.size(); ++i) {
            const auto& module = p.charter.package[i];
            const auto& work = p.rows[i];
            const auto definition = std::find_if(catalog.begin(), catalog.end(), [&](const auto& d) {
                return d.version == p.charter.catalogVersion && d.kind == module.kind;
            });
            const double required =
                definition == catalog.end() ? 0 : definition->assemblyWorkdays * module.quantity;
            ImGui::Text("Row %zu: %s x%d | %.6g / %.6g work | pinned hull #%lld", i + 1,
                        moduleNames[static_cast<std::size_t>(module.kind)], module.quantity,
                        work.workCompleted, required, number(work.workshopShipId));
            materials("Consumed", work.consumed);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Construction work receipts")) {
        for (const auto& receipt : p.workReceipts) {
            ImGui::Text("Day %lld / revision %d: %s row %d, %.6g work; builder #%lld, hull #%lld, team "
                        "#%lld, leader #%lld",
                        static_cast<long long>(receipt.day), receipt.charterRevision,
                        receipt.kind == SiteDevelopmentWorkKind::Assembly ? "Assembly" : "Commissioning",
                        receipt.packageRow + 1, receipt.work, number(receipt.builderId),
                        number(receipt.workshopShipId), number(receipt.teamId), number(receipt.leaderId));
            materials("Actual input debit", receipt.consumed);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Development reports")) {
        for (const auto& r : p.reports) {
            ImGui::Text("Days %lld-%lld%s | revision %d | audit through #%lld",
                        static_cast<long long>(r.startDay), static_cast<long long>(r.endDay),
                        r.isNinetyDayReview ? " (90-day review)" : "", r.charterRevision,
                        static_cast<long long>(r.auditThroughId));
            ImGui::Text("Assembly %.6g, commissioning %.6g | loaded fuel %.6g, burned %.6g", r.assemblyWork,
                        r.commissioningWork, r.fuelLoaded, r.fuelBurned);
            ImGui::Text("Builder #%lld, engineer #%lld, body #%lld | commissioned: %s", number(r.builderId),
                        number(r.teamId), number(r.bodyId), r.commissioned ? "yes" : "no");
            materials("Period materials consumed", r.consumed);
            materials("Historical protected site floors", r.policy.constructionFloors);
            if (r.policy.materialAllowances)
                materials("Historical lifetime material allowances", *r.policy.materialAllowances);
            ImGui::TextWrapped("%s", r.waitingReason.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
void SiteDevelopmentPanel::renderRelatedFreight(const std::vector<FreightProgramId>& ids,
                                                const SimulationQueries& q, SimulationService& service) {
    if (ids.empty()) {
        ImGui::TextUnformatted("No related delivery or collection commitments.");
        return;
    }
    ImGui::TextWrapped("Related freight remains independently authorized when construction or operation "
                       "stops. Change each commitment explicitly.");
    for (const auto& freight : q.freightPrograms())
        if (std::find(ids.begin(), ids.end(), freight.id) != ids.end()) {
            ImGui::PushID(std::to_string(freight.id.value).c_str());
            ImGui::Text("Freight #%lld %s: %s -> %s (%s)", number(freight.id), freight.charter.name.c_str(),
                        freight.sourceName.c_str(), freight.destinationName.c_str(),
                        freight.materialName.c_str());
            ImGui::TextWrapped("%s", freight.condition.c_str());
            if (freight.lifecycle != FreightProgramLifecycle::Closed) {
                if (freight.lifecycle == FreightProgramLifecycle::Suspended) {
                    if (ImGui::Button("Resume this freight"))
                        notice_ = service.execute(ResumeFreightProgramCommand{freight.id}).message;
                } else if (ImGui::Button("Suspend this freight"))
                    notice_ = service.execute(SuspendFreightProgramCommand{freight.id}).message;
                ImGui::SameLine();
                if (ImGui::Button("Cancel this freight"))
                    notice_ = service.execute(CancelFreightProgramCommand{freight.id}).message;
            }
            ImGui::PopID();
        }
}
void SiteDevelopmentPanel::renderSite(const SiteSummary& row, const SimulationQueries& q,
                                      SimulationService& service) {
    const auto& site = row.site;
    ImGui::PushID(std::to_string(site.id.value).c_str());
    ImGui::Text("%s at %s | %s", site.name.c_str(), row.bodyName.c_str(),
                site.operatingPolicy.enabled ? "Operation enabled" : "Operation suspended");
    ImGui::TextWrapped("%s", row.condition.c_str());
    if (row.constructionOwner) {
        for (const auto& p : q.siteDevelopments())
            if (p.program.id == row.constructionOwner)
                ImGui::Text("Site construction owner: %s (#%lld)", p.program.charter.assignments.name.c_str(),
                            number(p.program.id));
    }
    ImGui::Text("Installed power %.3f / %.3f | rated Ice %.3f/day", row.installed.powerGeneration,
                row.installed.powerDemand, row.installed.ratedExtraction);
    ImGui::Text("Shared raw storage %.6g / %.6g | supported raw handling %.6g/day", row.rawOccupancy,
                row.installed.rawStorage, row.rawHandling);
    ImGui::Text("Next-opening supported duty %.6g (conditional on present stock) | lifetime duty %.6g",
                row.supportedDuty, row.dutySpent);
    ImGui::Text("Operating leader #%lld | requested Ice %.6g/day", number(site.operatingPolicy.leaderId),
                site.operatingPolicy.requestedIcePerDay);
    if (ImGui::Button("Edit operating policy")) {
        editingOperation_ = site.id;
        operationDraft_ = site.operatingPolicy;
    }
    ImGui::SameLine();
    if (site.operatingPolicy.enabled) {
        if (ImGui::Button("Suspend site operation"))
            notice_ = service.execute(SuspendSiteOperationCommand{site.id}).message;
    } else if (ImGui::Button("Resume site operation"))
        notice_ = service.execute(ResumeSiteOperationCommand{site.id}).message;
    ImGui::SameLine();
    if (ImGui::Button("Draft additive expansion")) {
        editingDevelopment_.reset();
        draft_ = {};
        draft_.charter.siteId = site.id;
        registerSite_ = false;
        quantities_ = {0, 0, 0, 1, 0};
        copyName(name_, "Expand " + site.name);
    }
    if (editingOperation_ == site.id) {
        operatingEditor(operationDraft_, q);
        if (ImGui::Button("Save operating policy")) {
            const auto result = service.execute(AmendSiteOperatingPolicyCommand{site.id, operationDraft_});
            notice_ = result.message;
            if (result.ok)
                editingOperation_.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard policy draft"))
            editingOperation_.reset();
    }
    if (site.issue.cause != SiteOperatingIssueCause::None) {
        ImGui::TextWrapped("Observed operating issue since day %lld: %s",
                           static_cast<long long>(site.issue.episodeStartedDay), site.issue.message.c_str());
        if (!site.issue.acknowledged && ImGui::Button("Acknowledge operating issue"))
            notice_ = service
                          .execute(AcknowledgeSiteOperatingIssueCommand{site.id, site.issue.cause,
                                                                        site.issue.episodeStartedDay})
                          .message;
    }
    materials("Delivered sealed construction / support stores", site.processedStock);
    for (std::size_t i = 0; i < mineralCount(); ++i)
        if (site.rawStock.amount[i] > 0)
            ImGui::Text("Raw %s: %.6g", std::string(toString(static_cast<Mineral>(i))).c_str(),
                        site.rawStock.amount[i]);
    double reactor = 0, composites = 0, ice = 0;
    for (const auto& d : site.dutyReceipts) {
        reactor += d.reactorFuel;
        composites += d.composites;
    }
    for (const auto& e : site.extractionReceipts)
        ice += e.recoveredIce;
    ImGui::Text(
        "Paid support: %.6g ReactorFuel + %.6g composites | observed attempts %zu, recovered Ice %.6g",
        reactor, composites, site.extractionReceipts.size(), ice);
    if (ImGui::TreeNode("Operating evidence and scientific records")) {
        const auto evidence = q.evidenceDossier(site.bodyId);
        ImGui::Text("Scientific records: %zu observations, %zu findings, %zu assessments",
                    evidence.observations.size(), evidence.findings.size(), evidence.assessments.size());
        for (const auto& channel : q.bodyDeposits(site.bodyId))
            if (channel.mineral == Mineral::WaterIce) {
                ImGui::TextWrapped("Published Water Ice indication: %s", channel.indication.c_str());
            }
        ImGui::TextWrapped("Dated operating records below are separate evidence. They do not revise "
                           "geological assessments or establish reserve lifetime.");
        for (const auto& attempt : site.extractionReceipts)
            ImGui::Text("Day %lld: attempted %.6g, recovered %.6g Water Ice | %s",
                        static_cast<long long>(attempt.day), attempt.nominalAttempt, attempt.recoveredIce,
                        attempt.observation.c_str());
        for (const auto& d : site.dutyReceipts)
            ImGui::Text("Day %lld: %.6g duty; ReactorFuel %.6g, composites %.6g; equipment cutoff %zu",
                        static_cast<long long>(d.day), d.duty, d.reactorFuel, d.composites,
                        d.installedGroupCutoff);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Site operating reports")) {
        for (const auto& r : site.reports) {
            ImGui::Text("Days %lld-%lld%s | policy revision %d | audit through #%lld",
                        static_cast<long long>(r.startDay), static_cast<long long>(r.endDay),
                        r.isNinetyDayReview ? " (90-day review)" : "", r.operatingRevision,
                        static_cast<long long>(r.auditThroughId));
            ImGui::Text("Duty %.6g; ReactorFuel %.6g; composites %.6g; %d attempts recovered %.6g",
                        r.supportedDuty, r.reactorFuel, r.composites, r.attempts, r.recoveredIce);
            ImGui::Text("Raw %.6g / %.6g | net exported Ice %.6g | delivered Ice %.6g", r.rawOccupancy,
                        r.rawCapacity, r.exportedIce, r.deliveredIce);
            ImGui::Text("Historical target %.6g/day | ReactorFuel/composites floors %.6g / %.6g | equipment "
                        "cutoff %zu",
                        r.policy.requestedIcePerDay, r.policy.reactorFuelFloor, r.policy.compositesFloor,
                        r.installedGroupCutoff);
            if (r.policy.lifetimeDutyAllowance)
                ImGui::Text("Historical lifetime duty allowance %.6g", *r.policy.lifetimeDutyAllowance);
            ImGui::TextWrapped("%s", r.waitingReason.c_str());
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Ice delivery and colony processing")) {
        ImGui::TextWrapped("Processing recipe: 1 Water Ice + 0.5 Volatiles -> 1 Propellant. These are "
                           "normalized game quantities. Raw Ice and ReactorFuel cannot fill engine tanks.");
        const auto freight = q.freightPrograms();
        std::vector<ColonyId> receiving;
        for (const auto& p : q.siteDevelopments())
            if (p.program.charter.siteId == site.id)
                receiving.push_back(p.program.charter.supportColonyId);
        for (const auto& p : freight)
            if (p.charter.source == StockLocation{site.id} &&
                p.charter.commodity == Commodity{Mineral::WaterIce})
                if (const auto* colony = std::get_if<ColonyId>(&p.charter.destination))
                    receiving.push_back(*colony);
        for (const auto& colony : q.colonies())
            if (std::find(receiving.begin(), receiving.end(), colony.id) != receiving.end()) {
                double delivered = 0, share = 0, propellantStock = 0;
                for (const auto& p : freight)
                    if (p.charter.source == StockLocation{site.id} &&
                        p.charter.destination == StockLocation{colony.id} &&
                        p.charter.commodity == Commodity{Mineral::WaterIce})
                        delivered += p.cargoDelivered;
                for (const auto& allocation : colony.effectiveProcessingAllocations)
                    if (allocation.material == ProcessedMaterial::Propellant)
                        share = allocation.normalizedPercent;
                for (const auto& stock : colony.processedStockpiles)
                    if (stock.material == ProcessedMaterial::Propellant)
                        propellantStock = stock.amount;
                const double nominal = colony.processorCapacity * share / 100;
                const double produced = colony.processedProductionTotals.get(ProcessedMaterial::Propellant);
                ImGui::Text("%s: Ice physically delivered from this site %.6g", colony.name.c_str(),
                            delivered);
                ImGui::Text("Current raw Ice %.6g; current Volatiles %.6g",
                            colony.rawStockpiles.get(Mineral::WaterIce),
                            colony.rawStockpiles.get(Mineral::Volatiles));
                ImGui::Text("Processor capacity %.6g/day; Propellant allocation %.3f%% (nominal %.6g/day)",
                            colony.processorCapacity, share, nominal);
                ImGui::Text("Nominal allocation needs %.6g Ice + %.6g Volatiles/day; actual output also "
                            "depends on recipe ordering and stock",
                            nominal, 0.5 * nominal);
                ImGui::Text("Gross campaign Propellant produced %.6g | current Propellant inventory %.6g",
                            produced, propellantStock);
                ImGui::Text("Recorded gross production represents %.6g Ice and %.6g Volatiles consumed by "
                            "that recipe",
                            produced, 0.5 * produced);
            }
        ImGui::TextWrapped("Delivered stock, gross production, current inventory, and engine fuel transfers "
                           "are distinct quantities. Today's later processing output cannot fund an opening "
                           "action that has already passed.");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Related freight commitments")) {
        renderRelatedFreight(row.relatedFreight, q, service);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
} // namespace deep::ui_imgui
