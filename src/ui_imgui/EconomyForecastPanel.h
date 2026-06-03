#pragma once

// Declares the Economy Forecast panel for the ImGui shell.
// The panel displays ForecastService cause-chain DTOs without adding new economy
// math or reading raw GameState collections.

#include "app/ForecastService.h"
#include "sim/Minerals.h"

#include <optional>

namespace deep::ui_imgui {

// Shows empire-level raw-resource and processed-material forecasts plus cause
// rows for the selected entry. This is read-only; gameplay actions remain in
// service command panels.
class EconomyForecastPanel {
public:
    // Draws forecast summary rows and cause drivers for selected resources.
    void render(const ForecastService& forecasts, bool& visible);

private:
    std::optional<Mineral> selectedMineral_;
    std::optional<ProcessedMaterial> selectedMaterial_;
};

} // namespace deep::ui_imgui
