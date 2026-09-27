#pragma once

// Coordinates explicit preview Go To with existing main selection, curated
// workspaces, and one-shot presentation reveals. It owns no navigation history,
// simulation data, or independent interaction state.

#include "ui_imgui/InformationPreviewLayer.h"
#include "ui_imgui/MainMenuBar.h"

#include <string>

namespace deep {
class InformationInteractionAdapter;
class SimulationQueries;
}

namespace deep::ui_imgui {

class StrategicMapPanel;
class ColonyPanel;
class FleetPanel;

enum class NavigationOutcome {
    Succeeded,
    ValidationRejected,
    DestinationUnavailable,
    SelectionRejected,
    PreviewClosureFailed
};

struct NavigationResult {
    NavigationOutcome outcome;
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return outcome == NavigationOutcome::Succeeded; }
};

// One serialized UI-thread operation. Source and displayed target validation,
// plus destination read preflight, precede every main-context mutation.
class InformationNavigation {
public:
    [[nodiscard]] NavigationResult goTo(
        PreviewGoToIntent intent,
        const SimulationQueries& queries,
        InformationInteractionAdapter& interactions,
        Workspace& workspace,
        PanelVisibility& visibility,
        StrategicMapPanel& strategicMap,
        ColonyPanel& colonies,
        FleetPanel& fleets) const;
};

} // namespace deep::ui_imgui
