# P3C implementation report

## Baseline and scope

- Repository: `killerbearddude/deep-signal`.
- Exact clean baseline: `310edd0f4af649cd0b3a08690e8649a44c52bb12`.
- Fetched `origin/master` matched the required commit and merge parents before edits.
- Branch: `p3c-tender-maintenance`.
- Implementation/test commit: `ee8434e3683e59487a7583fb698a4afbc6da4787`.
- Proposed PR: `P3C: restore survey equipment through delegated tender servicing`.
- Scope: survey-instrument condition and colony-supported tender servicing only.
- Final implementation head, publication status and PR are recorded in the review return.

The baseline suites passed sequentially: **25/25 headless**, **32/32 UI-enabled**.
No dependency installation, baseline reset, or unrelated architecture change was needed.

## Physical condition and catalog

`EquipmentFamilyId`, `MaintenanceTeamId`, and `MaintenanceProgramId` are distinct
typed identities. The campaign has Standard and Specialist Survey Instrument
families. Each managed survey-component definition holds a per-unit service
profile; each physical ship holds one ordered condition row keyed by the exact
managed installation's component ID. Repeated units share per-unit used duty,
while their installation quantity scales service materials and labor.

Class revisions remain immutable and contain no used-duty value. New hulls use
one shared initializer, including explicitly authored test ships. Load reads
authoritative condition and rejects missing/duplicate/extra/mismatched rows.

| Fixture value | Standard / Specialist instrument |
|---|---:|
| Normal campaign duty capacity per unit | 120 survey workdays |
| Dedicated proof capacity per unit | 10 survey workdays |
| Labor per restored duty per unit | 0.2 maintenance-team workdays |
| Electronics per restored duty per unit | 0.5 processed units |
| Industrial Composites per restored duty per unit | 0.5 processed units |
| Standard engineering team capacity | 1 maintenance-team workday/day |
| Compatible workshop capacity | 1 maintenance-team workday/day |
| Default remaining-duty trigger | 0.25 |

The specialist array has the standard array's nominal performance and a different
family. Standard and specialist workshops have separate explicit family rates.
Duplicate family entries in one definition reject. The starting engineering
team is qualified for Standard instruments; qualification is real authored data,
not a capability bestowed by selecting a workshop or role.

| Quantity | One workshop module | Reference tender |
|---|---:|---:|
| Dry mass | 100 | 570 |
| Internal volume used | 200 | 470 / 1,000 |
| Power generation | 0 | 120 |
| Power demand | 30 | 50 |
| Tankage | 0 | 1,000 |
| Survey / cargo capability | 0 / 0 | 0 / 0 |
| Build points | 100 | 530 |
| Structural Alloys | 40 | 290 |
| Electronics | 20 | 60 |
| Reactor Fuel | 0 | 20 |
| Industrial Composites | 20 | 70 |

The reference tender uses the existing hull, reactor, tank and general systems
plus a standard workshop. Survey Cutter and reference Freighter compositions
and nominal totals remain unchanged. The reference tender is supplied in the
dedicated scenario; the workshop catalog is available to ordinary design drafts.

## Action-specific capability and actual work

`EquipmentServiceRules` prepares survey duty before applying an action. It
separates nominal, powered and usable capability. Timed survey work requires one
duty unit on every contributing managed group. Immediate manual survey requires
five units for its complete pass; preview and command agree. The result helper
does not charge again. Zero-information passes still consume actual duty.
Waiting, transit, inspection, unpowered instruments and suspended work consume
none. Exhaustion affects survey instruments, not tanks, cargo or movement.

For a service group, restoration is bounded by used duty, per-unit quantity-scaled
labor, the selected powered workshop, the qualified team's capacity, and every
recipe ingredient above floors/within allowance. One provider works on one
group per opening. It does not spend leftovers on a second group or client.

Current condition lives only on the ship. Job targets snapshot the original
service debt and participants. Positive receipts record before/after condition,
actual restoration, installed quantity, actual materials, engineering work,
workshop hull and applicable revisions. Counters reconcile with those receipts.
Full service stays active after crossing the preventive trigger until all its
selected groups finish or an explicit withdrawal ends it.

Numerical history comparisons use absolute `1e-9` plus relative `1e-10`.
Execution rounding uses relative scale only: an accumulated final-step residue
may normalize when recipe/throughput requirements are relatively equivalent,
with actual debits capped at available resources. A genuinely tiny shortage
cannot become free restoration. Unrepresentable positive debits/changes remain
visible execution limits. No significant negative stock is clamped away.

### Independent arithmetic evidence

The ten-duty, six-pass fixture earns **30 duty**, restores **20 duty**, finishes
with **10 used duty**, performs **4 service days**, consumes **4 team-workdays**,
and debits **10 Electronics + 10 Composites**. No final overhaul is generated.
Transit days come from the existing planner and are additional.

Quantity tests independently check N=2 at 2.5 restored duty/day and N=3 completing
ten per-unit duty in six service days, including an exact 15-unit material cap.
A powerless N=3 client can be externally serviced, but remains unable to survey.

## Ownership, policy, phase order and supply

`ProgramController` now distinguishes all three program kinds. Actual leases
remain canonical on their separate records. Maintenance acquires a tender and
engineering team together at the service colony. It never acquires the client's
movement lease. All direct fleet guards, labels, issue paths and UI actions
handle the third kind. Survey/Freight same-number identity protection remains.

The dispatch merge compares only the next stored head of each vector by creation
day, with Survey/Freight/Maintenance tie order. Within-type order is preserved.
Transient opening context holds occupied teams/fleets, spendable material budgets,
and eligible service/client holds. Released capacity and incoming stock cannot
be reused for another physical action during that same opening.

Survey policy selects an optional existing provider and remaining-duty trigger.
It requests service before dispatch/refueling when the trigger is reached or no
powered group can sustain the next full/remaining pass. A fresh design with an
inadequate duty envelope creates no zero-work job. Unready selected support is a
specific wait. Distinct service/home colonies on one body keep separate stocks.

The first eligible workshop hull in persisted tender roster order is selected
per group, then pinned while that group is active. Another hull cannot lend it
power or silently take over. Unstarted incompatible groups remain unresolved
while another executable group can work on a later day.

Daily order remains opening programs, mining, processing, shipyards, movement,
then reports/issues/arrival bookkeeping. Arrival cannot service that day; repair
completion cannot enable later same-opening client action. Tests explicitly put
the older provider ahead of the client to exercise that second constraint.

Real P3B freight supplies both recipe ingredients. Contract cargo cannot pay for
repair before unloading, and unloading credit waits until the next opening.
The same survey and service intentions recover without reauthorization. An
additional fixture makes all three program kinds consume one nine-unit Propellant
stock in one opening: maintenance spends five recipe units, then survey and
freight each load two minimum-price-leg fuel units, leaving zero without overspend.

## Stops, detours, reports and issues

Provider suspension/cancellation withdraws its current service claim and safely
releases stationary assets. Actual restoration, consumed materials and historical
participants remain. Resume requests new work from remaining physical wear when
the client's policy needs it. Replacing assets/leader or removing the active
client withdraws the old job; limits/name-only changes preserve it.

Client suspension/cancellation or support reassignment withdraws service before
client lease release. Removing preventive support restores nothing. An exhausted
partial field pass can return using real fuel, retain target/participants/first
workday/progress, be serviced, travel back, and finish only its remaining days.
Insufficient return fuel remains a real block; no rescue or teleport is added.

Standing providers report every global 30 days, with 90-day review markers.
Reports retain policy snapshots. Their audit watermark distinguishes publication
from commands later on the same integer date: those later outcomes enter the
next report instead of rewriting an already read one. Work/material totals stay
receipt-based. Closure stops future reports and retains final job history and
consumption summary. P3A post-closure reports are unchanged.

Initial unready intent and ordinary recurring service/supply waits do not force
daily acknowledgment. New loss of a pinned prerequisite or exhausted authority
after work can raise stable typed issues. All advance entry points use the same
runner and return actual elapsed days. Acknowledgment does not create supplies,
change qualifications or restore condition.

## Persistence and validation

Current gameplay saves are **v15 only**. v14 and older files are rejected before
reconstruction, opened read-only and left unchanged. Unsupported replacement
destinations are refused. No old reader, compatibility generator or migration
was introduced.

The current schema has 50 tables: 17 additions for families, profiles/recipes,
workshops, condition, teams/qualifications, standing programs/policies, jobs and
ordered targets, work/material receipts, and reports/material-policy snapshots.
The survey policy extension includes the retained detour marker. All new numeric
values, enum/boolean storage and child ordinals are read strictly. Material rows
must be dense and managed profiles must match their declared presence.

Graph checks enforce exact condition keys, finite envelopes, exclusive three-kind
fleet control, atomic tender/team co-location, real survey client ownership,
single active claims, original participants, ordered target identity, positive
conservative work, per-day team/workshop/client exclusivity, receipt/audit links,
counter reconciliation, full completion and exact report publication boundaries.
Waiting because resources are absent remains valid saved state.

Tests directly fingerprint every new field, then compare complete durable SQL
contents after equivalent continuation. They cover healthy/worn states, supply
waits, partial and multiple-group service, stops, policy changes, pending issues,
maintenance return, subsequent outbound work, report boundaries and freight
unloading immediately before service. Malformed condition, recipe, job, lease,
work, ordinal and publication-cutoff rows reject without replacing the live world.
Existing transaction rollback and unsupported-schema tests remain in the full suite.

## Files and legitimate predecessor adaptations

| Area | Files / purpose |
|---|---|
| Domain / rules | `IdTypes.h`, `Domain.h`, `GameState.h`, `MaintenanceProgram.h`, `EquipmentServiceRules.*`, `MaintenanceProgramRules.*`, `ShipDesignRules.*` — condition, compatibility, recipes and derived readiness |
| Execution / commands | `MaintenanceProgramExecution.*`, `SimulationMaintenance.cpp`, `Commands.h`, `Events.h`, `Simulation.*`, `SurveyProgram*`, `ProgramControl.*` — actual work, authority, holds, detours, duty and three-kind routing |
| Validation | `MaintenanceProgramValidation.*`, `GameStateValidation.cpp`, equipment validator — graph, accounting and history checks |
| Scenario / CLI | `ScenarioFactory.*`, `src/cli/main.cpp` — healthy initialization, tender and freight-supply fixtures, export and typed audit output |
| Persistence | `MaintenancePersistence.*`, `MaintenanceSchema.cpp`, `Schema.*`, `SaveGameRepository.*`, `EventJson.cpp` — active v15 mapping and strict reconstruction |
| Queries / UI | `MaintenanceQueries.cpp`, `SimulationQueries.*`, `MaintenanceProgramsPanel.*`, survey/design/fleet panels, shell/menu — owned projections, authoring, condition and safe actions |
| Tests / build | Six new maintenance test files, CMake targets and narrow predecessor adaptations |

Authored predecessor test ships now explicitly use the shared healthy initializer.
The dual-capability freight fixture initializes its newly added managed array;
the deliberately sensorless replacement fixture removes old authored condition.
Catalog assertions account for nine definitions and retain the existing cargo
bay's ID/order. Event comparators cover the new typed payloads. Save-contract
tests target v15 and add v14 rejection. Visibility expectations include the new
panel. These changes preserve prior behavioral assertions; no historical fixture
or predecessor behavioral test was removed.

## Acceptance mapping

R = `maintenance_rules_tests.cpp`; E = `maintenance_execution_tests.cpp`;
I = `maintenance_integration_tests.cpp`; S = `maintenance_save_tests.cpp`;
A = `maintenance_app_tests.cpp`; U = `maintenance_ui_tests.cpp`.

| ID | Concrete evidence |
|---|---|
| P3C-01 | R `nominal_and_independent_hulls`; retained P2 design tests |
| P3C-02 | R `nominal_and_independent_hulls`; retained immutable order/ship binding tests |
| P3C-03 | R `manual_duty_and_initialization`; S malformed conditions |
| P3C-04 | E `repeated_service`, stop/detour cases; R contributing-hull evaluation |
| P3C-05 | E `repeated_service` asserts exactly 30 one-duty events and six zero-information passes |
| P3C-06 | R independent hulls, families/power; A physical-versus-nominal projection |
| P3C-07 | R manual duty; A `manual_preview_and_owned_condition` |
| P3C-08 | E `groups_compatibility_and_pinned_workshop`; R family/qualification arithmetic |
| P3C-09 | R `quantity_families_and_power`; E pinned workshop loss and powerless client |
| P3C-10 | R N=2 arithmetic; E N=3 exact-cap completion |
| P3C-11 | E `repeated_service` independent 30/20/10 duty, 4 workdays, 10/10 materials |
| P3C-12 | I `typed_arbitration_and_waiting_intent`, remote asset case; A unready authoring |
| P3C-13 | R malformed profile/condition/family cases; I duplicate clients; S strict malformed rows |
| P3C-14 | I `remote_assets_and_closed_reporting`; lease/held-client assertions throughout E/S |
| P3C-15 | I typed three-kind contention/manual guards; A typed ownership and interruption |
| P3C-16 | I head-only nonchronological three-way merge; retained P3B no-reuse tests |
| P3C-17 | I `client_priority_and_shared_supply_floors`; E one-group-per-day proof |
| P3C-18 | A `provider_intent_history_and_issues`; E actual client/provider leases |
| P3C-19 | E `early_provider_completion_and_design_envelope`, full-job stop tests |
| P3C-20 | E older-provider completion and detour-arrival receipt dates |
| P3C-21 | R independent formula; I partial supply; E fractional-group/tiny-bound proofs |
| P3C-22 | I `all_three_kinds_share_one_opening_stock`, floors/cap test and `industry_supply_is_available_next_opening` |
| P3C-23 | I `supply_and_bulk_daily_equivalence`; S mixed supply checkpoints |
| P3C-24 | I cargo/stock/repair conservation and next-opening first repair |
| P3C-25 | E partial-service stops; I real team disembark/closed reporting; S stopped jobs |
| P3C-26 | E client stop/provider removal withdrawals; S stopped-job continuation |
| P3C-27 | E leader replacement retains provenance; job/workshop identity and qualification tests |
| P3C-28 | I lowered allowance below expenditure; A acknowledgment/remaining-limit behavior |
| P3C-29 | E `exhausted_partial_visit_detour_and_return_fuel`; S detour continuation |
| P3C-30 | E real return fuel shortage and retained partial work |
| P3C-31 | E fresh short-life envelope and removal of preventive policy |
| P3C-32 | I ordinary waits; A exact two-day typed interruption boundary |
| P3C-33 | A all advance wrappers and acknowledgment; S pending/acknowledged issue round trips |
| P3C-34 | I closed reporting; S historical policies and post-publication command cutoff |
| P3C-35 | S direct fingerprint of all new fields and explicit ordered reads |
| P3C-36 | S every physical checkpoint plus complete durable-world comparison |
| P3C-37 | S malformed data/failed live Load; retained save replacement rollback tests |
| P3C-38 | `save_contract_tests` v10–v14 rejection/source non-modification |
| P3C-39 | I mixed bulk/daily equivalence; U rendering has no physical side effects |
| P3C-40 | A owned queries/manual preview; U real render/reset; existing shell tests and typed fleet controls |

## Exact verification commands

Baseline suites were run sequentially before editing:

```sh
ctest --test-dir build-p3b --output-on-failure -j 2
ctest --test-dir build-p3b-ui --output-on-failure -j 2
```

Local dependency paths were verified and kept out of committed CMake configuration:

```sh
cmake -S . -B build-p3c -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p3c -j 2
ctest --test-dir build-p3c --output-on-failure -j 2

cmake -S . -B build-p3c-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p3c-ui -j 2
ctest --test-dir build-p3c-ui --output-on-failure -j 2
git diff --check
git diff --cached --check
```

Final builds succeeded. Full suites ran sequentially:

| Gate | Result |
|---|---|
| Final headless suite | **30/30 passed**, 0 failed; 5.97 seconds |
| Final UI-enabled suite | **38/38 passed**, 0 failed; 5.99 seconds |
| Worktree/staged diff checks | Passed |
| CLI smoke | Exit 0; day 10, one commissioned ship/fleet |
| Inspection fixture export | Exit 0; v15, 50 tables, one survey/provider pair and two freight programs |
| Exported fixture foreign-key check | No violations |
| Dummy-renderer startup | No error output; intentional three-second timeout, exit 124 |

The startup command was run from `/tmp`:

```sh
timeout 3s env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  /home/daniel/deep_signal/build-p3c-ui/deep_signal_imgui
```

The six added CTest targets cover rules, execution, mixed integration, persistence,
application projections and actual ImGui rendering. Verification is local;
these results are not presented as GitHub Actions results.

Intermediate corrections: the first full run exposed an old SQL `version=14`
constraint and predecessor catalog/condition fixture assumptions; those were
updated for the intentional new model. An independent shared-fuel fixture first
assumed fractional sub-unit leg prices and was corrected for the retained
one-unit minimum per positive leg. Exact-capped N=3 repair exposed an accumulated
rounding residue, now covered by conservative relative-only normalization tests.
Publication-boundary analysis added the audit watermark and a regression for
commands after a day-30 report. No assertion was removed to accommodate these
corrections. Existing third-party ImPlot enum warnings are separate from project
compilation. New source/test files received a comment/readability pass using the
already installed formatter.

The staged diff check also caught trailing whitespace in newly formatted SQL;
it was removed before the final build/suite runs and commit. Acknowledged limits
now remain latent through idle/reacquisition gaps, with actual successful work
clearing them so a later independent loss can raise a fresh issue.

## Inspection fixture and limitations

```sh
./build-p3c/deep_signal_cli --write-maintenance-fixture /tmp/deep-signal-p3c.sqlite
```

The fixture has an authorized survey/support pair, worn instruments, empty parts
bins and two authorized real freight deliveries. It can be loaded in the desktop
shell for inspection without another setup sequence.

Neither DISPLAY nor WAYLAND_DISPLAY is available. Native click-through is **not
performed**. Actual ImGui text-log rendering and owned query/command tests provide
automated evidence; a dummy-renderer startup is not presented as visual interaction.

This is a stationary colony-supported workshop loop. It does not add autonomous
tender deployment, rendezvous, rescue, onboard consumable stores, cargo-to-tank
conversion, engine/reactor/hull/cargo wear, random failures, refits, workforce
recruitment/training/payroll, final propulsion, a generic task framework, P4 or
P5 systems. No broader remaining P3 mechanics were started.
