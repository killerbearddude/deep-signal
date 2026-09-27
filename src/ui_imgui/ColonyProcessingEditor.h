#pragma once

// One modeless processing editor owns one world-stamped Colony target and its
// unapplied draft. Neither main selection nor a source preview owns the draft.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "ui_imgui/ShellLayout.h"

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep::ui_imgui {

struct ColonyProcessingEditorId {
    WorldGeneration world;
    std::uint64_t value = 0;
    constexpr auto operator<=>(const ColonyProcessingEditorId&) const = default;
};

// A policy-specific compare basis replaces a general Colony revision field.
// Time, stockpile, and production changes do not invalidate these settings.
struct ColonyProcessingConfigurationBasis {
    ProcessingPolicy appliedPolicy = ProcessingPolicy::Balanced;
    std::array<double, processedMaterialCount()> manualWeights{};
};

struct ColonyProcessingDraft {
    ProcessingPolicy policy = ProcessingPolicy::Balanced;
    std::array<double, processedMaterialCount()> manualWeights{};
};

struct ColonyProcessingEditorRecord {
    ColonyProcessingEditorId id;
    ObjectReference target;
    std::string targetNameAtOpen;
    ColonyProcessingConfigurationBasis basis;
    ColonyProcessingDraft draft;
    bool discardConfirmation = false;
    bool focusRequested = false;
    std::string statusMessage;
};

enum class EditorOpenOutcome { Opened, ExistingFocused, OtherEditorActive, SourceRejected, TargetUnavailable };
struct EditorOpenResult {
    EditorOpenOutcome outcome;
    std::string message;
    [[nodiscard]] bool accepted() const noexcept {
        return outcome == EditorOpenOutcome::Opened || outcome == EditorOpenOutcome::ExistingFocused;
    }
};

enum class EditorApplyOutcome {
    Applied, InvalidEditor, UnavailableTarget, StaleBasis, InvalidDraft, NoChange, CommandRejected
};
struct EditorApplyResult {
    EditorApplyOutcome outcome;
    std::string message;
    [[nodiscard]] bool applied() const noexcept { return outcome == EditorApplyOutcome::Applied; }
};

enum class EditorCloseOutcome { Closed, ConfirmationRequired, Rejected };

// Returned fresh for each render or validation. Query DTOs never live in the
// editor record; the draft preview is advice, not command authorization.
struct ColonyProcessingEditorAssessment {
    std::optional<ColonySummary> current;
    std::optional<ColonyProcessingDraftPreview> preview;
    bool dirty = false;
    bool stale = false;
    bool canApply = false;
    std::string reason;
};

// Positive rows in deterministic ProcessedMaterial order. The command consumes
// relative weights, never UI percentages; zero rows are omitted.
[[nodiscard]] std::vector<ProcessingAllocation> processingAllocationsForDraft(
    const ColonyProcessingDraft& draft);

class ColonyProcessingEditor {
public:
    // Validate the exact displayed source through the adapter before copying a
    // live Colony configuration. A second target never replaces an open editor.
    [[nodiscard]] EditorOpenResult open(ColonyProcessingOpenIntent intent,
                                        const SimulationQueries& queries,
                                        InformationInteractionAdapter& interactions);

    // Successful New/Load clears the old-world editor synchronously. A failed
    // Load never invokes this callback. The numeric ID sequence never resets.
    void resetWorldState() noexcept { record_.reset(); }
    [[nodiscard]] const std::optional<ColonyProcessingEditorRecord>& current() const noexcept { return record_; }

    [[nodiscard]] bool setDraftPolicy(ColonyProcessingEditorId id, ProcessingPolicy policy);
    [[nodiscard]] bool setManualWeight(ColonyProcessingEditorId id, ProcessedMaterial material, double weight);
    [[nodiscard]] bool normalizeManualWeights(ColonyProcessingEditorId id);
    [[nodiscard]] ColonyProcessingEditorAssessment assess(
        const SimulationQueries& queries, const InformationInteractionAdapter& interactions) const;
    [[nodiscard]] bool reviewLatest(ColonyProcessingEditorId id,
                                    const SimulationQueries& queries,
                                    const InformationInteractionAdapter& interactions);
    [[nodiscard]] EditorApplyResult apply(ColonyProcessingEditorId id,
                                          const SimulationQueries& queries,
                                          SimulationService& service,
                                          InformationInteractionAdapter& interactions);
    [[nodiscard]] EditorCloseOutcome requestCancel(ColonyProcessingEditorId id);
    [[nodiscard]] bool keepEditing(ColonyProcessingEditorId id);
    [[nodiscard]] bool discard(ColonyProcessingEditorId id);

    // ImGui owns only the live rectangle, keyed by EditorId. Presentation and
    // actions run on the UI thread; Apply routes through SimulationService.
    void render(const SimulationQueries& queries, SimulationService& service,
                InformationInteractionAdapter& interactions, ShellRegion workArea);

private:
    std::optional<ColonyProcessingEditorRecord> record_;
    std::uint64_t nextEditorId_ = 1;
};

} // namespace deep::ui_imgui
