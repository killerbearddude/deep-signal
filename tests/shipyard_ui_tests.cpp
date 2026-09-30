// Exercises the actual Shipyard panel at the fixed desktop canvas. Clipboard
// capture proves submitted hierarchy and mouse-driven navigation; it does not
// establish native visual quality or replace the screenshot review.
#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/OperationalWindow.h"
#include "ui_imgui/ShipyardPanel.h"
#include "ui_imgui/UiTheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>
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
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            captured = text;
        };
        ui_imgui::applyDeepSignalTheme();
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
            table.OuterRect.Min.y + table.InstanceDataFirst.LastTopHeadersRowHeight +
                ImGui::GetStyle().CellPadding.y + 8.0F};
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
    requireText(orders, {"Shipyard / Production", "Orders", "New Revision", "Shipyard orders",
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
    requireText(returned, {"Shipyard orders", "No shipyard capacity"});
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
    requireText(replaced, {"Shipyard orders", "No shipyard orders"});
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
} // namespace

int main() {
    try {
        default_orders_and_revision_navigation();
        order_selection_follows_persisted_work();
        world_replacement_resets_the_workflow();
        wrapped_class_row_remains_selectable();
        std::cout << "Shipyard UI: 4 scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Shipyard UI failure: " << error.what() << '\n';
        return 1;
    }
}
