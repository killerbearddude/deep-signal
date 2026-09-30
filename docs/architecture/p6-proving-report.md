# P6 automated whole-loop proving report

## Status and provenance

P6 is an **automated proving implementation with native human acceptance
pending**. The branch is `p6-whole-loop-proving` against the exact integrated P5
baseline `55f402686b41859dda2dcafede677e743cddd505`. Local `master`,
`origin/master` and the branch starting commit were verified at that SHA before
editing. The initial working tree was clean, pinned ImGui/ImPlot submodules were
present, and `src/save/Schema.h` declared v18. No P6 gameplay field or schema
v19 was added. The tested implementation commit is
`1c17df893b84203b6ce984faf99cbf5287884173`; [PR #15](https://github.com/killerbearddude/deep-signal/pull/15)
is open for review and remains unmerged. The final documentation/PR head is
recorded in the handback after publication.

The package's `START_HERE.md`, P6 handoff, scope decisions, canonical scenarios,
parent map, human protocol, source basis, current state contract, P5 report and
parent playable-slice specification were reviewed. Root `AGENTS.md` governed
build and submodule handling.

## Verification

Complete baseline suites were run sequentially before repository edits; the
final complete suites were also run sequentially because predecessor save tests
have fixed temporary SQLite paths. Configurations used the existing local
SQLite3 and SDL3 development paths without adding project dependencies.

| Gate | P5 integrated baseline | P6 final |
| --- | --- | --- |
| Full headless configure/build | Passed | Passed |
| Full headless CTest | **48/48**, 28.40 s | **56/56**, 36.40 s |
| Full UI-enabled configure/build | Passed | Passed |
| Full UI-enabled CTest | **59/59**, 27.32 s | **67/67**, 36.73 s |
| `git diff --check` / staged diff | Not applicable before edits | Passed during final staging |
| Native SDL3/Dear ImGui click-through | Not run in this environment | Pending H1–H7 human review |

Exact configure/build/test commands:

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
git diff --cached --check
```

Baseline build output was viewed through the terminal and not retained as a
complete warning log. Final build logs were captured under `/tmp` and contained
no compiler warnings. UI compilation, offscreen ImGui tests and save checks are distinct
from the missing native human interaction evidence.

## S1–S8 canonical scenario outcomes

| Scenario | Automated outcome | Evidence |
| --- | --- | --- |
| S1 — investigated established supply | Command-earned class revision and seven hulls; delegated field observation precedes finite analysis and assessment; real site authorization, freight deliveries, builder return, Ice recovery and colony Propellant production follow. No P5 program is required. | `deep_signal_p6_whole_loop_tests`, P6 fixture, accounting/continuation tests. |
| S2 — blind useful site | Same structurally valid unknown-site authorization, five commissioned modules, supported Ice recovery, delivery and Propellant output without any target observation or assessment. | `deep_signal_p6_whole_loop_tests`, leakage comparison. |
| S3 — blind zero-yield twin | Same pre-interaction projections/admission and physical construction; at least five positive-nominal zero-recovery attempts spend support and raise only the observed zero-result issue. | Whole-loop, leakage and accounting tests. |
| S4 — earned Precision success | Three actual tests establish threshold 6; prototype hull, later serial process/hull, exact-team support, weak-signal field detection and ordinary specialist maintenance are earned. | `deep_signal_p6_technical_tests`, accounting and retained P5 integration tests. |
| S5 — earned Precision miss | Threshold 9 misses public target 7, raises an observed issue, survives acknowledgment, does not detect signal 7, and leaves established equipment usable when later maturity is declined. | Technical and leakage tests. |
| S6 — finite contention | Field science blocks duplicate analysis labor; real engineering and freight ownership remain exclusive; two technical programs share one finite facility with accurate zero/partial conditions; distinct freighters cannot load the same finite source stock twice. | `deep_signal_p6_contention_tests`, retained program-control tests. |
| S7 — continuation | Ten command-earned cross-system checkpoints, each compared immediately after Load and for five later days. Selected stable paths compare 30-day block advancement with daily calls; a same-day post-report command retains prior report rows. | `deep_signal_p6_continuation_tests`. |
| S8 — five-year standing run | Earned S1 world advances 1,825 days with annual validation, Save/Load, independent table comparison and seven-day continuation samples. A supplemental earned P5 world retains single one-time technical artifacts for another 1,825 days. | `deep_signal_p6_soak_tests`, annual table below. |

The S1 fixture authors only starting geometry, hidden geology, existing
catalogs/people/teams/facility capacity and finite stocks. Design revisions,
hulls, observation batches, assessments, site modules, freight transfers and
production totals are created by normal commands and elapsed execution. The
S4 reference fixture similarly earns P5's design, physical tests, classes,
prototype consumption, process, support and maintenance.

## Hidden information and accounting

The geology twin comparison covers body overview, declared mineral channels,
raw evidence, exploration summaries, site preview/admission, registered cold
site summaries, freight endpoint/readiness advice, knowledge-limited mineral
and processed forecasts, ship class/catalog consequences, and pending-program
counts. The threshold-6/9 twin compares public opportunity/status, unearned
component catalog/profiles, ship design, technical preview/work/material bill,
program admission and common forecasts before physical tests. Identical
well-formed intentions receive identical pre-interaction admission results.

A scoped source audit of `src/app` and `src/ui_imgui` found no direct hidden
geology or candidate-truth read in player query/UI code. The direct reads under
`src/app` occur only in the existing command-earned technical fixture builder.
This static finding supplements the executed DTO equality tests; it is not
treated as sufficient evidence alone.

Independent P6 ledgers reconcile:

| Boundary | Tested equation or identity |
| --- | --- |
| Ordinary shipyard | Colony hull-material debit equals immutable evaluated class cost; one-day BP requirement fits finite yard capacity. Commissioning Propellant debit equals actual new-hull tank credits separately. |
| Prototype hull | Frozen current-hull plan subtracts exactly one developed component's serial material and BP. The actual completion debit matches its frozen material plan; exactly one physical integration receipt names the produced ship. |
| Freight custody | Per program, cumulative load equals delivered plus source-returned plus cargo still on exact hulls; dated transfer receipts match counters and commodity identities. |
| Site extraction | Initial physical Ice minus final physical Ice equals summed recovered Ice; recovered Ice equals final site raw stock plus net physical Ice loaded, after source returns. |
| Site support | Actual delivered Reactor Fuel/Composites equal retained site stock plus dated supported-duty debits and assembly inputs. Positive zero-yield attempts still pay support. |
| Processing | Delivered Ice minus final colony Ice equals gross Propellant produced; Volatiles consumed equal half gross output. Starting inventory and tank transfers are not counted as production. |
| Optional technical/service | Five finite stage-work totals and 40 Alloys/145 Electronics/61 Composites reconcile to receipts. Colony debits also reconcile technical spending, all serial hull costs, one embodied prototype credit and ordinary specialist maintenance parts. |
| Phase boundaries | First actual Ice unload can feed later same-day processing; produced Propellant cannot fund a program opening already passed. New process/support qualifications retain D+1 dates. Existing site/freight/technical first-use tests remain in the full suite. |

The P6 established run also produces a pre-existing freight fuel wait at day
459 in the long-session branch. The *same* program loads fuel at day 487, only
after new Propellant existed before that opening. Repeated future fuel issues
occur after new shipments and fresh physical work, not as unchanged daily
prompts. Acknowledgment changes no stock or ship fuel by itself.

## S7 continuation evidence

Checkpoints: (1) zero-capacity ship order; (2) paid survey transit; (3) acquired
observation under partial analysis; (4) loaded freight in transit; (5) partial
field assembly; (6) commissioned site with acknowledged operating issue;
(7) first complete technical test; (8) prototype reserved to an unfinished
hull; (9) active advanced service history; and (10) a global report boundary
followed by a same-day processing-policy command.

Each uninterrupted and reloaded branch advances five later days under the
same inputs and matches its decision result, live date/IDs/ship fuel/cargo and
colony stocks. A separate test-only reader enumerates all v18 tables and
columns, sorts logical rows while retaining stored ordinal values, and compares
the full persisted snapshots independently of insertion order. It does not
replace the live-state checks or assert cross-build floating-point identity.
Three selected starting states also match after one 30-day block versus thirty
daily calls at the same first decision boundary.

## S8 annual measurements

The main soak begins at earned day 100 and ends at day 1,925. The following
sampled local run records fixed 365-day blocks; seconds are measurements, not
portable performance gates. Annual source/receipt growth follows actual
standing freight and extraction work. A closed SurveyProgram stays at one
historical report.

| Year | Day | Block seconds | Events / added | Extraction receipts / added | Freight receipts / added | Site reports | Closed survey reports | Assessments | Ships |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 465 | 0.080 | 1,349 / 1,088 | 203 / 182 | 323 / 267 | 15 | 1 | 1 | 7 |
| 2 | 830 | 0.160 | 2,085 / 736 | 388 / 185 | 404 / 81 | 27 | 1 | 1 | 7 |
| 3 | 1,195 | 0.192 | 2,822 / 737 | 573 / 185 | 485 / 81 | 39 | 1 | 1 | 7 |
| 4 | 1,560 | 0.232 | 3,564 / 742 | 758 / 185 | 570 / 85 | 52 | 1 | 1 | 7 |
| 5 | 1,925 | 0.274 | 4,298 / 734 | 941 / 183 | 651 / 81 | 64 | 1 | 1 | 7 |

Site installation groups and assessments also remain unchanged. Technical
demonstration, prototype integration, process and support records each remain
single in the separate 1,825-day P5-history branch. Both branches validate
annually or at completion. Yearly v18 loads match the uninterrupted world and
seven later days of execution. No negative/nonfinite stock or duplicate asset
identity occurred. Fixed-length block runtime rises with accumulated history:
`siteDutySpent` scans all earlier duty receipts during readiness, and periodic
site reports scan receipt vectors. At the measured five-year scale this is not
a gameplay blocker; [R21](p6-residual-register.md) records the performance
follow-up without adding a persisted cache or a machine-specific speed gate.

## Narrow corrections

| Class / ID | Reproducer and correction |
| --- | --- |
| A / P6-A1 (R11) | New survey regression first failed because a closed program kept publishing later reports. A dated closure audit now permits a due closure-day report once and stops later reports. Validation accepts frozen closed cursors and contiguous historical v18 rows; date-limit preflight ignores closed future reports. Existing history is not deleted. |
| B / P6-B1 | S6 first proved later technical work received zero shared facility capacity while its query said ready. Scratch next-opening projection now debits earlier eligible technical work, reports full contention or positive partial share, and leaves execution/order untouched. A suspension regression proves the same later intent begins when capacity returns. |

## Native review and closeout boundary

The seven-save v18 review pack is generated by
`deep_signal_cli --write-p6-human-review-pack DIRECTORY`. Each save is reloaded,
validated, checked with `PRAGMA foreign_key_check`, and advanced through an
equivalent next-day branch before the directory is published. The CLI refuses
an existing destination. A pack generated from the tested implementation commit
is available at `/tmp/deep-signal-p6-review-1c17df8`; direct SQLite checks on
all seven files returned v18 and no foreign-key rows. See
[native review worksheet](p6-human-play-review.md)
for H1–H7. Neither `DISPLAY` nor `WAYLAND_DISPLAY` exists in this session, so
the mandatory interactive H1–H7 result is **pending**. The branch is ready for
code review, but P6 cannot be called fully accepted or merged on automation
alone.

No P7 or Class C mechanic was implemented. The
[residual register](p6-residual-register.md) records the deferred parent scope
and the measured performance observation.

## P6-01–44 evidence map

`Proven` means exercised by P6; `Retained` means the accepted predecessor test
was rerun in the complete suite; `Pending` requires native human evidence.

| ID | Status | Evidence or limit |
| --- | --- | --- |
| P6-01 | Proven | Pinned baseline, schema, clean tree and full baseline suites. |
| P6-02 | Proven | S1 command-earned established supply chain. |
| P6-03 | Proven | Seven physical hulls and new revision ordered and commissioned. |
| P6-04 | Proven | Timed field batch then finite analysis precede site intention. |
| P6-05 | Proven | Builder fleet/team return; five installed groups and operation persist. |
| P6-06 | Proven | Ice freight and exact gross Propellant recipe. |
| P6-07 | Proven | Day-459 waiting freight ID refuels at day 487 after new output. |
| P6-08 | Proven | S2/S3 public DTO/forecast/preview equality. |
| P6-09 | Proven | Same pre-interaction site command accepted in both twins. |
| P6-10 | Proven | Blind useful site extracts/delivers normally. |
| P6-11 | Proven | Blind absent site pays support and reveals observed zero result only. |
| P6-12 | Proven | Same physical site/equipment continued with and without acquired observation; receipts equal. |
| P6-13 | Proven | Threshold-6/9 technical DTO/admission equality before tests. |
| P6-14 | Proven | S4 actual test, prototype, process and support artifacts. |
| P6-15 | Proven | Normal observation: threshold 10 misses signal 7; earned 6 detects it. |
| P6-16 | Proven | Normal worn-component maintenance uses exact specialist support. |
| P6-17 | Proven | Measured threshold 9 issue and acknowledgment; signal 7 still missed. |
| P6-18 | Proven | Target-miss branch declines further maturity while established component remains. |
| P6-19 | Proven | Shared science/engineering owners prevent duplicate labor. |
| P6-20 | Proven | Separate freight stock, fleet and technical facility budgets spent once. |
| P6-21 | Proven | Accepted fuel limitation waits without daily repeat; later episode follows new shipment work. |
| P6-22 | Retained | `program_control_tests` checks same-opening release and later reacquisition. |
| P6-23 | Proven | Ordinary design cost/BP and separate exact tank transfer ledger. |
| P6-24 | Proven | Frozen prototype material/BP credit, actual debit and one integration receipt. |
| P6-25 | Proven | Freight load/delivery/return/onboard receipts and exact hull custody. |
| P6-26 | Proven | Hidden Ice depletion and site raw/export equation. |
| P6-27 | Proven | Supported duty costs also paid on zero recovery. |
| P6-28 | Proven | Delivered Ice and Volatiles reconcile to gross Propellant. |
| P6-29 | Proven | Same-opening Ice processing, next-opening fuel, D+1 maturity and retained site/freight/technical phase tests. |
| P6-30 | Proven | Ten Save/Load checkpoints continue five days with complete logical v18 comparison. |
| P6-31 | Proven | Selected 30-day block/daily boundary and state comparison. |
| P6-32 | Proven | Same-day post-report command leaves published snapshot immutable. |
| P6-33 | Proven | Two 1,825-day worlds validate and retain one-time site/science/technical artifacts. |
| P6-34 | Proven | Annual runtime/cardinality table and R21 finding. |
| P6-35 | Proven | R11 red regression, corrected closure-day and future-report behavior. |
| P6-36 | Proven | Complete v18/malformed/rollback predecessor suites rerun. |
| P6-37 | Proven | Seven earned v18 saves, reloaded/FK-checked/continued; pack test. |
| P6-38 | Pending | Native interactive H1–H7 review. |
| P6-39 | Pending | Native H3 blind-development controls. Automated DTO equality passed. |
| P6-40 | Pending | Native H7 90-day player intervention count. Automated soak passed. |
| P6-41 | Proven | Final parent A01–A24 map below. |
| P6-42 | Proven | R01–R21 register, with human findings to append later. |
| P6-43 | Proven | Final complete headless/UI builds, suites and diff checks after the last code edit. |
| P6-44 | Proven | PR #15 is open and unmerged at the tested implementation branch; native acceptance remains pending. |

## Parent A01–A24 coverage

`Retained` means predecessor implementation and its focused tests passed again;
`Partial` identifies a bounded model or missing native review. None of the
deferred items is relabeled complete merely to close the map.

| Parent ID | Status | Evidence or residual |
| --- | --- | --- |
| A01 | Proven | P1 zero-capacity order in P6 checkpoint and matrix. |
| A02 | Retained | P1 waiting-order Save/Load and same-order recovery tests. |
| A03 | Deferred | Shipyard suspend/cancel absent; R09. |
| A04 | Proven | S6 finite people/fleet/facility/stock contention. |
| A05 | Proven | S1 concurrent independent intentions and retained site/program tests. |
| A06 | Retained | P2 mission-unsuitable but buildable class tests. |
| A07 | Partial | Real Propellant transfer/wait proven; crew/stores and later provisioning command absent, R08. |
| A08 | Partial | Component capability/maintenance proven; native H2 and final propulsion/crew model remain pending/deferred. |
| A09 | Partial | Automated 90-day delegated period proven; native H7 pending, no staffed Mission Control. |
| A10 | Retained | Distinct reproducible leader approach/order tests. |
| A11 | Proven | S7 block/daily durable and decision comparison. |
| A12 | Partial | Field/analysis share one science team; technical engineering uses separate pool, R05. |
| A13 | Proven | Public geology/technical twins and same admission. |
| A14 | Proven | S2 blind useful development. |
| A15 | Proven | S3 blind zero-yield investment with real support cost. |
| A16 | Retained | Valid no-detection and instrument-limit scientific tests. |
| A17 | Retained | Analysis cannot infer absent measurement; provenance/assessment tests. |
| A18 | Proven | S1 physical collection/processing and S7 mid-transit continuation. |
| A19 | Retained | Freight cancellation and physical settlement focused suite. |
| A20 | Partial | Finite bottleneck and accepted limitation proven; broader explicit reallocation/native H5 remains. |
| A21 | Proven | S4 and S5 optional real technical dependencies. |
| A22 | Proven | S1 complete useful path with no P5 authorization. |
| A23 | Partial | Site/industry/component capability persists; native H6 and formal operating-evidence analysis pending/R14. |
| A24 | Proven | Strict validation, malformed v18 rejection and transactional save suites rerun. |

The native worksheet and residual register remain live closeout inputs. The
automated result establishes the bounded integrated behavior above; it does not
certify native comprehensibility or deferred parent mechanics.
