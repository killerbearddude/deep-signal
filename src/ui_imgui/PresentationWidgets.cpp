#include "ui_imgui/PresentationWidgets.h"

// Layout and visual hierarchy only. Every visible label remains an ordinary
// ImGui text item, including badges, so submission logging retains its meaning.

#include "ui_imgui/UiTheme.h"

#include <algorithm>
#include <cmath>

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

void screenTitle(std::string_view title, std::string_view subtitle) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    ImGui::PushTextWrapPos(0.0F);
    text(title);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (!subtitle.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
        ImGui::PushTextWrapPos(0.0F);
        text(subtitle);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
}

void sectionTitle(std::string_view title) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.125F);
    text(title);
    ImGui::PopFont();
    ImGui::Separator();
}

void statusBadge(UiStatus status, std::string_view label) {
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
}

void keyValue(std::string_view label, std::string_view value) {
    const float start = ImGui::GetCursorPosX();
    const float available = ImGui::GetContentRegionAvail().x;
    const float labelWidth = textSize(label).x;
    const float valueWidth = textSize(value).x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0F;
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    text(label);
    ImGui::PopStyleColor();
    if (labelWidth + gap + valueWidth <= available) {
        ImGui::SameLine(start + available - valueWidth);
        text(value);
    } else {
        ImGui::PushTextWrapPos(0.0F);
        text(value);
        ImGui::PopTextWrapPos();
    }
}

void metric(std::string_view label, std::string_view value) {
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    text(label);
    ImGui::PopStyleColor();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.375F);
    text(value);
    ImGui::PopFont();
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
    ImGui::ProgressBar(static_cast<float>(fraction), {-1.0F, 10.0F}, "");
    ImGui::PopStyleColor();
}

} // namespace deep::ui_imgui
