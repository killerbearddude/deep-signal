#include "ui_imgui/ShipyardPanel.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/PresentationWidgets.h"
#include "ui_imgui/UiTheme.h"

// Presents current production commitments first. Selection, tabs and drafts are
// UI-local; conditions and work demand come from owned application projections.

#include "sim/Commands.h"
#include "sim/Error.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] std::optional<ColonySummary> firstProductionColony(const std::vector<ColonySummary>& colonies) {
    return colonies.empty() ? std::optional<ColonySummary>{} : std::optional<ColonySummary>{colonies.front()};
}

// Formatting and visual status mapping consume owned app projections only.
std::string amount(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1) << value;
    return out.str();
}

UiStatus badgeState(ProductionBacklogState state) {
    switch (state) {
    case ProductionBacklogState::Building:
        return UiStatus::Building;
    case ProductionBacklogState::Waiting:
        return UiStatus::Waiting;
    case ProductionBacklogState::Completed:
        return UiStatus::Completed;
    }
    return UiStatus::Unknown;
}

const char* stateLabel(ProductionBacklogState state) {
    switch (state) {
    case ProductionBacklogState::Building:
        return "Building";
    case ProductionBacklogState::Waiting:
        return "Waiting";
    case ProductionBacklogState::Completed:
        return "Completed";
    }
    return "Unknown";
}

std::string etaLabel(const ProductionBacklogSummary& order) {
    return order.etaDays ? std::to_string(*order.etaDays) + " d" : "No ETA";
}

} // namespace

void ShipyardPanel::render(const SimulationQueries& queries, SimulationService& service, bool& visible) {
    if (!visible)
        return;
    // An explicitly opened operational screen should reveal its current work.
    // Existing floating position/size remains the user's layout, within the shell.
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
    if (!beginOperationalWindow("Shipyard / Production", &visible, 1420.0F, 1020.0F, false)) {
        ImGui::End();
        return;
    }
    // This screen owns its identity once. Empty header space still supports
    // ordinary window dragging, and the explicit close action replaces chrome.
    const auto headerOrigin = ImGui::GetCursorScreenPos();
    const float titleWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x - 82.0F);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 66.0F);
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
    if (ImGui::Button("Close##Shipyard", {66.0F, 28.0F}))
        visible = false;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(headerOrigin);
    screenTitle("Shipyard / Production", "Production commitments and current yard state", titleWidth);
    if (!visible) {
        ImGui::End();
        return;
    }
    ImGui::Dummy({0.0F, 6.0F});
    const auto classes = queries.shipClasses();
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Tab));
    if (ImGui::BeginTabBar("ShipyardViews")) {
        const auto ordersFlags = selectOrdersTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Orders", nullptr, ordersFlags)) {
            renderOrders(queries, service, classes);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("New Revision")) {
            renderDesignEditor(queries, service, classes);
            if (feedbackIsRevision_)
                renderFeedback();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        selectOrdersTab_ = false;
    }
    ImGui::PopFont();
    ImGui::End();
}

void ShipyardPanel::renderOrders(const SimulationQueries& queries, SimulationService& service,
                                 const std::vector<ShipClassSummary>& classes) {
    const auto backlog = queries.productionBacklog();
    const auto current = std::find_if(backlog.begin(), backlog.end(),
                                      [&](const auto& row) { return selectedOrderId_ == row.orderId; });
    if (current == backlog.end()) {
        const auto firstLive = std::find_if(backlog.begin(), backlog.end(), [](const auto& row) {
            return row.state != ProductionBacklogState::Completed;
        });
        selectedOrderId_ = firstLive != backlog.end() ? std::optional{firstLive->orderId}
                           : backlog.empty()          ? std::nullopt
                                                      : std::optional{backlog.front().orderId};
    }

    int building = 0, waiting = 0, completed = 0;
    for (const auto& row : backlog) {
        switch (row.state) {
        case ProductionBacklogState::Building:
            ++building;
            break;
        case ProductionBacklogState::Waiting:
            ++waiting;
            break;
        case ProductionBacklogState::Completed:
            ++completed;
            break;
        }
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("ShipyardCounts", 4, ImGuiTableFlags_SizingStretchSame,
                          {std::min(860.0F, ImGui::GetContentRegionAvail().x), 0.0F})) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        metric("Orders", std::to_string(backlog.size()));
        ImGui::TableSetColumnIndex(1);
        metric("Building", std::to_string(building));
        ImGui::TableSetColumnIndex(2);
        metric("Waiting", std::to_string(waiting), waiting > 0 ? UiStatus::Waiting : UiStatus::Normal);
        ImGui::TableSetColumnIndex(3);
        metric("Complete", std::to_string(completed));
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // One desktop composition: current commitments beside their selected detail.
    // Secondary detail may scroll; no responsive screen modes or saved UI state.
    const float bodyHeight =
        std::max(300.0F, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y * 2.0F);
    if (ImGui::BeginTable("ShipyardContent", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Commitments", ImGuiTableColumnFlags_WidthStretch, 0.66F);
        ImGui::TableSetupColumn("Selected order", ImGuiTableColumnFlags_WidthStretch, 0.34F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("ShipyardCommitments", {0.0F, bodyHeight}, ImGuiChildFlags_None);
        sectionTitle("Shipyard orders");
        if (backlog.empty()) {
            ImGui::TextWrapped(
                "No shipyard orders. Create a build order below to record a production commitment.");
        } else {
            constexpr float rowHeight = 92.0F;
            const float desired = 56.0F + rowHeight * static_cast<float>(backlog.size());
            const float tableHeight = std::min(std::max(200.0F, desired),
                                               std::max(200.0F, ImGui::GetContentRegionAvail().y - 150.0F));
            const auto tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("ShipyardOrderTable", 7, tableFlags, {0.0F, tableHeight})) {
                ImGui::TableSetupColumn("QUEUE", ImGuiTableColumnFlags_WidthFixed, 46.0F);
                ImGui::TableSetupColumn("COLONY", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                ImGui::TableSetupColumn("CLASS", ImGuiTableColumnFlags_WidthStretch, 1.9F);
                ImGui::TableSetupColumn("PROGRESS", ImGuiTableColumnFlags_WidthFixed, 72.0F);
                ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthFixed, 100.0F);
                ImGui::TableSetupColumn("ETA", ImGuiTableColumnFlags_WidthFixed, 64.0F);
                ImGui::TableSetupColumn("CURRENT CONDITION", ImGuiTableColumnFlags_WidthStretch, 1.8F);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
                ImGui::TableHeadersRow();
                ImGui::PopFont();
                for (const auto& row : backlog) {
                    ImGui::PushID(static_cast<int>(row.orderId.value));
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                    // Long class names may grow a row. Measure the same wrapped
                    // cells so its entire visible height remains selectable.
                    const auto wrappedHeight = [](int column, const std::string& text) {
                        ImGui::TableSetColumnIndex(column);
                        return ImGui::CalcTextSize(text.c_str(), nullptr, false,
                                                   ImGui::GetContentRegionAvail().x)
                            .y;
                    };
                    const auto cls = std::find_if(classes.begin(), classes.end(), [&](const auto& entry) {
                        return entry.id == row.shipClassId;
                    });
                    const std::string classNote =
                        cls == classes.end() ? std::string{}
                                             : cls->roleName + " vessel / r" + std::to_string(cls->revision);
                    const float colonyHeight = wrappedHeight(1, row.colonyName);
                    const float nameHeight = wrappedHeight(2, row.shipClassName);
                    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
                    const float noteHeight = classNote.empty() ? 0.0F : wrappedHeight(2, classNote);
                    ImGui::PopFont();
                    const float classHeight = nameHeight + (noteHeight > 0.0F ? 4.0F + noteHeight : 0.0F);
                    const float conditionHeight = wrappedHeight(6, row.primaryCondition);
                    const float contentHeight = std::max({rowHeight - ImGui::GetStyle().CellPadding.y * 2.0F,
                                                          colonyHeight, classHeight, conditionHeight});
                    ImGui::TableSetColumnIndex(0);
                    const auto origin = ImGui::GetCursorScreenPos();
                    const auto cell = [&](int column, float height) {
                        ImGui::TableSetColumnIndex(column);
                        ImGui::SetCursorScreenPos(
                            {ImGui::GetCursorScreenPos().x, origin.y + (contentHeight - height) * 0.5F});
                    };
                    const bool selected = selectedOrderId_ == row.orderId;
                    if (selected) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                               ImGui::GetColorU32(uiColor(UiColor::SelectedSurface)));
                    }
                    const std::string queue =
                        row.queuePosition > 0 ? "#" + std::to_string(row.queuePosition) : "--";
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0F);
                    // Selectable expands its hit box by half the item spacing.
                    // Include the row's breathing room without increasing layout height.
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x,
                                                                    ImGui::GetStyle().CellPadding.y * 2.0F});
                    if (ImGui::Selectable("##Order", selected,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowOverlap,
                                          {0.0F, contentHeight})) {
                        selectedOrderId_ = row.orderId;
                    }
                    ImGui::PopStyleVar();
                    cell(0, ImGui::GetFontSize());
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0F);
                    ImGui::TextUnformatted(queue.c_str());
                    if (selected) {
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            {origin.x, origin.y}, {origin.x + 3.0F, origin.y + contentHeight},
                            ImGui::GetColorU32(uiColor(UiColor::Focus)));
                    }
                    cell(1, colonyHeight);
                    ImGui::TextWrapped("%s", row.colonyName.c_str());
                    cell(2, classHeight);
                    const auto classOrigin = ImGui::GetCursorScreenPos();
                    ImGui::TextWrapped("%s", row.shipClassName.c_str());
                    if (!classNote.empty()) {
                        ImGui::SetCursorScreenPos({classOrigin.x, classOrigin.y + nameHeight + 4.0F});
                        ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
                        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
                        ImGui::TextWrapped("%s", classNote.c_str());
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }
                    cell(3, ImGui::GetFontSize());
                    ImGui::Text("%d / %d", row.quantityCompleted, row.quantityRequested);
                    cell(4, uiTextSize(UiTextRole::Section) + 8.0F);
                    statusBadge(badgeState(row.state), stateLabel(row.state));
                    cell(5, ImGui::GetFontSize());
                    ImGui::TextWrapped("%s", etaLabel(row).c_str());
                    cell(6, conditionHeight);
                    ImGui::TextWrapped("%s", row.primaryCondition.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
        renderBuildOrder(queries, service, classes);
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(UiColor::Surface));
        ImGui::PushStyleColor(ImGuiCol_Border, uiColor(UiColor::Divider));
        ImGui::BeginChild("ShipyardSelectedOrder", {0.0F, bodyHeight}, ImGuiChildFlags_Borders);
        const auto selected = std::find_if(backlog.begin(), backlog.end(),
                                           [&](const auto& row) { return selectedOrderId_ == row.orderId; });
        if (selected != backlog.end())
            renderOrderDetail(*selected, classes);
        else
            ImGui::TextWrapped("Select an order to inspect its current condition and remaining work.");
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
}

void ShipyardPanel::renderOrderDetail(const ProductionBacklogSummary& order,
                                      const std::vector<ShipClassSummary>& classes) {
    const auto cls = std::find_if(classes.begin(), classes.end(),
                                  [&](const auto& row) { return row.id == order.shipClassId; });
    const std::string subtitle =
        (cls == classes.end() ? std::string{} : cls->roleName + " vessel / ") + order.colonyName;
    objectTitle(order.shipClassName, subtitle);
    ImGui::Dummy({0.0F, 4.0F});
    statusBadge(badgeState(order.state), stateLabel(order.state));
    sectionTitle("Current condition");
    ImGui::TextWrapped("%s", order.primaryCondition.c_str());
    if (!order.blockingMaterialName.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::Warning));
        ImGui::TextWrapped("Blocking material: %s", order.blockingMaterialName.c_str());
        ImGui::PopStyleColor();
    }
    if (order.blockedByComponentSupply && !order.componentSupplyExplanation.empty()) {
        sectionTitle("Component supply");
        ImGui::TextWrapped("%s", order.componentSupplyExplanation.c_str());
    }

    sectionTitle("Progress");
    keyValue("Hulls",
             std::to_string(order.quantityCompleted) + " / " + std::to_string(order.quantityRequested));
    if (order.state == ProductionBacklogState::Completed) {
        keyValue("Remaining build work", "0 BP");
    } else {
        keyValue("Build work",
                 amount(order.accumulatedBuildPoints) + " / " + amount(order.currentHullBuildPoints) + " BP");
        progressMeter("", order.accumulatedBuildPoints, order.currentHullBuildPoints);
        keyValue("Order work remaining", amount(order.buildPointsRemaining) + " BP");
    }
    keyValue("ETA", order.etaDays ? etaLabel(order) : "No completion estimate");

    sectionTitle("Materials remaining");
    if (order.requiredMaterialsRemaining.empty())
        ImGui::TextUnformatted("No materials remaining");
    else
        for (const auto& material : order.requiredMaterialsRemaining)
            keyValue(material.materialName, amount(material.amount));

    sectionTitle("Yard");
    keyValue("Effective capacity", amount(order.effectiveShipyardCapacity) + " BP/day");
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Detailed explanation"))
        ImGui::TextWrapped("%s", order.explanation.c_str());
    ImGui::Spacing();
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Provenance));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextMuted));
    ImGui::Text("Order #%lld | Class #%lld", static_cast<long long>(order.orderId.value),
                static_cast<long long>(order.shipClassId.value));
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void ShipyardPanel::renderBuildOrder(const SimulationQueries& queries, SimulationService& service,
                                     const std::vector<ShipClassSummary>& classes) {
    if (!ImGui::CollapsingHeader("New build order"))
        return;
    const auto colonies = queries.colonies();
    if (!colonies.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
        ImGui::TextWrapped("Build location: %s", colonies.front().name.c_str());
        ImGui::PopStyleColor();
    }
    if (!classes.empty()) {
        auto selected = std::find_if(classes.begin(), classes.end(),
                                     [&](const auto& row) { return selectedBuildClassId_ == row.id; });
        if (selected == classes.end()) {
            selectedBuildClassId_ = classes.front().id;
            selected = classes.begin();
        }
        const std::string label = selected->name + " r" + std::to_string(selected->revision);
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##BuildClass", label.c_str())) {
            for (const auto& row : classes) {
                const std::string option = row.name + " r" + std::to_string(row.revision) + " (#" +
                                           std::to_string(row.id.value) + ")";
                if (ImGui::Selectable(option.c_str(), selectedBuildClassId_ == row.id))
                    selectedBuildClassId_ = row.id;
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::Button("Build one"))
        buildSelectedClass(queries, service);
    if (!feedbackIsRevision_)
        renderFeedback();
}

void ShipyardPanel::renderFeedback() const {
    if (lastCommandMessage_.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          uiColor(lastCommandSucceeded_ ? UiColor::Positive : UiColor::Danger));
    ImGui::TextWrapped("%s", lastCommandMessage_.c_str());
    ImGui::PopStyleColor();
}

void ShipyardPanel::buildSelectedClass(const SimulationQueries& queries, SimulationService& service) {
    feedbackIsRevision_ = false;
    const std::optional<ColonySummary> colony = firstProductionColony(queries.colonies());
    if (!colony.has_value()) {
        applyResult(CommandResult::failure("No colony is available"));
        return;
    }

    if (!selectedBuildClassId_.has_value()) {
        applyResult(CommandResult::failure("No ship class is selected"));
        return;
    }

    applyResult(service.execute(AssignShipyardBuildCommand{
        .colonyId = colony->id, .shipClassId = *selectedBuildClassId_, .quantity = 1}));
}

void ShipyardPanel::resetWorldState() {
    selectedOrderId_.reset();
    selectOrdersTab_ = true;
    lastCommandMessage_.clear();
    feedbackIsRevision_ = false;
    selectedBuildClassId_.reset();
    draftSourceId_.reset();
    draftQuantities_.clear();
    draftName_.fill('\0');
}

void ShipyardPanel::selectDraftSource(const ShipClassSummary& source,
                                     const std::vector<ShipComponentSummary>& catalog) {
    draftSourceId_ = source.id;
    draftQuantities_.assign(catalog.size(), 0);
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        for (const ShipComponentInstall& install : source.components) {
            if (install.componentId == catalog[i].id) draftQuantities_[i] = install.quantity;
        }
    }
    draftName_.fill('\0');
    const std::string name = source.name + " revision";
    std::memcpy(draftName_.data(), name.data(), std::min(name.size(), draftName_.size() - 1));
}

void ShipyardPanel::renderDesignEditor(const SimulationQueries& queries, SimulationService& service,
                                      const std::vector<ShipClassSummary>& classes) {
    if (classes.empty()) return;
    const auto catalog = queries.shipComponents();
    const auto source = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
        return draftSourceId_ == row.id;
    });
    if (source == classes.end() || draftQuantities_.size() != catalog.size()) {
        selectDraftSource(classes.front(), catalog);
    }
    const auto selected = std::find_if(classes.begin(), classes.end(), [this](const ShipClassSummary& row) {
        return draftSourceId_ == row.id;
    });
    ImGui::TextUnformatted("New immutable design revision");
    const std::string currentLabel = selected->name + " r" + std::to_string(selected->revision);
    if (ImGui::BeginCombo("Start from", currentLabel.c_str())) {
        for (const ShipClassSummary& row : classes) {
            const std::string label = row.name + " r" + std::to_string(row.revision) +
                " (#" + std::to_string(row.id.value) + ")";
            if (ImGui::Selectable(label.c_str(), draftSourceId_ == row.id)) selectDraftSource(row, catalog);
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Revision name", draftName_.data(), draftName_.size());
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(catalog[i].name.c_str());
        ImGui::SameLine();
        // Preserve an editable value beside both step buttons at the new scale.
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 2.0F + ImGui::CalcTextSize("0000").x +
                                ImGui::GetStyle().FramePadding.x * 2.0F +
                                ImGui::GetStyle().ItemInnerSpacing.x * 2.0F);
        ImGui::InputInt("Quantity", &draftQuantities_[i]);
        draftQuantities_[i] = std::max(0, draftQuantities_[i]);
        ImGui::TextDisabled("Mass %.0f  Vol %.0f  Hull space %.0f  Power +%.0f / -%.0f  Tank %.0f  Survey %.0f  BP %.0f",
            catalog[i].mass, catalog[i].volume, catalog[i].internalVolumeCapacity,
            catalog[i].powerGeneration, catalog[i].powerDemand,
            catalog[i].propellantCapacity, catalog[i].surveyCapability, catalog[i].buildPoints);
        ImGui::TextDisabled("Cargo %.1f normalized units | Handling %.1f units/day when powered",
            catalog[i].cargoCapacity, catalog[i].cargoHandlingPerDay);
        if(catalog[i].measurementProfile) {
            const auto& method=*catalog[i].measurementProfile;
            ImGui::Text("%s v%d: threshold %.1f normalized signal; accessibility %s",method.name.c_str(),
                method.methodVersion,method.detectionThreshold,method.measuresAccessibility?"coarse class":"unmeasured");
        }
        if (catalog[i].demonstrated) {
            ImGui::Text("Demonstrated technical component; public target %.3f, acquired tests %zu",
                        catalog[i].publicTargetThreshold.value_or(0.0),
                        catalog[i].testProvenance.size());
            ImGui::Text("Serial production colonies: %zu | Available local prototypes: %zu | Support teams: %zu",
                        catalog[i].serialProductionColonies.size(),
                        catalog[i].availablePrototypeColonies.size(),
                        catalog[i].supportQualifiedTeams.size());
            ImGui::TextWrapped("Design admission is independent of production and support readiness. "
                               "A shipyard order waits for a local prototype or qualified process.");
        }
        if (catalog[i].serviceProfile) {
            const auto& profile = *catalog[i].serviceProfile;
            ImGui::TextDisabled("Survey duty capacity %.1f/unit | family #%lld | %.3f team-workdays/restored duty/unit",
                profile.dutyCapacity, static_cast<long long>(profile.familyId.value), profile.teamWorkdaysPerDuty);
        }
        for (const auto& rate : catalog[i].workshopRates) {
            ImGui::TextDisabled("Workshop family #%lld: %.3f team-workdays/day when powered",
                static_cast<long long>(rate.familyId.value), rate.teamWorkdaysPerDay);
        }
        std::ostringstream componentCost;
        for (std::size_t material = 0; material < processedMaterialCount(); ++material) {
            if (catalog[i].buildCost.amount[material] > 0.0) {
                if (componentCost.tellp() > 0) componentCost << ", ";
                componentCost << toString(static_cast<ProcessedMaterial>(material)) << ' '
                              << catalog[i].buildCost.amount[material];
            }
        }
        const std::string componentCostText = componentCost.str();
        ImGui::TextWrapped("Materials: %s", componentCostText.empty() ? "--" : componentCostText.c_str());
        ImGui::PopID();
    }
    std::vector<ShipComponentInstall> draft;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (draftQuantities_[i] > 0) draft.push_back({catalog[i].id, draftQuantities_[i]});
    }
    const ShipDesignDraftPreview preview = queries.previewShipDesign(draft);
    const auto& design = preview.design;
    ImGui::Text("Dry mass %.1f | Volume %.1f / %.1f (available %.1f)",
                design.dryMass, design.usedVolume, design.volumeCapacity, design.availableVolume);
    ImGui::Text("Power %.1f generated / %.1f demanded (margin %.1f)",
                design.powerGeneration, design.powerDemand, design.powerMargin);
    ImGui::Text("Tankage %.1f | Survey %.1f | Build points %.1f",
                design.propellantCapacity, design.surveyCapability, design.buildPoints);
    ImGui::Text("Cargo %.1f normalized units | Installed handling %.1f units/day",
                design.cargoCapacity, design.cargoHandlingPerDay);
    ImGui::TextWrapped("Each hull requires enough total power to operate its handling equipment. Cargo remains separate from engine tanks; payload does not yet change prototype transit time or fuel cost.");
    for (const auto& rate : design.workshopRates) {
        ImGui::Text("Installed workshop family #%lld: %.3f team-workdays/day; qualified team and colony supplies required",
            static_cast<long long>(rate.familyId.value), rate.teamWorkdaysPerDay);
    }
    ImGui::Text("Constructible: %s", design.constructible ? "Yes" : "No");
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        if (design.buildCost.amount[i] > 0.0) {
            ImGui::Text("%s: %.1f", toString(static_cast<ProcessedMaterial>(i)).data(), design.buildCost.amount[i]);
        }
    }
    for (const std::string& warning : preview.warnings) ImGui::TextWrapped("Warning: %s", warning.c_str());
    if (ImGui::Button("Save New Revision")) {
        feedbackIsRevision_ = true;
        applyResult(service.execute(CreateShipClassRevisionCommand{
            .name = draftName_.data(), .role = selected->role,
            .basedOnClassId = draftSourceId_, .components = draft
        }));
        if (lastCommandSucceeded_) {
            const auto updated = queries.shipClasses();
            selectedBuildClassId_ = updated.back().id;
            selectDraftSource(updated.back(), catalog);
        }
    }
}

void ShipyardPanel::applyResult(const CommandResult& result) {
    lastCommandSucceeded_ = result.ok;
    lastCommandMessage_ = result.message.empty() ? "Command completed" : result.message;
}

} // namespace deep::ui_imgui
