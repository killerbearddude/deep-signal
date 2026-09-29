#include "ui_imgui/TimeControlPanel.h"
#include "ui_imgui/OperationalWindow.h"

// Implements time controls for the first functional ImGui shell.
// Mutations are deliberately routed through SimulationService::execute so the UI
// cannot bypass command validation or future audit behavior.

#include "sim/Commands.h"
#include "sim/Error.h"
#include "app/SimulationQueries.h"

#include <imgui.h>

namespace deep::ui_imgui {

void TimeControlPanel::render(SimulationService& service, bool& visible) {
    if (!visible) {
        return;
    }

    if (!beginOperationalWindow("Time Control", &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Advance simulation time");
    const SurveyTimeSummary time = SimulationQueries{service}.surveyTime();
    ImGui::Separator();

    if (ImGui::Button("Advance 1 day")) {
        advance(service, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Advance 5 days")) {
        advance(service, 5);
    }
    ImGui::SameLine();
    if (ImGui::Button("Advance 30 days")) {
        advance(service, 30);
    }

    if (!time.daysUntilThirty) ImGui::BeginDisabled();
    if (ImGui::Button("30-day report") && time.daysUntilThirty) {
        advance(service, *time.daysUntilThirty);
    }
    if (!time.daysUntilThirty) ImGui::EndDisabled();
    if (time.nextThirtyDay) {
        ImGui::SetItemTooltip("Advance to the next global 30-day boundary: day %lld",
            static_cast<long long>(*time.nextThirtyDay));
    } else {
        ImGui::SetItemTooltip("No representable future 30-day boundary");
    }
    ImGui::SameLine();
    if (!time.daysUntilNinety) ImGui::BeginDisabled();
    if (ImGui::Button("90-day review") && time.daysUntilNinety) {
        // Re-read after a 30-day button click in the same frame.
        const SurveyTimeSummary current = SimulationQueries{service}.surveyTime();
        if (current.daysUntilNinety) advance(service, *current.daysUntilNinety);
    }
    if (!time.daysUntilNinety) ImGui::EndDisabled();
    if (time.nextNinetyDay) {
        ImGui::SetItemTooltip("Advance to the next global 90-day boundary: day %lld",
            static_cast<long long>(*time.nextNinetyDay));
    } else {
        ImGui::SetItemTooltip("No representable future 90-day boundary");
    }

    ImGui::Separator();
    ImGui::Text("Current day: %lld", static_cast<long long>(SimulationQueries{service}.surveyTime().day));
    ImGui::Text("Last command: %s", lastCommandMessage_.c_str());
    ImGui::Text("Status: %s", lastCommandSucceeded_ ? "OK" : "Rejected");

    ImGui::End();
}

void TimeControlPanel::advance(SimulationService& service, const int days) {
    const CommandResult result = service.execute(AdvanceDaysCommand{.days = days});
    lastCommandSucceeded_ = result.ok;
    lastCommandMessage_ = result.message.empty() ? "Simulation advanced" : result.message;
}

void TimeControlPanel::resetWorldState() {
    lastCommandMessage_ = "Ready";
    lastCommandSucceeded_ = true;
}

} // namespace deep::ui_imgui
