# P3B implementation report

## Baseline and delivery

- Repository: `killerbearddude/deep-signal`.
- Exact clean starting commit: `dc6959bb5fd1ae6d4b7e6bc18131556a3042de25`.
- Fetched `origin/master` matched that commit before editing.
- Branch: `p3b-delegated-freight`.
- Implementation/test commit: `bc18331bc046f651cb99224ab893d2d786195728`.
- PR title: `P3B: deliver real cargo through delegated freight programs`.
- The final commit SHA and PR URL are recorded in the implementation return and
  GitHub metadata. This report is part of the reviewed branch.
- Scope: the supplied P3B freight slice. No lifecycle, accounting, or authority
  deviations were needed. No later P3, P4, or P5 work was started.

## Behavior and model

Before P3B, ships had engine fuel but no physical cargo, and only survey programs
could own fleets. P3B adds durable finite processed-material delivery intent,
per-hull cargo, rate-limited handling, repeated trips and shared typed control.
An unready coherent charter is accepted without reserving goods or claiming a
busy fleet. Actual deliveries can supply existing shipbuilding and survey intent.

`ShipCargo` is an optional positive lot on a ship: typed freight program ID,
shipment number, material and quantity. There is no fleet cargo inventory cache.
`FreightProgram` owns charter, lease, retained task, shipment identity, ordered
manifest limits, actual transfer counters/receipts, reports, issue and closure
date. Its fixed contract is source/destination/material. Amendments expose only
name, target, requested fleet/leader and resource policies. A committed shipment
retains its original fleet, leader, revision and contract.

`ProgramController` is a variant of `SurveyProgramId` and `FreightProgramId`.
Canonical leases remain on their respective programs; fleet reverse ownership is
derived. All five manual fleet mutation paths and program departures check that
shared typed boundary. Freight uses no survey-team lease and does not relocate
an embarked team.

The CargoBay enum is appended. The existing shared ship evaluator derives cargo
capacity and installed handling alongside all P2 quantities. A carrying hull's
handling is operational only when its own generation covers its total demand.
Ship role provides no capacity or handling. Engine Propellant and cargo
Propellant remain separate accounts.

### Catalog fixture

| Quantity | Standard Cargo Bay | Reference Freighter |
|---|---:|---:|
| Dry mass | 60 | 590 |
| Used internal volume | 200 | 670 |
| Internal capacity | 0 | 1,000 |
| Power generation | 0 | 120 |
| Power demand | 20 | 60 |
| Engine tankage | 0 | 1,000 |
| Cargo capacity, normalized units | 100 | 200 |
| Handling, units/day | 25 | 50 |
| Build points | 60 | 550 |
| Structural Alloys | 40 | 330 |
| Electronics | 10 | 60 |
| Reactor Fuel | 0 | 20 |
| Industrial Composites | 20 | 90 |

The reference freighter installs the existing hull, reactor, tank and general
systems plus two bays. It has no survey array. Survey Cutter composition and
totals remain unchanged. Newly completed ships have empty cargo holds.

## Planning, transfers and timing

Planning allocates a bounded candidate to hulls in persisted roster order.
Candidates respect demand, operational storage and source stock. The search has
at most 14 candidate evaluations: maximum batch, a fuel-adjusted smaller batch
for shared Propellant, then bounded halves. Queries use the next opening date;
execution uses the current opening date. No whole-program completion ETA is
invented when future supply is unknown.

Fuel preparation includes loading duration and a separate refill day when
needed. Given outbound departure D, arrival A and per-hull maximum unloading
duration U, return departure is A + U + 1. Both legs use the existing transit
planner and adjusted commander fuel rule. Departure is rechecked against the
actual manifest and date. Remote refueling and cargo-to-tank conversion do not
exist. A smaller useful partial load can proceed when further source stock
disappears; batches do not grow indefinitely with arriving stock.

Transfers are prepared and checked before mutation. Each hull receives at most
its own handling rate for the day's load/unload action. Colony debits equal lot
credits and lot debits equal colony credits. Precision checks refuse a transfer
whose debit or credit cannot be represented; they do not discard a small lot.
The shared comparison tolerance is absolute `1e-9` plus relative `1e-10`.

The isolated 500-unit proof delivers **200, 200, 100**, with **4, 4, 2** loading
days and the same unloading counts. Independent daily conservation checks cover
source + destination + onboard cargo, and source/destination Propellant + cargo
Propellant + engine tanks + actual travel burn. Operating transfers are counted
once against lifetime allowance; later burn is separate attribution.

For shared Propellant, floors use their maximum. The 120-stock / 20-floor /
10-operating-refill case selects 90 payload. Already aboard engine fuel is not
purchased again, and payload is not charged to the operating allowance.

The opening pass stably merges stored program vectors by their next heads'
creation day, Survey first on a tie. It never sorts either vector. Existing and
newly acquired leases remain occupied for the entire opening phase. Stock
budgets begin with actual opening stock and shrink on withdrawals; unloading
does not enlarge them. Another program can use incoming supply next opening.
Later industry can consume material actually unloaded earlier that day.

The daily order is opening programs, mining, processing, shipyards, movement,
then arrival/report/issue bookkeeping. No last-load/departure, arrival/unload,
last-unload/return-departure or refill/load collapse is possible. Distinct
same-body colonies skip transit but keep separate handling days.

## Lifecycle and oversight

- Suspension retains loaded custody and paid trajectories and stops transfers
  and departures. Empty stationary assets may release; retained return work
  reacquires its actual fleet.
- Cancel future pickups returns unshipped source cargo over handling days, or
  finishes delivery of dispatched cargo at its original destination. It does
  not mandate a new empty return. An already paid empty leg finishes normally.
- Cancellation intent survives suspension/resumption; loaded cargo never becomes
  an unowned or Closed-program lot.
- Normal completion requires actual delivery, empty cargo and physical source
  return. Unfinished closing states retain amendment, suspension/resumption,
  cancellation and issue acknowledgment controls where applicable.
- Lowered targets/fuel caps preserve prior expenditure and committed shipments.
  Replacement fleets take effect after the old shipment's empty source return.
- Stable typed issues stop every advance path at complete daily boundaries.
  Acknowledgment retains limits without daily repeated interruptions.
- Global 30-day reports and 90-day markers use durable receipts/counters.
  Each report retains the floors, lifetime allowance and contingency in effect
  at publication; later amendments do not rewrite those historical limits.
  Freight reports stop after Closed; a report due on closure day is published
  once. P3A's existing post-closure reporting behavior is unchanged.

## Persistence

The active gameplay format is **v14 only**, with 33 tables. Older development
saves, including v13, are rejected read-only without source modification. Save
also refuses unsupported replacements. No compatibility reader, synthetic
component generation, migration or automatic conversion was added.

New mappings persist cargo component fields, freight counter/program vector,
shipment commitment, ordered manifest, per-hull lot, ordered transfer receipts,
ordered reports, closure and issues. `ship_cargo` references the exact active
program/shipment. Existing component/install, fleet/roster, survey, yard and event
ordering remains intact. Opening budgets and renderer/editor state are not saved.

Preflight, supported-shape checking, foreign keys, strict numeric readers,
transactional replacement/reread/commit, rollback and failed-Load world
preservation remain. New tests continue from every physical freight phase and
from suspended cargo, blocked completion return and cancellation settlement.
No existing behavioral test or historical fixture was removed.

## Files changed and responsibilities

| Files | Reason |
|---|---|
| `src/sim/FreightProgram.h`, `FreightProgramRules.*`, `FreightProgramExecution.*`, `FreightProgramValidation.*` | Freight intent/history, pure planning, bounded execution and strict graph/accounting validation |
| `src/sim/ProgramControl.*` | Typed ownership, stable mixed order, pending issues and opening stock budgets |
| `IdTypes.h`, `Domain.h`, `GameState.h`, `Commands.h`, `Events.h` | Typed freight identity, per-hull lot, durable records and command/audit envelopes |
| `ShipDesignRules.*`, `ScenarioFactory.*` | Cargo derivation, authoritative bay/reference freighter and isolated supply/survey fixture |
| `Simulation.*`, `GameStateValidation.cpp`, `SurveyProgramExecution.*` | Command admission, shared movement/control/day phases, freight validation and survey budget participation |
| `src/save/Schema.*`, `SaveGameRepository.*`, `FreightPersistence.*`, `EventJson.cpp` | v14 structure, ordered mappings, strict reconstruction and typed freight audits |
| `src/app/SimulationQueries.*` | Owned charter/manifest/history previews and common controller projections |
| `src/ui_imgui/FreightProgramsPanel.*` | Narrow authoring, advanced policies, manifests, lifecycle actions, reports and issues |
| `FleetPanel.cpp`, `FleetOrdersPanel.cpp`, `SurveyProgramsPanel.cpp`, `ShipyardPanel.cpp` | Typed control display, safe command routing and cargo design preview |
| `ImGuiApp.*`, `MainMenuBar.*` | Panel registration and reset after successful world replacement |
| `src/cli/main.cpp` | Typed event/advance output and explicit current-format fixture export |
| `CMakeLists.txt`, new freight/control tests and affected existing tests | Source/target wiring, new behavior, and narrow enum/catalog/schema/DTO adaptations |
| `README.md`, state contract and fixture README | Actual freight/persistence policy, units, phase semantics, limits and fixture instructions |

## Acceptance-to-test map

Abbreviations: **R** = `freight_program_rules_tests.cpp`; **E** =
`freight_program_execution_tests.cpp`; **X** = `freight_edge_tests.cpp`; **C** =
`program_control_tests.cpp`; **S** = `freight_save_tests.cpp`; **A** =
`freight_program_app_tests.cpp`; **U** = `freight_program_ui_tests.cpp`.

| ID | Concrete test evidence |
|---|---|
| P3B-01 | R `catalog_and_capability`; A `ownedPreviewsAndUnreadyAuthoring`; retained P2 design tests |
| P3B-02 | R `catalog_and_capability`; X `unreadyEquipmentAndBusyFleetRemainIntent` |
| P3B-03 | X `unreadyEquipmentAndBusyFleetRemainIntent`; E `admission_waiting_and_recovery` |
| P3B-04 | R `malformed_and_readiness`; E `admission_waiting_and_recovery` |
| P3B-05 | E `repeated_shipments_and_conservation` |
| P3B-06 | E `mixed_hulls_and_midload_competition`; S `test_order_and_all_materials` |
| P3B-07 | E `repeated_shipments_and_conservation`, `dispatched_cancellation_and_amendment`; C inbound-yard proof |
| P3B-08 | E `admission_waiting_and_recovery`, `mixed_hulls_and_midload_competition` |
| P3B-09 | X `twoConsumersUseOneRealStockBudget`; C `test_stable_merge_and_budget` |
| P3B-10 | R `shared_propellant_and_dated_return`; X allowance test; E Propellant conservation |
| P3B-11 | E `cargo_propellant_is_not_engine_fuel_and_issues` |
| P3B-12 | X `operatingAllowanceCountsOnlyRealAdditionalFuel` |
| P3B-13 | X `movingDestinationPricesReturnAfterUnloading`; R dated return assertions |
| P3B-14 | X `unreadyEquipmentAndBusyFleetRemainIntent`; R fuel/allowance tests |
| P3B-15 | C `test_typed_arbitration_and_manual_guards`; A typed fleet owner projection |
| P3B-16 | C stable merge, typed arbitration/release and `test_older_freight_and_mixed_bulk_equivalence` |
| P3B-17 | C `test_waiting_survey_uses_delivery_next_opening`, `test_inbound_stock_supplies_later_shipyard_phase` |
| P3B-18 | C same survey and shipyard integration proofs |
| P3B-19 | E `source_cancellation_and_suspension`; A lifecycle projection; S suspended custody |
| P3B-20 | E `dispatched_cancellation_and_amendment`; C `test_processing_supply_and_retained_empty_return` |
| P3B-21 | E `source_cancellation_and_suspension`; A actual source-return projection |
| P3B-22 | E `dispatched_cancellation_and_amendment` |
| P3B-23 | E `source_cancellation_and_suspension`; S `test_suspended_and_cancelled_custody` |
| P3B-24 | E dispatched amendment; C `test_replacement_fleet_waits_for_committed_return`; S zero-target continuation |
| P3B-25 | E actionable return issue; S `test_completion_and_cancellation_issue_round_trips` |
| P3B-26 | E issue acknowledgment; X allowance exhaustion/amendment; existing P3A issues |
| P3B-27 | A `sharedAdvanceInterruption`; retained survey app/command/execution tests; CLI common runner |
| P3B-28 | C `test_older_freight_and_mixed_bulk_equivalence`; S complete durable continuation comparisons |
| P3B-29 | E repeated loop/report cutoff; C `test_scheduled_closure_report_once`; S `test_report_limits_survive_amendment_and_load`; A historical policy snapshots |
| P3B-30 | S `test_every_physical_phase_round_trips` |
| P3B-31 | S suspended custody and completion/cancellation issue round trips |
| P3B-32 | S `test_order_and_all_materials`; retained H1B/P3A continuation |
| P3B-33 | S `test_malformed_current_snapshot_and_failed_load`, `test_report_history_corruption_rejects`; E numeric transfer tests |
| P3B-34 | Existing save-contract old-schema/source-byte checks (now including v13) and injected rollback tests |
| P3B-35 | E same-body case; C `test_processing_supply_and_retained_empty_return` with actual industry |
| P3B-36 | A all four scenarios; U `visibleIntentCustodyAndReplacement`; existing shell visibility tests |

Additional boundaries include a fractional positive shipment, refusal of
unrepresentable stock debits/credits and engine-fuel debits, and stopping before an unrepresentable
future reporting cursor would partially advance the world.

## Verification commands and results

Baseline suites ran sequentially at the pinned clean commit:

```sh
ctest --test-dir build-p3a --output-on-failure -j 2
ctest --test-dir build-p3a-ui --output-on-failure -j 2
```

Results: **19/19 headless**, then **25/25 UI-enabled**.

The machine's existing dependency paths were verified before use; these are
local invocation flags, not committed project dependencies:

```sh
cmake -S . -B build-p3b -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p3b -j 2
ctest --test-dir build-p3b --output-on-failure -j 2

cmake -S . -B build-p3b-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p3b-ui -j 2
ctest --test-dir build-p3b-ui --output-on-failure -j 2
git diff --check
git diff --cached --check
```

Final builds exited successfully. Full suites were run sequentially:

| Gate | Result |
|---|---|
| Headless CTest | **25/25 passed**, 0 failed, 3.73 seconds |
| UI-enabled CTest | **32/32 passed**, 0 failed, 4.28 seconds |
| Freight persistence executable within each suite | **7/7 scenarios**, including policy history and every physical continuation checkpoint |
| Freight application executable within each suite | **4/4 scenarios** |
| Worktree and staged diff checks | Passed |
| CLI smoke run | Exit 0; actual day 10, one commissioned ship/fleet |
| Fixture export | Exit 0; current-format `/tmp/deep-signal-p3b.sqlite` |
| Dummy-renderer startup | No error output; deliberately stopped after 3 seconds (exit 124) |

The startup command, run from `/tmp`, was:

```sh
timeout 3s env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  /home/daniel/deep_signal/build-p3b-ui/deep_signal_imgui
```

The dummy check establishes process startup only, not native visual interaction.

Intermediate failures were corrected: the old exhaustive event-test comparator
needed the new freight payload; the query gained a next-opening-date planner
argument before the header change landed; and one existing query test still
expected the former starting class count. The initial complete headless run was
23/24 with that last stale assertion. No predecessor behavioral assertion was
removed. A zero-target amendment schema restriction was also corrected during
persistence review before the final tests. Third-party ImPlot emits existing
deprecated enum-conversion warnings; project-owned compilation is warning-clean.

The new reporting-limit test initially attempted charter creation beyond the
existing admission date window; its setup was corrected to start earlier and
exercise an already running program approaching that limit. Final contract
review also added durable historical report policy snapshots before publication.

## UI evidence and retained limitations

Automated UI evidence uses actual Dear ImGui rendering/text capture and owned
query DTOs. It covers unready authorization, fixed contract labels, physical
manifest/custody controls and failed/successful world replacement. It does not
stand in for native clicking. Neither `DISPLAY` nor `WAYLAND_DISPLAY` is present,
so native visual interaction is **not performed**. No display-server repair was
attempted.

The fixture is reproducible without another authoring sequence:

```sh
./build-p3b/deep_signal_cli --write-freight-fixture /tmp/deep-signal-p3b.sqlite
```

Limitations are intentional: normalized cargo units; prototype propulsion
ignores payload mass; one processed material/two colonies/one fleet; source-only
operating fuel; no remote rescue, refueling, cargo-to-tank conversion, density,
crew, market, tender maintenance, P4 or P5 systems. The planner is bounded and
non-optimal. P3A closed-program periodic reporting and unleased manual movement
cancellation behavior are retained; freight custody is protected from that
manual shortcut. Native UI review remains outstanding.
