# Working in Deep Signal with Codex

Deep Signal is a C++20 home-system operations prototype. Start with
[README.md](README.md) for build and run instructions, then inspect the relevant
code and tests. The [simulation state contract](docs/architecture/simulation-state-contract.md)
records gameplay and persistence boundaries; phase reports in
`docs/architecture/` record historical decisions and verification. Follow the
[code documentation standard](Code_Commenting_and_Documentation_Standard.md)
without copying its rules into every change.

## Architectural boundaries

- `src/sim` owns `GameState`, commands, daily advancement, domain rules, and
  validation. Keep it independent of SQLite, SDL, ImGui, and platform code.
  External state must pass the simulation's validation boundary.
- `src/save` owns SQLite schema and full-snapshot persistence. The active schema
  version is declared in `src/save/Schema.h`; inspect the schema creation files
  under `src/save` for table definitions. Older development saves are
  unsupported. Treat changes to the schema or load policy as explicit behavior
  changes with validation, persistence tests, and updated contract documentation.
- `src/app` owns the active simulation service and display-ready queries. CLI
  and UI actions should use simulation commands or the app service, not mutate
  `GameState` directly. `src/render`, `src/platform`, and `src/ui_imgui` belong
  to the optional desktop path.
- Collection order, daily phase order, physical stocks, and program ownership
  affect behavior. Inspect their rules and tests before changing them; test
  the consequences of an intended change.

## Change discipline

- Treat the current handoff as change scope, the state contract as accepted
  behavior, source code as implementation, and tests as evidence only for what
  they cover. Report conflicts instead of inferring intended behavior from code.
- Check Git status and inspect the relevant implementation, tests, and contract
  before editing. Make the smallest coherent change and preserve unrelated
  work and behavior.
- Follow existing C++20 conventions, including typed IDs, value-owned domain
  records, const-correct access, and RAII for owned resources. Add dependencies
  or abstractions only when the task establishes a concrete need.
- Add or adjust focused tests when behavior changes. Update documentation when
  documented behavior or architecture changes. Keep historical phase reports
  as records of their own baselines rather than rewriting them as current status.
- ImGui and ImPlot are pinned submodules. Initialize them with
  `git submodule update --init --recursive third_party/imgui third_party/implot`;
  do not delete and re-add them as setup.
- Do not silently synthesize missing save data, weaken validation, or claim
  compatibility with older save schemas.

## Validation and handback

- Use the CMake configurations in `README.md`. A simulation-only CTest run does
  not validate save, app, CLI, or UI targets. UI build or submission tests do
  not establish native visual usability.
- Some save tests use fixed temporary SQLite paths. Run complete headless and
  UI-enabled CTest suites sequentially within one checkout.
- Report changed files, checks actually executed and their results, assumptions,
  and unresolved issues. State when a check was not run or a dependency blocked
  it; never claim an unexecuted check passed.
