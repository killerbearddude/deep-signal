#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/ColonyProcessingEditor.h"

// Production editor model with real queries, adapter lifecycle, and commands.
// Public ImGui text logging checks its native surface without an SDL display.

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {
using namespace deep;
using namespace deep::ui_imgui;

std::string capturedText;
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}
void contains(const std::string& text, std::string_view needle) {
    require(text.find(needle) != std::string::npos,
            "editor text missing: " + std::string{needle} + "\nActual: " + text);
}

GameState makeWorld() {
    auto world = createHomeSystemScenario();
    world.colonies.at(1).name = "A ##literal ###stable";
    for (const char* name : {"B Mars Colony", "C Mars Colony"}) {
        auto colony = world.colonies.at(1);
        colony.id = ColonyId{world.ids.nextColonyId++};
        colony.name = name;
        world.colonies.push_back(std::move(colony));
    }
    return world;
}

struct Fixture {
    ColonyProcessingEditor editor;
    SimulationService service{makeWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter adapter{service, [this] { editor.resetWorldState(); }};

    ColonyId a() const { return service.state().colonies.at(1).id; }
    ColonyId b() const { return service.state().colonies.at(4).id; }
    ColonyId c() const { return service.state().colonies.at(5).id; }
    ObjectReference ref(ColonyId id) const { return {adapter.world(), id}; }
    void select(ColonyId id) { require(adapter.select({adapter.world(), id}), "select fixture colony"); }
    EditorOpenResult openMain(ColonyId id) { select(id); return editor.open({ref(id), std::nullopt}, queries, adapter); }
    EditorOpenResult openPreview(ColonyId id, PreviewId source) {
        return editor.open({ref(id), source}, queries, adapter);
    }
    ProcessingPolicy policy(ColonyId id) const {
        const auto& rows = service.state().colonies;
        const auto it = std::find_if(rows.begin(), rows.end(), [id](const Colony& colony) { return colony.id == id; });
        require(it != rows.end(), "fixture colony still exists");
        return it->processingPolicy;
    }
    auto gameFingerprint() const {
        const auto& world = service.state();
        return std::tuple{world.date.day, world.eventLog.size(), world.ids.nextEventId,
                          policy(a()), policy(b()), policy(c())};
    }
};

void source_validation_and_fixed_target() {
    Fixture f;
    require(f.openMain(f.a()).outcome == EditorOpenOutcome::Opened, "main A opens editor");
    const auto id = f.editor.current()->id;
    require(id.world == f.adapter.world() && id.value == 1, "first EditorId is world stamped and monotonic");
    require(f.editor.current()->target == f.ref(f.a()) && !f.editor.assess(f.queries, f.adapter).dirty,
            "new editor fixes A and starts clean despite usable default Manual weights");
    require(f.editor.setDraftPolicy(id, ProcessingPolicy::FuelFocus), "change draft policy only");
    const auto draft = f.editor.current()->draft;
    const auto worldBefore = f.gameFingerprint();

    f.select(f.b());
    const auto temporary = f.adapter.inspect(f.ref(f.b()));
    require(temporary && f.adapter.inspect(f.ref(f.c())) == temporary,
            "B to C retargets independent temporary preview");
    f.select(f.c());
    require(f.editor.current()->id == id && f.editor.current()->target.object == ObjectTarget{f.a()}
            && f.editor.current()->draft.policy == draft.policy && f.gameFingerprint() == worldBefore,
            "main selection, inspection, and Go To style selection cannot retarget or apply A draft");

    require(f.editor.open({f.ref(f.a()), std::nullopt}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "delayed main-A Configure rejects after selection changed");
    const auto pin = f.adapter.inspect(f.ref(f.a()));
    require(pin && f.adapter.pin(*pin), "pin A as independent Configure source");
    require(f.openPreview(f.a(), *pin).outcome == EditorOpenOutcome::ExistingFocused,
            "same-target Configure retains editor and draft");
    require(f.editor.current()->id == id && f.editor.current()->draft.policy == ProcessingPolicy::FuelFocus
            && f.editor.current()->focusRequested, "same-target Configure requests focus once");
    require(f.editor.open({f.ref(f.c()), temporary}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "old temporary source cannot authorize C after it retargeted to A");
    const auto other = f.adapter.inspect(f.ref(f.c()));
    require(other && f.openPreview(f.c(), *other).outcome == EditorOpenOutcome::OtherEditorActive,
            "different target cannot replace even a clean or dirty existing editor");
    contains(f.editor.current()->statusMessage, "Finish or discard");
    require(f.editor.current()->id == id && f.editor.current()->target.object == ObjectTarget{f.a()},
            "different-target request preserves A identity");
}

void source_rejections_and_id_lifecycle() {
    Fixture f;
    const auto currentWorld = f.adapter.world();
    const auto a = f.ref(f.a());
    require(f.editor.open({a, std::nullopt}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "Configure without a main selection rejects");
    require(f.adapter.select({currentWorld, BodyId{2}}), "select Body for non-Colony rejection");
    require(f.editor.open({{currentWorld, BodyId{2}}, std::nullopt}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "Body Configure rejects");
    require(f.editor.open({{currentWorld, ColonyId{999999}}, std::nullopt}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "missing Colony Configure rejects");
    require(f.editor.open({{WorldGeneration{0}, f.a()}, std::nullopt}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "old-world Configure rejects");
    const auto preview = f.adapter.inspect(a);
    require(preview && f.adapter.closePreview(*preview), "close source preview before dispatch");
    require(f.editor.open({a, *preview}, f.queries, f.adapter).outcome == EditorOpenOutcome::SourceRejected,
            "closed preview Configure rejects");
    require(!f.editor.current(), "all rejected opens leave editor absent");

    require(f.openMain(f.a()).accepted(), "valid A opens");
    const auto first = f.editor.current()->id;
    require(f.editor.requestCancel(first) == EditorCloseOutcome::Closed, "clean Cancel closes immediately");
    require(!f.editor.setDraftPolicy(first, ProcessingPolicy::Manual)
            && f.editor.requestCancel(first) == EditorCloseOutcome::Rejected,
            "delayed closed-editor actions reject");
    require(f.openMain(f.b()).accepted(), "B opens after A closes");
    require(f.editor.current()->id != first && f.editor.current()->id.value > first.value,
            "new editor identity is never recycled");
}

void draft_cancel_and_manual_apply() {
    Fixture f;
    require(f.openMain(f.a()).accepted(), "open A");
    const auto id = f.editor.current()->id;
    const auto before = f.gameFingerprint();
    require(f.editor.apply(id, f.queries, f.service, f.adapter).outcome == EditorApplyOutcome::NoChange,
            "no-op draft does not execute command");
    require(f.editor.setDraftPolicy(id, ProcessingPolicy::Manual), "switch to Manual draft");
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        require(f.editor.setManualWeight(id, static_cast<ProcessedMaterial>(i), 0.0), "zero Manual weight");
    }
    require(f.editor.assess(f.queries, f.adapter).dirty && !f.editor.assess(f.queries, f.adapter).canApply,
            "zero-total Manual draft is dirty but not applicable");
    require(f.editor.apply(id, f.queries, f.service, f.adapter).outcome == EditorApplyOutcome::InvalidDraft,
            "zero-total Manual rejects before command");
    require(!f.editor.setManualWeight(id, ProcessedMaterial::Propellant, -1.0)
            && !f.editor.setManualWeight(id, ProcessedMaterial::Propellant, std::numeric_limits<double>::infinity()),
            "negative and nonfinite editor inputs reject");
    require(f.editor.normalizeManualWeights(id), "Normalize makes a usable Manual draft");
    require(f.editor.requestCancel(id) == EditorCloseOutcome::ConfirmationRequired,
            "dirty Cancel enters confirmation without command");
    require(f.editor.current()->discardConfirmation && f.editor.keepEditing(id)
            && !f.editor.current()->discardConfirmation, "Keep editing retains draft and target");
    require(f.editor.requestCancel(id) == EditorCloseOutcome::ConfirmationRequired
            && f.editor.discard(id) && !f.editor.current(), "Discard closes dirty editor without command");
    require(f.gameFingerprint() == before, "draft and discard never mutate A/B/C or append events");

    require(f.openMain(f.a()).accepted(), "reopen A for Manual Apply");
    const auto manualId = f.editor.current()->id;
    require(f.editor.setDraftPolicy(manualId, ProcessingPolicy::Manual), "Manual policy draft");
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        require(f.editor.setManualWeight(manualId, static_cast<ProcessedMaterial>(i), 0.0), "clear weight");
    }
    require(f.editor.setManualWeight(manualId, ProcessedMaterial::Propellant, 3.0)
            && f.editor.setManualWeight(manualId, ProcessedMaterial::ReactorFuel, 1.0),
            "set two positive relative weights");
    const auto rows = processingAllocationsForDraft(f.editor.current()->draft);
    require(rows.size() == 2 && rows.at(0).material == ProcessedMaterial::Propellant
            && rows.at(1).material == ProcessedMaterial::ReactorFuel
            && rows.at(0).weight == 3.0 && rows.at(1).weight == 1.0,
            "command rows omit zeros and follow ProcessedMaterial order");
    f.select(f.b());
    const auto bBefore = f.policy(f.b());
    const auto cBefore = f.policy(f.c());
    const auto pin = f.adapter.inspect(f.ref(f.a()));
    require(pin && f.adapter.pin(*pin), "retain pinned A preview before Apply");
    const auto previews = f.adapter.previewSnapshot();
    require(f.editor.apply(manualId, f.queries, f.service, f.adapter).applied(),
            "authoritative Manual command changes original A");
    require(!f.editor.current() && f.policy(f.a()) == ProcessingPolicy::Manual
            && f.policy(f.b()) == bBefore && f.policy(f.c()) == cBefore,
            "Apply changes only the editor's original Colony A");
    require(f.adapter.mainSelection().isColonySelected(f.b())
            && f.adapter.previewSnapshot() == previews, "Apply preserves main selection and pin");
    const auto& applied = f.service.state().colonies.at(1).manualProcessingAllocations;
    require(applied.size() == 2 && applied.at(0).material == ProcessedMaterial::Propellant
            && applied.at(1).material == ProcessedMaterial::ReactorFuel,
            "simulation stores deterministic Manual relative weights");
}

void tiny_positive_weights_remain_distinct() {
    Fixture f;
    require(f.openMain(f.a()).accepted(), "open A for small positive Manual weights");
    const auto first = f.editor.current()->id;
    require(f.editor.setDraftPolicy(first, ProcessingPolicy::Manual), "select Manual");
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        require(f.editor.setManualWeight(first, static_cast<ProcessedMaterial>(i), 0.0), "clear Manual row");
    }
    require(f.editor.setManualWeight(first, ProcessedMaterial::StructuralAlloys, 0.6e-9) &&
            f.editor.setManualWeight(first, ProcessedMaterial::Electronics, 0.6e-9),
            "set two individually small weights with a valid combined total");
    const auto rows = processingAllocationsForDraft(f.editor.current()->draft);
    require(rows.size() == 2 && rows.at(0).weight == 0.6e-9 && rows.at(1).weight == 0.6e-9,
            "editor conversion retains positive weights below individual epsilon");
    require(f.editor.assess(f.queries, f.adapter).canApply &&
            f.editor.apply(first, f.queries, f.service, f.adapter).applied(),
            "small positive Manual configuration reaches the authoritative command");

    require(f.openMain(f.a()).accepted(), "reopen A on applied small Manual weights");
    const auto second = f.editor.current()->id;
    require(f.editor.setManualWeight(second, ProcessedMaterial::Electronics, 0.7e-9),
            "make a small but distinct draft edit");
    require(f.editor.assess(f.queries, f.adapter).dirty &&
            f.editor.assess(f.queries, f.adapter).canApply,
            "configuration identity does not use the allocation-validity epsilon");
    require(f.service.execute(SetColonyProcessingPolicyCommand{
        .colonyId = f.a(), .policy = ProcessingPolicy::Manual,
        .manualAllocations = {{ProcessedMaterial::StructuralAlloys, 0.6e-9},
                              {ProcessedMaterial::Electronics, 0.8e-9}}
    }).ok, "another real command changes A by less than epsilon");
    require(f.editor.assess(f.queries, f.adapter).stale &&
            f.editor.apply(second, f.queries, f.service, f.adapter).outcome == EditorApplyOutcome::StaleBasis,
            "small competing edit rejects stale Apply and retains the draft");
}

void stale_basis_time_and_revalidation() {
    Fixture f;
    require(f.openMain(f.a()).accepted(), "open A");
    const auto id = f.editor.current()->id;
    require(f.editor.setDraftPolicy(id, ProcessingPolicy::FuelFocus), "make A draft dirty");
    const auto draft = f.editor.current()->draft;
    require(f.service.execute(SetColonyProcessingPolicyCommand{
        .colonyId = f.a(), .policy = ProcessingPolicy::ElectronicsFocus, .manualAllocations = {}
    }).ok, "outside command changes A configuration");
    const auto eventsBefore = f.service.state().eventLog.size();
    const auto stale = f.editor.assess(f.queries, f.adapter);
    require(stale.stale && !stale.canApply && stale.current
            && stale.current->processingPolicy == ProcessingPolicy::ElectronicsFocus,
            "operation-specific basis detects real configuration change");
    require(f.editor.apply(id, f.queries, f.service, f.adapter).outcome == EditorApplyOutcome::StaleBasis
            && f.service.state().eventLog.size() == eventsBefore,
            "stale Apply rejects before a second command or event");
    require(f.editor.current()->draft.policy == draft.policy && f.editor.current()->id == id,
            "stale rejection retains draft and identity");
    require(f.editor.reviewLatest(id, f.queries, f.adapter), "explicit review refreshes basis only");
    require(f.editor.current()->basis.appliedPolicy == ProcessingPolicy::ElectronicsFocus
            && f.editor.current()->draft.policy == ProcessingPolicy::FuelFocus
            && !f.editor.assess(f.queries, f.adapter).stale,
            "review preserves draft and performs no command");
    require(f.editor.apply(id, f.queries, f.service, f.adapter).applied()
            && f.policy(f.a()) == ProcessingPolicy::FuelFocus,
            "Apply succeeds against reviewed latest state");

    require(f.openMain(f.a()).accepted(), "open A again for time changes");
    const auto timeId = f.editor.current()->id;
    require(f.editor.setDraftPolicy(timeId, ProcessingPolicy::StockpileRecovery), "draft live recovery policy");
    const auto before = f.editor.assess(f.queries, f.adapter);
    require(before.current && before.preview && before.preview->valid, "live recovery preview exists");
    for (const int days : {1, 5, 30}) {
        require(f.service.execute(AdvanceDaysCommand{.days = days}).ok, "advance simulation while editor dirty");
    }
    const auto after = f.editor.assess(f.queries, f.adapter);
    require(!after.stale && after.dirty && after.current && after.preview && after.preview->valid
            && f.editor.current()->id == timeId
            && f.editor.current()->draft.policy == ProcessingPolicy::StockpileRecovery,
            "time and stockpile movement do not make configuration basis stale or replace draft");
    require(before.current->totalRawStockpile != after.current->totalRawStockpile ||
            before.current->totalProcessedStockpile != after.current->totalProcessedStockpile,
            "live current values update while draft remains fixed");
}

struct TempSave {
    std::filesystem::path path;
    TempSave() {
        path = std::filesystem::temp_directory_path() /
            ("deep-signal-3g-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    }
    ~TempSave() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};

void lifecycle_and_missing_target() {
    Fixture f;
    require(f.openMain(f.a()).accepted(), "open A for lifecycle");
    const auto id = f.editor.current()->id;
    require(f.editor.setDraftPolicy(id, ProcessingPolicy::FuelFocus), "dirty lifecycle draft");
    const auto draft = f.editor.current()->draft;
    require(!f.adapter.loadGame({}).ok && f.editor.current()->id == id
            && f.editor.current()->draft.policy == draft.policy,
            "failed Load keeps fixed target and dirty draft");
    TempSave save;
    require(f.service.saveGame(save.path).ok, "Save does not reset editor");
    require(f.editor.current()->id == id, "Save preserves draft");
    require(f.adapter.newGame().ok && !f.editor.current(), "successful New Game clears editor synchronously");
    require(!f.editor.setDraftPolicy(id, ProcessingPolicy::Manual)
            && f.editor.apply(id, f.queries, f.service, f.adapter).outcome == EditorApplyOutcome::InvalidEditor,
            "old EditorId actions reject after world replacement");
    require(f.openMain(f.service.state().colonies.at(1).id).accepted()
            && f.editor.current()->id.world != id.world && f.editor.current()->id.value > id.value,
            "reused numeric ColonyId gets a new editor identity");
    const auto loadedId = f.editor.current()->id;
    require(f.adapter.loadGame(save.path).ok && !f.editor.current(), "successful Load clears editor synchronously");
    require(!f.editor.setDraftPolicy(loadedId, ProcessingPolicy::Balanced), "old loaded-world action rejects");

    Fixture missing;
    const ColonyId target = missing.c();
    require(missing.openMain(target).accepted(), "open removable test Colony C");
    const auto missingId = missing.editor.current()->id;
    require(missing.editor.setDraftPolicy(missingId, ProcessingPolicy::Manual), "dirty missing-target draft");
    const auto retained = missing.editor.current()->draft;
    auto changed = missing.service.state();
    std::erase_if(changed.colonies, [target](const Colony& colony) { return colony.id == target; });
    missing.service = SimulationService{std::move(changed)};
    const auto assessment = missing.editor.assess(missing.queries, missing.adapter);
    require(!assessment.current && !assessment.canApply && assessment.reason == "Original colony unavailable."
            && missing.editor.current()->target.object == ObjectTarget{target}
            && missing.editor.current()->draft.policy == retained.policy,
            "missing original target stays fixed and unavailable without fallback");
    require(missing.editor.apply(missingId, missing.queries, missing.service, missing.adapter).outcome == EditorApplyOutcome::UnavailableTarget,
            "missing target rejects Apply");
    require(missing.editor.requestCancel(missingId) == EditorCloseOutcome::ConfirmationRequired
            && missing.editor.discard(missingId), "missing-target dirty draft remains discardable");
}

struct ImGuiFixture {
    ImGuiFixture() {
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
        io.DisplaySize={1280.0F,900.0F}; io.DeltaTime=1.0F/60.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){capturedText=text;};
    }
    ~ImGuiFixture(){ImGui::DestroyContext();}
    std::string render(Fixture& f) {
        capturedText.clear(); ImGui::NewFrame(); ImGui::LogToClipboard();
        f.editor.render(f.queries,f.service,f.adapter,{0.0F,20.0F,920.0F,850.0F});
        ImGui::LogFinish(); ImGui::Render(); return capturedText;
    }
};

void native_editor_text_and_identity() {
    Fixture f;
    ImGuiFixture imgui;
    require(f.openMain(f.a()).accepted(), "open literal-name A for renderer test");
    const auto id=f.editor.current()->id;
    const auto name="ColonyProcessingEditor_W"+std::to_string(id.world.value)+"_E"+std::to_string(id.value);
    auto text=imgui.render(f);
    contains(text,"A ##literal ###stable");
    contains(text,"CURRENT STATE"); contains(text,"DRAFT POLICY");
    contains(text,"Cancel"); contains(text,"Apply policy");
    require(std::string{ImGui::SaveIniSettingsToMemory()}.find(name)==std::string::npos,
            "session editor identity writes no saved settings");
    require(f.editor.setDraftPolicy(id,ProcessingPolicy::FuelFocus),"dirty renderer draft");
    text=imgui.render(f); contains(text,"Fuel Focus");
    require(f.service.execute(SetColonyProcessingPolicyCommand{
        .colonyId=f.a(),.policy=ProcessingPolicy::ElectronicsFocus,.manualAllocations={}
    }).ok,"make rendered basis stale");
    text=imgui.render(f); contains(text,"Configuration changed since this editor opened.");
    contains(text,"Review latest state and retain draft");
}

void editor_docks_and_survives_layout_rebuild() {
    Fixture f;
    ImGuiFixture imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    require(f.openMain(f.a()).accepted(), "open editor for docking");
    const auto id = f.editor.current()->id;
    require(f.editor.setDraftPolicy(id, ProcessingPolicy::FuelFocus), "retain draft across reset");
    constexpr ImGuiID dock = 0x91AB4D23;
    const std::string name = "Configure processing###ColonyProcessingEditor_W" +
        std::to_string(id.world.value) + "_E" + std::to_string(id.value);
    const auto renderDocked = [&](const bool reset = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 20.0F});
        ImGui::SetNextWindowSize({920.0F, 850.0F});
        ImGui::Begin("##EditorTestHost", nullptr, ImGuiWindowFlags_NoDocking);
        if (reset) {
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderDockWindow(name.c_str(), dock);
            ImGui::DockBuilderFinish(dock);
        }
        ImGui::DockSpace(dock);
        ImGui::End();
        f.editor.render(f.queries, f.service, f.adapter, {0.0F, 20.0F, 920.0F, 850.0F}, dock);
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i) renderDocked();
    const auto* window = ImGui::FindWindowByName(name.c_str());
    require(window && window->DockId == dock, "editor occupies operational dock");
    renderDocked(true);
    renderDocked();
    window = ImGui::FindWindowByName(name.c_str());
    require(window && window->DockId == dock, "editor redocks after reset");
    require(f.editor.current() &&
            f.editor.current()->draft.policy == ProcessingPolicy::FuelFocus,
            "layout rebuild retains editor record and draft");
}

} // namespace

int main(){
 try{
  source_validation_and_fixed_target();std::cout<<"PASS source validation and fixed target\n";
  source_rejections_and_id_lifecycle();std::cout<<"PASS source rejection and EditorId lifecycle\n";
  draft_cancel_and_manual_apply();std::cout<<"PASS draft Cancel and Manual Apply\n";
  tiny_positive_weights_remain_distinct();std::cout<<"PASS small positive editor weights and stale basis\n";
  stale_basis_time_and_revalidation();std::cout<<"PASS stale basis, review, and live time\n";
  lifecycle_and_missing_target();std::cout<<"PASS world lifecycle and missing target\n";
  native_editor_text_and_identity();std::cout<<"PASS native editor text and literal identity\n";
  editor_docks_and_survives_layout_rebuild();std::cout<<"PASS editor docking and draft-safe layout rebuild\n";
  return EXIT_SUCCESS;
 }catch(const std::exception& error){std::cerr<<"Colony editor test failed: "<<error.what()<<'\n';return EXIT_FAILURE;}
}
