#pragma once

// Coordinates the identity model with live application queries and actual
// New/Load outcomes. Owns the only main selection; borrows a stable service.
// No rendering, navigation execution, or changes to simulation/save semantics.

#include "app/InformationInteractionState.h"
#include "app/SimulationService.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace deep {

// Produced by an actual widget/pick activation. Capture world before reading the
// displayed DTOs, not when dispatching; null target is an explicit empty pick.
struct MainSelectionIntent {
    WorldGeneration world;
    std::optional<ObjectTarget> target;
};

class InformationInteractionAdapter {
public:
    // The service and callback's workflow owners must outlive this adapter's use.
    // The synchronous callback clears world-bound workflow caches, even hidden
    // ones. It must not reenter replacement. Failure propagates as an integration
    // error after replacement, never as a recoverable failed service operation.
    explicit InformationInteractionAdapter(SimulationService& service,
                                           std::function<void()> resetWorldWorkflows = {});

    [[nodiscard]] WorldGeneration world() const noexcept;
    [[nodiscard]] const InformationInteractionState& state() const noexcept;

    // Reconcile before returning each consumer's fresh read-only value projection.
    // Repeated reads never replay selectMain or overwrite a relationship preview.
    [[nodiscard]] SelectionState mainSelection();
    void reconcile();

    [[nodiscard]] bool select(MainSelectionIntent intent);
    [[nodiscard]] std::optional<PreviewId> inspect(ObjectReference target);
    [[nodiscard]] bool pin(PreviewId id);

    // Shared by File and legacy-panel routes through SaveLoadPanel. Execute once,
    // then acknowledge the returned result once. Preserve the original result;
    // worldReplacementFinished's bool is not operation success. Unexpected service
    // exceptions propagate without issuing a false success notification.
    [[nodiscard]] CommandResult newGame();
    [[nodiscard]] CommandResult loadGame(const std::filesystem::path& path);

private:
    [[nodiscard]] bool targetExists(const ObjectTarget& target) const;
    [[nodiscard]] CommandResult finishReplacement(WorldGeneration origin, CommandResult result);

    SimulationService& service_;
    InformationInteractionState state_;
    std::function<void()> resetWorldWorkflows_;
};

} // namespace deep
