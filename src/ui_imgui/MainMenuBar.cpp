#include "ui_imgui/MainMenuBar.h"

// Implements application-level ImGui menu commands.
// Persistence actions still flow through SimulationService via SaveLoadPanel,
// while Workspace presets and manual View toggles compose existing panels
// without coupling them to global state.

#include "sim/Commands.h"
#include "app/SimulationQueries.h"

#include <imgui.h>

namespace deep::ui_imgui {

void applyWorkspace(Workspace workspace, PanelVisibility& visibility) {
    // Clear every operational panel so manual View changes cannot leak into the
    // next preset. The two global windows are controlled only through View.
    visibility.strategicMap = false;
    visibility.bodies = false;
    visibility.inspector = false;
    visibility.colonies = false;
    visibility.fleets = false;
    visibility.fleetOrders = false;
    visibility.surveyPrograms = false;
    visibility.freightPrograms = false;
    visibility.maintenancePrograms = false;
    visibility.science = false;
    visibility.shipyard = false;
    visibility.economyForecast = false;
    visibility.eventLog = false;

    switch (workspace) {
    case Workspace::System:
        visibility.strategicMap = true;
        visibility.bodies = true;
        break;
    case Workspace::Economy:
        visibility.economyForecast = true;
        break;
    case Workspace::Production:
        visibility.shipyard = true;
        visibility.colonies = true;
        visibility.economyForecast = true;
        break;
    case Workspace::Fleets:
        visibility.strategicMap = true;
        visibility.fleets = true;
        visibility.fleetOrders = true;
        visibility.freightPrograms = true;
        visibility.maintenancePrograms = true;
        break;
    case Workspace::Intelligence:
        visibility.science = true;
        // Exploration intelligence and Resource Survey remain in these panels
        // until the later workspace consolidation patches.
        visibility.strategicMap = true;
        visibility.bodies = true;
        visibility.fleetOrders = true;
        visibility.surveyPrograms = true;
        break;
    case Workspace::History:
        visibility.eventLog = true;
        break;
    }
}

float MainMenuBar::render(SimulationService& service, SaveLoadPanel& saveLoadPanel,
                         InformationInteractionAdapter& interactions, Workspace& workspace, PanelVisibility& visibility) {
    if (!ImGui::BeginMainMenuBar()) {
        return ImGui::GetMainViewport()->WorkPos.y;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Game")) {
            saveLoadPanel.newGame(interactions);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save")) {
            saveLoadPanel.save(service);
        }
        if (ImGui::MenuItem("Load")) {
            saveLoadPanel.load(interactions);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Workspace")) {
        constexpr struct {
            Workspace workspace;
            const char* label;
        } workspaces[] = {
            {Workspace::System, "System"},
            {Workspace::Economy, "Economy"},
            {Workspace::Production, "Production"},
            {Workspace::Fleets, "Fleets"},
            {Workspace::Intelligence, "Intelligence"},
            {Workspace::History, "History"}
        };

        for (const auto& entry : workspaces) {
            // The active item stays clickable to restore its preset after a
            // manual View change or a panel being closed.
            if (ImGui::MenuItem(entry.label, nullptr, workspace == entry.workspace)) {
                workspace = entry.workspace;
                applyWorkspace(workspace, visibility);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        // MenuItem stores directly into the shared visibility flags, making a
        // closed panel immediately reopenable without each panel knowing about
        // the main menu.
        ImGui::MenuItem("Save / Load", nullptr, &visibility.saveLoad);
        ImGui::MenuItem("Time Control", nullptr, &visibility.timeControl);
        ImGui::MenuItem("Strategic Map", nullptr, &visibility.strategicMap);
        ImGui::MenuItem("Bodies / System", nullptr, &visibility.bodies);
        ImGui::MenuItem("Legacy Inspector", nullptr, &visibility.inspector);
        ImGui::MenuItem("Colonies", nullptr, &visibility.colonies);
        ImGui::MenuItem("Fleets", nullptr, &visibility.fleets);
        ImGui::MenuItem("Fleet Orders", nullptr, &visibility.fleetOrders);
        ImGui::MenuItem("Survey Programs", nullptr, &visibility.surveyPrograms);
        ImGui::MenuItem("Freight / Supply Programs", nullptr, &visibility.freightPrograms);
        ImGui::MenuItem("Maintenance / Support Programs", nullptr, &visibility.maintenancePrograms);
        ImGui::MenuItem("Evidence / Analysis",nullptr,&visibility.science);
        ImGui::MenuItem("Shipyard / Production", nullptr, &visibility.shipyard);
        ImGui::MenuItem("Economy Forecast", nullptr, &visibility.economyForecast);
        ImGui::MenuItem("Event Log", nullptr, &visibility.eventLog);
        ImGui::EndMenu();
    }

    // Keep the controls at a stable position after the menus, independent of
    // workspace, day digit count, or optional rejection feedback.
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    if (ImGui::SmallButton("+1")) {
        advanceTime(service, 1);
    }
    ImGui::SetItemTooltip("Advance 1 simulation day");
    ImGui::SameLine();
    if (ImGui::SmallButton("+5")) {
        advanceTime(service, 5);
    }
    ImGui::SetItemTooltip("Advance 5 simulation days");
    ImGui::SameLine();
    if (ImGui::SmallButton("+30")) {
        advanceTime(service, 30);
    }
    ImGui::SetItemTooltip("Advance 30 simulation days");
    ImGui::SameLine();
    const SurveyTimeSummary time = SimulationQueries{service}.surveyTime();
    if (!time.daysUntilThirty) ImGui::BeginDisabled();
    if (ImGui::SmallButton("30-day report") && time.daysUntilThirty) {
        advanceToBoundary(service, 30);
    }
    if (!time.daysUntilThirty) ImGui::EndDisabled();
    if (time.nextThirtyDay) {
        ImGui::SetItemTooltip("Advance to the next global 30-day boundary (day %lld)",
            static_cast<long long>(*time.nextThirtyDay));
    } else {
        ImGui::SetItemTooltip("No representable future 30-day boundary");
    }
    ImGui::SameLine();
    if (!time.daysUntilNinety) ImGui::BeginDisabled();
    if (ImGui::SmallButton("90-day review") && time.daysUntilNinety) {
        advanceToBoundary(service, 90);
    }
    if (!time.daysUntilNinety) ImGui::EndDisabled();
    if (time.nextNinetyDay) {
        ImGui::SetItemTooltip("Advance to the next global 90-day boundary (day %lld)",
            static_cast<long long>(*time.nextNinetyDay));
    } else {
        ImGui::SetItemTooltip("No representable future 90-day boundary");
    }
    ImGui::SameLine();
    // Read after dispatch so a successful click updates the date in this frame.
    // This const service view also observes New Game, Load, and legacy controls.
    ImGui::Text("Day %lld", static_cast<long long>(SimulationQueries{service}.surveyTime().day));

    if (!timeMessage_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(lastTimeSucceeded_ ? ImVec4{0.75f, 0.85f, 0.7f, 1.0f}
                                               : ImVec4{1.0f, 0.45f, 0.35f, 1.0f},
                           "%s", timeMessage_.c_str());
        ImGui::SetItemTooltip("%s", timeMessage_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##time_message")) {
            timeMessage_.clear();
        }
    }

    const float menuBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
    return menuBottom;
}

void MainMenuBar::advanceTime(SimulationService& service, const int days) {
    const CommandResult result = service.execute(AdvanceDaysCommand{.days = days});
    lastTimeSucceeded_ = result.ok;
    timeMessage_ = result.message.empty() ? (result.ok ? "Time advanced" : "Time command rejected") : result.message;
}

void MainMenuBar::advanceToBoundary(SimulationService& service, const int interval) {
    const SurveyTimeSummary time = SimulationQueries{service}.surveyTime();
    const std::optional<int> days = interval == 30 ? time.daysUntilThirty : time.daysUntilNinety;
    if (!days) {
        lastTimeSucceeded_ = false;
        timeMessage_ = "Reporting boundary exceeds one command's day limit";
        return;
    }
    advanceTime(service, *days);
}

void MainMenuBar::resetWorldState() {
    timeMessage_.clear();
    lastTimeSucceeded_ = true;
}

} // namespace deep::ui_imgui
