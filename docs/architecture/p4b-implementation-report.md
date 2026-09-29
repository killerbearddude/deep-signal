# P4B implementation report

## Baseline, branch and scope

- Pinned and verified baseline: `1a8885d599431b16bc46830ad1ecf4c80c41e633`.
- Branch: `p4b-site-development-ice-supply`.
- Baseline local `master` and fetched `origin/master` matched, with a clean worktree before implementation.
- Scope: unrestricted site investment, physical field construction, supported Ice recovery, and raw feed delivered to colony industry.
- Implementation/tests commit: `72834b6a33e456cc38034c0f2c41f28b8f59a918`.
- Documentation is committed separately. The final handback identifies the documentation head and review PR.
- No merge is authorized or performed.

This implements a bounded P4B slice. It does not complete all parent P4 or the entire playable slice.

## Behavior and ownership

Previously, freight carried only processed material between colonies and refueled at its source. There was no separately owned resource site, field-construction obligation, site operating history, or actual cumulative processing-output counter.

Now a player can authorize a site at a known public body without investigation. Registration creates no working hardware or inventory. Sealed processed cargo can reach a cold site using real ship handling. A real equipped builder and qualified engineer assemble delivered inputs and perform commissioning. Installed hardware becomes eligible at the following opening. The builder returns; the site keeps its own operating authority, supplies, hardware, observations and reports.

Site operation consumes actual Reactor Fuel and Industrial Composites. Its shared raw handler serves freight before extraction. Ice collection uses the same freight engine with an explicit colony destination/base: fuel there, travel empty, load actual remote stock, and carry it home. Colony processing retains the Ice/Volatiles recipe and records gross production separately from stocks and transfers.

### File responsibilities

| Area | Files and responsibility |
| --- | --- |
| Typed stock identity | `StockTypes.*`, `StockAccess.*`, `Domain.h`: raw/processed commodities, colony/site locations, real inventory access, cargo tags, production totals. |
| Catalog and pure arithmetic | `SitePackageRules.*`, `SiteWorkRules.*`, `SiteOperationRules.*`: versioned module definitions, additive totals, proportional work, supported duty, eligible commissioned capabilities. |
| Construction | `SiteDevelopmentProgram.h`, `SiteDevelopmentCommands.h`, `SiteDevelopmentRules.*`, `SiteDevelopmentExecution.*`, `SimulationSiteDevelopment.cpp`: fixed route/package intent, participants, paid work, commissioning and physical return. |
| Operation | `ResourceSite.h`, `SiteOperationExecution.*`, `SimulationSiteOperation.cpp`, `SiteEvents.h`: standing authority, paid support, physical recovery boundary, observed decisions and durable reports. |
| Common control and tick | `ProgramControl.*`, `Simulation.*`, `Commands.h`, `Events.h`, `GameState.h`, `IdTypes.h`: five-kind head merge, shared engineering/site leases, transient stock/space/handling budgets, separate decision identity and phase integration. |
| Freight | `FreightProgram*`, `FreightPersistence.cpp`: typed endpoints/commodity/base, collection phase, no speculative stock reservation, base fuel, retained manifest/cargo, exact shared handling timetable, validation and persistence. |
| Validation | `SiteDevelopmentValidation.*`, `SiteOperationValidation.*`, `GameStateValidation.cpp`: durable graph, work/material/installed provenance, real ownership, dated receipts/reports, numeric/tag/reference protection. |
| Scenario and design | `ScenarioFactory.*`, `ShipDesignRules.*`: authored construction family/workshop/builder/team; separate finite inspection scenario. Existing cutter/freighter composition is preserved. |
| Application | `SiteQueries.cpp`, `SimulationQueries.*`, `MaintenanceQueries.cpp`: owned site, construction, typed freight and shared engineering views. `SiteDevelopmentFixture.*` earns inspection state through commands. |
| UI and CLI | `SiteDevelopmentPanel.*`, freight/fleet/maintenance panels, existing shell/menu, CLI: authoring, previews, lifecycle/authority controls, separate operational history, explicit related obligations and fixture export. |
| Persistence | `SiteSchema.cpp`, `SitePersistence.*`, `Schema.*`, `SaveGameRepository.*`, `EventJson.cpp`: v17 tables, ordering, counters, typed events and transactional save/load. |
| Verification/docs | New site/development tests, predecessor fixture adaptations, README, state contract and this report. CMake registers focused sources and test targets. |

### Audit of old assumptions

| Before | P4B boundary |
| --- | --- |
| `sourceColonyId` / `destinationColonyId` | `StockLocation` variants; same numeric colony/site IDs remain distinct. |
| Freight/cargo `ProcessedMaterial` | `Commodity` variant; raw and processed channels never alias by ordinal. |
| Source always refuels | Immutable `operatingBaseColonyId`, which must be a colony endpoint. Source-base delivery and destination-base collection have explicit cycles. |
| Source fuel floor | `basePropellantFloor`; combined with cargo floor only when both refer to the same actual Propellant store. |
| Ship handling alone | Processed cold staging uses ship handling; raw site transfers also debit one shared paid site handler and opening receiving-space budget. |
| Team ownership scanned within maintenance | Common typed `controllingEngineeringTeam()` spans maintenance and development before location explanations. |
| Four program alternatives | Development appended to `ProgramController`; dispatch, labels, pending issues, manual-fleet protection, queries and fleet controls handle it. Existing tie order remains Survey → Freight → Maintenance → Analysis → Development. |
| Every decision has a program ID | `DecisionSource` distinguishes a program controller from an operating `SiteId`. `AdvanceResult.issueSource` is canonical; the old program-only field is an ephemeral projection for predecessor callers. CLI labels either source. |
| Last catalog class is freighter | Scenario/tests select the actual carrying ship class or fixed authored freighter entry. The new builder is an additional immutable design. |
| Current stock used as production evidence | `processedProductionTotals` accumulates actual recipe output only. Coupled raw debit/output/history changes are checked before mutation. |

No reverse lease registry, persisted daily share, future stock reservation or second fleet executor was introduced.

## Literal model constants

All quantities are authoritative catalog data. UI code reads owned projections.

| Module | Alloys | Electronics | Composites | Assembly workdays | Consequence |
| --- | ---: | ---: | ---: | ---: | --- |
| Ice Extraction | 80 | 20 | 10 | 4 | 10 rated Ice/day, 10 power demand |
| Power | 60 | 20 | 10 | 3 | 30 generation, 1 Reactor Fuel/full-duty day |
| Bulk Handling | 30 | 10 | 10 | 2 | 50 raw units/day, 5 power demand |
| Bulk Storage | 40 | 0 | 20 | 2 | 200 shared raw units |
| Automation Support | 30 | 20 | 10 | 3 | Supports one extractor, 5 power demand, 1 Composite/full-duty day |
| **Reference package** | **240** | **70** | **60** | **14 + 2 commissioning** | **30 generation / 20 demand; 10 extraction / 50 handling / 200 storage** |

The Field Construction Workshop has mass 100, volume 150, demand 30, rate one compatible engineer-workday/day, 100 BP and cost 60 Alloys + 20 Electronics + 10 Composites. The explicit family binding is authored in scenario data; runtime rules do not infer capability from its name, numeric ID or ship role.

The Reference Builder uses hull, reactor, tank, general systems and that workshop. Independent assertions check mass **570**, volume **420**, generation/demand **120/50**, tank **1000**, **530 BP**, and cost **310 Alloys + 60 Electronics + 20 Reactor Fuel + 60 Composites**. The normal scenario adds the design and one finite qualified home team, not a free built ship.

Supported duty `u` is bounded by one day, lifetime authority and above-floor eligible Reactor Fuel / power-unit count and Composites / automation count. Positive duty pays those support costs even at a full bin or unsuccessful prospect. With positive room and handler, attempted nominal work is `u * min(ratedExtraction, requestedExtractionPerDay)`. Recovery is capped by true remaining Ice, nominal × clamped accessibility, actual room and remaining shared handling. Raw output debits the same physical deposit.

Processing remains **1 Ice + 0.5 Volatiles → 1 Propellant** under existing allocation/capacity rules. Independent tests prove 100 + 50 → 100 gross output, zero output without Volatiles, and later-opening refueling of the same already-authorized freight intent.

## Acceptance matrix

The function names below identify executable cases, not manual test claims. `site_save_tests::earnedCheckpoints` repeatedly saves, loads, fingerprints and continues actual command-earned stages.

| ID | Concrete coverage |
| --- | --- |
| P4B-01 | `development_execution_tests::creation_and_expansion`; `site_integration_tests::current_knowledge_equality`: accepted unfunded registration without free hardware/stocks or scientific gate. |
| P4B-02 | `site_integration_tests::current_knowledge_equality`, `earned_scientific_evidence_does_not_grant_output`; `site_operation_tests::actual_geology_and_non_attempts`: known-state equality and no physical bonus from actual acquired P4A evidence. |
| P4B-03 | `site_package_tests::malformed_definitions_and_rows`; `development_execution_tests::creation_and_expansion`; malformed registration preview/command checks in `current_knowledge_equality`. |
| P4B-04 | `site_package_tests::literal_package_totals`, `proportional_work_and_supported_duty`; `development_execution_tests::deficient_package_and_workshop_constraints`: actual storage-only commissioning; operating limits remain real. |
| P4B-05 | `site_package_tests::reference_builder_totals`; `development_execution_tests::deficient_package_and_workshop_constraints`, `distinct_fuel_action_and_validation`; predecessor ship-design/service capability tests. |
| P4B-06 | `site_integration_tests::shared_engineering_and_typed_decisions`: separate real fleets, one dual-qualified team, equal numeric program IDs, exact owner and named wait. |
| P4B-07 | `development_execution_tests::distinct_fuel_action_and_validation`, `earned_reference`; earned save checkpoints. |
| P4B-08 | `site_package_tests::proportional_work_and_supported_duty`; `development_execution_tests::independent_rows_and_opening_snapshot`, `earned_reference`. |
| P4B-09 | `development_execution_tests::independent_rows_and_opening_snapshot`; unchanged FIFO shipyard regressions. |
| P4B-10 | `development_execution_tests::earned_reference`; `site_operation_tests::commissioning_and_shared_raw_pool`; daily earned persistence checkpoints. |
| P4B-11 | `site_integration_tests::additive_expansion`; paid storage-expansion branch in `site_save_tests::earnedCheckpoints`. |
| P4B-12 | Development suspension/resume tests plus earned outbound/assembly/commissioning save branches in `site_save_tests::earnedCheckpoints`. |
| P4B-13 | `development_execution_tests::cancel_remote_retains_custody`; earned partial-work cancellation persistence branch. |
| P4B-14 | `development_execution_tests::retained_participants_and_completion_controls`; actual return/task identities and actionable completion controls. |
| P4B-15 | `site_integration_tests::earned_and_continued`, `additive_expansion`: closed builders, continuing operation and independent freight/reports. |
| P4B-16 | `site_package_tests::distinct_stock_identities`, `typed_inventory_access`; freight raw/processed persistence and event round trips. |
| P4B-17 | Actual four-commodity cold staging in `earnSiteDevelopmentFixture` and `site_save_tests::earnedCheckpoints`; focused typed freight planning. |
| P4B-18 | `site_operation_tests::competing_collectors_and_real_support_delivery`: actual support unload cannot fund that opening, then the same waiting raw work resumes. |
| P4B-19 | `site_operation_tests::incoming_raw_channels_share_one_bin`, `competing_collectors_and_real_support_delivery`: shared capacity/handler and retained excess cargo. |
| P4B-20 | `site_operation_tests::commissioning_and_shared_raw_pool`: outgoing load does not replenish incoming opening space, while later extraction uses actual room. |
| P4B-21 | Retained freight rules/execution/edge/program-control/save suites, with setup adapted to explicit source base. |
| P4B-22 | `freight_program_execution_tests::raw_same_body_collection_custody`; earned collection voyages and closeout in site save/integration tests. |
| P4B-23 | `site_integration_tests::earned_and_continued`: preauthorized collector waits at zero remote stock and later transports genuinely extracted Ice. |
| P4B-24 | `site_operation_tests::competing_collectors_and_real_support_delivery`: two plans do not reserve stock; actual shared budget limits pickup and retained intent recovers. |
| P4B-25 | `site_operation_tests::remote_payloads_never_refill_engines`; typed floor and predecessor Propellant-cargo separation tests. |
| P4B-26 | `freight_program_execution_tests::raw_same_body_collection_custody`: separate inventories, real handling, no invented travel. |
| P4B-27 | Retained freight lifecycle/amendment tests; raw source-return blocked-space fixture; earned cancellation continuation in site save tests. |
| P4B-28 | `site_operation_tests::duty_authority_and_phase_stock`; half-duty save branch; explicit inadequate policy test in `shared_engineering_and_typed_decisions`. |
| P4B-29 | `site_operation_tests::actual_geology_and_non_attempts`; command-earned `site_integration_tests::unsuccessful_investment`. |
| P4B-30 | `site_operation_tests::actual_geology_and_non_attempts`, `nominal_duty_precedes_recovery_caps`, `physical_precision_and_missing_capability`. |
| P4B-31 | `site_extraction_order_tests::earned_sites_follow_actual_colony_tick`: two command-earned sites and a colony on one body, actual tick recovering 3 then 10 then 0 from reserve13. |
| P4B-32 | `site_operation_tests::commissioning_and_shared_raw_pool`: actual freight 45 then recovery 5 within one supported 50-unit pool. |
| P4B-33 | `site_operation_tests::actual_zero_episode_and_shared_reserve`: genuine attempts, retained acknowledgment, distinct new supply/allowance episode and restored prior acknowledgment. |
| P4B-34 | `site_integration_tests::shared_engineering_and_typed_decisions`: direct/service/command entry points and standalone site source; CLI uses the same runner and canonical label; UI delegates through service. |
| P4B-35 | Opening-stock, commissioning, freight collection, earned processing and later-refueling cases across site operation/save/integration tests. |
| P4B-36 | `site_integration_tests::processing_recovers_existing_intent`; earned isolated 100-Ice/50-Volatiles processing proof in site save tests; retained allocation regressions. |
| P4B-37 | `site_integration_tests::processing_recovers_existing_intent`: newly manufactured fuel is unavailable in its production opening and then fuels the same old program. |
| P4B-38 | `site_operation_tests::actual_geology_and_non_attempts`: recorded outcome survives changed physical geology; P4A observation/analysis isolation regressions retained. |
| P4B-39 | Day30/90 and 360-day reports, policy amendment, closure and paid expansion in site save/integration tests; receipt/cutoff validation. |
| P4B-40 | `site_save_tests::earnedCheckpoints`; exact continuation in `site_integration_tests::earned_and_continued`; predecessor ordering/continuation suites. |
| P4B-41 | `site_save_tests::malformedAndOrdering`, site/development validator tests, strict typed freight rejection and retained transactional save-contract tests. |
| P4B-42 | Save-contract unsupported versions, now including v16; source byte equality after Load and overwrite rejection. |
| P4B-43 | `development_ui_tests`, freight UI tests, site authoring/query tests; owned DTOs, no render mutation, successful-world reset and failed-load preservation. |
| P4B-44 | `site_integration_tests::earned_and_continued` and `site_save_tests::earnedCheckpoints`: day90 operation, at least day360, exact saved continuation, reported runtime and continuing independent obligations. |

## Conservation and knowledge evidence

The earned chain starts with no built vessels or site. Commands build six hulls, commission them using colony Propellant, register an unsurveyed site, authorize four physical supply charters and a destination-base raw collector, and perform elapsed engineering work. Tests reconcile delivered Reactor Fuel against paid duty plus site stock, and delivered Composites against 60 assembly units plus paid duty plus remaining stock. Freight retains per-hull cargo and transfer conservation. Save tests fingerprint stocks, trajectories, cargo, policies, row work/materials, receipts and audits across the same subsequent commands.

No site authoring, package preview, freight plan or readiness path reads physical geology. Site extraction is the new physical interaction boundary. Dated operating results are separate from P4A evidence/assessments and do not establish reserves or profitability. The zero-result earned fixture still completes construction, returns engineers and spends operating support.

## Persistence

Current gameplay reads/writes **v17 only**. Older development files remain disposable: read-only Load rejects them before reconstruction and Save refuses overwrite. There is no migration, compatibility component/site synthesis, or in-place repair. No historical fixture was removed.

Typed stock identities are stored as explicit kind/value pairs. Ordered catalogs, sites, developments, installed groups, package rows, work receipts and histories retain their scoped contiguous ordinals. Daily budgets and reverse ownership remain derived. Historical work/material/capacity/policy/receipt relations are validated before a loaded world is adopted. Existing transaction rollback and failed-Load active-world protection remain in force.

## Verification record

Before implementation, fresh sequential full baseline runs passed:

- Headless: **35/35**, 11.28 seconds.
- UI enabled: **44/44**, 11.23 seconds.
- Baseline UI build emitted three existing ImPlot deprecated enum-combination warnings; no project compile errors.

Final full-suite gates on the completed implementation:

| Gate | Result |
| --- | --- |
| Full headless configure/build | Passed; no project warnings in the final build |
| Full headless CTest | **41/41 passed**, 20.45 seconds |
| Full UI-enabled configure/build | Passed; no project warnings in the final build |
| Full UI-enabled CTest | **51/51 passed**, 20.61 seconds |
| `git diff --check` | Passed |
| CLI smoke | Passed, exit0; day10, one ship/fleet |
| Useful and zero-outcome earned exports | Passed, schema 17; both foreign-key checks empty |
| Native click-through | Not performed; no display connection/native control |

These are locally executed results, not claims of registered GitHub CI checks.

Commands used for the headless build (the baseline used `build-p4b-baseline` with the same switches):

```sh
cmake -S . -B build-p4b -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=OFF \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0
cmake --build build-p4b --parallel 2
ctest --test-dir build-p4b --output-on-failure -j 2
```

UI-enabled configuration (the baseline used `build-p4b-ui-baseline`):

```sh
cmake -S . -B build-p4b-ui -G Ninja \
  -DDEEP_SIGNAL_BUILD_TESTS=ON -DDEEP_SIGNAL_BUILD_SAVE=ON \
  -DDEEP_SIGNAL_BUILD_APP=ON -DDEEP_SIGNAL_BUILD_UI=ON \
  -DSQLite3_INCLUDE_DIR=/home/daniel/.o3de/3rdParty/packages/SQLite-3.37.2-rev1-linux/SQLite \
  -DSQLite3_LIBRARY=/lib/x86_64-linux-gnu/libsqlite3.so.0 \
  -DSDL3_DIR=/home/daniel/.local/deep_signal_deps/sdl3/lib/cmake/SDL3
cmake --build build-p4b-ui --parallel 2
ctest --test-dir build-p4b-ui --output-on-failure -j 2
git diff --check
```

The complete headless and UI CTest runs are sequential to avoid predecessor fixed-path SQLite collisions. Parallel agents used separate build directories; their focused results are additional evidence, not substitutes for these gates.

CLI and fixture checks:

```sh
build-p4b/deep_signal_cli
build-p4b/deep_signal_cli --write-site-development-fixture /tmp/deep-signal-p4b-artifacts/useful.sqlite
build-p4b/deep_signal_cli --write-site-development-zero-fixture /tmp/deep-signal-p4b-artifacts/zero.sqlite
sqlite3 /tmp/deep-signal-p4b-artifacts/useful.sqlite 'SELECT version FROM schema_version; PRAGMA foreign_key_check;'
sqlite3 /tmp/deep-signal-p4b-artifacts/zero.sqlite 'SELECT version FROM schema_version; PRAGMA foreign_key_check;'
```

Both exports reached day90 and schema 17 with no foreign-key violations. Both earned five installed groups and paid 37 duty-days (37 Reactor Fuel, 37 Composites). The useful fixture recorded 21 attempts / 210 Ice recovered, 10 Ice delivered and 10 gross Propellant produced by the isolated supply base by day 90; full storage during long transit explains nonattempt days. The zero fixture recorded 37 attempts / zero recovered Ice. Neither exported fixture has observations or assessments. These are finite shipments and the retained physical transit model, not an invented instantaneous supply loop.

The separate earned day90→360 continuation test completed in approximately 0.4 seconds on this environment, with 826 audit events and 168 genuine attempts in the useful branch before the last focused rerun; final values are verified in its output. These are bounded fixture measurements, not general performance guarantees.

Native interaction was not performed: this execution environment exposes neither `DISPLAY` nor `WAYLAND_DISPLAY`, and native app control is unavailable. The real ImGui submission tests, UI-enabled build, owned-query checks and reset tests are reported separately; they do not prove native visual usability.

### Findings resolved during implementation

Tests and bounded code cross-checks found and corrected:

- A residual schema-version SQL constraint still naming16.
- Missing nonnegative validation for the new gross production counters.
- Predecessor test setup assuming the freighter was the last class.
- Multi-row proportional-cost rounding residue preventing a final paid assembly step.
- Shared engineer custody being obscured by a physical-location explanation.
- Nominal extraction scaling/cap order at partial duty and constrained room/handling.
- Acknowledged unsuccessful-extraction episodes masking a later support shortage.
- Unrepresentable extraction or processing transfers creating an unequal inventory delta.
- Preview/command disagreement for assigned new-site IDs and invalid institution references.
- Pending site issues surviving explicit operating-policy responses.
- New work/audit numerical limits and historical policy validation.

No predecessor behavioral assertion was deleted to obtain a passing model. Setup and serialization expectations changed only for new catalog entries, typed cargo/base identity and the active schema.

## Limits and residual scope

- No native desktop click-through is claimed by offscreen ImGui tests or a successful compile. Actual display availability and any startup attempt are recorded separately at handback.
- No local site refining, equipment-lot manufacture/genealogy, population, crews, recruitment, salvage, terrain/depth hazards, remote rescue/refueling, priorities/global scheduler, Mission Control, research, random failure or final propulsion was added.
- Operating records are not yet formal analysis inputs. They do not automatically create P4A observations/assessments.
- Existing closed-survey reports, shipyard cancellation and unleased manual-cancellation debt remain outside scope.
- Whole simulation days are not globally transactional. New coupled actions prepare/check their own physical deltas; SQLite snapshot replacement remains transactional.
- Current transit still ignores cargo mass. Catalog numbers are fixtures rather than final balance.
- The inspection exporter explicitly acknowledges disclosed issues to continue a bounded demonstration; it does not repair resources or trajectories to force success.
- Human play review and the remaining parent-scope items remain outstanding milestones. No P4C/P5 work was started.
