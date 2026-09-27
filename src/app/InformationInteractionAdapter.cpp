#include "app/InformationInteractionAdapter.h"

// Serialized application boundary: generation checks precede numeric ID lookup;
// current DTOs are local values, never stored by the adapter. Lifecycle invalidation
// completes synchronously before another panel can consume a replacement world.

#include "app/SimulationQueries.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace deep {

InformationInteractionAdapter::InformationInteractionAdapter(
    SimulationService& service, std::function<void()> resetWorldWorkflows)
    : service_{service}, resetWorldWorkflows_{std::move(resetWorldWorkflows)} {}

WorldGeneration InformationInteractionAdapter::world() const noexcept { return state_.world(); }

const InformationInteractionState& InformationInteractionAdapter::state() const noexcept { return state_; }

bool InformationInteractionAdapter::targetExists(const ObjectTarget& target) const {
    const SimulationQueries queries{service_};
    return std::visit([&](const auto id) {
        using IdType = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<IdType, BodyId>) {
            return queries.strategicBody(id).has_value();
        } else if constexpr (std::is_same_v<IdType, FleetId>) {
            return queries.fleet(id).has_value();
        } else {
            const auto colonies = queries.colonies();
            return std::any_of(colonies.begin(), colonies.end(), [id](const auto& colony) { return colony.id == id; });
        }
    }, target);
}

void InformationInteractionAdapter::reconcile() {
    // Do not translate query exceptions into "missing": the model stages removals
    // and leaves itself unchanged if a current-data lookup throws.
    if (!state_.reconcile(world(), [this](const auto& target) { return targetExists(target); })) {
        throw std::logic_error{"Interaction reconciliation rejected the active world"};
    }
}

SelectionState InformationInteractionAdapter::mainSelection() {
    reconcile();
    return state_.mainSelection();
}

std::vector<InformationPreview> InformationInteractionAdapter::previewSnapshot() {
    reconcile();
    return state_.previewSnapshot();
}

bool InformationInteractionAdapter::select(const MainSelectionIntent intent) {
    if (!intent.target) {
        return state_.clearMain(intent.world);
    }
    return state_.selectMain(ObjectReference{intent.world, *intent.target},
                             [this](const auto& target) { return targetExists(target); });
}

std::optional<PreviewId> InformationInteractionAdapter::inspect(const ObjectReference target) {
    return state_.inspect(target, [this](const auto& object) { return targetExists(object); });
}

bool InformationInteractionAdapter::pin(const PreviewId id) { return state_.pin(id); }

bool InformationInteractionAdapter::unpin(const PreviewId id) { return state_.unpin(id); }

bool InformationInteractionAdapter::closePreview(const PreviewId id) { return state_.close(id); }

std::optional<PreviewGoToRequest> InformationInteractionAdapter::requestGoTo(
    const PreviewId sourcePreviewId, const ObjectReference displayedTarget) {
    reconcile();
    const auto previews = state_.previewSnapshot();
    const auto source = std::find_if(previews.begin(), previews.end(), [sourcePreviewId](const auto& preview) {
        return preview.id == sourcePreviewId;
    });
    if (source == previews.end() || source->target != displayedTarget
        || !state_.requestGoTo(displayedTarget, [this](const auto& target) { return targetExists(target); })) {
        return std::nullopt;
    }
    return PreviewGoToRequest{sourcePreviewId, displayedTarget, source->pinned};
}

std::optional<ObjectReference> InformationInteractionAdapter::validateColonyProcessingOpen(
    const ColonyProcessingOpenIntent intent) {
    reconcile();
    if (intent.displayedTarget.world != world()
        || !std::holds_alternative<ColonyId>(intent.displayedTarget.object)) {
        return std::nullopt;
    }

    if (intent.sourcePreviewId) {
        if (intent.sourcePreviewId->world != world()) return std::nullopt;
        const auto previews = state_.previewSnapshot();
        const auto source = std::find_if(previews.begin(), previews.end(), [&](const InformationPreview& preview) {
            return preview.id == *intent.sourcePreviewId;
        });
        if (source == previews.end() || source->target != intent.displayedTarget) return std::nullopt;
    } else if (state_.mainTarget() != intent.displayedTarget) {
        return std::nullopt;
    }

    return targetExists(intent.displayedTarget.object)
        ? std::optional<ObjectReference>{intent.displayedTarget} : std::nullopt;
}

CommandResult InformationInteractionAdapter::finishReplacement(const WorldGeneration origin, CommandResult result) {
    if (!state_.worldReplacementFinished(origin, result.ok)) {
        // The service may already have replaced the world. Continuing with stale
        // targets or relabeling this as an ordinary failed Load would be unsafe.
        throw std::logic_error{"World replacement lifecycle notification was rejected"};
    }
    if (result.ok) {
        if (resetWorldWorkflows_) {
            resetWorldWorkflows_();
        }
        reconcile();
    }
    return result;
}

CommandResult InformationInteractionAdapter::newGame() {
    const auto origin = world();
    return finishReplacement(origin, service_.newGame());
}

CommandResult InformationInteractionAdapter::loadGame(const std::filesystem::path& path) {
    const auto origin = world();
    // Preserve the existing UI empty-path rejection and status. It is a failed
    // attempt, so interaction state and workflow drafts stay intact.
    const auto result = path.empty() ? CommandResult::failure("Load path is empty") : service_.loadGame(path);
    return finishReplacement(origin, result);
}

} // namespace deep
