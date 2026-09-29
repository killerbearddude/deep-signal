# P4A implementation report — observations, analysis and assessments

## Provenance and delivery

- Starting and pinned baseline: `88701a348554775c4b1b4d14324d47418a595032` (integrated PR #10).
- Starting checkout: clean `master`, equal to `origin/master`.
- Feature branch: `p4a-observations-analysis-assessments`.
- Scope authority: supplied `START_HERE.md`, P3 closeout register, P4A handoff and code-commenting standard, read in the supplied order. All package SHA256 checks passed.
- Verified implementation commit: `94ce2cdcd76bb68f65075a25ba481eaa4135da99`.
- Delivery status: implementation and verification complete. This report is the review checkpoint; final branch head and PR URL accompany the delivery. Do not merge automatically.

## Old and new behavior

Previously, a survey changed `MineralDeposit.confidence`; ordinary body and forecast views used hidden deposit existence, exact remaining quantities and accessibility. A completed pass was presented as improved resource certainty.

Now a completed pass seals one instrument observation batch. It leaves physical geology unchanged. Raw data is immediately inspectable and becomes eligible for analysis at D+1. An authorized AnalysisProgram uses an existing, physically local SurveyTeam and real colony laboratory throughput to earn three scientific team-workdays per batch. Completed jobs publish immutable findings and dated assessment revisions from recorded inputs only.

One team can fieldwork and then analyze after actual return/release; it cannot do both at once. A separate scientist at the laboratory permits concurrent work. Repeated results add history without increased certainty. Negative readings establish only non-detection within the recorded method's limits. Reserve quantity remains Unmeasured; site suitability remains Unassessed.

## Model and constants

| Item | Implemented contract |
| --- | --- |
| Scientific identities | Typed MeasurementProfileId, ObservationBatchId, AnalysisProgramId, AnalysisJobId and AssessmentId with durable allocation counters |
| Reconnaissance profile | ID 1, model version 1, detection threshold 50 normalized signal units; no accessibility reading |
| Characterization profile | ID 2, model version 1, threshold 10; detected readings classify accessibility |
| Declared channels | All 14 existing Mineral values in declared enum order; authored subsets remain valid |
| Signal | `remaining * min(accessibility, 1.0)`; signal/remaining/exact accessibility never appear in scientific outputs |
| Accessibility | Low `[0, .25)`, Moderate `[.25, .75)`, High `[.75, unbounded)`; an enum represents the open upper bound |
| Hardware bindings | Existing component 4 uses Reconnaissance; component 7 uses Characterization. Mass, power, costs, maintenance families and nominal survey work remain unchanged |
| Exposure | Five actual contributions per installation for full sensitivity; installation quantity does not accelerate acquisition. Partial groups retain InsufficientExposure rather than a negative reading |
| Field work | Existing five-day passes and one duty/day remain; manual actions retain five duty/action with one actual date and no invented field team |
| Sampling date | Physical completion boundary, not an average over the work window |
| Information delivery | Available at opening D+1, independent of distance; no personnel/cargo transport is implied |
| Workforce | Existing SurveyTeam pool, at most one team-workday/day per team, shared typed Survey/Analysis ownership |
| Laboratory | Terra is authored with 1.0 scientific team-workday/day; other home-scenario colonies remain zero. Dedicated evidence fixtures explicitly give their existing fixed survey base 1.0 |
| Job | One immutable batch input, exactly 3.0 team-workdays; one job's work per program/opening |
| Work accounting | Positive dated receipts are authoritative progress and spent allowance. Receipts retain actual team, leader, colony, charter revision and historical installed lab capacity |
| Charter sources | FollowSurvey or ordered unique FixedBatches; source and lab fixed. Name/team/leader/allowance can be amended |
| Allowance | Optional lifetime team-workday limit; absent uncapped, zero no work; lowering below spent is valid |
| Dispatch | Head-only stored-vector merge, equal-day ties Survey → Freight → Maintenance → Analysis |
| Assessments | Pure interpretation and assembly; latest applicable acquisition epoch per claim, immutable revision chain, ordered completed input links and retained alternatives |
| Unsupported claims | Explicit QuantityAssessment::Unmeasured and SiteAssessment::Unassessed |

An active partially worked analysis may retain its analyst while temporarily unready. Suspension and assignment changes release the stationary lease while retaining documented work. Cancellation preserves raw records and completed assessments, and never cancels the source survey. A follower closes only after source closure and backlog completion. Empty-source closure means no observations, not no resources.

Reports use global 30-day boundaries and 90-day markers, copied authority/source/work and an audit cutoff. Closed analysis programs stop future reports. P3A's older closed-survey reporting behavior is retained.

## Files and responsibilities

| Area | Files / responsibility |
| --- | --- |
| Scientific records | `Observation.h`, `AssessmentRules.h`, `AnalysisProgram.h`, `IdTypes.h`, `GameState.h`, `Domain.h`: scientific identities, captured outputs, claims, finite work and laboratory/profile links |
| Sampling and acquisition | `ObservationRules.*`, `ObservationAcquisition.*`, `EquipmentServiceRules.*`, `SurveyProgram.h`, `SurveyProgramExecution.*`, `Simulation.*`: real contributor exposure, retained dates, one batch per completion and unchanged duty rules |
| Interpretation | `AssessmentRules.cpp`: pure record-only interpretation, semantic repetition, chronology and same-epoch alternatives |
| Analysis commands and execution | `AnalysisProgramRules.*`, `AnalysisProgramExecution.*`, `SimulationAnalysis.cpp`, `Commands.h`, `Events.h`: source queues, actual scientist leases, finite lab budgets, lifecycle, reports and issues |
| Common control | `ProgramControl.*`: fourth typed program, shared scientific-team ownership, D+1 opening input snapshot and per-colony lab budgets |
| Validation | `ScienceValidation.*`, `GameStateValidation.cpp`, `SurveyProgramValidation.cpp`: shapes, references, ordered provenance, one batch/event per completed pass, exclusive leases, labor reconciliation and reproducible findings/revisions |
| Persistence | `SciencePersistence.*`, `Schema.*`, `SaveGameRepository.*`, `EventJson.cpp`: v16-only explicit tables, strict current-format reads, new audit type, counters and removal of scientific confidence columns |
| Application | `ScienceQueries.cpp`, `SimulationQueries.*`, `ForecastService.*`: owned science DTOs, previews and knowledge-limited replacement of existing geology surfaces |
| Native UI | `SciencePanel.*`, `ImGuiApp.*`, `MainMenuBar.*`, `BodiesPanel.cpp`, `InspectorPanel.cpp`, `InformationPanel.cpp`, `InformationPreviewLayer.cpp`, `FleetOrdersPanel.cpp`, `EconomyForecastPanel.cpp`, `ShipyardPanel.cpp`: authoring/lifecycle/dossier, resets, profile display and migrated old views |
| Fixtures/build | `ScenarioFactory.cpp`, `src/cli/main.cpp`, `CMakeLists.txt`: catalog/lab authoring, earned inspection saves and focused test targets |
| Documentation | README, simulation state contract, this report, and the supplied historical P3 residual register (wording preserved; Markdown line breaks normalized) |

## Before/after geology consumer inventory

| Consumer | Previous authority | P4A replacement |
| --- | --- | --- |
| MineralDeposit / helper family | Confidence, confirmed/estimated/uncertain partitions | Physical remaining/accessibility only; confidence and partition helpers removed |
| Manual/timed completion | Improved-deposit counts and average confidence | Typed immutable ObservationBatch link; no geological mutation |
| BodySystemSummary | Hidden deposit counts and reserve totals | Public body/colony/fleet metadata and acquired batch/assessment counts |
| bodyDeposits / inspector | Physical row existence, exact accessibility and quantities | Every declared mineral channel with published indication, coarse class and scientific as-of date |
| explorationIntelligence / Bodies | Low-confidence hidden deposits and true unknown potential | Declared public subjects, limited assessed knowledge and acquired-batch audit links |
| ResourceSurveyPreview | Number of improvable hidden deposits and projected confidence | Actual ownership/location/stationary/equipment admission plus raw-data limitations; no geological lookup |
| Mining forecast | Live true remaining/accessibility, including shared deposits | Current-session output telemetry per colony/declared mineral channel; explicit lack of geological production estimate |
| Raw-material cause chains | True reserve partitions plus flows | Known inventories, processing inputs and recorded output; no reserve amounts |
| Exhaustion forecast | True reserve divided by physical extraction rate | Public body/channel rows with absent geological ETA and insufficient-evidence explanation |
| Information panel / preview | Deposit counts and exact reserve partitions | Acquired-record counts, Unmeasured reserve and Unassessed site suitability |
| Map/navigation | Public astronomical identities and positions | Retained; no new truth-dependent map metadata or admission gate |
| Shipbuilding/freight/service | Actual design/inventory/asset state | Retained physical authority; no observation/assessment prerequisite |

The source audit finds no `mineralDeposits` access in `src/app` or `src/ui_imgui`. Mining, persistence, validation of physical state and developer tests may still inspect physical geology. Current-session output telemetry is not presented as durable reserve history.

## Schema and continuity

Current gameplay saves are **v16 only**. There is no v15 or earlier reader/migration. Older Load and overwrite attempts reject unchanged. No historical SQL fixtures were deleted.

The retained 50-table snapshot gains 26 focused scientific tables. Ordered profiles/channels, active exposure/dates, observations/results, sources/fixed input order, jobs/work, findings/readings, assessments/claims/provenance and reports are persisted explicitly. Existing survey receipt tables now hold physical work; a unique scientific link ties each completed receipt to its batch. Supplemental colony/component records are checked for completeness, including zero capacity and null profile rows.

Reverse owners, latest-assessment lookup, pending source queue and opening data/lab budgets are derived. Method snapshots are reconstructed from immutable persisted catalog references; Load never samples geology or performs analyst work. Assessment validation reproduces outputs from stored completed findings, without physical truth access.

Existing preflight, schema-shape comparison, bound statements, foreign keys, detached validation, replace/reread/commit and rollback remain. The existing malformed receipt-gap fixture now disables foreign keys deliberately during corruption because its newly retained batch link otherwise prevents constructing the invalid file; the loader rejection assertion remains.

## Acceptance-to-test map

Abbreviations: **rules** = `observation_rules_tests`; **execution** = `analysis_execution_tests`; **integration** = `science_integration_tests`; **save** = `science_save_tests`; **UI** = `science_ui_tests`. Predecessor target names below are unchanged.

| ID | Concrete evidence |
| --- | --- |
| P4A-01 | `sim_tests::test_resource_survey_acquires_physical_truth_independent_records`, integration `complete_projection_isolation`: reserves/accessibility unchanged; later analysis ignores changed truth |
| P4A-02 | rules `thresholds_and_limits`: literal R/A fixture outputs, exact thresholds and .25/.75 boundaries, accessibility above one |
| P4A-03 | rules `thresholds_and_limits`: absent, sub-threshold and inaccessible channels are field-equal with 14 declared outputs |
| P4A-04 | rules `chronological_interpretation`, integration repeated analyses: quantity/site unsupported and reconnaissance accessibility unmeasured |
| P4A-05 | execution, save every-day continuation: one raw batch per timed/manual pass, stable linked audit, exactly-once publications |
| P4A-06 | integration `changing_instrument_exposure`: three/two contributions yield no full reading; strengthened P3C detour regression retains all five actual dates through service |
| P4A-07 | execution literal ten field duty / five manual duty; mixed physical comparison shows no lab wear |
| P4A-08 | integration changed exposure and baseline negative-pass regressions: zero usable readings and empty physical deposits still complete real work |
| P4A-09 | execution first independent analyst day equals availability; save checkpoints include acquisition day and D+1 |
| P4A-10 | integration unready authoring and zero-capacity recovery; no idle team lease |
| P4A-11 | integration malformed name/team/source/negative/NaN allowance, execution duplicate fixed inputs; no partial program creation |
| P4A-12 | execution same-team follower automatic backlog and closure; integration empty canceled source closure without resources claim |
| P4A-13 | execution manual fixed input plus reversed fixed selections of another program's timed batches |
| P4A-14 | execution shared-team path; validation team/day exclusivity and duplicate lease rejection |
| P4A-15 | execution separate-team path starts at first eligible opening while field team remains deployed |
| P4A-16 | integration `typed_head_order`: deliberately nonchronological stored survey vector, four-way tie order, equal numeric Survey/Analysis IDs and no analysis fleet owner |
| P4A-17 | integration lab capacity 1/2 tests and same-team release test: no same-opening borrowing |
| P4A-18 | execution two-pass fixture: exactly six analyst team-workdays; mixed comparison preserves inventories/fuel/engineering and duty |
| P4A-19 | execution fixed job histories, save daily/bulk continuation, failure-boundary retry: immutable input and no duplicate completion |
| P4A-20 | pure record-only API and integration changed hidden world after capture produce identical findings/assessments |
| P4A-21 | rules repetition and integration second analyst of same batch: history retained without content/certainty promotion |
| P4A-22 | rules old-after-new acquisition and same-epoch Low/High mixed accessibility; no hidden-truth resolution |
| P4A-23 | integration two scientists at capacity 2 publish distinct ordered revisions on day 3; save simultaneous-publication checkpoint |
| P4A-24 | rules insufficient exposure remains Unknown; recon negative / characterization positive retains method alternatives |
| P4A-25 | integration and save suspension, fractional work, scientist replacement and charter revision attribution |
| P4A-26 | integration cancellation preserves raw records and earns no partial-job assessment; canceled source with no batches closes follower honestly |
| P4A-27 | integration consequential allowance issue through direct/event/command/service advance paths; acknowledgment and lowered authority preserve spent work |
| P4A-28 | save day-30 report followed by same-day assignment/limit amendment and cancellation; closed reporting stops |
| P4A-29 | integration complete DTO equality: body counts, all channel rows, intelligence/advice, forecasts, optional fields and every-body preview across different hidden worlds |
| P4A-30 | source consumer audit above, updated predecessor app/forecast/information assertions and complete projection-equality test |
| P4A-31 | predecessor mining/forecast tests use actual output telemetry and preserve known-stock, processing, shipping and production assertions |
| P4A-32 | integration unknown-world construction authorization; unchanged full freight/service suites authorize without evidence prerequisites |
| P4A-33 | save direct equality of all new scientific records/counters and event JSON; every-day partial field/analysis round trips and lifecycle/source variants |
| P4A-34 | save 65 successive checkpoints, zero lab, fractional/limited work, acknowledgment, replacement, suspension/cancellation, source end, same-body publications and report cutoffs; predecessor daily/bulk and P3C detour continuation |
| P4A-35 | save 25 SQL corruptions spanning methods/channels/exposure/source/leases/work/claims/ordinals/counters; failed application Load preserves the live state |
| P4A-36 | save v15 byte-for-byte rejection for Load/Save; retained historical-version and transactional rollback contract suites |
| P4A-37 | owned preview/record queries, UI text capture of authoring/lifecycle/raw-vs-assessed/date/limit displays, no render work, failed Load preservation and New reset |
| P4A-38 | integration `mixed_physical_conservation` compares otherwise identical survey/freight/tender loops with and without real analysis; retained P3 suites preserve leadership/physical stops/opening rules |

## Test expectation changes

No test executable or historical SQL fixture was removed. Scientific-confidence assertions were replaced in simulation, event JSON, survey receipts, app queries, forecasts, information rendering and save comparisons. They now assert immutable batch identity, complete declared channels, explicit unknown/unmeasured states, and separate analysis.

Mining forecast tests first execute real mining to establish output telemetry; shared-deposit accounting assertions remain and still verify the first colony's capped output and the later colony's zero output. Their geological-lifetime assertions intentionally become absent-ETA/insufficient-evidence assertions because P4A does not measure reserves. Production, processing, transit/fuel, FIFO, ownership, persistence-order, malformed-input and rollback protections remain.

One previously invalid receipt-ordinal mutation must bypass the new SQL foreign key to reach the reader test. This strengthens stored linkage rather than removing malformed-save coverage.

## Exact commands and results

Dependencies were inspected in the live environment before configuring fresh directories. The SQLite header/library and SDL3 config paths below are machine-local verification evidence, not committed build requirements. Both baseline suites ran before edits, sequentially.

```sh
cmake -S . -B build-p4a -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p4a -j 2
ctest --test-dir build-p4a --output-on-failure -j 2

cmake -S . -B build-p4a-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p4a-ui -j 2
ctest --test-dir build-p4a-ui --output-on-failure -j 2

git diff --check
```

- Fresh baseline headless: **30/30 passed**, 6.35 seconds.
- Fresh baseline UI-enabled: **38/38 passed**, 6.42 seconds.
- Fresh vendor compile reports the existing third-party ImPlot enum-conversion warnings. No new dependency was installed.
- Final headless: **34/34 passed**, 10.43 seconds. Final UI-enabled: **43/43 passed**, 10.35 seconds. Both final builds completed without project-code warnings.
- Native click-through: **not performed**; neither DISPLAY nor WAYLAND_DISPLAY is available. ImGui rendering tests and dummy startup are separate evidence, not native interaction or usability review.

### Smoke checks and diff checks

```sh
./build-p4a/deep_signal_cli
./build-p4a/deep_signal_cli --write-evidence-fixture /tmp/deep-signal-p4a-shared.sqlite
./build-p4a/deep_signal_cli --write-evidence-concurrent-fixture /tmp/deep-signal-p4a-concurrent.sqlite
# Run from /tmp so startup writes no settings into the checkout:
timeout 3s env SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software /home/daniel/deep_signal/build-p4a-ui/deep_signal_imgui
git diff --check
git diff --cached --check
```

The ordinary CLI smoke exited 0. Both fixture exports exited 0. Read-only SQLite inspection confirmed v16, 76 tables, two batches, one assessment and no foreign-key violations in each file. The shared-team fixture has 3.0 workdays on its completed local job and no follower work; the independent-team fixture additionally has 1.0 real follower workday. Dummy startup emitted no errors and reached its deliberate timeout (exit 124); it is not native interaction evidence. Working-tree and staged diff checks passed.

A final source review added preparation of both durable and returned audit storage before scientific mutation and moved field-progress commitment before diagnostic emission. The complete suites were rerun after that correction. Scientific audit-ID exhaustion is covered by the no-duty/no-publication failure test.

The comment pass covered new module purpose, public boundaries, units, immutable provenance, transient budgets, finite labor, phase ordering and failure limits. Stale confidence descriptions in active source/contracts were removed. No dependencies were installed; an existing local formatter was used on new C++ files.

## Inspection fixture and limitations

The CLI offers shared-team and independent-analyst evidence fixtures. Each adds a real characterization hull using the existing specialist array, an authored 1.0-capacity laboratory at the existing fixed base, and WaterIce R=100/A=.4 at home and the field target. An immediate local observation earns a real three-day assessment first; a later two-pass source and follower acquire transmitted data through normal execution. The shared version visibly waits for the field scientist; the independent version authors a second finite scientist at home.

No archive is synthesized on New/Load. Profiles are fictional declared prototype rules, not real instrument calibration. Delivery at D+1, transferable documented partial analysis, one team-day/day and a three-day job are explicit P4A choices. There are no lab construction commands, team transport beyond existing field return, material analysis consumables, reserve estimates, quality/confidence multipliers, autonomous evidence-based leadership, or construction prerequisites.

Publication preparation protects duty and final analyst work on the tested failure boundaries. The existing simulation tick is not globally transactional: an unexpected preparation exception may leave the calendar advanced without that physical/scientific action. Persistence retains transactional replacement and failed-Load preservation.

P4A addresses R04's bounded shared field/analysis expertise. It does not complete requirements/design staffing or the broader parent expedition vision. The historical residual register remains explicit. **No P3D, site-development, research, final propulsion, remote tender/Mission Control or other deferred system was started.**
