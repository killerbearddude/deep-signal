#include "ui_imgui/EventLogPanel.h"

// Implements the first player-facing event log table for the ImGui shell.
// Input rows are already flattened by SimulationQueries, keeping event visitor
// logic centralized in the app layer instead of duplicating it in UI code.

#include <imgui.h>

#include <vector>

namespace deep::ui_imgui {

void EventLogPanel::render(const SimulationQueries& queries, bool& visible) const {
    if (!visible) {
        return;
    }

    if (!ImGui::Begin("Event Log", &visible)) {
        ImGui::End();
        return;
    }

    const std::vector<EventLogEntrySummary> events = queries.recentEvents(kRecentEventLimit);
    ImGui::Text("Recent audit events: %zu", events.size());

    if (ImGui::BeginTable("EventLogTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Day");
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Severity");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Message");
        ImGui::TableHeadersRow();

        for (const EventLogEntrySummary& event : events) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(event.day));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%lld", static_cast<long long>(event.id.value));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(event.severityName.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(event.eventType.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(event.message.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
