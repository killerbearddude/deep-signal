#include "ui_imgui/ShipyardComparisonView.h"

// Compares owned query snapshots. Numeric differences are presentation-only
// B - A arithmetic; installed profiles remain separate and have no winner.

#include "ui_imgui/PresentationWidgets.h"
#include "ui_imgui/UiTheme.h"

#include <imgui.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

namespace deep::ui_imgui {
namespace {

constexpr const char* absent = "—";

std::string number(double value) {
    if (value == 0.0)
        return "0";
    std::ostringstream out;
    // Keep the 0.20 / 0.25 service tradeoff and small recipe rates legible.
    out << std::setprecision(6) << std::defaultfloat << value;
    return out.str();
}

std::string difference(double a, double b) {
    const double delta = b - a;
    return (delta > 0.0 ? "+" : "") + number(delta);
}

void secondary(const std::string& text) {
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

const ShipClassSummary* selectedClass(const std::vector<ShipClassSummary>& classes,
                                      std::optional<ShipClassId> selected) {
    const auto it =
        std::find_if(classes.begin(), classes.end(), [&](const auto& row) { return selected == row.id; });
    return it == classes.end() ? nullptr : &*it;
}

void establishSelections(const std::vector<ShipClassSummary>& classes, std::optional<ShipClassId>& a,
                         std::optional<ShipClassId>& b) {
    if (classes.empty()) {
        a.reset();
        b.reset();
        return;
    }
    if (!selectedClass(classes, a))
        a = classes.front().id;
    if (!selectedClass(classes, b)) {
        const auto other =
            std::find_if(classes.begin(), classes.end(), [&](const auto& row) { return row.id != *a; });
        b = other == classes.end() ? *a : other->id;
    }
}

std::string optionLabel(const ShipClassSummary& row) {
    return row.name + " r" + std::to_string(row.revision) + " · " + row.roleName;
}

float identityHeight(const ShipClassSummary* row, float width) {
    if (!row)
        return 134.0F;
    // Match objectTitle's ASCII heading treatment when measuring wrapped names.
    std::string title = row->name;
    for (char& c : title)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::ObjectTitle));
    const float textHeight = ImGui::CalcTextSize(title.c_str(), nullptr, false, width).y;
    ImGui::PopFont();
    return 134.0F + std::max(0.0F, textHeight - uiTextSize(UiTextRole::ObjectTitle));
}

void identity(const char* side, const char* childId, const char* comboId,
              const std::vector<ShipClassSummary>& classes, std::optional<ShipClassId>& selected,
              float height) {
    ImGui::BeginChild(childId, {0.0F, height}, ImGuiChildFlags_None);
    const auto* row = selectedClass(classes, selected);
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Section));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(UiColor::TextSecondary));
    ImGui::TextUnformatted(side);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (row) {
        ImGui::SameLine();
        secondary(std::string("Constructible: ") + (row->design.constructible ? "Yes" : "No"));
        objectTitle(row->name, row->roleName + " vessel / r" + std::to_string(row->revision));
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo(comboId, optionLabel(*row).c_str())) {
            for (const auto& option : classes) {
                ImGui::PushID(std::to_string(option.id.value).c_str());
                if (ImGui::Selectable(optionLabel(option).c_str(), selected == option.id))
                    selected = option.id;
                if (selected == option.id)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndChild();
}

// One-line comparisons use the approved 20 px body face with 6 px cell padding.
// This is local table geometry; Orders and the shared theme keep their spacing.
bool beginComparisonTable(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {8.0F, 6.0F});
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::PopStyleVar();
        return false;
    }
    ImGui::TableSetupColumn("Characteristic", ImGuiTableColumnFlags_WidthStretch, 1.35F);
    ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthStretch, 1.45F);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthStretch, 1.45F);
    ImGui::TableSetupColumn("B - A", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::PushFont(nullptr, uiTextSize(UiTextRole::Secondary));
    ImGui::TableHeadersRow();
    ImGui::PopFont();
    return true;
}

void endComparisonTable() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void comparisonRow(const std::string& label, const std::string& a, const std::string& b,
                   const std::string& delta, bool changed) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, 32.0F);
    if (changed)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(uiColor(UiColor::Surface)));
    ImGui::TableSetColumnIndex(0);
    secondary(label);
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(changed ? UiColor::TextPrimary : UiColor::TextSecondary));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", a.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", b.c_str());
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(3);
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(changed ? UiColor::TextPrimary : UiColor::TextMuted));
    ImGui::TextWrapped("%s", delta.c_str());
    ImGui::PopStyleColor();
}

void numericRow(const std::string& label, double a, double b) {
    comparisonRow(label, number(a), number(b), difference(a, b), a != b);
}

void textRow(const std::string& label, const std::string& a, const std::string& b) {
    comparisonRow(label, a, b, a == b ? absent : "Different", a != b);
}

void optionalNumericRow(const std::string& label, std::optional<double> a, std::optional<double> b,
                        const std::string& displayA = {}, const std::string& displayB = {}) {
    comparisonRow(label, displayA.empty() ? (a ? number(*a) : absent) : displayA,
                  displayB.empty() ? (b ? number(*b) : absent) : displayB,
                  a && b ? difference(*a, *b) : absent,
                  a != b || (!displayA.empty() && !displayB.empty() && displayA != displayB));
}

void coreDesign(const ShipClassComparisonSummary& a, const ShipClassComparisonSummary& b) {
    sectionTitle("Core design");
    if (!beginComparisonTable("CompareCore"))
        return;
    numericRow("Dry mass", a.design.dryMass, b.design.dryMass);
    comparisonRow("Volume used / hull", number(a.design.usedVolume) + " / " + number(a.design.volumeCapacity),
                  number(b.design.usedVolume) + " / " + number(b.design.volumeCapacity),
                  difference(a.design.usedVolume, b.design.usedVolume) + " used",
                  a.design.usedVolume != b.design.usedVolume ||
                      a.design.volumeCapacity != b.design.volumeCapacity);
    comparisonRow(
        "Power gen / demand", number(a.design.powerGeneration) + " / " + number(a.design.powerDemand),
        number(b.design.powerGeneration) + " / " + number(b.design.powerDemand),
        difference(a.design.powerDemand, b.design.powerDemand) + " demand",
        a.design.powerGeneration != b.design.powerGeneration || a.design.powerDemand != b.design.powerDemand);
    numericRow("Power margin", a.design.powerMargin, b.design.powerMargin);
    numericRow("Propellant capacity", a.design.propellantCapacity, b.design.propellantCapacity);
    comparisonRow("Survey installed / powered",
                  number(a.design.surveyCapability) + " / " + number(a.poweredSurveyCapability),
                  number(b.design.surveyCapability) + " / " + number(b.poweredSurveyCapability),
                  difference(a.poweredSurveyCapability, b.poweredSurveyCapability),
                  a.design.surveyCapability != b.design.surveyCapability ||
                      a.poweredSurveyCapability != b.poweredSurveyCapability);
    numericRow("Build points", a.design.buildPoints, b.design.buildPoints);
    if (a.design.cargoCapacity != 0.0 || b.design.cargoCapacity != 0.0) {
        numericRow("Cargo capacity", a.design.cargoCapacity, b.design.cargoCapacity);
        numericRow("Installed handling / day", a.design.cargoHandlingPerDay, b.design.cargoHandlingPerDay);
    }
    endComparisonTable();
}

void materialRows(const std::vector<ProcessedMaterialStockpileSummary>& a,
                  const std::vector<ProcessedMaterialStockpileSummary>& b, bool perDuty = false,
                  bool aDefined = true, bool bDefined = true) {
    // Query rows contain every enum slot in order. No material-name matching.
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const double amountA = i < a.size() ? a[i].amount : 0.0;
        const double amountB = i < b.size() ? b[i].amount : 0.0;
        if (amountA == 0.0 && amountB == 0.0)
            continue;
        const auto& name = i < a.size() ? a[i].materialName : b[i].materialName;
        // A missing service profile has no defined recipe. Keep that distinct
        // from a present profile that needs zero of this particular material.
        optionalNumericRow(name + (perDuty ? " / duty" : ""),
                           aDefined ? std::optional{amountA} : std::nullopt,
                           bDefined ? std::optional{amountB} : std::nullopt);
    }
}

std::string instrumentName(const ShipClassInstrumentSummary* instrument) {
    return instrument ? instrument->componentName + " x" + std::to_string(instrument->quantity)
                      : "Not installed";
}

void instruments(const ShipClassComparisonSummary& a, const ShipClassComparisonSummary& b) {
    sectionTitle("Survey instrumentation");
    if (a.instruments.empty() && b.instruments.empty()) {
        secondary("Neither design installs measurement or service equipment.");
        return;
    }
    const std::vector<ProcessedMaterialStockpileSummary> noMaterials;
    const auto count = std::max(a.instruments.size(), b.instruments.size());
    if (count > 1)
        secondary("A: " + std::to_string(a.instruments.size()) +
                  " types; B: " + std::to_string(b.instruments.size()) +
                  " types. Install order is preserved; profiles stay separate.");
    for (std::size_t i = 0; i < count; ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto* left = i < a.instruments.size() ? &a.instruments[i] : nullptr;
        const auto* right = i < b.instruments.size() ? &b.instruments[i] : nullptr;
        const auto* ma = left && left->measurement ? &*left->measurement : nullptr;
        const auto* mb = right && right->measurement ? &*right->measurement : nullptr;
        const auto* sa = left && left->service ? &*left->service : nullptr;
        const auto* sb = right && right->service ? &*right->service : nullptr;
        if (beginComparisonTable("CompareInstrument")) {
            textRow("Equipment", instrumentName(left), instrumentName(right));
            textRow(
                "Method",
                ma ? ma->profileName + " / v" + std::to_string(ma->methodVersion) : "No measurement profile",
                mb ? mb->profileName + " / v" + std::to_string(mb->methodVersion) : "No measurement profile");
            optionalNumericRow("Detection threshold",
                               ma ? std::optional{ma->detectionThreshold} : std::nullopt,
                               mb ? std::optional{mb->detectionThreshold} : std::nullopt);
            textRow("Accessibility", ma ? ma->accessibilityName : absent,
                    mb ? mb->accessibilityName : absent);
            if (sa || sb) {
                textRow("Service family", sa ? sa->familyName : absent, sb ? sb->familyName : absent);
                const auto capacity = [](const auto* install, const auto* service) {
                    return service ? number(service->dutyCapacityPerUnit) + " x " +
                                         std::to_string(install->quantity) + " = " +
                                         number(service->totalDutyCapacity)
                                   : std::string{absent};
                };
                optionalNumericRow("Duty: unit x qty",
                                   sa ? std::optional{sa->totalDutyCapacity} : std::nullopt,
                                   sb ? std::optional{sb->totalDutyCapacity} : std::nullopt,
                                   capacity(left, sa), capacity(right, sb));
                optionalNumericRow("Work / duty / unit",
                                   sa ? std::optional{sa->teamWorkdaysPerRestoredDutyPerUnit} : std::nullopt,
                                   sb ? std::optional{sb->teamWorkdaysPerRestoredDutyPerUnit} : std::nullopt);
                materialRows(sa ? sa->materialsPerDutyPerUnit : noMaterials,
                             sb ? sb->materialsPerDutyPerUnit : noMaterials, true, sa != nullptr,
                             sb != nullptr);
            }
            endComparisonTable();
        }
        ImGui::PopID();
    }
    secondary("Threshold: normalized signal. Work: team-workdays per restored duty per unit. Service "
              "materials: per duty per unit.");
}

std::string join(const std::vector<std::string>& names, const char* empty) {
    std::string result;
    for (const auto& name : names) {
        if (!result.empty())
            result += ", ";
        result += name;
    }
    return result.empty() ? empty : result;
}

void readiness(const ShipClassComparisonSummary& design, const char* side) {
    secondary(std::string(side) + " / " + design.name);
    if (design.developedComponents.empty()) {
        ImGui::TextUnformatted("Established catalog components");
        return;
    }
    for (const auto& component : design.developedComponents) {
        ImGui::TextWrapped("%s x%d", component.componentName.c_str(), component.quantity);
        keyValue("Serial production",
                 join(component.serialProductionColonyNames, "None currently qualified"));
        std::vector<std::string> locations;
        for (const auto& location : component.prototypeAvailability)
            locations.push_back(location.colonyName + " x" + std::to_string(location.quantityAvailable));
        keyValue("Available prototypes", join(locations, "None available"));
        keyValue("Qualified support teams",
                 join(component.supportQualifiedTeamNames, "None currently qualified"));
    }
}

void workshops(const ShipClassComparisonSummary& design, const char* side) {
    secondary(std::string(side) + " / " + design.name);
    if (design.workshops.empty()) {
        ImGui::TextUnformatted("No installed workshop");
        return;
    }
    for (const auto& component : design.workshops) {
        ImGui::TextWrapped("%s x%d", component.componentName.c_str(), component.quantity);
        for (const auto& rate : component.rates) {
            keyValue(rate.familyName, number(rate.teamWorkdaysPerDayPerUnit) + " x " +
                                          std::to_string(component.quantity) + " = " +
                                          number(rate.totalTeamWorkdaysPerDay));
        }
    }
}

} // namespace

void renderShipyardComparison(const SimulationQueries& queries, const std::vector<ShipClassSummary>& classes,
                              std::optional<ShipClassId>& selectionA,
                              std::optional<ShipClassId>& selectionB) {
    establishSelections(classes, selectionA, selectionB);
    sectionTitle("Compare designs");
    if (classes.empty()) {
        ImGui::TextUnformatted("No immutable ship classes are available.");
        return;
    }
    if (ImGui::BeginTable("CompareIdentityTable", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const float heightA =
            identityHeight(selectedClass(classes, selectionA), ImGui::GetContentRegionAvail().x);
        ImGui::TableSetColumnIndex(1);
        const float heightB =
            identityHeight(selectedClass(classes, selectionB), ImGui::GetContentRegionAvail().x);
        ImGui::TableSetColumnIndex(0);
        identity("DESIGN A", "CompareIdentityA", "##CompareA", classes, selectionA,
                 std::max(heightA, heightB));
        ImGui::TableSetColumnIndex(1);
        identity("DESIGN B", "CompareIdentityB", "##CompareB", classes, selectionB,
                 std::max(heightA, heightB));
        ImGui::EndTable();
    }
    const auto a = queries.shipClassComparison(*selectionA);
    const auto b = queries.shipClassComparison(*selectionB);
    if (!a || !b)
        return;
    if (classes.size() == 1)
        secondary("Only one immutable class is available; both sides show the same revision.");
    else if (selectionA == selectionB)
        secondary("Same revision on both sides; numeric differences are zero.");

    // Identities/selectors stay fixed while exceptional multi-installation or
    // readiness detail may scroll. The H2 core comparison fits in this region.
    ImGui::BeginChild("CompareDetails", {0.0F, 0.0F}, ImGuiChildFlags_None);
    for (const auto* side : {&*a, &*b})
        if (!side->design.constructible)
            for (const auto& constraint : side->design.constraints)
                ImGui::TextWrapped("%s: %s", side->name.c_str(), constraint.c_str());
    if (ImGui::BeginTable("CompareSections", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        coreDesign(*a, *b);
        sectionTitle("Build materials");
        if (beginComparisonTable("CompareBuildMaterials")) {
            materialRows(a->buildMaterials, b->buildMaterials);
            endComparisonTable();
        }
        ImGui::TableSetColumnIndex(1);
        instruments(*a, *b);
        ImGui::EndTable();
    }
    sectionTitle("Production & support");
    secondary("Current readiness is separate from design constructibility. Qualifications do not imply a "
              "free, co-located team.");
    if (ImGui::BeginTable("CompareReadiness", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        readiness(*a, "A");
        ImGui::TableSetColumnIndex(1);
        readiness(*b, "B");
        ImGui::EndTable();
    }
    if (!a->workshops.empty() || !b->workshops.empty()) {
        sectionTitle("Workshop capability");
        secondary("Installed team-workdays/day: per unit x quantity = total. Work still requires power, "
                  "qualified teams and supplies.");
        if (ImGui::BeginTable("CompareWorkshops", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            workshops(*a, "A");
            ImGui::TableSetColumnIndex(1);
            workshops(*b, "B");
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace deep::ui_imgui
