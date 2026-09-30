# P5 implementation report

## Baseline and scope

- Corrected pinned baseline: `0bd66f555102e9d18488b650d5b2729c8569b231`.
- Branch: `p5-precision-characterization-development`.
- Implementation/tests commit: `f41186dd258eaba5db181864f977cd400bc16cb0`.
- Documentation commit and final PR head are recorded in the review handback.
- Baseline local `master`, `origin/master`, and clean `HEAD` were verified before editing.
- Scope: one optional Precision Characterization Array development, physical prototype,
  test evidence, local serial process, and exact-team support qualification.
- Target persistence: v18 only. v17 and older development saves are unsupported.
- No P6 or general technology-tree work is included.

The package's `START_HERE.md`, implementation handoff, scope decisions,
commenting standard, source basis, and parent playable-slice specification were
read before implementation. The package still named the prior P4B merge as its
baseline; the user's explicit correction to `0bd66f...` was applied.

## Implemented model and responsibilities

| Area | Responsibility |
| --- | --- |
| `TechnicalDevelopment.h`, IDs, `GameState` | Public opportunity, hidden candidate truth, local facility, program/work/report state, frozen design, prototype unit, test evidence, demonstrated revision, local process, support record, prototype integration receipt, and counters. |
| `TechnicalDevelopmentRules.*` | Stage constants/costs, typed readiness, public-only preview inputs, first missing durable artifact, local prototype/process/support queries. |
| `TechnicalDevelopmentExecution.*`, `SimulationTechnicalDevelopment.cpp` | Durable intent commands, one positive work action/opening, proportional material accounting, artifact publication, target-miss decision, D+1 timing, lifecycle and reports. |
| `TechnicalShipyardRules.*`, `Simulation.cpp` | Complete next-hull supply planning, positive-capacity prototype reservation, frozen effective cost/BP, one-time consumption, serial locality and retained FIFO. |
| `ProgramControl.*` | Sixth typed controller, shared engineering ownership, stable head merge, transient technical-facility budget. |
| `TechnicalDevelopmentValidation.*`, `GameStateValidation.cpp` | Artifact/work/test reconstruction, comparator/provenance integrity, local process/support checks, supply-plan credits/reservations, shared team-day exclusivity and audit validation. |
| `TechnicalSchema.cpp`, `TechnicalPersistence.cpp`, schema/repository/event JSON | v18 tables and mappings, ordered children, new counters/audits, frozen plans, reread validation and current-only policy. |
| `TechnicalDevelopmentQueries.cpp`, `SimulationQueries.*`, forecast/shipyard UI | Public opportunity/status, intent/detail, test provenance, demonstrated catalog metadata, prototype/process/support distinctions and honest shipyard blocker. |
| `TechnicalDevelopmentPanel.*`, shell/menu | Compact opportunity/authoring/detail workflow with reset on successful New/Load. |
| `TechnicalDevelopmentFixture.*`, CLI | Command-earned whole loop and `--write-technical-development-fixture`. |

Reverse owners, UI drafts, daily facility budgets, derived readiness caches and
global technology levels are not persisted.

## Reference constants

The established Specialist Survey Array remains the comparator: mass 30, volume
80, power demand 40, 40 Electronics, 70 BP, Characterization threshold 10,
accessibility measurement, Specialist Survey Instruments, 120 duty,
0.2 team-workdays/restored duty and 0.5 Electronics + 0.5 Composites/restored duty.

The public opportunity target is threshold **≤7**. Ordinary authored candidate
truth is **6**; the controlled miss fixture is **9**. Candidate truth is read by
the test executor only and is absent from queries/UI before acquired tests.

The concept design is Precision Characterization Array Mk I: mass 35, volume
100, power demand 55, survey capability 1, serial cost 60 Electronics +
10 Industrial Composites, 90 BP, Specialist Survey Instruments, 90 duty,
0.25 team-workdays/restored duty, and 0.75 Electronics + 0.75 Composites/restored duty.

| Stage | Work | Material | Result |
| --- | ---: | --- | --- |
| Concept | 5 | 10 Electronics + 5 Composites | Frozen mechanical/electrical design |
| Fabrication | 4 | 80 Electronics + 20 Composites | One physical prototype at the development colony |
| Testing | 3 | 15 Electronics + 6 Composites | Three dated tests and test-derived component/profile |
| Local production process | 4 | 40 Alloys + 30 Electronics + 20 Composites | Colony/facility-local capability, available D+1 |
| Support qualification | 2 | 10 Electronics + 10 Composites | Exact team service qualification, usable D+1 |

Full scope is exactly 18 workdays, 40 Alloys, 145 Electronics, and 61 Composites.

## Public knowledge versus technical truth

`TechnologyOpportunity` contains baseline, target and qualitative tradeoff.
`TechnologyCandidateTruth` contains only the authored achieved threshold.
Before tests, two threshold-6/threshold-9 worlds have identical opportunity,
authorization, work/material preview, waiting text, catalog and rendered panel.
Concept work publishes no threshold. Each complete testing workday records the
same deterministic measured result. The third creates a new immutable profile
and component; neither public target nor established threshold is substituted.

Technical tests are engineering evidence. They do not create P4A observations,
P4B operating records, geological findings or assessments.

## Prototype and serial conservation

The fabricated prototype paid 80 Electronics + 20 Composites and four engineering
workdays. Of that, 60 Electronics + 10 Composites and 90 component BP are embodied
serial component content; 20 Electronics + 10 Composites remain sunk overhead.

The shipyard reserves no prototype at order submission or zero capacity. At the
first positive yard allocation it requires a complete local path for every
developed installation. A prototype-backed plan subtracts exactly the embodied
component material/BP. Focused execution proves the prototype hull pays 40
Electronics for its other components instead of the serial hull's 100. The
prototype is consumed once and linked to its produced ship. A class requiring
two units cannot bind one prototype. A process qualified during prototype WIP
does not rewrite that hull; the next quantity can bind serial supply and pays
the full class cost/BP.

Production capability names exact colony/facility/program and `availableDay`.
Terra qualification does not enable Mars; a second real four-day local program
is required there.

## Support separation

`EngineeringQualification::PrototypeInstrumentation` permits P5 development and
is independent from `qualifiedFamilies`. Support work creates a dated record and
adds Specialist Survey Instruments to the exact real team at end of day, usable
by maintenance on D+1. An already qualified team satisfies support scope without
duplicate work/material/record. The whole-loop fixture still requires an actual
specialist workshop tender, co-location, parts and elapsed maintenance work.

## Acceptance map

| ID | Automated evidence |
| --- | --- |
| P5-01 | `technical_integration_tests::optional_path_and_whole_loop`: command-earned P4B useful path completes with no P5 program/component. |
| P5-02 | `technical_observation_tests::pretest_truth_isolation`; `technical_development_ui_tests`: threshold-6/9 public projections and rendered text match. |
| P5-03 | `technical_development_tests::malformed_and_every_stage_suspension`; `technical_save_tests::malformedSnapshotsReject`. |
| P5-04 | `technical_program_edge_tests::accepted_wait_and_finite_facility`: 30-day no-resource intent/report, no artifact/progress/interruption. |
| P5-05 | `technical_program_edge_tests::maintenance_ownership_is_shared_and_typed`: actual maintenance lease, equal numeric typed IDs, named owner, zero duplicate work. |
| P5-06 | `accepted_wait_and_finite_facility`: 0.25 facility yields exactly 1 work and proportional cost in four days; missing/zero facility waits. |
| P5-07 | `technical_development_tests::exact_full_chain`: exact concept work/cost and one design. |
| P5-08 | `technical_save_tests`: day-9 physical prototype, exact fabrication accounting, no demonstrated component/process. |
| P5-09 | `technical_save_tests` daily checkpoints and `exact_full_chain`: three dated tests; publication only after third. |
| P5-10 | `exact_full_chain`, `technical_observation_tests`: test/profile threshold 6. |
| P5-11 | `performance_miss_and_public_isolation`: observed 9-versus-7 issue, retained component, acknowledged continuation. |
| P5-12 | Daily partial-test v18 checkpoints and deterministic test/provenance validation. |
| P5-13 | Exact comparator/new-component assertions and demonstrated catalog DTO provenance. |
| P5-14 | `prototype_backed_shipyard`: class revision saved before process qualification. |
| P5-15 | `shipyard_query_names_local_supply_blocker`: valid zero-progress remote order, local-supply reason, no ETA. |
| P5-16 | `prototype_backed_shipyard`: zero capacity reserves nothing; first positive work binds exact unit. |
| P5-17 | Same test plus integration fixture: one-time consumption and 60E+10C/90BP credit only. |
| P5-18 | `technical_shipyard_tests::locality_and_complete_plan`: two installed units plus one prototype yields no plan/progress. |
| P5-19 | Exact full-chain production stage work/material and D+1 capability. |
| P5-20 | `waiting_order_recovers_on_d_plus_one`: same order resumes automatically. |
| P5-21 | `frozen_prototype_then_serial_quantity`: process completes during WIP; current plan remains prototype. |
| P5-22 | Same and `waiting_order...`: next serial hull pays full 100 Electronics class bill, no prototype consumed. |
| P5-23 | Remote-colony order in `locality_and_complete_plan`/query test waits despite global demonstration. |
| P5-24 | `second_colony_requires_local_qualification`: four real workdays at second facility create only that local process. |
| P5-25 | Exact full-chain final two workdays/10E+10C and D+1 team record. |
| P5-26 | `cancellation_and_scope_continuation`: prequalified team closes satisfied support scope with no charge/record. |
| P5-27 | `technical_integration_tests`: worn advanced sensor receives normal specialist-workshop/team/material maintenance receipt. |
| P5-28 | `demonstrated_threshold_drives_sampling`: signal 7, threshold 10 miss versus threshold 6 detect. |
| P5-29 | Same: demonstrated threshold 9 does not detect signal 7. |
| P5-30 | `malformed_and_every_stage_suspension`: partial state preserved and resumed in every stage. |
| P5-31 | `cancellation_and_scope_continuation`: partial fabrication remains sunk and does not restart. |
| P5-32 | Same: new program reuses completed design only and begins fabrication at zero. |
| P5-33 | Same: demonstration closes; later ProductionReady program starts at process qualification without duplicate artifacts/tests. |
| P5-34 | `opening_stock_and_shared_facility`, `program_control_tests`: two real teams/intents share one facility in stable sixth-kind order. |
| P5-35 | `actual_freight_delivery_waits_for_next_opening` and processing branch: actual same-opening unload/later processing first funds next opening. |
| P5-36 | `waiting_order_recovers_on_d_plus_one`: same-day later shipyard remains at zero on qualification day; D+1 progresses. |
| P5-37 | Day-30/60/90 report persistence, same-day amendment immutability, receipt reconciliation and closed-report halt. |
| P5-38 | `performance_decision_uses_every_time_entry`: direct/service/command paths stop at day 12; acknowledgment preserves evidence and continues. |
| P5-39 | `technical_save_tests`: daily design/fabrication/test checkpoints, demonstrated/reserved/process/support state, exact continuation; whole-loop final save. |
| P5-40 | Malformed tests/profile/prototype/process/support/supply-plan/integration/ordinal snapshots reject; failed Load/overwrite safety retained. |
| P5-41 | `save_contract_tests`: v17 and older markers reject on Load/overwrite with source bytes/logical content unchanged. |
| P5-42 | `technical_integration_tests` and CLI fixture: full optional loop, prototype + serial ship, weak observation, wear and qualified service; no-P5 P4 comparator. |

## Persistence

Schema v18 stores the complete P5 graph and retains existing dynamic component,
profile, class, ship, team and event tables. Technical tables use prepared value
bindings, explicit ordinals, FKs and strict readers. Save validates the source,
checks an existing destination, replaces rows transactionally, rereads and
validates before commit. Load reconstructs detached state in one read transaction.
Malformed v18 Load preserves the active world; failed overwrite preserves the
prior destination. No v17 migration or synthetic technology reader exists.

## Verification

Baseline before editing:

- Headless: **41/41 passed**, 21.53 seconds.
- UI enabled: **51/51 passed**, 21.77 seconds.
- Baseline UI dependency build emitted the three existing ImPlot deprecated enum warnings.

Final feature gates:

| Gate | Result |
| --- | --- |
| Full headless build | Passed; no project warnings |
| Full headless CTest | **47/47 passed**, 25.49 seconds |
| Full UI-enabled build | Passed; no project warnings in the final incremental build |
| Full UI-enabled CTest | **58/58 passed**, 25.48 seconds |
| `git diff --check` | Passed |
| CLI smoke | Passed; day 10, one ship/fleet |
| Earned P5 export | Passed; schema 18, day 25, empty `foreign_key_check` |
| SDL dummy startup | Exited successfully within the five-second bound |
| Native click-through | Not performed; neither `DISPLAY` nor `WAYLAND_DISPLAY` is available |

Exact build commands:

```sh
cmake -S . -B build-p5 -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p5 --parallel 2
ctest --test-dir build-p5 --output-on-failure -j 2

cmake -S . -B build-p5-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p5-ui --parallel 2
ctest --test-dir build-p5-ui --output-on-failure -j 2
git diff --check
```

The complete suites were run sequentially because predecessor save tests use
fixed temporary SQLite paths. The fresh baseline UI dependency build emitted
the three existing ImPlot deprecated enum-combination warnings. The completed
feature's final builds emitted no project warning.

CLI/export inspection:

```sh
build-p5/deep_signal_cli
build-p5/deep_signal_cli --write-technical-development-fixture /tmp/deep-signal-p5-final.sqlite
sqlite3 /tmp/deep-signal-p5-final.sqlite \
  'SELECT version FROM schema_version; PRAGMA foreign_key_check;'
SDL_VIDEODRIVER=dummy timeout 5s build-p5-ui/deep_signal_imgui
```

The earned export contains one program/design/prototype/developed revision,
three tests all measuring 6, one local process, one support record, one consumed
prototype integration, three observation batches, and one advanced-component
maintenance receipt. It is an earned current-format inspection artifact, not a
checked-in save fixture.

No predecessor behavioral assertion or historical fixture was deleted. Existing
tests were updated only for the sixth controller alternative, v18 current-schema
wording, current-hull plan initialization/event variants, and the more accurate
generic FIFO-blocker label.

Compilation/offscreen ImGui submission/dummy SDL startup do not establish native
usability. Human workflow/play review remains P6/residual work.

## Residual scope

P5 does not add a generic tech tree/points, random breakthroughs, arbitrary
component invention, generic equipment cargo/warehouse genealogy, prototype
transport/refit, facility construction, broad education/recruitment, Mission
Control, remote rescue/refueling, local site refining, formal P4B operating-data
analysis, final propulsion, random failure, shipyard cancellation, manual transit
cancellation cleanup, closed-survey report cleanup, or P6 human/whole-slice proving.
Native human play review remains separate from compilation and ImGui submission.

## PR #14 review revision: completed tests and participant provenance

This revision is relative to reviewed head
`bc0a37941554db8d863f9ed2fa84894e2f3d7381`, on the same
`p5-precision-characterization-development` branch. The original implementation
and verification above remain a historical record. The corrected P5 baseline
remains `0bd66f555102e9d18488b650d5b2729c8569b231`.

### Corrected behavior

- Completed test records reduce a successor's testing obligation to the missing
  tests. Its own `stageWork` and expenditure start at zero. Cancellation after
  one, two, or 1.5 paid test-workdays leaves two, one, or two new full workdays,
  respectively. Each newly paid test costs 5 Electronics and 2 Composites;
  cancelled fractional work remains sunk.
- Test publication is bounded at three records. Only sequence three creates
  the one demonstrated revision, measurement profile and component. Provenance
  links follow explicit test sequence without reordering persisted records.
- Validation accepts evidence from multiple programs for the same prototype and
  opportunity. Every test must match a complete-work boundary and its actual
  dated receipt's facility, team and leader. Duplicate/out-of-range sequences,
  mixed prototypes, inconsistent measurements, unpaid tests and duplicate
  demonstrated revisions still reject.
- Historical tests and qualification artifacts no longer depend on mutable
  current requested IDs. Positive production work pins its facility; positive
  support work pins its exact team. All four production workdays must occur at
  that facility, and all two support workdays must belong to that team.
- Pins are derived from existing receipts. Requests can change without
  transferring paid qualification work. Completing the pinned support course
  finishes that program; another team requires a later program. Query/UI labels
  distinguish requested participants from current work participants.
- Commands and previews share scope reconciliation. Extending scope before
  administrative closure starts the newly authorized stage. Narrowing and
  reexpanding a stage in the same program restores only that program's paid
  receipts, including its participant pin. Suspension remains suspension.
- Testing requires the actual prototype at the development colony. A remote
  successor remains accepted waiting intent, incurs no cost, and cannot create
  invalid remote test evidence. Completed demonstration knowledge still permits
  production qualification at another colony.
- Published reports keep their historical snapshots. For a period containing
  work, the participant summary names the last actual receipt in that period;
  every action remains individually inspectable through receipts.

### Changed files

| Files | Review correction |
| --- | --- |
| `src/sim/TechnicalDevelopmentRules.h/.cpp` | Shared remaining test bill, receipt-derived qualification participants, amendment reconciliation, and physical prototype readiness. |
| `src/sim/TechnicalDevelopmentExecution.cpp` | Bounded test publication, actual receipt provenance, canonical evidence links, pinned artifact publication, and report participants. |
| `src/sim/TechnicalDevelopmentValidation.cpp` | Cross-program evidence reconstruction, complete-work boundaries, qualification locality/team integrity, and malformed evidence rejection. |
| `src/sim/SimulationTechnicalDevelopment.cpp` | Shared reconciliation after a valid charter amendment. |
| `src/sim/TechnicalDevelopment.h` | Comments documenting own-program work and receipt-derived commitments; no new saved fields. |
| `src/app/SimulationQueries.h`, `src/app/TechnicalDevelopmentQueries.cpp` | Accurate remaining test bills and effective/requested participant projections; shared amendment preview. |
| `src/ui_imgui/TechnicalDevelopmentPanel.cpp` | Requested/current participant labels, qualification commitment explanation, and credited testing bill. |
| `tests/technical_revision_tests.cpp`, `CMakeLists.txt` | One focused regression target with unique temporary save directories. |
| `docs/architecture/simulation-state-contract.md`, this report | Corrected durable evidence, participant and amendment contracts, plus review evidence. |

### Regression coverage

`deep_signal_technical_revision_tests` adds seven groups:

1. Cancellation after one, two and 1.5 testing workdays, Save/Load before and
   during continuation, exact new material debit, one demonstration and no
   fourth test. Reversed predecessor storage order survives continuation.
2. Independent leader, team and facility amendments after test one, Save/Load,
   correct later participants and immutable day-30 report snapshots.
3. Production and support reassignment after partial work: only the original
   facility/team finishes and qualifies; requested/effective query labels agree.
4. A 2.5-workday opening publishes two tests, the remaining half completes the
   third, and a forged full test backed by only half a workday rejects.
5. Scope extension before closure and partial qualification narrow/reexpand
   preserve same-program work, cost and pins across Save/Load and suspension.
6. Remote cancelled-test continuation waits without work or consumption; later
   remote production qualification remains possible after local demonstration.
7. Duplicate/out-of-range sequences, inconsistent measurements, missing physical
   receipts, mixed real prototypes, dangling references and duplicate
   demonstrated revisions reject.

This completes the missing P5-32 test-evidence coverage. The original P5-32
mapping above covered concept reuse and partial fabrication only. The first
focused run against the reviewed behavior reproduced the cross-program
demonstration rejection: `Developed component was not earned by three complete
test workdays`.

### Revision verification

The exact headless and UI configure commands in the original Verification
section were rerun with the same build directories, flags and dependency paths.
Both configurations and complete builds succeeded with no compiler warnings.
The full CTest suites ran sequentially after the final code changes:

| Gate | Revision result |
| --- | --- |
| Complete headless CTest | **48/48 passed**, 27.23 seconds |
| Complete UI-enabled CTest | **59/59 passed**, 27.28 seconds |
| New technical revision target | Passed in both suites, 2.10 / 2.00 seconds |
| Existing technical save and integration tests | Passed in both suites |
| Existing full Save/Load and continuation tests | Passed in both suites |
| `git diff --check` | Passed |
| Native click-through | Not performed; `DISPLAY` and `WAYLAND_DISPLAY` are absent |

Exact build and suite commands:

```sh
cmake --build build-p5 --parallel 2
ctest --test-dir build-p5 --output-on-failure -j 2
cmake --build build-p5-ui --parallel 2
ctest --test-dir build-p5-ui --output-on-failure -j 2
git diff --check
```

These are local results. The earlier CLI/export/dummy-SDL results above belong
to the original implementation run; those checks were not repeated for this
revision. Automated ImGui tests and successful UI compilation do not establish
native usability. PR #14 remains open for re-review and is not merged.

Schema remains **v18**, with no persistence-layout changes, migration, or new
saved scheduling/assignment state. No existing tests, assertions or fixtures
were removed. Six-kind ordering, shipyard accounting, transit and P3/P4 mechanics
are unchanged by this revision. No P6 or broader technology work was introduced.
