// Exercises the actual Shipyard panel at the fixed desktop canvas. Clipboard
// capture proves submitted hierarchy and mouse-driven navigation; it does not
// establish native visual quality or replace the screenshot review.
#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "app/TechnicalDevelopmentFixture.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/ShipyardPanel.h"
#include "ui_imgui/UiTheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace deep;
std::string captured;

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

void requireText(const std::string& text, std::initializer_list<std::string_view> values) {
    for (const auto value : values)
        require(text.find(value) != std::string::npos,
                "Shipyard submission is missing: " + std::string(value));
}

struct ImGuiSession {
    ImGuiSession() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {1920, 1080};
        io.DeltaTime = 1.0F / 60.0F;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            captured = text;
        };
        ui_imgui::applyDeepSignalTheme();
        require(io.FontDefault->IsGlyphInFont(0x00B7),
                "the refined face retains the middle-dot separator used by existing panels");
    }
    ~ImGuiSession() {
        ImGui::DestroyContext();
    }
    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;
};

std::string render(ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    captured.clear();
    ImGui::NewFrame();
    ImGui::LogToClipboard();
    ImGui::SetNextWindowPos({20, 20}, ImGuiCond_Always);
    ui_imgui::setOperationalWorkArea({0, 0, 1920, 1080});
    bool visible = true;
    panel.render(SimulationQueries{service}, service, visible);
    ImGui::LogFinish();
    ImGui::Render();
    require(visible, "Shipyard remains open during submission");
    return captured;
}

// These read-only lookups use the pinned ImGui's submitted geometry to locate
// controls. Navigation itself goes through ordinary mouse input; no application
// selection or tab state is injected, and runtime code needs no test hooks.
ImVec2 tabPoint(std::string_view label) {
    auto& context = *ImGui::GetCurrentContext();
    for (int i = 0; i < context.TabBars.GetMapSize(); ++i) {
        auto* bar = context.TabBars.TryGetMapData(i);
        if (bar == nullptr || bar->CurrFrameVisible != context.FrameCount)
            continue;
        for (auto& tab : bar->Tabs) {
            if (label == ImGui::TabBarGetTabName(bar, &tab))
                return {bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5F,
                        (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5F};
        }
    }
    throw std::runtime_error("Cannot locate Shipyard tab: " + std::string(label));
}

const ImGuiTable& orderTable() {
    auto& context = *ImGui::GetCurrentContext();
    for (int i = 0; i < context.Tables.GetMapSize(); ++i) {
        const auto* table = context.Tables.TryGetMapData(i);
        if (table == nullptr || table->LastFrameActive != context.FrameCount || table->ColumnsCount != 7)
            continue;
        require(table->InstanceDataFirst.LastTopHeadersRowHeight > 0.0F,
                "strategic orders table has a submitted header");
        return *table;
    }
    throw std::runtime_error("Cannot locate strategic orders table");
}

ImVec2 firstOrderClassPoint() {
    const auto& table = orderTable();
    return {(table.Columns[2].MinX + table.Columns[2].MaxX) * 0.5F,
            // Select through the row's top padding, not only its text content.
            table.OuterRect.Min.y + table.InstanceDataFirst.LastTopHeadersRowHeight + 2.0F};
}

std::string click(ImVec2 position, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(position.x, position.y);
    render(panel, service);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    render(panel, service);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    render(panel, service);
    return render(panel, service);
}

ImGuiID tabId(std::string_view label) {
    auto& context = *ImGui::GetCurrentContext();
    for (int i = 0; i < context.TabBars.GetMapSize(); ++i) {
        auto* bar = context.TabBars.TryGetMapData(i);
        if (bar == nullptr || bar->CurrFrameVisible != context.FrameCount)
            continue;
        for (auto& tab : bar->Tabs)
            if (label == ImGui::TabBarGetTabName(bar, &tab))
                return tab.ID;
    }
    throw std::runtime_error("Cannot resolve active tab identity");
}

const ImGuiWindow& visibleWindow(std::string_view name) {
    const auto& context = *ImGui::GetCurrentContext();
    for (const auto* window : context.Windows)
        if (window->LastFrameActive == context.FrameCount &&
            std::string_view(window->Name).find(name) != std::string_view::npos)
            return *window;
    throw std::runtime_error("Cannot resolve visible Shipyard child: " + std::string(name));
}

std::string key(ImGuiKey value, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(value, true);
    render(panel, service);
    io.AddKeyEvent(value, false);
    render(panel, service);
    return render(panel, service);
}

// Ordinary Tab input locates the submitted control and scrolls it into view.
// The read-only navigation rectangle then supplies a real mouse click target;
// neither panel state nor ImGui focus/selection state is injected by the test.
ImVec2 controlPoint(ImGuiID id, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    for (int attempt = 0; attempt < 160; ++attempt) {
        const auto& context = *ImGui::GetCurrentContext();
        if (context.NavId == id && context.NavWindow != nullptr) {
            return ImGui::WindowRectRelToAbs(context.NavWindow,
                                             context.NavWindow->NavRectRel[context.NavLayer])
                .GetCenter();
        }
        key(ImGuiKey_Tab, panel, service);
    }
    throw std::runtime_error("Cannot navigate to Shipyard control " + std::to_string(id));
}

std::string clickControl(ImGuiID id, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    return click(controlPoint(id, panel, service), panel, service);
}

std::string chooseCombo(ImGuiID id, const std::string& option, std::optional<std::string> optionId,
                        ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    clickControl(id, panel, service);
    const auto& context = *ImGui::GetCurrentContext();
    require(!context.OpenPopupStack.empty() && context.OpenPopupStack.back().Window != nullptr,
            "a real click opens the class selector");
    auto seed = context.OpenPopupStack.back().Window->IDStack.back();
    if (optionId)
        seed = ImHashStr(optionId->c_str(), 0, seed);
    return clickControl(ImHashStr(option.c_str(), 0, seed), panel, service);
}

std::string chooseComparison(char side, const ShipClassSummary& row, ui_imgui::ShipyardPanel& panel,
                             SimulationService& service) {
    const auto& child = visibleWindow(side == 'A' ? "CompareIdentityA" : "CompareIdentityB");
    const auto id = ImHashStr(side == 'A' ? "##CompareA" : "##CompareB", 0, child.IDStack.back());
    // Child windows have separate keyboard focus scopes. A passive heading
    // click enters the side before Tab locates its selector.
    click({child.Pos.x + 8.0F, child.Pos.y + 8.0F}, panel, service);
    return chooseCombo(id, row.name + " r" + std::to_string(row.revision) + " · " + row.roleName,
                       std::to_string(row.id.value), panel, service);
}

std::string comparisonIdentity(const std::string& text, char side) {
    const std::string label = side == 'A' ? "DESIGN A" : "DESIGN B";
    const auto start = text.find(label);
    require(start != std::string::npos, "both comparison side headings are submitted");
    const auto end = text.find(side == 'A' ? "DESIGN B" : "CORE DESIGN", start + label.size());
    require(end != std::string::npos, "comparison identity has a following section");
    return text.substr(start, end - start);
}

void requireCompared(const std::string& text, const ShipClassSummary& a, const ShipClassSummary& b) {
    for (const auto& [side, row] : {std::pair{'A', &a}, std::pair{'B', &b}}) {
        const auto identity = comparisonIdentity(text, side);
        require(identity.find(row->name + " r" + std::to_string(row->revision)) != std::string::npos,
                "Unexpected comparison side " + std::string(1, side) + ": " + identity);
    }
}

void replaceInput(ImGuiID id, const char* value, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    clickControl(id, panel, service);
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    key(ImGuiKey_A, panel, service);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    render(panel, service);
    io.AddInputCharactersUTF8(value);
    render(panel, service);
    key(ImGuiKey_Enter, panel, service);
}

std::string inputValue(ImGuiID id, ui_imgui::ShipyardPanel& panel, SimulationService& service) {
    clickControl(id, panel, service);
    const auto* input = ImGui::GetInputTextState(id);
    require(input != nullptr, "real input receives focus and exposes its submitted text");
    return input->TextA.Data;
}

void requireUnchanged(const GameState& before, const GameState& after) {
    require(before.date.day == after.date.day && before.eventLog.size() == after.eventLog.size() &&
                before.ids.nextEventId == after.ids.nextEventId &&
                before.ids.nextShipClassId == after.ids.nextShipClassId &&
                before.ids.nextShipyardOrderId == after.ids.nextShipyardOrderId &&
                before.shipClasses.size() == after.shipClasses.size() &&
                before.shipyardOrders.size() == after.shipyardOrders.size() &&
                before.prototypeComponentUnits.size() == after.prototypeComponentUnits.size(),
            "comparison navigation records no command, new revision, order, prototype, or elapsed day");
    for (std::size_t index = 0; index < before.shipClasses.size(); ++index) {
        const auto& a = before.shipClasses[index];
        const auto& b = after.shipClasses[index];
        require(a.id == b.id && a.name == b.name && a.revision == b.revision && a.role == b.role &&
                    a.basedOnClassId == b.basedOnClassId && a.components == b.components &&
                    a.speedKmPerDay == b.speedKmPerDay,
                "comparison leaves every immutable class and its installation order unchanged");
    }
    for (std::size_t index = 0; index < before.shipyardOrders.size(); ++index) {
        const auto& a = before.shipyardOrders[index];
        const auto& b = after.shipyardOrders[index];
        require(a.id == b.id && a.colonyId == b.colonyId && a.shipClassId == b.shipClassId &&
                    a.quantityRequested == b.quantityRequested &&
                    a.quantityCompleted == b.quantityCompleted &&
                    a.accumulatedBuildPoints == b.accumulatedBuildPoints && a.status == b.status &&
                    a.currentHullSupplyPlan == b.currentHullSupplyPlan,
                "comparison preserves durable order identity, revision binding, and physical progress");
    }
    for (std::size_t index = 0; index < before.colonies.size(); ++index)
        require(before.colonies[index].processedStockpile.amount ==
                        after.colonies[index].processedStockpile.amount &&
                    before.colonies[index].stockpile.amount == after.colonies[index].stockpile.amount,
                "comparison does not consume inventory");
    for (std::size_t index = 0; index < before.prototypeComponentUnits.size(); ++index) {
        const auto& a = before.prototypeComponentUnits[index];
        const auto& b = after.prototypeComponentUnits[index];
        require(a.state == b.state && a.reservedOrderId == b.reservedOrderId &&
                    a.reservedHullNumber == b.reservedHullNumber && a.consumedShipId == b.consumedShipId,
                "comparison does not reserve or consume a physical prototype");
    }
}

struct TemporarySave {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("deep_signal_shipyard_compare_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    ~TemporarySave() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

GameState waitingFixture() {
    auto state = createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 0.0;
    SimulationService service(std::move(state));
    require(service
                .execute(AssignShipyardBuildCommand{service.state().colonies.front().id,
                                                    service.state().shipClasses.front().id, 1})
                .ok,
            "zero-capacity production intent is accepted for the UI fixture");
    return service.state();
}

GameState completedFixture() {
    auto state = createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 1000.0;
    state.colonies.front().processorCapacity = 0.0;
    SimulationService service(std::move(state));
    for (int i = 0; i < 2; ++i) {
        require(service
                    .execute(AssignShipyardBuildCommand{service.state().colonies.front().id,
                                                        service.state().shipClasses.front().id, 1})
                    .ok,
                "completed-order fixture records its production intent");
        require(service.advanceDaysDetailed(1).advancedDays == 1 &&
                    service.state().shipyardOrders.back().status == ShipyardOrderStatus::Completed,
                "completed-order fixture earns its hull through real daily construction");
    }
    return service.state();
}

void default_orders_and_revision_navigation() {
    ImGuiSession gui;
    SimulationService service(waitingFixture());
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    const auto orders = render(panel, service);
    requireText(orders, {"SHIPYARD / PRODUCTION", "Orders", "New Revision", "SHIPYARD ORDERS",
                         "Survey Cutter", "Waiting", "No shipyard capacity", "No completion estimate"});
    for (const auto* editorText :
         {"New immutable design revision", "Compact Survey Hull", "Standard Instrument Workshop"})
        require(orders.find(editorText) == std::string::npos,
                "Orders must not submit the full component editor before current commitments");
    require(orders.find("Status: OK") == std::string::npos && orders.find("Ready") == std::string::npos,
            "no command feedback is invented before an action");
    requireText(click(tabPoint("New Revision"), panel, service),
                {"New immutable design revision", "Compact Survey Hull", "Standard Instrument Workshop"});
    const auto returned = click(tabPoint("Orders"), panel, service);
    requireText(returned, {"SHIPYARD ORDERS", "No shipyard capacity"});
    require(returned.find("New immutable design revision") == std::string::npos,
            "returning to Orders hides the editor again");
}

void order_selection_follows_persisted_work() {
    ImGuiSession gui;
    auto state = completedFixture();
    state.colonies.front().shipyardCapacity = 0.0;
    SimulationService service(std::move(state));
    require(service
                .execute(AssignShipyardBuildCommand{service.state().colonies.front().id,
                                                    service.state().shipClasses.front().id, 1})
                .ok,
            "a new waiting commitment follows completed history");
    const auto eventCount = service.state().eventLog.size();
    const auto orderCount = service.state().shipyardOrders.size();
    const auto nextOrder = service.state().ids.nextShipyardOrderId;
    const auto stock = service.state().colonies.front().processedStockpile.amount;
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    requireText(render(panel, service), {"Order #3 | Class #1", "No shipyard capacity"});
    const auto selected = click(firstOrderClassPoint(), panel, service);
    requireText(selected, {"Order #1 | Class #1", "Remaining build work", "0 BP", "No materials remaining"});
    requireText(render(panel, service), {"Order #1 | Class #1"});
    require(service.state().eventLog.size() == eventCount &&
                service.state().shipyardOrders.size() == orderCount &&
                service.state().ids.nextShipyardOrderId == nextOrder &&
                service.state().colonies.front().processedStockpile.amount == stock,
            "viewing and selecting production history does not mutate gameplay");

    SimulationService completed(completedFixture());
    ui_imgui::ShipyardPanel completedPanel;
    render(completedPanel, completed);
    requireText(render(completedPanel, completed), {"Order #1 | Class #1", "No materials remaining"});
}

void world_replacement_resets_the_workflow() {
    ImGuiSession gui;
    SimulationService service(waitingFixture());
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    render(panel, service);
    requireText(click(tabPoint("New Revision"), panel, service), {"New immutable design revision"});
    int resets = 0;
    InformationInteractionAdapter interactions(service, [&] {
        ++resets;
        panel.resetWorldState();
    });
    require(!interactions.loadGame({}).ok && resets == 0, "failed Load preserves the Shipyard workflow");
    requireText(render(panel, service), {"New immutable design revision"});
    require(interactions.newGame().ok && resets == 1,
            "successful replacement resets Shipyard selection and editor state");
    render(panel, service);
    const auto replaced = render(panel, service);
    requireText(replaced, {"SHIPYARD ORDERS", "No shipyard orders"});
    require(replaced.find("Order #") == std::string::npos &&
                replaced.find("New immutable design revision") == std::string::npos,
            "the new world's empty Orders view has no selected old order or editor");
}

void wrapped_class_row_remains_selectable() {
    ImGuiSession gui;
    auto state = completedFixture();
    state.colonies.front().shipyardCapacity = 0.0;
    SimulationService service(std::move(state));
    const auto source = service.state().shipClasses.front();
    require(
        service
            .execute(CreateShipClassRevisionCommand{
                .name = "Outer System Survey Cutter Extended Reconnaissance Configuration With Additional "
                        "Mission Planning Reference Notes",
                .role = source.role,
                .basedOnClassId = source.id,
                .components = source.components})
            .ok,
        "a long but valid immutable revision name is accepted through the command boundary");
    const auto classId = service.state().shipClasses.back().id;
    require(service.execute(AssignShipyardBuildCommand{service.state().colonies.front().id, classId, 1}).ok,
            "the long-named revision becomes a real waiting order after completed history");
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    render(panel, service);
    requireText(click(firstOrderClassPoint(), panel, service), {"Order #1 | Class #1"});

    // Scroll through the real table so the lower part of the final wrapped row
    // is visible. A fixed-height hit target used to stop before this text ended.
    const auto& beforeScroll = orderTable();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent((beforeScroll.Columns[2].MinX + beforeScroll.Columns[2].MaxX) * 0.5F,
                        (beforeScroll.InnerClipRect.Min.y + beforeScroll.InnerClipRect.Max.y) * 0.5F);
    render(panel, service);
    io.AddMouseWheelEvent(0.0F, -10.0F);
    render(panel, service);
    render(panel, service);
    const auto& table = orderTable();
    const ImVec2 lowerClassCell{(table.Columns[2].MinX + table.Columns[2].MaxX) * 0.5F,
                                table.RowPosY2 - ImGui::GetStyle().CellPadding.y - 8.0F};
    require(lowerClassCell.y - table.RowPosY1 > ImGui::GetFontSize() * 4.0F,
            "the regression clicks well below the former fixed row hit target");
    require(table.InnerClipRect.Contains(lowerClassCell),
            "the lower wrapped class cell is visibly inside the scrolled table");
    const auto selected = click(lowerClassCell, panel, service);
    requireText(selected, {"Order #3 | Class #" + std::to_string(classId.value), "No shipyard capacity"});
}

void compare_earned_designs_through_real_selectors() {
    ImGuiSession gui;
    SimulationService service(earnTechnicalDevelopmentFixture(6.0));
    const auto before = service.state();
    const auto classes = SimulationQueries{service}.shipClasses();
    const auto find = [&](std::string_view name) -> const ShipClassSummary& {
        const auto row = std::find_if(classes.begin(), classes.end(),
                                      [&](const auto& value) { return value.name == name; });
        require(row != classes.end(), "the command-earned fixture contains both comparison targets");
        return *row;
    };
    const auto& established = find("Established Characterization Cutter");
    const auto& precision = find("Precision Characterization Cutter");
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    render(panel, service);
    require(tabPoint("Orders").x < tabPoint("Compare Designs").x &&
                tabPoint("Compare Designs").x < tabPoint("New Revision").x,
            "Compare Designs is the middle Shipyard tab");
    auto compared = click(tabPoint("Compare Designs"), panel, service);
    requireText(compared, {"COMPARE DESIGNS", "DESIGN A", "DESIGN B", "CORE DESIGN", "SURVEY INSTRUMENTATION",
                           "BUILD MATERIALS"});
    requireCompared(compared, classes.front(), classes.at(1));
    compared = chooseComparison('A', established, panel, service);
    requireCompared(compared, established, classes.at(1));
    compared = chooseComparison('B', precision, panel, service);
    requireCompared(compared, established, precision);
    const auto threshold = compared.find("Detection threshold");
    require(threshold != std::string::npos, "scientific values have their detection-threshold label");
    requireText(compared.substr(threshold, compared.find('\n', threshold) - threshold), {"10", "6"});
    require(compared.find("family #") == std::string::npos && compared.find("team #") == std::string::npos &&
                compared.find("colony #") == std::string::npos,
            "default comparison uses resolved equipment, team and colony names");
    requireUnchanged(before, service.state());
}

void compare_selection_preserves_other_shipyard_workflows() {
    ImGuiSession gui;
    auto state = completedFixture();
    state.colonies.front().shipyardCapacity = 0.0;
    SimulationService service(std::move(state));
    require(service
                .execute(AssignShipyardBuildCommand{service.state().colonies.front().id,
                                                    service.state().shipClasses.front().id, 1})
                .ok,
            "the isolation fixture has an active order after completed history");
    const auto classes = SimulationQueries{service}.shipClasses();
    const auto catalog = SimulationQueries{service}.shipComponents();
    const auto tank = std::find_if(catalog.begin(), catalog.end(),
                                   [](const auto& row) { return row.name == "Standard Propellant Tank"; });
    require(tank != catalog.end(), "the draft fixture has an editable propellant tank");
    const int tankIndex = static_cast<int>(tank - catalog.begin());
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    render(panel, service);
    requireText(click(firstOrderClassPoint(), panel, service), {"Order #1 | Class #1"});

    const auto& commitments = visibleWindow("ShipyardCommitments");
    auto seed = commitments.IDStack.back();
    click({commitments.Pos.x + 8.0F, commitments.Pos.y + 8.0F}, panel, service);
    clickControl(ImHashStr("New build order", 0, seed), panel, service);
    seed = visibleWindow("ShipyardCommitments").IDStack.back();
    chooseCombo(ImHashStr("##BuildClass", 0, seed),
                classes.at(2).name + " r" + std::to_string(classes.at(2).revision) + " (#" +
                    std::to_string(classes.at(2).id.value) + ")",
                std::nullopt, panel, service);

    click(tabPoint("New Revision"), panel, service);
    const auto editorSeed = tabId("New Revision");
    const auto nameId = ImHashStr("Revision name", 0, editorSeed);
    const auto quantityId = ImHashStr("Quantity", 0, ImHashData(&tankIndex, sizeof(tankIndex), editorSeed));
    replaceInput(nameId, "Independent unsaved draft", panel, service);
    replaceInput(quantityId, "2", panel, service);
    const auto before = service.state();
    click(tabPoint("Compare Designs"), panel, service);
    auto compared = chooseComparison('A', classes.at(1), panel, service);
    requireCompared(compared, classes.at(1), classes.at(1));
    compared = chooseComparison('B', classes.front(), panel, service);
    requireCompared(compared, classes.at(1), classes.front());
    requireUnchanged(before, service.state());

    const auto editor = click(tabPoint("New Revision"), panel, service);
    requireText(editor, {classes.front().name + " r" + std::to_string(classes.front().revision)});
    require(inputValue(nameId, panel, service) == "Independent unsaved draft",
            "comparison leaves the edited draft name intact");
    require(inputValue(quantityId, panel, service) == "2",
            "comparison leaves the edited installed quantity intact");
    requireUnchanged(before, service.state());

    requireText(click(tabPoint("Orders"), panel, service), {"Order #1 | Class #1"});
    const auto& returnedCommitments = visibleWindow("ShipyardCommitments");
    seed = returnedCommitments.IDStack.back();
    click({returnedCommitments.Pos.x + 8.0F, returnedCommitments.Pos.y + 8.0F}, panel, service);
    clickControl(ImHashStr("Build one", 0, seed), panel, service);
    require(service.state().shipyardOrders.size() == before.shipyardOrders.size() + 1 &&
                service.state().shipyardOrders.back().shipClassId == classes.at(2).id,
            "Build one retains its separately selected typed class across comparison changes");
}

void same_class_and_single_class_comparison() {
    ImGuiSession gui;
    SimulationService service(createHomeSystemScenario());
    ui_imgui::ShipyardPanel panel;
    const auto classes = SimulationQueries{service}.shipClasses();
    render(panel, service);
    render(panel, service);
    click(tabPoint("Compare Designs"), panel, service);
    requireCompared(chooseComparison('B', classes.front(), panel, service), classes.front(), classes.front());
    requireCompared(chooseComparison('A', classes.at(2), panel, service), classes.at(2), classes.front());

    auto single = createHomeSystemScenario();
    single.shipClasses.resize(1);
    SimulationService oneClass(std::move(single));
    panel.resetWorldState();
    render(panel, oneClass);
    render(panel, oneClass);
    const auto compared = click(tabPoint("Compare Designs"), panel, oneClass);
    requireCompared(compared, classes.front(), classes.front());
    requireText(compared, {"Only one"});
}

void long_comparison_names_keep_selectors_visible() {
    ImGuiSession gui;
    SimulationService service(createHomeSystemScenario());
    const auto source = service.state().shipClasses.front();
    require(service
                .execute(CreateShipClassRevisionCommand{
                    .name = "Outer System Characterization Cutter Extended Reconnaissance Configuration "
                            "With Additional Mission Planning Reference Notes And Long Duration "
                            "Scientific Instrument Support Requirements",
                    .role = source.role,
                    .basedOnClassId = source.id,
                    .components = source.components})
                .ok,
            "a long immutable comparison name is accepted through the command boundary");
    const auto classes = SimulationQueries{service}.shipClasses();
    const auto before = service.state();
    ui_imgui::ShipyardPanel panel;
    render(panel, service);
    render(panel, service);
    click(tabPoint("Compare Designs"), panel, service);
    chooseComparison('A', classes.back(), panel, service);
    requireCompared(chooseComparison('B', classes.back(), panel, service), classes.back(), classes.back());
    render(panel, service);
    for (const auto* name : {"CompareIdentityA", "CompareIdentityB"})
        require(visibleWindow(name).ScrollMax.y == 0.0F,
                "wrapped comparison identities must not hide their selectors in an inner header scroll");
    requireCompared(chooseComparison('A', classes.front(), panel, service), classes.front(), classes.back());
    requireCompared(chooseComparison('B', classes.at(1), panel, service), classes.front(), classes.at(1));
    requireUnchanged(before, service.state());
}

void world_replacement_resets_comparison_defaults() {
    ImGuiSession gui;
    SimulationService service(createHomeSystemScenario());
    ui_imgui::ShipyardPanel panel;
    const auto classes = SimulationQueries{service}.shipClasses();
    TemporarySave saved;
    require(service.saveGame(saved.path).ok, "the replacement fixture is a real current-schema save");
    render(panel, service);
    render(panel, service);
    click(tabPoint("Compare Designs"), panel, service);
    chooseComparison('A', classes.at(2), panel, service);
    requireCompared(chooseComparison('B', classes.front(), panel, service), classes.at(2), classes.front());
    int resets = 0;
    InformationInteractionAdapter interactions(service, [&] {
        ++resets;
        panel.resetWorldState();
    });
    require(!interactions.loadGame({}).ok && resets == 0, "failed Load does not reset comparison");
    requireCompared(render(panel, service), classes.at(2), classes.front());
    require(interactions.loadGame(saved.path).ok && resets == 1,
            "successful Load resets old-world selections");
    render(panel, service);
    requireText(render(panel, service), {"SHIPYARD ORDERS"});
    requireCompared(click(tabPoint("Compare Designs"), panel, service), classes.front(), classes.at(1));
    chooseComparison('A', classes.at(2), panel, service);
    chooseComparison('B', classes.front(), panel, service);
    require(interactions.newGame().ok && resets == 2,
            "successful New resets comparison even with matching IDs");
    render(panel, service);
    requireText(render(panel, service), {"SHIPYARD ORDERS"});
    requireCompared(click(tabPoint("Compare Designs"), panel, service), classes.front(), classes.at(1));
}
} // namespace

int main() {
    try {
        default_orders_and_revision_navigation();
        order_selection_follows_persisted_work();
        world_replacement_resets_the_workflow();
        wrapped_class_row_remains_selectable();
        compare_earned_designs_through_real_selectors();
        compare_selection_preserves_other_shipyard_workflows();
        same_class_and_single_class_comparison();
        long_comparison_names_keep_selectors_visible();
        world_replacement_resets_comparison_defaults();
        std::cout << "Shipyard UI: 9 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Shipyard UI failure: " << error.what() << '\n';
        return 1;
    }
}
