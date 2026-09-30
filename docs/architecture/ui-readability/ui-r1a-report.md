# UI-R0 / UI-R1A — native Shipyard checkpoint

## Scope and provenance

- Baseline: `1698b093a53afffdade4f2a7ae7c0494a38894ca` (integrated P6).
- Branch: `p6-ui-readability`, created from clean `master` at that exact SHA.
- Implementation commit: `da11cfe541c18aa78f6b04c4f957a6849238e382`.
- Final head: the documentation checkpoint containing this report; the exact
  pushed SHA is recorded in the implementation handback and draft PR head.
- Owner visual review: **pending**. This is the stop point before any later UI slice.
- Schema remains v18. No `src/sim` or `src/save` changes.
- `p6-ui-evidence` remains at
  `e7c7740e1b112c3ae6e4b3d60c85e0e8fe785bf7`; its original native findings and
  screenshots were not rewritten.

P6 whole-loop proving remains complete. The historical native review found no
Class A correctness defects and identified material Class B presentation issues.
This separate patch establishes a visual foundation and addresses Shipyard
orientation/readability and UI-005; it does not recast the original review as a pass.

## Implementation

### Visual foundation

`UiTheme` centralizes the handoff palette: near-black background, charcoal panels,
steel-gray structure, cool primary text, silver secondary text, cyan selection,
amber waiting, green completion, and coral command rejection. The UI uses a
16 px base font, 24 px screen titles, 18 px section titles, and 22 px metrics.
Geometry is square with restrained one-pixel control rounding. Status badges
have a neutral fill and a meaningful border/text color.

`PresentationWidgets` supplies screen/section titles, status badges, key/value
rows, summary metrics, and progress meters. It contains only ImGui and standard
C++ presentation code. It owns no simulation state or queries. Text remains
available to ordinary ImGui submission logging.

The semantic theme applies globally; only Shipyard receives a hierarchy change.
The operational-window helper accepts an optional initial size so Shipyard can
request a 1420 x 900 region while other panels retain their existing defaults.
Existing saved window sizes and the shell's confinement rules remain in effect.

### Shipyard hierarchy

Orders is the initial tab and the first view after successful New/Load. It shows
typed-state summary counts, seven strategic columns, and selected-order detail.
The detail separates status, current condition, hull progress, BP work, ETA,
remaining materials, yard capacity, and an expandable full explanation.
Developed-component waits also expose their supply explanation.

Selection is UI-local and typed. It stays on a surviving selected order,
otherwise chooses the first non-completed row, then the first completed row,
then nothing. Each row has a full-width input target, a cyan-slate selected
surface, and one cyan edge. Wrapped text contributes to the row's input height.

The existing revision editor is under New Revision. Its component, evaluation,
and command behavior is preserved. A collapsed New build order section follows
the commitments and sends quantity 1 through the existing command. Feedback is
empty until an attempted command and then appears with that action.

### UI-005 and app projection

The former backlog formula always added the first hull's remaining BP even when
the requested quantity was complete. Developed-component supply planning could
also fabricate a next-hull requirement after completion.

The app forecast now recognizes completed commitments before any supply planning.
Completed rows expose zero hulls, BP, and materials outstanding; no material or
component blocker; ETA 0; and typed Completed state. The legacy order query also
avoids planning a hypothetical next hull. Physical order state is unchanged.

`ProductionBacklogState` and `primaryCondition` give the UI a concise condition
without parsing prose. Existing design/FIFO/component/capacity/material facts
determine that projection. `currentHullBuildPoints` carries the actual planned
or frozen first-hull requirement, including earned prototype credits, for the
detail meter. The full existing explanation remains available.

Existing capacity-only ETA semantics and full-outstanding-order material-risk
semantics are preserved. The UI does not add a scheduler, material reservations,
or new authoritative waiting state.

## Changed files

| Area | Files | Purpose |
|---|---|---|
| Theme | `src/ui_imgui/UiTheme.h/.cpp`, `ImGuiApp.cpp` | Semantic palette, typography, and central application |
| Presentation | `src/ui_imgui/PresentationWidgets.h/.cpp` | Small reusable visual primitives |
| Shipyard | `src/ui_imgui/ShipyardPanel.h/.cpp` | Orders hierarchy, selection/detail, editor tab, command feedback |
| Initial region | `src/ui_imgui/OperationalWindow.h/.cpp` | Optional initial dimensions, unchanged defaults elsewhere |
| App projection | `src/app/ForecastService.h/.cpp`, `SimulationQueries.h/.cpp` | Completed-demand correction and display-ready state/condition/work |
| Tests/build | `tests/forecast_tests.cpp`, `tests/app_query_tests.cpp`, `tests/shipyard_ui_tests.cpp`, `CMakeLists.txt` | Forecast/query regressions and actual ImGui submission/input target |
| Evidence | This report and `screenshots/ui-r1a-*.png` | Native checkpoint for owner review |

## Automated verification

Before edits, the existing builds were built and complete suites ran sequentially:

```sh
cmake --build build-p6 --parallel 2
cmake --build build-p6-ui --parallel 2
ctest --test-dir build-p6 --output-on-failure -j 2
ctest --test-dir build-p6-ui --output-on-failure -j 2
```

- Baseline headless: **56/56**, 36.46 s.
- Baseline UI-enabled: **67/67**, 36.52 s.

Final configurations/builds and full suites use the README's optional-target
configurations with local dependency paths:

```sh
cmake -S . -B build-p6 -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p6 --parallel 2
ctest --test-dir build-p6 --output-on-failure -j 2

cmake -S . -B build-p6-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p6-ui --parallel 2
ctest --test-dir build-p6-ui --output-on-failure -j 2
git diff --check
```

- Final headless: **56/56**, 36.73 s.
- Final UI-enabled: **68/68**, 36.57 s (includes the new Shipyard target).
- No compiler warnings in the project build logs. Working-tree, staged, and
  baseline-to-implementation `git diff --check` passed.

The initial full UI run also passed 68/68 (36.61 s). Native inspection then found
the selection edge clipped at the first cell boundary. Its placement and the
compact empty ETA label were corrected, the complete UI build was repeated, and
the complete UI suite passed again as recorded above. All committed captures use
that final implementation. Headless code was unchanged by this presentation tune.

The suites ran sequentially because existing persistence tests share fixed
temporary SQLite paths. Build configuration reports the existing EventJson
fallback parser because `nlohmann/json.hpp` is absent; this is not a new warning.

Added coverage proves ordinary and earned developed-component completion has
zero demand, including no phantom next-hull supply plan; active component waits
remain honest; concise conditions cover all requested precedence branches; and
the first-hull BP projection includes prototype credit.

The new UI target has four scenarios at 1920 x 1080 with ini persistence disabled:
default Orders / mouse-driven New Revision navigation; persisted row selection
and no gameplay mutation; failed Load versus successful world replacement; and
clicking the lower part of a long wrapped class row. It uses real ImGui code and
public mouse IO. Read-only test access to pinned ImGui geometry locates controls.
These tests establish submission/input behavior, not native visual usability.

## Native checkpoint

Environment: Linux Mint 22.1, Cinnamon/X11 display `:0`, 1920 x 1080 at 60 Hz,
text scaling 1.0. The actual SDL3/ImGui application ran on the native desktop;
the window manager placed it on a full 1920 x 1080 canvas. A temporary working
directory isolated its ImGui ini file from the normal repository session.

Captured originals, with no cropping or rescaling:

| Screenshot | Observed state |
|---|---|
| [Orders — waiting](screenshots/ui-r1a-orders-waiting-1920x1080.png) | H1, day 0; one standing Survey Cutter order; Waiting, No shipyard capacity, No ETA; selected detail visible without scrolling |
| [Orders — expanded detail](screenshots/ui-r1a-orders-detail-1920x1080.png) | Same H1 order after the native +5 action; day 5, still 0/1 hulls and 0/500 BP; full explanation expanded; window made taller with the normal resize grip |
| [Orders — completed and current work](screenshots/ui-r1a-orders-completed-1920x1080.png) | H2 plus two real Build one commands and one native simulation day; 1 Building, 1 Waiting, 3 Complete; completed Precision order selected with 2/2 hulls, 0 BP, no remaining materials, ETA 0 d |
| [New Revision](screenshots/ui-r1a-new-revision-1920x1080.png) | Actual tab click exposes the preserved editor and component catalog; it does not precede Orders |

The saves came from the existing P6 native review pack:
`/tmp/deep-signal-p6-ui-evidence-f0f1167/01-waiting-intent.sqlite` and
`02-design-choice.sqlite`. They were loaded through the Save/Load panel. No save
overwrote either fixture. Before/after SHA-256 values were identical:

```text
01: f251b7eca8fbf7092bef9a40ec7adc785e697637f043f868507687ad580ec81a
02: d3863bbf234f56e0f1fcc26c811bcfd97d58e0e735bea7bfddbb2488c64c8f98
```

The native session also observed accepted build feedback beside the action and
successful Load resetting New Revision to Orders. All four PNG headers were
checked as 1920 x 1080. The disposable review application closed normally with
exit 0 and no runtime diagnostic output.

### Acceptance observations

These are the implementation agent's observations; owner visual approval remains
pending.

| Question | Native observation |
|---|---|
| Standing order immediately visible? | Yes. Orders opens with the row, counts, and selected detail above any authoring UI. |
| Waiting / Building / Completed distinguishable? | Yes. The mixed frame shows amber Waiting, neutral Building, and green Completed with explicit labels. |
| Cause readable? | Yes. No shipyard capacity fits on one line; Queued behind earlier work uses two short lines. Full prose lives in detail. |
| Selected identity obvious? | Yes. Cyan-slate row, visible cyan edge, class title, role/colony, and muted typed IDs agree. |
| Detail separates status, progress, materials, condition, explanation? | Yes. Labeled sections, numeric alignment, a BP meter, and an expandable explanation provide separate reading positions. |
| Cyan mainly selection/action? | Yes. Selected row/tab, the edge, and expandable action/detail headers carry the accent; body text and structural lines stay neutral. |
| Status color sparse and meaningful? | Yes. Amber/green are concentrated in state badges and actual action feedback. No severe rejection occurred, so coral was not exercised natively. |
| Less like a raw debug panel? | In the agent's judgment, the operational hierarchy and structured detail are a substantial improvement. Overall aesthetic approval is the owner's checkpoint. |
| Editor reachable without dominating Orders? | Yes, by a direct New Revision tab click. The preserved editor still scrolls and remains dense. |
| Completed order has zero outstanding demand? | Yes. The actual completed developed-component order shows 2/2 hulls, 0 BP remaining, no materials remaining, ETA 0 d, and no component-supply wait. |

## Limits and stop

- Owner approval of this visual language is pending.
- The existing shell and fixed information sidebar remain. Saved small Shipyard
  geometry may require user resizing; this slice does not reset personal layouts.
  Shipyard selection is local to this panel; the existing global information
  sidebar is not a Shipyard selection mirror. Other panels' visual usability was
  not re-audited as part of this bounded checkpoint.
- The preserved editor remains a long, dense authoring view. Its redesign and
  design comparison belong to later approved work.
- No Compare Designs, workspace layout, Operational Summary, Sites, Evidence,
  Attention, program-panel, or report redesign was started.
- No simulation, persistence, transit, accounting, production, or P5 technical
  development rule changed. No schema or new persisted UI state was introduced.
- No merge performed. Stop here for owner review of the native screenshots.
