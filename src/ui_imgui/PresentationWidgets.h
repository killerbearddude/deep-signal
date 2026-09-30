#pragma once

// Small stateless ImGui presentation helpers. All labels, quantities, and status
// roles come from the caller; these helpers do not inspect simulation or queries.

#include <string_view>

namespace deep::ui_imgui {

enum class UiStatus { Normal, Building, Waiting, Completed, Suspended, Attention, Unknown };

void screenTitle(std::string_view title, std::string_view subtitle = {});
void sectionTitle(std::string_view title);
void statusBadge(UiStatus status, std::string_view label);
void keyValue(std::string_view label, std::string_view value);
void metric(std::string_view label, std::string_view value);

// Quantities must share units. The meter clamps its display to [0, 1] and shows
// no fill when a finite positive denominator is unavailable.
void progressMeter(std::string_view label, double completed, double total);

} // namespace deep::ui_imgui
