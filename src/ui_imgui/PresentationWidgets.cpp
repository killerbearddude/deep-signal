#include "ui_imgui/PresentationWidgets.h"

// Layout and visual hierarchy only. Every visible label remains an ordinary
// ImGui text item, including badges, so submission logging retains its meaning.

#include "ui_imgui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace deep::ui_imgui {
namespace {

void text(std::string_view value) {
    if (value.empty()) {
        ImGui::TextUnformatted("");
    } else {
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }
}

ImVec2 textSize(std::string_view value) {
    return value.empty() ? ImVec2{} : ImGui::CalcTextSize(value.data(), value.data() + value.size());
}

std::string heading(std::string_view value) {
    std::string result(value);
    for (char& c : result)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return result;
}

void drawTitle(std::string_view label, std::string_view subtitle, UiTextRole role,
               float availableWidth = 0.0F) {
    const float wrap = availableWidth > 0.0F ? ImGui::GetCursorPosX() + availableWidth : 0.0F;
    ImGui::PushFont(nullptr, uiTextSize(role));
    ImGui::PushTextWrapPos(wrap);
    text(heading(label));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (!subtitle.empty()) {
        ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
        ImGui::PushTextWrapPos(wrap);
        text(subtitle);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

UiColor statusColor(UiStatus status) {
    switch (status) {
    case UiStatus::Waiting:
        return UiColor::Warning;
    case UiStatus::Completed:
        return UiColor::Positive;
    case UiStatus::Attention:
        return UiColor::Danger;
    case UiStatus::Suspended:
    case UiStatus::Unknown:
        return UiColor::TextMuted;
    case UiStatus::Normal:
    case UiStatus::Building:
        return UiColor::TextPrimary;
    }
    return UiColor::TextPrimary;
}

} // namespace

void screenTitle(std::string_view title, std::string_view subtitle, float availableWidth) {
    drawTitle(title, subtitle, UiTextRole::ScreenTitle, availableWidth);
}

void objectTitle(std::string_view title, std::string_view subtitle) {
    drawTitle(title, subtitle, UiTextRole::ObjectTitle);
}

void sectionTitle(std::string_view title) {
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Section));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    text(heading(title));
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void statusBadge(UiStatus status, std::string_view label) {
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Section));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 labelSize = textSize(label);
    const ImVec2 padding{8.0F, 4.0F};
    const ImVec2 size{labelSize.x + padding.x * 2.0F, labelSize.y + padding.y * 2.0F};
    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    const ImVec4 color = uiColor(statusColor(status));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, ImGui::GetColorU32(uiColor(UiColor::Surface)), 1.0F);
    draw->AddRect(origin, end, ImGui::GetColorU32(color), 1.0F);

    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos({origin.x + padding.x, origin.y + padding.y});
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    text(label);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGui::EndGroup();
    ImGui::PopFont();
}

void keyValue(std::string_view label, std::string_view value) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float valueWidth = textSize(value).x;
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
    const float labelWidth = textSize(label).x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0F;
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    text(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (labelWidth + gap + valueWidth <= available) {
        // Relative spacing works in both child windows and table columns.
        // Absolute SameLine offsets would apply the table's column offset twice.
        ImGui::SameLine(0.0F, available - labelWidth - valueWidth);
        text(value);
    } else {
        ImGui::PushTextWrapPos(0.0F);
        text(value);
        ImGui::PopTextWrapPos();
    }
}

void metric(std::string_view label, std::string_view value, UiStatus emphasis) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 82.0F};
    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    ImGui::GetWindowDrawList()->AddRectFilled(origin, end, ImGui::GetColorU32(uiColor(UiColor::Surface)));
    ImGui::GetWindowDrawList()->AddRect(origin, end, ImGui::GetColorU32(uiColor(UiColor::Divider)));
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos({origin.x + 14.0F, origin.y + 10.0F});
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    text(heading(label));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos({origin.x + 14.0F, origin.y + 35.0F});
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Metric));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(statusColor(emphasis)));
    text(value);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void progressMeter(std::string_view label, double completed, double total) {
    if (!label.empty()) {
        text(label);
    }
    const double fraction = std::isfinite(completed) && std::isfinite(total) && total > 0.0
                                ? std::clamp(completed / total, 0.0, 1.0)
                                : 0.0;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, uiColor(UiColor::ControlBorder));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, uiColor(UiColor::Background));
    ImGui::PushStyleColor(ImGuiCol_Border, uiColor(UiColor::ControlBorder));
    ImGui::ProgressBar(static_cast<float>(fraction), {-1.0F, 24.0F}, "");
    ImGui::PopStyleColor(3);
}

} // namespace deep::ui_imgui
