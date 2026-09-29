#include "ui_imgui/FreightProgramsPanel.h"

// Presents finite delivery intentions, physical per-hull custody, and durable
// accounting. The query layer owns all readiness and transfer calculations.
#include "ui_imgui/OperationalWindow.h"
#include "sim/FreightProgramRules.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace deep::ui_imgui {
namespace {

template <typename Id>
std::string namedOption(const std::string& name, const Id id) {
    return name + " (#" + std::to_string(id.value) + ")";
}

// Identical table layout for tentative assignments and actual cargo, with both
// values labeled separately so a planned shipment never appears as inventory.
void renderHulls(const std::vector<FreightHullSummary>& hulls, const char* tableId) {
    if (hulls.empty()) {
        ImGui::TextUnformatted("No fleet manifest available.");
        return;
    }
    if (!ImGui::BeginTable(tableId, 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) return;
    for (const char* column : {"Ship", "Capacity (units)", "Powered rate/day", "Planned limit", "Actually aboard", "Cargo identity"}) {
        ImGui::TableSetupColumn(column);
    }
    ImGui::TableHeadersRow();
    for (const FreightHullSummary& hull : hulls) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(namedOption(hull.shipName, hull.shipId).c_str());
        ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", hull.cargoCapacity);
        ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", hull.operationalHandlingPerDay);
        ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", hull.plannedQuantity);
        ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", hull.cargoQuantity);
        ImGui::TableSetColumnIndex(5);
        if (hull.cargoProgramId) {
            ImGui::Text("Freight #%lld / shipment %d / %s", static_cast<long long>(hull.cargoProgramId->value),
                        hull.shipmentNumber, hull.materialName.c_str());
        } else ImGui::TextUnformatted("Empty hold");
    }
    ImGui::EndTable();
}

} // namespace

void FreightProgramsPanel::resetWorldState() {
    draftName_.fill('\0');
    draft_ = FreightProgramCharter{};
    selectedProgramId_.reset();
    editingProgramId_.reset();
    notice_ = "Ready";
    lastActionSucceeded_ = true;
}

FreightProgramCharter FreightProgramsPanel::currentCharter() const {
    FreightProgramCharter charter = draft_;
    charter.name = draftName_.data();
    return charter;
}

void FreightProgramsPanel::applyResult(const CommandResult& result) {
    lastActionSucceeded_ = result.ok;
    notice_ = result.message.empty() ? (result.ok ? "Action accepted" : "Action rejected") : result.message;
}

void FreightProgramsPanel::editProgram(const FreightProgramSummary& program) {
    draft_ = program.charter;
    draftName_.fill('\0');
    std::memcpy(draftName_.data(), draft_.name.data(), std::min(draft_.name.size(), draftName_.size() - 1));
    editingProgramId_ = program.id;
}

void FreightProgramsPanel::render(const SimulationQueries& queries, SimulationService& service, bool& visible) {
    if (!visible) return;
    if (!beginOperationalWindow("Freight / Supply Programs", &visible)) { ImGui::End(); return; }
    renderEditor(queries, service);
    ImGui::SeparatorText("Delivery programs");
    // Commands in the editor may append programs, so acquire a fresh owned list.
    const auto programs = queries.freightPrograms();
    if (programs.empty()) {
        selectedProgramId_.reset();
        ImGui::TextUnformatted("No freight programs authorized yet.");
    } else {
        if (std::none_of(programs.begin(), programs.end(), [this](const auto& row) { return selectedProgramId_ == row.id; })) {
            selectedProgramId_ = programs.front().id;
        }
        if (ImGui::BeginTable("FreightOverview", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
            for (const char* label : {"Program", "Lifecycle", "Delivered / target", "Phase", "Condition"}) ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            for (const FreightProgramSummary& row : programs) {
                // Program IDs are durable 64-bit values. Keep every digit in
                // the widget namespace rather than truncating to ImGui's int.
                const std::string widgetId = std::to_string(row.id.value);
                ImGui::PushID(widgetId.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(row.charter.name.c_str(), selectedProgramId_ == row.id)) selectedProgramId_ = row.id;
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row.lifecycleName.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f / %.3f", row.cargoDelivered, row.charter.totalQuantity);
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(row.taskName.c_str());
                ImGui::TableSetColumnIndex(4); ImGui::TextWrapped("%s", row.condition.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        const auto selected = std::find_if(programs.begin(), programs.end(), [this](const auto& row) { return selectedProgramId_ == row.id; });
        if (selected != programs.end()) renderDetail(*selected, service);
    }
    ImGui::Separator();
    ImGui::Text("Last action: %s", lastActionSucceeded_ ? "Accepted" : "Rejected");
    ImGui::TextWrapped("%s", notice_.c_str());
    ImGui::End();
}

void FreightProgramsPanel::renderEditor(const SimulationQueries& queries, SimulationService& service) {
    if (!ImGui::CollapsingHeader("Authorize or amend delivery", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto colonies = queries.colonies();
    const auto fleets = queries.fleets();
    const auto people = queries.personnel();
    if (!draft_.sourceColonyId && !colonies.empty()) draft_.sourceColonyId = colonies.front().id;
    if (!draft_.destinationColonyId && colonies.size() > 1) draft_.destinationColonyId = colonies[1].id;
    if (editingProgramId_) {
        ImGui::Text("Amending freight #%lld", static_cast<long long>(editingProgramId_->value));
        ImGui::SameLine();
        if (ImGui::SmallButton("New delivery charter")) {
            draft_ = FreightProgramCharter{};
            draftName_.fill('\0');
            editingProgramId_.reset();
        }
    }
    ImGui::InputText("Name", draftName_.data(), draftName_.size());
    // Contract identity is immutable. Disabled selectors expose the original
    // route while mutable policy edits are sent without these fields.
    ImGui::BeginDisabled(editingProgramId_.has_value());
    const auto colonySelector = [&colonies](const char* label, ColonyId& selected) {
        std::string current = "Select colony";
        for (const ColonySummary& row : colonies) if (row.id == selected) current = namedOption(row.name, row.id);
        if (ImGui::BeginCombo(label, current.c_str())) {
            for (const ColonySummary& row : colonies) {
                if (ImGui::Selectable(namedOption(row.name, row.id).c_str(), selected == row.id)) selected = row.id;
            }
            ImGui::EndCombo();
        }
    };
    colonySelector("Source colony", draft_.sourceColonyId);
    colonySelector("Destination colony", draft_.destinationColonyId);
    if (ImGui::BeginCombo("Material", toString(draft_.material).data())) {
        for (std::size_t index = 0; index < processedMaterialCount(); ++index) {
            const auto material = static_cast<ProcessedMaterial>(index);
            if (ImGui::Selectable(toString(material).data(), draft_.material == material)) draft_.material = material;
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Source, destination and material are fixed for this contract. A different route or commodity requires a new program.");
    ImGui::InputDouble("Cumulative delivery target (normalized units)", &draft_.totalQuantity, 1.0, 100.0, "%.3f");
    std::string fleetLabel = "Unassigned";
    for (const FleetSummary& row : fleets) if (draft_.requestedFleetId == row.id) fleetLabel = namedOption(row.name, row.id);
    if (ImGui::BeginCombo("Requested fleet", fleetLabel.c_str())) {
        if (ImGui::Selectable("Unassigned", !draft_.requestedFleetId)) draft_.requestedFleetId.reset();
        for (const FleetSummary& row : fleets) {
            if (ImGui::Selectable(namedOption(row.name, row.id).c_str(), draft_.requestedFleetId == row.id)) draft_.requestedFleetId = row.id;
            if (row.controllingProgram) ImGui::SetItemTooltip("Currently controlled by %s", row.controllingProgramLabel.c_str());
        }
        ImGui::EndCombo();
    }
    std::string leaderLabel = "Unassigned";
    for (const PersonSummary& row : people) if (draft_.requestedLeaderId == row.id) leaderLabel = namedOption(row.name, row.id);
    if (ImGui::BeginCombo("Responsible leader", leaderLabel.c_str())) {
        if (ImGui::Selectable("Unassigned", !draft_.requestedLeaderId)) draft_.requestedLeaderId.reset();
        for (const PersonSummary& row : people) {
            if (ImGui::Selectable(namedOption(row.name, row.id).c_str(), draft_.requestedLeaderId == row.id)) draft_.requestedLeaderId = row.id;
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("Freight uses one explicit delivery procedure; the responsible leader adds no new freight bonus. Existing fleet-commander fuel effects still apply.");
    if (ImGui::TreeNode("Advanced stock and operating-fuel policy")) {
        ImGui::InputDouble("Source cargo stock floor", &draft_.policy.sourceCargoFloor, 1.0, 100.0, "%.3f");
        ImGui::InputDouble("Source operating-Propellant floor", &draft_.policy.sourcePropellantFloor, 1.0, 100.0, "%.3f");
        bool limited = draft_.policy.maxAdditionalPropellant.has_value();
        if (ImGui::Checkbox("Limit lifetime additional operating fuel", &limited)) {
            draft_.policy.maxAdditionalPropellant = limited ? std::optional<double>{0.0} : std::nullopt;
        }
        if (draft_.policy.maxAdditionalPropellant) ImGui::InputDouble("Lifetime operating-fuel allowance", &*draft_.policy.maxAdditionalPropellant, 1.0, 100.0, "%.3f");
        ImGui::InputDouble("Return contingency fraction", &draft_.policy.returnContingencyFraction, 0.05, 0.1, "%.3f");
        ImGui::TextWrapped("Routine fuel comes only from the source colony. Floors are withdrawal limits, not reservations. Propellant payload uses the larger floor; payload is never engine fuel or charged to the operating allowance.");
        ImGui::TreePop();
    }
    const FreightProgramCharter charter = currentCharter();
    const auto preview = queries.previewFreightProgramCharter(charter, editingProgramId_);
    ImGui::TextWrapped("%s", preview.validationMessage.c_str());
    if (preview.structurallyValid) {
        ImGui::TextWrapped("Next-opening outlook: %s", preview.executionCondition.c_str());
        for (const auto& reason : preview.waitingReasons) ImGui::TextWrapped("Waiting: %s", reason.c_str());
        if (preview.shipmentReady) {
            ImGui::Text("Candidate %.3f units | Operating fuel %.3f required, %.3f additional",
                        preview.plannedQuantity, preview.requiredOperatingFuel, preview.additionalOperatingFuel);
            ImGui::Text("Handling: %lld loading day(s), %lld unloading day(s)",
                        static_cast<long long>(preview.loadingDays), static_cast<long long>(preview.unloadingDays));
            if (preview.projectedDepartureDay && preview.projectedReturnDepartureDay) {
                ImGui::Text("Uninterrupted candidate: depart day %lld, earliest return departure day %lld",
                            static_cast<long long>(*preview.projectedDepartureDay), static_cast<long long>(*preview.projectedReturnDepartureDay));
            }
        }
        renderHulls(preview.hulls, "FreightDraftHulls");
    }
    ImGui::TextWrapped("Cargo uses normalized units. Current prototype transit ignores payload mass. Candidate dates describe one uninterrupted shipment; whole-program completion depends on future stock, fuel and authority.");
    ImGui::BeginDisabled(!preview.structurallyValid);
    if (ImGui::Button(editingProgramId_ ? "Apply delivery amendment" : "Authorize delivery")) {
        if (editingProgramId_) {
            applyResult(service.execute(AmendFreightProgramCommand{*editingProgramId_, freightAmendmentFromCharter(charter)}));
        } else {
            applyResult(service.execute(CreateFreightProgramCommand{charter}));
            if (lastActionSucceeded_) {
                const auto created = queries.freightPrograms();
                if (!created.empty()) selectedProgramId_ = created.back().id;
                draft_ = FreightProgramCharter{};
                draftName_.fill('\0');
            }
        }
    }
    ImGui::EndDisabled();
}

void FreightProgramsPanel::renderDetail(const FreightProgramSummary& program, SimulationService& service) {
    ImGui::SeparatorText("Delivery and custody");
    ImGui::Text("%s | revision %d | %s", program.charter.name.c_str(), program.charterRevision, program.lifecycleName.c_str());
    ImGui::Text("%s -> %s | %s", program.sourceName.c_str(), program.destinationName.c_str(), program.materialName.c_str());
    ImGui::TextWrapped("%s", program.condition.c_str());
    ImGui::Text("Requested fleet: %s | Leased: %s | Committed task: %s", program.requestedFleetName.c_str(), program.leasedFleetName.c_str(), program.taskFleetName.c_str());
    if (program.pendingFleetChange) ImGui::TextWrapped("Requested fleet change is pending the empty source planning boundary; current cargo and return retain their original fleet.");
    ImGui::Text("Responsible leader: %s", program.leaderName.c_str());
    ImGui::Text("Phase: %s | Location: %s", program.taskName.c_str(), program.locationName.c_str());
    if (program.currentLegArrivalDay) ImGui::Text("Current leg to %s arrives day %lld", program.routeDestinationName.c_str(), static_cast<long long>(*program.currentLegArrivalDay));
    if (program.shipment) ImGui::Text("Shipment %d from charter r%d; committed day %lld; leader %s", program.shipment->number, program.shipment->charterRevision, static_cast<long long>(program.shipment->committedDay), program.shipmentLeaderName.c_str());
    ImGui::Text("Delivered %.3f / target %.3f | Unpicked %.3f | Existing commitment %.3f", program.cargoDelivered, program.charter.totalQuantity, program.unpickedQuantity, program.committedQuantity);
    if (program.commitmentAboveTarget > 0.0) ImGui::TextWrapped("%.3f units are already committed above the revised target; prior shipments remain honored.", program.commitmentAboveTarget);
    ImGui::Text("Cargo loaded %.3f | Delivered %.3f | Returned to source %.3f | Aboard %.3f", program.cargoLoaded, program.cargoDelivered, program.cargoReturned, program.cargoAboard);
    ImGui::Text("Operating fuel loaded %.3f | Burned %.3f", program.fuelLoaded, program.fuelBurned);
    if (program.fuelAllowanceRemaining) ImGui::Text("Remaining lifetime operating-fuel allowance: %.3f", *program.fuelAllowanceRemaining);
    else ImGui::TextUnformatted("Operating-fuel allowance: unlimited; actual source supply still required");
    ImGui::Text("Current source cargo stock %.3f | Source Propellant %.3f | Destination stock %.3f", program.sourceCargoStock, program.sourcePropellantStock, program.destinationStock);
    ImGui::Text("Withdrawal floors: cargo %.3f / operating Propellant %.3f | Return contingency %.3f",
                program.charter.policy.sourceCargoFloor, program.charter.policy.sourcePropellantFloor,
                program.charter.policy.returnContingencyFraction);
    ImGui::TextWrapped("Cumulative delivered records actual unloads. Destination stock may later be consumed. Arrival alone credits no stock; cargo cannot fuel engines implicitly.");
    if (program.lifecycle == FreightProgramLifecycle::Suspended && program.cargoAboard > 0.0) ImGui::TextWrapped("Suspended; fleet retained while cargo is aboard. Resume or cancel future pickups to settle the load.");
    if (program.nextReportDay) ImGui::Text("Next global report: day %lld", static_cast<long long>(*program.nextReportDay));
    if (program.closedDay) ImGui::Text("Closed on day %lld; no further periodic freight reports", static_cast<long long>(*program.closedDay));
    renderHulls(program.hulls, "FreightActualHulls");
    if (!program.issue.message.empty()) {
        ImGui::TextWrapped("Issue: %s (%s)", program.issue.message.c_str(), program.issue.acknowledged ? "acknowledged" : "decision pending");
        if (!program.issue.acknowledged && ImGui::Button("Acknowledge freight issue")) applyResult(service.execute(AcknowledgeFreightProgramIssueCommand{program.id, program.issue.signature}));
    }
    if (program.canAmend && ImGui::Button("Edit delivery commitment")) editProgram(program);
    if (program.canSuspend) { ImGui::SameLine(); if (ImGui::Button("Suspend")) applyResult(service.execute(SuspendFreightProgramCommand{program.id})); }
    if (program.canResume) { ImGui::SameLine(); if (ImGui::Button("Resume")) applyResult(service.execute(ResumeFreightProgramCommand{program.id})); }
    if (program.canCancel) { ImGui::SameLine(); if (ImGui::Button("Cancel future pickups")) applyResult(service.execute(CancelFreightProgramCommand{program.id})); }
    ImGui::TextWrapped("Cancel future pickups returns unshipped cargo to source over handling days, or completes unloading of already dispatched cargo at its original destination. Active paid transit continues; goods are never discarded.");
    if (ImGui::CollapsingHeader("Transfer receipts")) {
        for (const auto& row : program.receipts) ImGui::TextWrapped("#%d day %lld / shipment %d: %s %.3f %s at %s; fleet %s, leader %s", row.receipt.sequence, static_cast<long long>(row.receipt.day), row.receipt.shipmentNumber, row.actionName.c_str(), row.receipt.amount, row.materialName.c_str(), row.colonyName.c_str(), row.fleetName.c_str(), row.leaderName.c_str());
    }
    if (ImGui::CollapsingHeader("Freight reports")) {
        for (auto it = program.reports.rbegin(); it != program.reports.rend(); ++it) {
            const auto& report = it->report;
            ImGui::Text("Days %lld-%lld%s | Charter r%d | %d shipments started", static_cast<long long>(report.startDay), static_cast<long long>(report.endDay), report.isNinetyDayReview ? " / 90-day review" : "", report.charterRevision, report.shipmentsStarted);
            ImGui::Text("Loaded %.3f / delivered %.3f / source return %.3f / aboard %.3f", report.cargoLoaded, report.cargoDelivered, report.cargoReturned, report.cargoAboard);
            ImGui::Text("Operating fuel loaded %.3f / burned %.3f | Cumulative %.3f / target %.3f | Commitment %.3f", report.fuelLoaded, report.fuelBurned, report.cumulativeDelivered, report.targetQuantity, report.committedQuantity);
            // A later amendment must not rewrite the limits under which this
            // historical reporting boundary was reached.
            ImGui::Text("Policy at report: cargo floor %.3f / operating Propellant floor %.3f / return contingency %.3f",
                        report.policy.sourceCargoFloor, report.policy.sourcePropellantFloor,
                        report.policy.returnContingencyFraction);
            if (report.policy.maxAdditionalPropellant) ImGui::Text("Lifetime operating-fuel allowance at report: %.3f", *report.policy.maxAdditionalPropellant);
            else ImGui::TextUnformatted("Lifetime operating-fuel allowance at report: unlimited");
            ImGui::TextWrapped("%s at %s: %s", it->fleetName.c_str(), it->locationName.c_str(), report.waitingReason.c_str());
            ImGui::Separator();
        }
    }
}

} // namespace deep::ui_imgui
