#include "ui_imgui/InformationNavigation.h"

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "ui_imgui/ColonyPanel.h"
#include "ui_imgui/FleetPanel.h"
#include "ui_imgui/StrategicMapPanel.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace deep::ui_imgui {
namespace {

[[nodiscard]] Workspace destinationWorkspace(const ObjectTarget& target) {
    return std::visit([](const auto id) {
        using Id = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<Id, BodyId>) {
            return Workspace::System;
        } else if constexpr (std::is_same_v<Id, ColonyId>) {
            return Workspace::Production;
        } else {
            return Workspace::Fleets;
        }
    }, target);
}

// A second current-data read prevents ordinary reveal failure from partially
// changing selection or workspace. Query exceptions remain application errors;
// they are not converted into an absent target or a successful navigation.
[[nodiscard]] bool destinationCanBeRevealed(const SimulationQueries& queries,
                                             const ObjectTarget& target) {
    return std::visit([&](const auto id) {
        using Id = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<Id, BodyId>) {
            const auto body = queries.strategicBody(id);
            return body && std::isfinite(body->x) && std::isfinite(body->y);
        } else if constexpr (std::is_same_v<Id, ColonyId>) {
            const auto rows = queries.colonies();
            return std::any_of(rows.begin(), rows.end(), [id](const ColonySummary& row) {
                return row.id == id;
            });
        } else {
            if (!queries.fleet(id)) return false;
            const auto markers = queries.strategicFleets();
            return std::any_of(markers.begin(), markers.end(), [id](const StrategicFleetSummary& row) {
                return row.id == id && std::isfinite(row.x) && std::isfinite(row.y);
            });
        }
    }, target);
}

} // namespace

NavigationResult InformationNavigation::goTo(
    const PreviewGoToIntent intent,
    const SimulationQueries& queries,
    InformationInteractionAdapter& interactions,
    Workspace& workspace,
    PanelVisibility& visibility,
    StrategicMapPanel& strategicMap,
    ColonyPanel& colonies,
    FleetPanel& fleets) const {
    const auto request = interactions.requestGoTo(intent.sourcePreviewId, intent.displayedTarget);
    if (!request) {
        return {NavigationOutcome::ValidationRejected,
                "Go To unavailable: preview changed or target no longer exists."};
    }

    const Workspace destination = destinationWorkspace(request->target.object);
    if (!destinationCanBeRevealed(queries, request->target.object)) {
        return {NavigationOutcome::DestinationUnavailable,
                "Go To unavailable: destination cannot be revealed."};
    }

    if (!interactions.select({request->target.world, request->target.object})) {
        return {NavigationOutcome::SelectionRejected,
                "Go To stopped: target selection was rejected."};
    }

    workspace = destination;
    applyWorkspace(workspace, visibility);
    switch (destination) {
    case Workspace::System:
        strategicMap.requestReveal(request->target);
        break;
    case Workspace::Production:
        colonies.requestReveal(request->target);
        break;
    case Workspace::Fleets:
        fleets.requestReveal(request->target);
        strategicMap.requestReveal(request->target);
        break;
    case Workspace::Economy:
    case Workspace::Intelligence:
    case Workspace::History:
        // destinationWorkspace maps only Body, Colony, and Fleet.
        break;
    }

    if (!request->sourceWasPinned && !interactions.closePreview(request->sourcePreviewId)) {
        return {NavigationOutcome::PreviewClosureFailed,
                "Go To selected the target, but could not close its temporary preview."};
    }
    return {NavigationOutcome::Succeeded, {}};
}

} // namespace deep::ui_imgui
