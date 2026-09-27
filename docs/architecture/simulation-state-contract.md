# Simulation state contract

This document records the H1A processing-configuration contract and the current
limits of simulation state persistence. H1B save continuity is a separate,
pending slice. The in-memory `GameState` remains the authority for gameplay;
SQLite stores explicit snapshots, not a second live world or a replay stream.

## Ownership and trust boundaries

`Simulation` owns the active `GameState`. UI intentions and application workflows
submit `SimCommand` values through `SimulationService`; query and forecast
services return owned presentation values and do not mutate gameplay state.
Scenario builders and the SQLite loader can assemble detached state, but
`validateGameState` must accept it before the `Simulation` constructor admits it.
Save also validates the state it is asked to persist. Validation throws
`std::runtime_error` on an invalid snapshot and does not repair it.

Access is serialized and single-threaded. A borrowed `GameState` reference is
not a snapshot; pointers, iterators, or references into its vectors must not be
retained across mutations that may invalidate them.

## H1A: processing allocations

Manual `ProcessingAllocation` rows are relative weights, not percentages. Rows
may repeat a material and may contain zero weight. Every supplied or stored row
must name a known `ProcessedMaterial` and have a finite, nonnegative weight.
Duplicate rows are added to their material subtotal in supplied order. The
completed subtotals are then added in fixed `ProcessedMaterial` index order.
Each subtotal and the combined total must remain finite and representable as a
`double`. Invalid rows or overflow are rejected; they are not clamped, skipped,
repaired, or converted to a preset.

An active `Manual` policy requires a combined total strictly greater than
`kProcessedMaterialComparisonEpsilon` (`1e-9`). The threshold applies to the
total, not to individual positive rows: several small rows can together form a
valid Manual allocation. A preset may retain an empty or zero-total stored
Manual configuration, but its dormant rows still must satisfy the material,
weight, subtotal, and combined-total checks. A request to select a preset also
validates every supplied Manual row even though that request does not install
those rows.

For a valid positive total, a material's capacity share is its subtotal divided
by that total; a displayed percentage is the share multiplied by 100. Shares
must be finite and nonnegative, bounded by one within the documented
floating-point tolerance, and sum to approximately one. Ordinary rounding and
negligible-share underflow are possible. Zero-total dormant storage has no
active Manual distribution: simulation rules neither divide by zero nor invent
equal shares. The processing editor may separately initialize an unapplied
draft for usability.

The shared simulation-layer allocation rule supplies these checks and derived
shares to the processing-policy command, `validateGameState`, the active Manual
daily calculation, stored and draft application projections, and Manual
forecast calculations. It derives fixed-size working values without sorting,
coalescing, or rewriting the persisted row vector. Preset weights, processing
recipes, recipe order, daily phase order, capacity rules, and forecast horizons
remain unchanged. A nominal allocation preview is not a promise of output:
available raw inputs can reduce actual production, and the forecast is advisory.
The H1A checks cover processing allocations; they are not a general guarantee
that every numeric calculation in the game is overflow-safe.

## Processing command effects and exceptions

An ordinary rejected `SetColonyProcessingPolicyCommand` returns a failed
`CommandResult`. It leaves gameplay records, inventories, capacities, orders,
the date, gameplay entity counters, and prior audit history unchanged. As with
other rejected commands, it appends exactly one `CommandRejectedEvent`, consuming
one Event ID. Rejection therefore does **not** mean byte-for-byte `GameState`
identity.

For an accepted request, the command resolves its Colony and validates the
policy and all supplied allocation rows before committing. It prepares the
replacement Manual vector and success result before changing the Colony. A
Manual request installs the submitted rows in their original order and with
their original values. A preset request changes the policy while preserving the
last stored Manual rows exactly. Preparation failures before commit leave the
Colony configuration unchanged; the final commit uses nonthrowing operations.
This is a focused guarantee for this command, not global rollback or a strong
exception guarantee for every command. The simulation does not catch allocation
failures and does not copy the world for each command.

## Persistence and session data

H1A leaves the SQLite format at schema v10; it adds no migration. Ordinary valid
v10 saves remain supported. The tighter processing rule intentionally rejects
constructed states and previously accepted malformed extreme-weight saves whose
per-material or combined totals are unrepresentable. It does not rewrite such
input into a usable configuration.

`dailyEconomySnapshots` is current-session telemetry and is not saved. Loaded
games start with an empty telemetry vector until later days run. Selection,
previews, world-generation UI references, processing-editor drafts, and window
geometry are also outside the game snapshot. A failed Load preserves the active
world and its workflows; successful Load replaces the world and clears old-world
interaction and editor state through the existing lifecycle.

## Ordering and H1B boundary

Order within in-memory collections matters to existing rules, including FIFO
shipyard work, shared fuel payment, and overlapping resource use. Manual
allocation rows themselves retain submitted order, while their derived
material subtotals use a fixed material order for normalization. H1A changes no
SQLite ordering or transaction behavior. Schema v10 must not be assumed to
preserve every durable collection or child-list order after Save/Load; H1A makes
no continuation-fidelity claim for those cases.

H1B execution awaits H1A review and a separate continuation instruction. Its
intended contract is to write schema v11 with explicit durable ordering, validate v11
reads, keep read compatibility for valid v10 saves, and compare same-build
post-load continuation against an unsaved run. Until that slice is implemented
and tested, no v11 write support, v10-to-v11 migration, or ordered Save/Load
continuity is claimed here. Neither slice promises bitwise results across
compilers or unchanged outcomes after gameplay rules change.

## Review evidence

H1A evidence should include baseline reproduction of same-material and
cross-material overflow, direct allocation-rule cases at the epsilon boundary,
hand-calculated Manual shares and percentages, command rejection with only the
permitted audit effect, imported-state rejection, daily processing output, and
agreement of read-only query and forecast projections with independent expected
values. A valid near-maximum finite weight must retain finite shares and
percentages. Build and test results, native UI checks, and ordinary v10
Save/Load checks belong in the H1A review report with executed and unperformed
checks clearly distinguished. This document does not itself certify that those
checks ran.
