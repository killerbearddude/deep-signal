#include "app/InformationInteractionState.h"

// Explicit UI intentions are the only selection/retargeting triggers. Queries,
// hovering, and window focus have no transition here. Main context and gameplay
// commands belong to future adapters, not this identity-only state machine.

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace deep {

WorldGeneration InformationInteractionState::world() const noexcept {
    return world_;
}

SelectionState InformationInteractionState::mainSelection() const noexcept {
    return mainSelection_;
}

std::optional<ObjectReference> InformationInteractionState::mainTarget() const noexcept {
    switch (mainSelection_.type()) {
    case SelectedObjectType::Body:
        return ObjectReference{world_, mainSelection_.bodyId()};
    case SelectedObjectType::Colony:
        return ObjectReference{world_, mainSelection_.colonyId()};
    case SelectedObjectType::Fleet:
        return ObjectReference{world_, mainSelection_.fleetId()};
    case SelectedObjectType::None:
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<InformationPreview> InformationInteractionState::previewSnapshot() const {
    return previews_;
}

std::optional<InformationPreview> InformationInteractionState::temporaryPreview() const {
    const auto it = std::find_if(previews_.begin(), previews_.end(), [](const auto& preview) {
        return !preview.pinned;
    });
    return it == previews_.end() ? std::nullopt : std::optional{*it};
}

bool InformationInteractionState::valid(const ObjectReference target, const TargetExists& exists) const {
    // Check identity before consulting the current world's queries: a reused
    // numeric ID must not make an old reference valid in a new world.
    return target.world == world_ && exists
        && std::visit([](const auto id) { return static_cast<bool>(id); }, target.object)
        && exists(target.object);
}

bool InformationInteractionState::selectMain(const ObjectReference target, const TargetExists& exists) {
    if (!valid(target, exists)) {
        return false;
    }
    std::visit([this](const auto id) {
        using IdType = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<IdType, BodyId>) {
            mainSelection_.selectBody(id);
        } else if constexpr (std::is_same_v<IdType, ColonyId>) {
            mainSelection_.selectColony(id);
        } else {
            mainSelection_.selectFleet(id);
        }
    }, target.object);
    for (auto& preview : previews_) {
        if (!preview.pinned) {
            preview.target = target;
        }
    }
    return true;
}

bool InformationInteractionState::clearMain(const WorldGeneration originatingWorld) {
    if (originatingWorld != world_) {
        return false;
    }
    mainSelection_.clear();
    std::erase_if(previews_, [](const auto& preview) { return !preview.pinned; });
    return true;
}

std::optional<PreviewId> InformationInteractionState::inspect(
    const ObjectReference target, const TargetExists& exists) {
    if (!valid(target, exists)) {
        return std::nullopt;
    }
    for (auto& preview : previews_) {
        if (!preview.pinned) {
            preview.target = target;
            return preview.id;
        }
    }

    // Exhaustion fails before mutation rather than wrapping to a stale ID.
    if (nextPreviewId_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{"Preview ID space exhausted"};
    }
    const PreviewId id{world_, nextPreviewId_};
    previews_.push_back(InformationPreview{id, target, false});
    ++nextPreviewId_;
    return id;
}

bool InformationInteractionState::pin(const PreviewId id) {
    if (id.world != world_) {
        return false;
    }
    for (auto& preview : previews_) {
        if (preview.id == id) {
            preview.pinned = true;
            return true;
        }
    }
    return false;
}

bool InformationInteractionState::unpin(const PreviewId id) {
    if (id.world != world_
        || std::none_of(previews_.begin(), previews_.end(), [id](const auto& preview) { return preview.id == id; })) {
        return false;
    }
    std::erase_if(previews_, [id](const auto& preview) { return !preview.pinned && preview.id != id; });
    // Erasing the former temporary record may move this record. Look it up again
    // by ID instead of retaining an iterator/reference across the erase.
    for (auto& preview : previews_) {
        if (preview.id == id) {
            preview.pinned = false;
        }
    }
    return true;
}

bool InformationInteractionState::close(const PreviewId id) {
    if (id.world != world_) {
        return false;
    }
    return std::erase_if(previews_, [id](const auto& preview) { return preview.id == id; }) != 0;
}

bool InformationInteractionState::reconcile(
    const WorldGeneration originatingWorld, const TargetExists& exists) {
    if (originatingWorld != world_ || !exists) {
        return false;
    }
    const auto selected = mainTarget();
    const bool clearSelection = selected.has_value() && !valid(*selected, exists);

    // Stage removals so a throwing lookup cannot leave a partially compacted
    // vector with duplicate window IDs or clear only part of the interaction.
    auto retained = previews_;
    std::erase_if(retained, [&](const auto& preview) { return !valid(preview.target, exists); });
    previews_.swap(retained);
    if (clearSelection) {
        // Missing main selection must not close an unrelated relationship preview.
        mainSelection_.clear();
    }
    return true;
}

std::optional<GoToRequest> InformationInteractionState::requestGoTo(
    const ObjectReference target, const TargetExists& exists) const {
    return valid(target, exists) ? std::optional{GoToRequest{target}} : std::nullopt;
}

bool InformationInteractionState::worldReplacementFinished(
    const WorldGeneration originatingWorld, const bool succeeded) {
    if (originatingWorld != world_) {
        return false;
    }
    if (succeeded) {
        if (world_.value == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error{"World generation space exhausted"};
        }
        ++world_.value;
        mainSelection_.clear();
        previews_.clear();
    }
    return true;
}

} // namespace deep
