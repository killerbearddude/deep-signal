# Deep Signal Phase 1

First buildable headless simulation slice for **Deep Signal Prototype 0.1 - Home System Operations**.

## What is included

- C++20 simulation core with no UI dependencies
- Deterministic Sol/Terra/Mars starter scenario
- Command-based mutation API
- Authoritative `GameState` invariant validation at trust boundaries
- Typed event log with isolated event JSON serialization
- Daily mining
- Shipyard build order progression
- Ship and fleet creation
- Fixed-duration fleet movement
- SQLite schema v2 save/load layer
- Full-save/full-load transactions
- Prepared statements for value-bearing SQL
- CLI smoke runner
- Minimal self-contained tests using CTest
- Optional SDL3/Dear ImGui desktop shell, isolated behind `DEEP_SIGNAL_BUILD_UI`
- Direct `GameStateValidation` regression tests
- Save/load round-trip regression test
- Malformed-save rejection tests for schema singleton, enum, range, stale counter, metadata, event chronology, shipyard lifecycle, fleet-order, foreign-key, and event-payload failures

## Architecture status

The project has three active CMake libraries by default:

```text
deep_signal_sim   # pure deterministic simulation; no SQLite/UI/platform deps
deep_signal_save  # SQLite C API repository and schema v2 mapping
deep_signal_app   # application service wrapping simulation plus save/load
```

The simulation library now owns domain validation through `src/sim/GameStateValidation.*`. This keeps the invariant contract independent from SQLite and lets every external state boundary share the same checks:

- `Simulation(GameState)` validates caller-provided snapshots.
- `SaveGameRepository::load()` validates fully assembled save graphs.
- `SaveGameRepository::save()` refuses to persist invalid state.

The persistence layer is intentionally isolated under `src/save`:

- `Database.*` owns the SQLite connection, prepared statements, and transactions.
- `Schema.*` creates and validates schema version 1.
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

## SQLite schema v2 coverage

The save file persists:

- schema version,
- current simulation day,
- ID counters,
- star systems,
- bodies,
- colonies,
- colony mineral stockpiles,
- mineral deposits,
- ship classes,
- ship class mineral costs,
- shipyard orders,
- fleets and active movement orders,
- ships,
- typed event log rows with JSON payload text.

Prototype 0.1 uses a replace-all save strategy inside one write transaction. Loading uses one read transaction, runs `PRAGMA foreign_key_check`, parses integer metadata strictly as canonical text, validates enum ordinals, validates the fully assembled `GameState`, and rejects missing, duplicate, or unsupported schema metadata. Schema v1 also includes `CHECK` constraints for core non-negative quantities, enum ranges, ID counters, production-order invariants, and fleet-order consistency.

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
- Sensors
- Research
- Procedural generation
- Incremental save diffs
- Schema migrations beyond v1
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

Dear ImGui and ImPlot are expected as source checkouts under `third_party/`:

```bash
# Remove placeholder documentation directories before replacing them with submodules.
rm -rf third_party/imgui third_party/implot

git submodule add -b docking https://github.com/ocornut/imgui third_party/imgui
git submodule add https://github.com/epezent/implot third_party/implot
git submodule update --init --recursive third_party/imgui third_party/implot
```

The docking branch is required because the UI shell enables ImGui docking. The project builds only the required core/backend source files; demo sources are intentionally not linked.

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
