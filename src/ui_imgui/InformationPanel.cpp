#include "ui_imgui/InformationPanel.h"

// Implements native overview presentation only. Every object is resolved anew
// through query DTOs; lifecycle and selection authority remain in the shell.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace deep::ui_imgui {
namespace {

constexpr ImVec4 kSurface{0.075F, 0.115F, 0.145F, 1.0F};
constexpr ImVec4 kText{0.85F, 0.91F, 0.94F, 1.0F};
constexpr ImVec4 kMuted{0.60F, 0.70F, 0.75F, 1.0F};
constexpr ImVec4 kAccent{0.55F, 0.79F, 0.77F, 1.0F};
constexpr ImVec4 kDivider{0.20F, 0.32F, 0.36F, 1.0F};
constexpr ImVec4 kAttention{0.92F, 0.70F, 0.37F, 1.0F};
// The bundled native font has no em-dash glyph. Keep absent values readable
// without adding a font dependency or mistaking absence for a numeric zero.
constexpr const char* kNotApplicable = "-";

// Text never supplies an ImGui ID or format string. Literal ##/### in an object
// name remain visible and cannot change window/table identity.
void wrappedText(const std::string_view text) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
}

[[nodiscard]] std::string namedOrUnknown(const std::string& name) {
    return name.empty() ? "Unknown" : name;
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

[[nodiscard]] std::string institution(const std::optional<InstitutionId>& id,
                                      const std::string& name) {
    return id.has_value() ? namedOrUnknown(name) : "Unassigned";
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

[[nodiscard]] bool beginFacts(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame |
                                ImGuiTableFlags_NoSavedSettings |
                                ImGuiTableFlags_NoPadOuterX)) {
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
    // Short values align consistently on the right; longer values wrap within
    // their cell instead of widening the fixed shell or shrinking the text.
    const float textWidth = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
    const float spareWidth = ImGui::GetContentRegionAvail().x - textWidth;
    if (spareWidth > 0.0F) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spareWidth);
    }
    wrappedText(value);
}

void identity(const std::string& name, const std::string& typeAndLocation,
              const std::string& responsibleInstitution) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    wrappedText(namedOrUnknown(name));
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    wrappedText(typeAndLocation);
    ImGui::Spacing();
    wrappedText("Responsible institution: " + responsibleInstitution);
    ImGui::PopStyleColor();
}

void unavailable() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAttention);
    wrappedText("Selected object unavailable");
    ImGui::PopStyleColor();
    wrappedText("The current selection could not be resolved.");
}

void bodyOverview(const SimulationQueries& queries, const BodyId id) {
    const auto bodies = queries.bodySystemOverview();
    const auto body = std::find_if(bodies.begin(), bodies.end(), [id](const BodySystemSummary& row) {
        return row.id == id;
    });
    if (body == bodies.end()) {
        unavailable();
        return;
    }

    identity(body->name, "BODY \xc2\xb7 " + namedOrUnknown(body->typeName),
             institution(body->ownerInstitutionId, body->ownerInstitutionName));
    if (beginFacts("##body_identity")) {
        fact("Strategic zone", namedOrUnknown(body->strategicZoneName));
        ImGui::EndTable();
    }
    section("OVERVIEW");
    if (beginFacts("##body_overview")) {
        fact("Colonies", std::to_string(body->colonyCount));
        fact("Fleets", std::to_string(body->fleetCount));
        fact("Deposits", std::to_string(body->mineralDepositCount));
        ImGui::EndTable();
    }
    section("ORBIT");
    if (beginFacts("##body_orbit")) {
        fact("Parent", body->parentBodyId.has_value() ? namedOrUnknown(body->parentBodyName) : "None");
        fact("Radius", body->orbitalRadiusKm >= 1'000'000.0
            ? quantity(body->orbitalRadiusKm / 1'000'000.0, 2, "M km")
            : quantity(body->orbitalRadiusKm, 1, "km"));
        fact("Period", quantity(body->orbitalPeriodDays, 1, "d"));
        ImGui::EndTable();
    }
    section("RESOURCE KNOWLEDGE");
    if (beginFacts("##body_resource_knowledge")) {
        fact("Known deposits", std::to_string(body->knownDepositCount));
        fact("Estimated deposits", std::to_string(body->estimatedDepositCount));
        fact("Unknown deposits", std::to_string(body->unknownDepositCount));
        fact("Confirmed quantity", quantity(body->confirmedDepositQuantity, 1, "units"));
        fact("Estimated quantity", quantity(body->estimatedDepositQuantity, 1, "units"));
        fact("Uncertain quantity", quantity(body->uncertainDepositQuantity, 1, "units"));
        ImGui::EndTable();
    }
}

void colonyOverview(const SimulationQueries& queries, const ColonyId id) {
    const auto colonies = queries.colonies();
    const auto colony = std::find_if(colonies.begin(), colonies.end(), [id](const ColonySummary& row) {
        return row.id == id;
    });
    if (colony == colonies.end()) {
        unavailable();
        return;
    }

    identity(colony->name, "COLONY \xc2\xb7 " + namedOrUnknown(colony->bodyName),
             institution(colony->ownerInstitutionId, colony->ownerInstitutionName));
    section("OVERVIEW");
    if (beginFacts("##colony_overview")) {
        fact("Mines", quantity(colony->mines, 2));
        fact("Processors/day", quantity(colony->processorCapacity, 2, "units"));
        fact("Processing policy", namedOrUnknown(colony->processingPolicyName));
        ImGui::EndTable();
    }
    section("PRODUCTION");
    if (beginFacts("##colony_production")) {
        fact("Shipyard BP/day", quantity(colony->effectiveShipyardCapacity, 2));
        fact("Base BP/day", quantity(colony->shipyardCapacity, 2));
        fact("Modifier", quantity(colony->shipyardModifierPercent, 1, "%"));
        ImGui::EndTable();
    }
    section("STOCKPILES");
    if (beginFacts("##colony_stockpiles")) {
        fact("Raw", quantity(colony->totalRawStockpile, 1, "units"));
        fact("Processed", quantity(colony->totalProcessedStockpile, 1, "units"));
        ImGui::EndTable();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    wrappedText("Stockpiles are totals across resource types.");
    ImGui::PopStyleColor();
}

void fleetOverview(const SimulationQueries& queries, const FleetId id) {
    const auto fleet = queries.fleet(id);
    if (!fleet.has_value()) {
        unavailable();
        return;
    }

    const std::string location = fleet->hasActiveOrder
        ? "In transit from " + namedOrUnknown(fleet->currentBodyName)
        : namedOrUnknown(fleet->currentBodyName);
    identity(fleet->name, "FLEET \xc2\xb7 " + location,
             institution(fleet->ownerInstitutionId, fleet->ownerInstitutionName));
    section("STATUS");
    if (beginFacts("##fleet_status")) {
        fact("Ships", std::to_string(fleet->shipCount));
        fact("Fuel", fleet->fuelCapacity > 0.0 ? quantity(fleet->fuelPercent, 1, "%") : kNotApplicable);
        fact("Range", quantity(fleet->currentRange, 1, "map units"));
        ImGui::EndTable();
    }
    section("CURRENT ORDER");
    if (beginFacts("##fleet_order")) {
        fact("Order", fleet->hasActiveOrder ? namedOrUnknown(fleet->activeOrderName) : "Idle");
        fact("Destination", fleet->hasActiveOrder && fleet->destinationBodyId.has_value()
            ? namedOrUnknown(fleet->destinationBodyName) : kNotApplicable);
        fact("ETA", !fleet->hasActiveOrder ? kNotApplicable
            : (fleet->activeOrderEtaDays.has_value() ? std::to_string(*fleet->activeOrderEtaDays) + " d" : "Unknown"));
        fact("Burn phase", fleet->hasActiveOrder ? namedOrUnknown(fleet->activeOrderBurnPhase) : kNotApplicable);
        ImGui::EndTable();
    }
    section("ROUTE");
    if (beginFacts("##fleet_route")) {
        fact("Queued orders", std::to_string(fleet->queuedOrders.size()));
        fact("Total duration", std::to_string(fleet->totalRouteDurationDays) + " d");
        ImGui::EndTable();
    }
}

} // namespace

void InformationPanel::render(const SimulationQueries& queries, const SelectionState& selection,
                              const ImVec2& position, const ImVec2& size) const {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{16.0F, 14.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0F, 4.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSurface);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##InformationPanel", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted("INFORMATION");
        ImGui::PopStyleColor();
        ImGui::Separator();
        switch (selection.type()) {
        case SelectedObjectType::None:
            ImGui::Spacing();
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25F);
            wrappedText("No selection");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            wrappedText("Select a body, colony, or fleet from the map or an operational view to inspect its overview.");
            ImGui::PopStyleColor();
            break;
        case SelectedObjectType::Body:
            bodyOverview(queries, selection.bodyId());
            break;
        case SelectedObjectType::Colony:
            colonyOverview(queries, selection.colonyId());
            break;
        case SelectedObjectType::Fleet:
            fleetOverview(queries, selection.fleetId());
            break;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}

} // namespace deep::ui_imgui
