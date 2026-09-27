#include "ui_imgui/ColonyProcessingEditor.h"

// Fixed-target workflow and native presentation. Live query copies describe the
// original Colony on every frame; only Apply sends the existing simulation command.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace deep::ui_imgui {
namespace {

using Weights = std::array<double, processedMaterialCount()>;
constexpr ImVec4 kSurface{0.075F, 0.125F, 0.15F, 1.0F};
constexpr ImVec4 kText{0.87F, 0.93F, 0.95F, 1.0F};
constexpr ImVec4 kMuted{0.60F, 0.71F, 0.76F, 1.0F};
constexpr ImVec4 kAccent{0.49F, 0.82F, 0.79F, 1.0F};
constexpr ImVec4 kAttention{0.92F, 0.70F, 0.37F, 1.0F};
constexpr ImVec4 kDivider{0.20F, 0.34F, 0.38F, 1.0F};

[[nodiscard]] bool validPolicy(const ProcessingPolicy policy) noexcept {
    switch (policy) {
    case ProcessingPolicy::Balanced:
    case ProcessingPolicy::ShipbuildingFocus:
    case ProcessingPolicy::FuelFocus:
    case ProcessingPolicy::ElectronicsFocus:
    case ProcessingPolicy::StockpileRecovery:
    case ProcessingPolicy::Manual:
        return true;
    }
    return false;
}

[[nodiscard]] const char* policyLabel(const ProcessingPolicy policy) noexcept {
    switch (policy) {
    case ProcessingPolicy::Balanced: return "Balanced";
    case ProcessingPolicy::ShipbuildingFocus: return "Shipbuilding Focus";
    case ProcessingPolicy::FuelFocus: return "Fuel Focus";
    case ProcessingPolicy::ElectronicsFocus: return "Electronics Focus";
    case ProcessingPolicy::StockpileRecovery: return "Stockpile Recovery";
    case ProcessingPolicy::Manual: return "Manual";
    }
    return "Unknown";
}

constexpr ProcessingPolicy kPolicies[]{
    ProcessingPolicy::Balanced, ProcessingPolicy::ShipbuildingFocus,
    ProcessingPolicy::FuelFocus, ProcessingPolicy::ElectronicsFocus,
    ProcessingPolicy::StockpileRecovery, ProcessingPolicy::Manual
};

[[nodiscard]] Weights canonicalWeights(const ColonySummary& colony) {
    Weights weights{};
    for (const auto& row : colony.manualProcessingAllocations) {
        const auto index = processedMaterialIndex(row.material);
        if (index >= weights.size() || !std::isfinite(row.weight) || row.weight < 0.0) {
            throw std::runtime_error{"Current colony manual allocation is invalid"};
        }
        weights[index] += row.weight;
        if (!std::isfinite(weights[index])) {
            throw std::runtime_error{"Current colony manual allocation total is invalid"};
        }
    }
    return weights;
}

[[nodiscard]] ColonyProcessingConfigurationBasis basisFrom(const ColonySummary& colony) {
    return {colony.processingPolicy, canonicalWeights(colony)};
}

// Empty stored Manual setup is a valid preset-era configuration. Give a newly
// opened draft useful equal weights without presenting that default as an edit.
[[nodiscard]] Weights usableDraftWeights(const Weights& stored) {
    double total = 0.0;
    for (const double weight : stored) total += weight;
    if (total > kProcessedMaterialComparisonEpsilon) return stored;
    Weights draft{};
    draft.fill(1.0);
    return draft;
}

[[nodiscard]] bool sameWeights(const Weights& a, const Weights& b) noexcept {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]) ||
            std::abs(a[i] - b[i]) > kProcessedMaterialComparisonEpsilon) return false;
    }
    return true;
}

[[nodiscard]] bool dirty(const ColonyProcessingEditorRecord& record) noexcept {
    return record.draft.policy != record.basis.appliedPolicy ||
        !sameWeights(record.draft.manualWeights, usableDraftWeights(record.basis.manualWeights));
}

[[nodiscard]] std::optional<ColonySummary> liveColony(const SimulationQueries& queries,
                                                       const ObjectReference target) {
    const auto* colonyId = std::get_if<ColonyId>(&target.object);
    if (colonyId == nullptr) return std::nullopt;
    const auto rows = queries.colonies();
    const auto found = std::find_if(rows.begin(), rows.end(), [colonyId](const ColonySummary& row) {
        return row.id == *colonyId;
    });
    return found == rows.end() ? std::nullopt : std::optional<ColonySummary>{*found};
}

[[nodiscard]] std::string windowName(const ColonyProcessingEditorId id) {
    return "Configure processing###ColonyProcessingEditor_W" + std::to_string(id.world.value) +
           "_E" + std::to_string(id.value);
}

struct ConstraintContext { const char* name; ShellRegion work; };
void constrainEditor(ImGuiSizeCallbackData* data) {
    const auto& context = *static_cast<const ConstraintContext*>(data->UserData);
    const ShellRegion current = confineFloatingWindow(
        {data->Pos.x, data->Pos.y, data->CurrentSize.x, data->CurrentSize.y}, context.work);
    ImGui::SetWindowPos(context.name, {current.x, current.y});
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                          !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool fromLeft = dragging && mouse.x < current.x + current.width * 0.5F;
    const bool fromTop = dragging && mouse.y < current.y + current.height * 0.5F;
    const float maxWidth = fromLeft ? current.x + current.width - context.work.x
                                   : context.work.x + context.work.width - current.x;
    const float maxHeight = fromTop ? current.y + current.height - context.work.y
                                    : context.work.y + context.work.height - current.y;
    data->DesiredSize.x = std::clamp(data->DesiredSize.x, 1.0F, std::max(1.0F, maxWidth));
    data->DesiredSize.y = std::clamp(data->DesiredSize.y, 1.0F, std::max(1.0F, maxHeight));
}

void heading(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void mutedText(const std::string_view value) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%.*s", static_cast<int>(value.size()), value.data());
    ImGui::PopStyleColor();
}

} // namespace

std::vector<ProcessingAllocation> processingAllocationsForDraft(const ColonyProcessingDraft& draft) {
    std::vector<ProcessingAllocation> rows;
    rows.reserve(draft.manualWeights.size());
    for (std::size_t i = 0; i < draft.manualWeights.size(); ++i) {
        if (draft.manualWeights[i] > kProcessedMaterialComparisonEpsilon) {
            rows.push_back({static_cast<ProcessedMaterial>(i), draft.manualWeights[i]});
        }
    }
    return rows;
}

EditorOpenResult ColonyProcessingEditor::open(const ColonyProcessingOpenIntent intent,
                                               const SimulationQueries& queries,
                                               InformationInteractionAdapter& interactions) {
    if (!interactions.validateColonyProcessingOpen(intent)) {
        return {EditorOpenOutcome::SourceRejected,
                "Configure unavailable: selection or preview changed, or colony is missing."};
    }
    if (record_) {
        record_->focusRequested = true;
        if (record_->target == intent.displayedTarget) {
            return {EditorOpenOutcome::ExistingFocused, {}};
        }
        record_->statusMessage = "Finish or discard the draft for " + record_->targetNameAtOpen +
                                 " before editing another colony.";
        return {EditorOpenOutcome::OtherEditorActive, record_->statusMessage};
    }

    // Resolve every value needed for a new record before consuming an EditorId.
    const auto colony = liveColony(queries, intent.displayedTarget);
    if (!colony) return {EditorOpenOutcome::TargetUnavailable, "Original colony unavailable."};
    const auto basis = basisFrom(*colony);
    const ColonyProcessingDraft draft{basis.appliedPolicy, usableDraftWeights(basis.manualWeights)};
    if (nextEditorId_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{"Colony processing editor IDs exhausted"};
    }
    const ColonyProcessingEditorId id{interactions.world(), nextEditorId_++};
    record_ = ColonyProcessingEditorRecord{id, intent.displayedTarget, colony->name,
        basis, draft, false, true, {}};
    return {EditorOpenOutcome::Opened, {}};
}

bool ColonyProcessingEditor::setDraftPolicy(const ColonyProcessingEditorId id, const ProcessingPolicy policy) {
    if (!record_ || record_->id != id || !validPolicy(policy)) return false;
    record_->draft.policy = policy;
    record_->statusMessage.clear();
    return true;
}

bool ColonyProcessingEditor::setManualWeight(const ColonyProcessingEditorId id,
                                              const ProcessedMaterial material, const double weight) {
    if (!record_ || record_->id != id) return false;
    const auto index = processedMaterialIndex(material);
    if (index >= record_->draft.manualWeights.size() || !std::isfinite(weight) || weight < 0.0 || weight > 10.0) {
        record_->statusMessage = "Manual weights must be finite and between 0 and 10.";
        return false;
    }
    record_->draft.manualWeights[index] = weight;
    record_->statusMessage.clear();
    return true;
}

bool ColonyProcessingEditor::normalizeManualWeights(const ColonyProcessingEditorId id) {
    if (!record_ || record_->id != id) return false;
    double total = 0.0;
    for (const double weight : record_->draft.manualWeights) total += weight;
    if (!std::isfinite(total)) return false;
    if (total <= kProcessedMaterialComparisonEpsilon) record_->draft.manualWeights.fill(1.0);
    else for (double& weight : record_->draft.manualWeights) weight /= total;
    record_->statusMessage = "Manual weights normalized; the draft is still unapplied.";
    return true;
}

ColonyProcessingEditorAssessment ColonyProcessingEditor::assess(
    const SimulationQueries& queries, const InformationInteractionAdapter& interactions) const {
    ColonyProcessingEditorAssessment result;
    if (!record_) {
        result.reason = "No processing editor is open.";
        return result;
    }
    result.dirty = dirty(*record_);
    if (record_->target.world != interactions.world()) {
        result.reason = "This editor belongs to an earlier world.";
        return result;
    }
    result.current = liveColony(queries, record_->target);
    if (!result.current) {
        result.reason = "Original colony unavailable.";
        return result;
    }
    const auto liveBasis = basisFrom(*result.current);
    result.stale = liveBasis.appliedPolicy != record_->basis.appliedPolicy ||
        !sameWeights(liveBasis.manualWeights, record_->basis.manualWeights);
    result.preview = queries.previewColonyProcessingPolicy(
        std::get<ColonyId>(record_->target.object), record_->draft.policy,
        processingAllocationsForDraft(record_->draft));
    if (record_->discardConfirmation) {
        result.reason = "Choose Keep editing or Discard draft.";
    } else if (result.stale) {
        result.reason = "Current processing configuration changed. Review latest state and retain draft.";
    } else if (!result.preview || !result.preview->valid) {
        result.reason = result.preview ? result.preview->validationMessage : "Original colony unavailable.";
    } else if (record_->draft.policy == result.current->processingPolicy &&
               (record_->draft.policy != ProcessingPolicy::Manual ||
                sameWeights(record_->draft.manualWeights, liveBasis.manualWeights))) {
        result.reason = "No processing change to apply.";
    } else {
        result.canApply = true;
    }
    return result;
}

bool ColonyProcessingEditor::reviewLatest(const ColonyProcessingEditorId id,
                                           const SimulationQueries& queries,
                                           const InformationInteractionAdapter& interactions) {
    if (!record_ || record_->id != id || record_->target.world != interactions.world()) return false;
    const auto current = liveColony(queries, record_->target);
    if (!current) return false;
    const auto freshBasis = basisFrom(*current);
    record_->basis = freshBasis;
    record_->discardConfirmation = false;
    record_->statusMessage = "Latest processing configuration reviewed; draft retained and not applied.";
    return true;
}

EditorApplyResult ColonyProcessingEditor::apply(const ColonyProcessingEditorId id,
                                                 const SimulationQueries& queries,
                                                 SimulationService& service,
                                                 InformationInteractionAdapter& interactions) {
    if (!record_ || record_->id != id) {
        return {EditorApplyOutcome::InvalidEditor, "The originating editor is no longer available."};
    }
    const auto assessment = assess(queries, interactions);
    if (!assessment.current) {
        record_->statusMessage = assessment.reason;
        return {EditorApplyOutcome::UnavailableTarget, assessment.reason};
    }
    if (assessment.stale) {
        record_->statusMessage = assessment.reason;
        return {EditorApplyOutcome::StaleBasis, assessment.reason};
    }
    if (!assessment.preview || !assessment.preview->valid || record_->discardConfirmation) {
        record_->statusMessage = assessment.reason;
        return {EditorApplyOutcome::InvalidDraft, assessment.reason};
    }
    if (!assessment.canApply) {
        record_->statusMessage = assessment.reason;
        return {EditorApplyOutcome::NoChange, assessment.reason};
    }
    const auto colonyId = std::get<ColonyId>(record_->target.object);
    const CommandResult command = service.execute(SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = record_->draft.policy,
        .manualAllocations = record_->draft.policy == ProcessingPolicy::Manual
            ? processingAllocationsForDraft(record_->draft) : std::vector<ProcessingAllocation>{}
    });
    if (!command.ok) {
        record_->statusMessage = command.message;
        return {EditorApplyOutcome::CommandRejected, command.message};
    }
    record_.reset();
    return {EditorApplyOutcome::Applied, command.message};
}

EditorCloseOutcome ColonyProcessingEditor::requestCancel(const ColonyProcessingEditorId id) {
    if (!record_ || record_->id != id) return EditorCloseOutcome::Rejected;
    if (!dirty(*record_)) {
        record_.reset();
        return EditorCloseOutcome::Closed;
    }
    record_->discardConfirmation = true;
    return EditorCloseOutcome::ConfirmationRequired;
}

bool ColonyProcessingEditor::keepEditing(const ColonyProcessingEditorId id) {
    if (!record_ || record_->id != id || !record_->discardConfirmation) return false;
    record_->discardConfirmation = false;
    return true;
}

bool ColonyProcessingEditor::discard(const ColonyProcessingEditorId id) {
    if (!record_ || record_->id != id || !record_->discardConfirmation) return false;
    record_.reset();
    return true;
}

void ColonyProcessingEditor::render(const SimulationQueries& queries, SimulationService& service,
                                     InformationInteractionAdapter& interactions, const ShellRegion workArea) {
    if (!record_ || workArea.width < 80.0F || workArea.height < 50.0F) return;
    const ColonyProcessingEditorId id = record_->id;
    const std::string name = windowName(id);
    const ShellRegion initial = confineFloatingWindow(
        {workArea.x + (workArea.width - std::min(540.0F, workArea.width)) * 0.5F,
         workArea.y + 30.0F, std::min(540.0F, workArea.width),
         std::min(570.0F, workArea.height)}, workArea);
    ImGui::SetNextWindowPos({initial.x, initial.y}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({initial.width, initial.height}, ImGuiCond_Once);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ConstraintContext constraints{name.c_str(), workArea};
    ImGui::SetNextWindowSizeConstraints(
        {std::min(360.0F, workArea.width), std::min(300.0F, workArea.height)},
        {workArea.width, workArea.height}, constrainEditor, &constraints);
    if (record_->focusRequested) {
        ImGui::SetNextWindowFocus();
        record_->focusRequested = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 12.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,
                        {std::min(360.0F, workArea.width), std::min(300.0F, workArea.height)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSurface);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
    ImGui::PushStyleColor(ImGuiCol_Border, kDivider);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool open = true;
    bool cancel = false, keep = false, discardDraft = false, applyDraft = false, review = false;
    if (ImGui::Begin(name.c_str(), &open, flags) && open) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted("CONFIGURE PROCESSING");
        ImGui::PopStyleColor();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3F);
        ImGui::TextUnformatted(record_->targetNameAtOpen.c_str());
        ImGui::PopFont();
        mutedText("Fixed target - changes are unapplied until Apply policy.");
        ImGui::Separator();

        const float footerHeight = record_->discardConfirmation ? 100.0F : 108.0F;
        if (ImGui::BeginChild("##editor_content", {0.0F, -footerHeight})) {
            auto state = assess(queries, interactions);
            heading("CURRENT STATE");
            if (state.current) {
                const auto& colony = *state.current;
                mutedText("Colony - " + colony.bodyName);
                mutedText("Responsible institution: " +
                    (colony.ownerInstitutionId ? colony.ownerInstitutionName : std::string{"Unassigned"}));
                ImGui::Text("Applied policy: %s", colony.processingPolicyName.c_str());
                ImGui::Text("Processor capacity: %.1f units/day", colony.processorCapacity);
                ImGui::Text("Raw stockpile: %.1f units", colony.totalRawStockpile);
                ImGui::Text("Processed stockpile: %.1f units", colony.totalProcessedStockpile);
                ImGui::Text("Day %lld", static_cast<long long>(service.state().date.day));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kAttention);
                ImGui::TextUnformatted("Original colony unavailable");
                ImGui::PopStyleColor();
                mutedText("The fixed target could not be resolved. Discard remains available.");
            }
            heading("DRAFT POLICY");
            if (ImGui::BeginCombo("Policy", policyLabel(record_->draft.policy))) {
                for (const auto policy : kPolicies) {
                    const bool selected = record_->draft.policy == policy;
                    if (ImGui::Selectable(policyLabel(policy), selected)) (void)setDraftPolicy(id, policy);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (record_->draft.policy == ProcessingPolicy::Manual) {
                constexpr double minWeight = 0.0, maxWeight = 10.0;
                for (std::size_t i = 0; i < record_->draft.manualWeights.size(); ++i) {
                    const auto material = static_cast<ProcessedMaterial>(i);
                    double weight = record_->draft.manualWeights[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::SliderScalar("Weight", ImGuiDataType_Double, &weight,
                                            &minWeight, &maxWeight, "%.2f")) {
                        (void)setManualWeight(id, material, weight);
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(toString(material).data(),
                        toString(material).data() + toString(material).size());
                    ImGui::PopID();
                }
                if (ImGui::Button("Normalize")) (void)normalizeManualWeights(id);
            } else {
                mutedText("Preset allocation is read-only. Manual weights remain in this draft if you switch back.");
            }

            state = assess(queries, interactions);
            heading("NOMINAL CAPACITY ALLOCATION");
            if (state.preview && state.preview->valid && state.current) {
                for (const auto& row : state.preview->effectiveAllocations) {
                    ImGui::Text("%s: %.1f%% (%.1f units/day)", row.materialName.c_str(),
                        row.normalizedPercent,
                        state.current->processorCapacity * row.normalizedPercent / 100.0);
                }
                mutedText("Actual output may be lower when raw inputs are unavailable.");
            } else if (state.preview) {
                mutedText(state.preview->validationMessage);
            }
            if (state.stale) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAttention);
                ImGui::TextUnformatted("Configuration changed since this editor opened.");
                ImGui::PopStyleColor();
                ImGui::Text("Latest applied policy: %s", state.current->processingPolicyName.c_str());
                if (ImGui::Button("Review latest state and retain draft")) review = true;
            }
            if (!record_->statusMessage.empty()) mutedText(record_->statusMessage);
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (record_->discardConfirmation) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAttention);
            ImGui::TextUnformatted("Discard unapplied draft?");
            ImGui::PopStyleColor();
            if (ImGui::Button("Keep editing")) keep = true;
            ImGui::SameLine();
            if (ImGui::Button("Discard draft")) discardDraft = true;
        } else {
            const auto state = assess(queries, interactions);
            if (!state.canApply) mutedText(state.reason);
            if (ImGui::Button("Cancel")) cancel = true;
            ImGui::SameLine();
            if (!state.canApply) ImGui::BeginDisabled();
            if (ImGui::Button("Apply policy")) applyDraft = true;
            if (!state.canApply) ImGui::EndDisabled();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    if (!open) cancel = true;
    if (keep) (void)keepEditing(id);
    if (discardDraft) (void)discard(id);
    if (cancel) (void)requestCancel(id);
    if (review) (void)reviewLatest(id, queries, interactions);
    if (applyDraft) (void)apply(id, queries, service, interactions);
}

} // namespace deep::ui_imgui
