#include "ui_imgui/InformationPreviewLayer.h"

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "ui_imgui/InformationPreviewGeometry.h"
#include "ui_imgui/InformationRelationshipRows.h"
#include "ui_imgui/InformationRelationships.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace deep::ui_imgui {
namespace {

constexpr ImVec4 kSurface{0.085F, 0.135F, 0.165F, 1.0F};
constexpr ImVec4 kText{0.87F, 0.93F, 0.95F, 1.0F};
constexpr ImVec4 kMuted{0.60F, 0.71F, 0.76F, 1.0F};
constexpr ImVec4 kAccent{0.49F, 0.82F, 0.79F, 1.0F};
constexpr ImVec4 kDivider{0.20F, 0.34F, 0.38F, 1.0F};
constexpr ImVec4 kAttention{0.92F, 0.70F, 0.37F, 1.0F};
constexpr ImVec4 kButton{0.13F, 0.33F, 0.36F, 1.0F};
constexpr ImVec4 kButtonHovered{0.18F, 0.43F, 0.44F, 1.0F};
constexpr ImVec4 kButtonActive{0.22F, 0.51F, 0.50F, 1.0F};
constexpr ImVec4 kTitle{0.07F, 0.11F, 0.14F, 1.0F};
constexpr const char* kNotApplicable = "-";

enum class ActionKind { Pin, Unpin, Close };

struct PreviewAction {
    PreviewId id;
    ActionKind kind;
};

struct ConstraintContext {
    const char* name;
    ShellRegion work;
};

// ImGui calls the size constraint during Begin, before it builds decorations
// and clipping. This also runs on an unchanged size, so title-bar movement is
// brought back into the same left work area as other native floating windows.
void constrainPreview(ImGuiSizeCallbackData* data) {
    const auto& context = *static_cast<const ConstraintContext*>(data->UserData);
    const ShellRegion current = confineFloatingWindow(
        {data->Pos.x, data->Pos.y, data->CurrentSize.x, data->CurrentSize.y}, context.work);
    ImGui::SetWindowPos(context.name, {current.x, current.y});

    // Preserve the opposite edge during a grip resize, including left/top
    // grips. A window can still shrink normally and scroll its compact content.
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

// Object text is never used as a format string or an ImGui widget ID.
void wrappedText(const std::string_view value) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopTextWrapPos();
}

[[nodiscard]] std::string namedOrUnknown(const std::string& name) {
    return name.empty() ? "Unknown" : name;
}

[[nodiscard]] std::string institution(const std::optional<InstitutionId>& id,
                                      const std::string& name) {
    return id.has_value() ? namedOrUnknown(name) : "Unassigned";
}

[[nodiscard]] std::string quantity(const double value, const int decimals,
                                   const std::string_view unit = {}) {
    if (!std::isfinite(value)) {
        return "Unknown";
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(decimals) << value;
    if (!unit.empty()) {
        text << ' ' << unit;
    }
    return text.str();
}

void identity(const std::string& name, const std::string& subtitle) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35F);
    wrappedText(namedOrUnknown(name));
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    wrappedText(subtitle);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
}

[[nodiscard]] bool beginFacts() {
    if (!ImGui::BeginTable("##preview_facts", 2, ImGuiTableFlags_SizingStretchSame |
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        return false;
    }
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    return true;
}

void fact(const char* label, const std::string_view value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    wrappedText(label);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    // Short values share a right edge; long names wrap instead of enlarging
    // the reference window or silently truncating institutional identity.
    const float textWidth = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
    const float spareWidth = ImGui::GetContentRegionAvail().x - textWidth;
    if (spareWidth > 0.0F) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spareWidth);
    }
    wrappedText(value);
}

void unavailable() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAttention);
    wrappedText("Selected object unavailable");
    ImGui::PopStyleColor();
    wrappedText("Current information could not be resolved.");
}

[[nodiscard]] bool bodyPreview(const SimulationQueries& queries, const BodyId id) {
    const auto bodies = queries.bodySystemOverview();
    const auto body = std::find_if(bodies.begin(), bodies.end(), [id](const BodySystemSummary& row) {
        return row.id == id;
    });
    if (body == bodies.end()) {
        unavailable();
        return false;
    }

    identity(body->name, "BODY \xc2\xb7 " + namedOrUnknown(body->typeName));
    if (beginFacts()) {
        fact("Strategic zone", namedOrUnknown(body->strategicZoneName));
        fact("Institution", institution(body->ownerInstitutionId, body->ownerInstitutionName));
        fact("Colonies", std::to_string(body->colonyCount));
        // This DTO count also includes moving fleets whose departure body is
        // still referenced. The relationship section lists only idle fleets.
        fact("Fleets referenced", std::to_string(body->fleetCount));
        fact("Deposits", std::to_string(body->mineralDepositCount));
        ImGui::EndTable();
    }
    return true;
}

[[nodiscard]] bool colonyPreview(const SimulationQueries& queries, const ColonyId id) {
    const auto colonies = queries.colonies();
    const auto colony = std::find_if(colonies.begin(), colonies.end(), [id](const ColonySummary& row) {
        return row.id == id;
    });
    if (colony == colonies.end()) {
        unavailable();
        return false;
    }

    identity(colony->name, "COLONY \xc2\xb7 " + namedOrUnknown(colony->bodyName));
    if (beginFacts()) {
        fact("Institution", institution(colony->ownerInstitutionId, colony->ownerInstitutionName));
        fact("Mines", quantity(colony->mines, 2));
        fact("Processing", namedOrUnknown(colony->processingPolicyName));
        fact("Shipyard", quantity(colony->effectiveShipyardCapacity, 2, "BP/day"));
        fact("Raw stockpile", quantity(colony->totalRawStockpile, 1, "units"));
        fact("Processed stockpile", quantity(colony->totalProcessedStockpile, 1, "units"));
        ImGui::EndTable();
    }
    return true;
}

[[nodiscard]] bool fleetPreview(const SimulationQueries& queries, const FleetId id) {
    const auto fleet = queries.fleet(id);
    if (!fleet.has_value()) {
        unavailable();
        return false;
    }

    const std::string location = fleet->hasActiveOrder
        ? "In transit from " + namedOrUnknown(fleet->currentBodyName)
        : namedOrUnknown(fleet->currentBodyName);
    identity(fleet->name, "FLEET \xc2\xb7 " + location);
    if (beginFacts()) {
        fact("Institution", institution(fleet->ownerInstitutionId, fleet->ownerInstitutionName));
        fact("Ships", std::to_string(fleet->shipCount));
        fact("Fuel", fleet->fuelCapacity > 0.0 ? quantity(fleet->fuelPercent, 1, "%") : kNotApplicable);
        fact("Order", fleet->hasActiveOrder ? namedOrUnknown(fleet->activeOrderName) : "Idle");
        fact("Destination", fleet->hasActiveOrder && fleet->destinationBodyId.has_value()
            ? namedOrUnknown(fleet->destinationBodyName) : kNotApplicable);
        fact("ETA", !fleet->hasActiveOrder ? kNotApplicable
            : (fleet->activeOrderEtaDays.has_value() ? std::to_string(*fleet->activeOrderEtaDays) + " d" : "Unknown"));
        ImGui::EndTable();
    }
    return true;
}

[[nodiscard]] bool renderTarget(const SimulationQueries& queries, const ObjectTarget& target) {
    return std::visit([&](const auto id) -> bool {
        using IdType = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<IdType, BodyId>) {
            return bodyPreview(queries, id);
        } else if constexpr (std::is_same_v<IdType, ColonyId>) {
            return colonyPreview(queries, id);
        } else {
            return fleetPreview(queries, id);
        }
    }, target);
}

void renderOne(const SimulationQueries& queries, const InformationPreview& preview,
               const ShellRegion work, std::vector<PreviewAction>& actions,
               std::vector<ObjectReference>& inspectionRequests,
               std::optional<PreviewGoToIntent>& goToIntent) {
    const std::string name = informationPreviewWindowName(preview.id);
    const ShellRegion initial = initialPreviewGeometry(preview.id, work);
    ImGui::SetNextWindowPos({initial.x, initial.y}, ImGuiCond_Once);
    ImGui::SetNextWindowSize({initial.width, initial.height}, ImGuiCond_Once);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ConstraintContext constraint{name.c_str(), work};
    ImGui::SetNextWindowSizeConstraints(
        {std::min(280.0F, work.width), std::min(240.0F, work.height)},
        {work.width, work.height}, constrainPreview, &constraint);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 11.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,
                        {std::min(280.0F, work.width), std::min(240.0F, work.height)});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {0.0F, 3.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSurface);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
    ImGui::PushStyleColor(ImGuiCol_Border, kDivider);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTitle);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTitle);
    ImGui::PushStyleColor(ImGuiCol_Button, kButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kButtonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonActive);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    bool open = true;
    if (ImGui::Begin(name.c_str(), &open, flags) && open) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted(preview.pinned ? "PINNED" : "PREVIEW");
        ImGui::PopStyleColor();
        if (ImGui::Button(preview.pinned ? "Unpin" : "Pin")) {
            actions.push_back({preview.id, preview.pinned ? ActionKind::Unpin : ActionKind::Pin});
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            actions.push_back({preview.id, ActionKind::Close});
        }
        ImGui::Separator();
        // Keep facts and relationships scrollable while Go To remains visible
        // at the bottom of a compact or resized preview.
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() +
                                   ImGui::GetStyle().ItemSpacing.y * 2.0F;
        bool resolved = false;
        const std::string contentName = informationPreviewContentName(preview.target);
        if (ImGui::BeginChild(contentName.c_str(), {0.0F, -footerHeight})) {
            resolved = renderTarget(queries, preview.target.object);
            if (resolved) {
                renderInformationRelationshipRows(
                    informationRelationships(queries, preview.target), inspectionRequests);
            }
        }
        ImGui::EndChild();
        if (resolved) {
            ImGui::Separator();
            // The window name contains the immutable PreviewId and world. The
            // explicit scope keeps this control independent of object names.
            ImGui::PushID(name.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            if (ImGui::Button("Go To >##preview_go_to",
                              {ImGui::GetContentRegionAvail().x, 0.0F}) &&
                !goToIntent.has_value()) {
                goToIntent = PreviewGoToIntent{preview.id, preview.target};
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar(4);
    if (!open) {
        actions.push_back({preview.id, ActionKind::Close});
    }
}

} // namespace

std::string informationPreviewWindowName(const PreviewId id) {
    return "Preview###InformationPreview_W" + std::to_string(id.world.value) +
           "_P" + std::to_string(id.value);
}

std::string informationPreviewContentName(const ObjectReference target) {
    const auto value = std::visit([](const auto id) { return id.value; }, target.object);
    return "##preview_content_W" + std::to_string(target.world.value) +
           "_T" + std::to_string(target.object.index()) +
           "_" + std::to_string(value);
}

std::optional<PreviewGoToIntent> InformationPreviewLayer::render(
    const SimulationQueries& queries, InformationInteractionAdapter& interactions,
    const ShellRegion workArea) const {
    const auto previews = interactions.previewSnapshot();
    // An almost minimized shell cannot expose a usable title bar. Keep the
    // records intact; the same native windows recover when the shell is larger.
    if (workArea.width < 80.0F || workArea.height < 50.0F) {
        return std::nullopt;
    }
    std::vector<PreviewAction> actions;
    std::vector<ObjectReference> inspectionRequests;
    std::optional<PreviewGoToIntent> goToIntent;
    actions.reserve(previews.size());
    for (const auto& preview : previews) {
        renderOne(queries, preview, workArea, actions, inspectionRequests, goToIntent);
    }
    for (const auto& action : actions) {
        switch (action.kind) {
        case ActionKind::Pin:
            (void)interactions.pin(action.id);
            break;
        case ActionKind::Unpin:
            (void)interactions.unpin(action.id);
            break;
        case ActionKind::Close:
            (void)interactions.closePreview(action.id);
            break;
        }
    }
    // Each reference came from the displayed projection and retains that
    // preview's world. In particular, an Inspect click inside a pin opens or
    // retargets the shared temporary without changing the pin or selection.
    for (const auto& target : inspectionRequests) {
        (void)interactions.inspect(target);
    }
    return goToIntent;
}

} // namespace deep::ui_imgui
