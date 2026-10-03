# Deep Signal

Deep Signal is a C++20 home-system operations prototype with a deterministic
simulation, SQLite saves, a CLI, and an optional desktop UI. For the current
stage and review boundary, see [Project state](docs/PROJECT_STATE.md). The
[simulation state contract](docs/architecture/simulation-state-contract.md)
records accepted gameplay and persistence behavior.

## What is included

- C++20 simulation core with no UI dependencies
- Deterministic Sol/Terra/Mars starter scenario
- Command-based mutation API
- Authoritative `GameState` invariant validation at trust boundaries
- Typed event log with isolated event JSON serialization
- Daily mining
- Shipyard build order progression
- Immutable component-based ship designs and derived build/survey capability
- Persistent home-supported survey programs with finite teams, real refueling,
  timed visits, reports, and interruption-aware time advancement
- Delegated colony/site freight with typed raw or processed cargo, powered handling,
  explicit colony operating bases, delivery/collection cycles and safe cargo settlement
- Survey-instrument duty and colony-supported tender maintenance with compatible
  powered workshops, finite engineering teams, actual parts and daily work
- Ship and fleet creation
- Prototype sustained-burn fleet movement; loaded cargo does not change its fuel/time model
- P4A immutable observations, staffed laboratory analysis, dated assessments, and knowledge-limited geology views
- P4B unrestricted resource-site investment: real deliveries, qualified field builders,
  finite assembly/commissioning, paid operating support, raw Ice collection and colony processing
- P5 optional Precision Characterization Array development with finite engineering,
  a physical local prototype, acquired test evidence, colony-local serial process,
  and separate real-team support qualification
- SQLite schema v18 save/load layer; older development saves are unsupported
- Full-save/full-load transactions
- Prepared statements for value-bearing SQL
- CLI smoke runner
- Minimal self-contained tests using CTest
- Optional SDL3/Dear ImGui desktop shell, isolated behind `DEEP_SIGNAL_BUILD_UI`
- Direct `GameStateValidation` regression tests
- Save/load round-trip regression test
- Malformed-save rejection tests for schema singleton, enum, range, stale counter, metadata, event chronology, shipyard lifecycle, fleet-order, foreign-key, and event-payload failures

## Architecture status

For an earned P5 inspection save, run:

```sh
./build-p5/deep_signal_cli --write-technical-development-fixture /tmp/deep-signal-p5.sqlite
```

Load it and open **Technical Development**, **Shipyard / Production**,
**Maintenance / Support Programs**, and **Evidence / Analysis**. The exported
world begins with the established catalog and earns the Precision component
through real concept, fabrication, and three test workdays. One local prototype
is integrated without charging its embodied component cost/BP twice; a later
hull uses Terra's separately qualified serial process. Technical target and
measured result remain distinct. The exact engineering team also completes the
separate specialist support work before normal workshop/material service.

The [P5 implementation report](docs/architecture/p5-implementation-report.md)
maps all acceptance scenarios to executable evidence and records residual scope.

For an earned P4B inspection save (actual shipyard builds, cold-site deliveries,
field construction, supported extraction, and a standing Ice collection route):

```sh
./build-p4b/deep_signal_cli --write-site-development-fixture /tmp/deep-signal-p4b.sqlite
./build-p4b/deep_signal_cli --write-site-development-zero-fixture /tmp/deep-signal-p4b-zero.sqlite
```

Open **Sites / Development** and **Freight / Supply Programs** after
loading. Registration gives a site no stock or capacity, and does not require
scientific approval. Sealed processed packages can be delivered to the cold site
using ship handling. Raw transfers require commissioned storage and supported
site handling. A collection fleet fuels at its colony destination/base, travels
empty to the source, and brings real cargo home; it cannot use remote stock or
cargo to refill its engine tanks.

Both inspection saves advance through day 90 with explicit acknowledgments of
reported issues. The zero-result variant still builds and pays operating costs;
it records unsuccessful attempts without declaring a measured reserve or a
formal scientific assessment. The [P4B implementation report](docs/architecture/p4b-implementation-report.md)
records verification, the acceptance matrix, and scope limits.

For the P3C inspection fixture, which starts with worn instruments and empty
service parts bins supplied by two real single-material freight programs:

```sh
./build-p3c/deep_signal_cli --write-maintenance-fixture /tmp/deep-signal-p3c.sqlite
```

Load it and open **Maintenance / Support Programs**, **Survey Programs** and
**Freight / Supply Programs**. Selected support holds the client while a full
service job is active. Removing support preserves actual remaining duty; it
does not heal the instruments. Immediate manual survey requires five duty units.
The [P3C report](docs/architecture/p3c-implementation-report.md) maps acceptance
requirements to implementation and test evidence.

For a current-format UI inspection save of the P3B proof loop, run:

```sh
./build-p3b/deep_signal_cli --write-freight-fixture /tmp/deep-signal-p3b.sqlite
```

Load that file in the desktop shell. It has a 500-unit Propellant delivery
program, a reference freighter, and a separately authorized receiving survey
fleet/team waiting for real supply. Open **Freight / Supply Programs** to inspect
the repeated trips, physical manifests and settlement controls. Cargo uses
normalized units; the prototype transit model ignores payload mass.

The project has three active CMake libraries by default:

```text
deep_signal_sim   # pure deterministic simulation; no SQLite/UI/platform deps
deep_signal_save  # SQLite C API repository and schema v18 mapping
deep_signal_app   # application service wrapping simulation plus save/load
```

The simulation library now owns domain validation through `src/sim/GameStateValidation.*`. This keeps the invariant contract independent from SQLite and lets every external state boundary share the same checks:

- `Simulation(GameState)` validates caller-provided snapshots.
- `SaveGameRepository::load()` validates fully assembled save graphs.
- `SaveGameRepository::save()` refuses to persist invalid state.

The persistence layer is intentionally isolated under `src/save`:

- `Database.*` owns the SQLite connection, prepared statements, and transactions.
- `Schema.*` creates and validates only the active schema v18 structure.
- `SaveGameRepository.*` maps `GameState` to/from SQLite rows.
- `EventJson.*` owns event payload JSON serialization/parsing so the repository does not contain event-specific JSON grammar.

SQLite discovery is conditional. A sim-only build can configure without SQLite installed:

```bash
cmake -S . -B build-sim-only -G Ninja -DDEEP_SIGNAL_BUILD_SAVE=OFF -DDEEP_SIGNAL_BUILD_APP=OFF
cmake --build build-sim-only
ctest --test-dir build-sim-only --output-on-failure
```

## Coding standard status

This revision applies the uploaded code-commenting standard across the Phase 1 source tree:

- non-trivial files have file-purpose comments,
- public APIs and important data structures are documented inline,
- non-obvious event flow, tick order, ownership, persistence, validation, and prototype assumptions are commented,
- tests explain the behavior and regression risk they cover,
- temporary prototype limitations use explicit `TEMP:` comments.

The source also follows the current project C++ direction:

- strongly typed IDs instead of interchangeable integer handles,
- value-owned domain records in `GameState`,
- command-return values instead of UI-side mutation,
- const-correct accessors,
- RAII for SQLite connections, statements, and transactions,
- no raw owning pointers,
- warning-clean CMake targets with `-Wall -Wextra -Wpedantic -Wconversion` on GCC/Clang.

## SQLite schema v18 coverage

The save file persists:

- schema version,
- current simulation day,
- ID counters,
- star systems,
- bodies,
- colonies,
- colony mineral stockpiles and actual cumulative processed-material output,
- ordered site catalogs, registrations, installed groups, typed stocks, operating
  policies/receipts/reports/issues, and development packages/work/custody/history,
- mineral deposits,
- immutable ship-class revisions and component installations,
- component processed-material construction costs,
- public technical opportunities and separate hidden deterministic candidate truth,
- technical facilities, engineering qualifications, programs, paid work, tests,
  physical prototypes, local production capabilities, and support records,
- frozen current-hull developed-component supply plans and prototype integrations,
- survey teams, charters, assignments, receipts, issues, and 30/90-day reports,
- freight charters, committed per-hull manifests, actual cargo lots, transfer
  receipts, issues, closure dates, and reports,
- shipyard orders,
- fleets and active movement orders,
- ships,
- typed event log rows with JSON payload text.

The current writer uses a replace-all save strategy inside one write transaction. Loading uses one read transaction, runs `PRAGMA foreign_key_check`, parses integer metadata strictly as canonical text, validates enum ordinals, validates the fully assembled `GameState`, and rejects missing, duplicate, or unsupported schema metadata. Schema v18 includes `CHECK` constraints for core non-negative quantities, enum ranges, ID counters, production-order invariants, fleet-order and prototype-supply consistency, and program/artifact references.

## Zero-trust hardening status

The loader no longer treats SQLite constraints as sufficient. The authoritative validation pass rejects:

- stale ID counters that would allocate duplicate IDs later,
- non-canonical or non-numeric metadata such as `current_day = 'abc'`,
- duplicate or non-positive IDs,
- missing foreign references,
- negative, NaN, or infinite domain numbers,
- impossible idle/moving fleet-order combinations,
- one-way ship/fleet relationships,
- invalid event severities and invalid event payload values,
- event chronology corruption, including future-dated events and event days that move backward by event ID,
- malformed shipyard lifecycle states, including completed orders retaining build progress and active orders that are already complete.

Direct validation tests now cover invalid in-memory `GameState` snapshots such as duplicate IDs, stale counters, negative production, non-finite quantities, broken ship/fleet links, impossible fleet orders, future event history, out-of-order event IDs, and completed orders with retained build progress.

## What is intentionally not included yet

- Combat
- Generic technology trees/research points or arbitrary component invention
- General equipment cargo, prototype transport, refits, and warehouse genealogy
- Mission Control, crew/population, final propulsion, and formal P4B operating-evidence analysis
- Procedural generation
- Incremental save diffs
- Backward save-schema migrations; only active pre-release v18 is supported
- full hard dependency on nlohmann/json; `EventJson.*` uses `<nlohmann/json.hpp>` automatically when the header is available and falls back to the local strict schema-v1 parser when it is not installed

## Build

Requires CMake 3.22 or newer and a C++20 compiler. The default build also requires SQLite development headers because save/load and the app service are enabled by default. Installing `nlohmann-json3-dev` is recommended; the code will use `<nlohmann/json.hpp>` automatically when available.

### Headless full build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/deep_signal_cli
```

### Simulation-only build

Use this when you want to build and test the pure simulation layer without SQLite, app, or UI dependencies.

```bash
cmake -S . -B build-sim-only -G Ninja -DDEEP_SIGNAL_BUILD_SAVE=OFF -DDEEP_SIGNAL_BUILD_APP=OFF
cmake --build build-sim-only
ctest --test-dir build-sim-only --output-on-failure
```

### Optional SDL3 / Dear ImGui UI build

The desktop UI is isolated behind `DEEP_SIGNAL_BUILD_UI=ON`. Headless builds do not require SDL3, Dear ImGui, or ImPlot.

Before configuring the UI target, install SDL3 development files so CMake can resolve `find_package(SDL3 CONFIG REQUIRED)` and the imported target `SDL3::SDL3`. If SDL3 is installed in a non-standard prefix, pass either `-DCMAKE_PREFIX_PATH=/path/to/sdl3/install` or `-DSDL3_DIR=/path/to/lib/cmake/SDL3`.

Dear ImGui and ImPlot are pinned submodules. Initialize them in place:

```bash
git submodule update --init --recursive third_party/imgui third_party/implot
```

The pinned ImGui checkout includes the docking support used by the UI shell.
The project builds only the required core/backend source files; demo sources
are intentionally not linked.

```bash
cmake -S . -B build-ui -G Ninja -DDEEP_SIGNAL_BUILD_UI=ON
cmake --build build-ui --target deep_signal_imgui
./build-ui/deep_signal_imgui
```

If Ninja is not installed:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## P4A evidence and analysis

Open **Evidence / Analysis** from the window menu or Intelligence workspace.
Authorize one analysis program to follow an existing survey, or select acquired
batches in a fixed order. It uses a real SurveyTeam at its laboratory colony;
a deployed field team cannot also analyze at home. A second actual team permits
concurrent fieldwork and interpretation. Missing inputs, staff, laboratory
throughput or work authority preserves intent.

Reconnaissance detects normalized signal at 50 and does not measure accessibility.
Characterization detects at 10 and reports a coarse accessibility class. Each
contributing installation needs five exposure units for its full profile. One
batch requires three scientific team-workdays to analyze; only the authored home
laboratory starts with one team-workday/day. Data becomes available at D+1,
independent of distance, without moving personnel.

Raw data is inspectable before analysis. Non-detection is not absence. Reserve
quantity remains **Unmeasured** and construction/site suitability **Unassessed**.
There is no investigation gate on existing construction or other authorizations.
Mining projections use known inventory and current-session output telemetry,
not hidden reserve quantities. Older development saves, including v15, are rejected
unchanged; there is no compatibility reader.

Reproducible inspection saves, acquired through normal commands and elapsed days:

```sh
./build-p4a/deep_signal_cli --write-evidence-fixture /tmp/deep-signal-p4a-shared.sqlite
./build-p4a/deep_signal_cli --write-evidence-concurrent-fixture /tmp/deep-signal-p4a-concurrent.sqlite
```

The first fixture has a visible shared-team staffing wait; the second has a real
home analyst. Both contain an earned local assessment and transmitted field data.
See [P4A implementation report](docs/architecture/p4a-implementation-report.md)
for fixture constants, acceptance evidence, and limitations.
