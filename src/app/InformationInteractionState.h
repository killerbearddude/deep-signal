#pragma once

// Responsibility: own main selection and object-preview lifecycle independently
// of rendering. Stores identities only, never query DTOs or simulation records.
// InformationInteractionAdapter connects the shell's selection and New/Load
// paths. Preview rendering, navigation execution, and editors remain separate.

#include "app/SelectionState.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace deep {

// Local to one interaction-state owner's lifetime, not a persisted game ID.
// Successful world replacement changes this even when object IDs are reused.
struct WorldGeneration {
    std::uint64_t value = 0;
    constexpr auto operator<=>(const WorldGeneration&) const = default;
};

using ObjectTarget = std::variant<BodyId, ColonyId, FleetId>;

struct ObjectReference {
    WorldGeneration world;
    ObjectTarget object;
    bool operator==(const ObjectReference&) const = default;
};

// Window identity is independent of target identity. A renderer must key its
// window by this ID so retargeting preserves geometry. IDs are never recycled.
struct PreviewId {
    WorldGeneration world;
    std::uint64_t value = 0;
    constexpr auto operator<=>(const PreviewId&) const = default;
};

struct InformationPreview {
    PreviewId id;
    ObjectReference target;
    bool pinned = false;
    bool operator==(const InformationPreview&) const = default;
};

// An inert navigation intention, not navigation execution. Its eventual consumer
// must revalidate the world/target before choosing an approved visible destination.
struct GoToRequest {
    ObjectReference target;
    bool operator==(const GoToRequest&) const = default;
};

// One UI owner, serialized on its thread. There is at most one unpinned preview;
// pins retain targets, not historical values. Duplicate pins require explicit
// inspection/pinning. No main-context, editor, persistence, or command authority.
//
// Render copied snapshots and collect intentions, then apply them in order after
// iteration. Every intention carries its originating world or preview ID; stale
// intentions are rejected rather than affecting a replacement world/window.
class InformationInteractionState {
public:
    InformationInteractionState() = default;

    // Copying/resetting the owner could recycle generations and window IDs while
    // old requests still exist. Use worldReplacementFinished for a new world.
    InformationInteractionState(const InformationInteractionState&) = delete;
    InformationInteractionState& operator=(const InformationInteractionState&) = delete;
    InformationInteractionState(InformationInteractionState&&) = delete;
    InformationInteractionState& operator=(InformationInteractionState&&) = delete;

    // A synchronous read-only lookup, supplied per call and never retained.
    // It must not reenter/mutate this model. Missing callbacks reject lookup-based
    // operations. Lookup exceptions propagate without changing interaction state.
    using TargetExists = std::function<bool(const ObjectTarget&)>;

    [[nodiscard]] WorldGeneration world() const noexcept;
    [[nodiscard]] SelectionState mainSelection() const noexcept;
    [[nodiscard]] std::optional<ObjectReference> mainTarget() const noexcept;
    [[nodiscard]] std::vector<InformationPreview> previewSnapshot() const;
    [[nodiscard]] std::optional<InformationPreview> temporaryPreview() const;

    // Call only for a fresh selection intention, never to poll unchanged state.
    // Retargets an existing temporary record, but creates none. Existing widget
    // activations, including reselecting a row, are routed explicitly by the shell.
    [[nodiscard]] bool selectMain(ObjectReference target, const TargetExists& exists);

    // Empty main selection closes the temporary preview, preserving all pins.
    [[nodiscard]] bool clearMain(WorldGeneration originatingWorld);

    // Explicit inspection opens/reuses the temporary record without touching
    // main selection or pins, including relationships followed from a pinned view.
    // Invalid requests leave existing records unchanged; nullopt means rejected.
    // Counter exhaustion throws before mutation instead of recycling identity.
    [[nodiscard]] std::optional<PreviewId> inspect(ObjectReference target, const TargetExists& exists);

    // Pin/unpin preserve this window's ID. Pin creates no replacement. Unpin
    // removes any other temporary record; other pins stay intact. Known records
    // already in the requested mode succeed unchanged. Unknown/stale IDs fail.
    [[nodiscard]] bool pin(PreviewId id);
    [[nodiscard]] bool unpin(PreviewId id);
    [[nodiscard]] bool close(PreviewId id);

    // Clear missing main targets and close previews of missing objects, retaining
    // unrelated records. Does not synchronize preview targets from main selection.
    // The UI adapter must reconcile before presenting records or offering actions.
    [[nodiscard]] bool reconcile(WorldGeneration originatingWorld, const TargetExists& exists);

    // Validates a target and returns data only: no context/selection/preview change.
    [[nodiscard]] std::optional<GoToRequest> requestGoTo(
        ObjectReference target, const TargetExists& exists) const;

    // The New/Load adapter reports both entry paths' actual outcomes.
    // Success advances generation and clears selection/previews; failure changes
    // nothing. Return value says whether this notification belongs to this world,
    // not whether loading succeeded. Old queued intentions then fail validation.
    // Generation exhaustion throws before mutation rather than wrapping.
    [[nodiscard]] bool worldReplacementFinished(WorldGeneration originatingWorld, bool succeeded);

private:
    [[nodiscard]] bool valid(ObjectReference target, const TargetExists& exists) const;

    WorldGeneration world_{1};
    std::uint64_t nextPreviewId_ = 1;
    SelectionState mainSelection_;
    std::vector<InformationPreview> previews_;
};

} // namespace deep
