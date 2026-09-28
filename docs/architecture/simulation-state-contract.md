# Simulation state contract

This document records the H1A processing-configuration contract and the H1B
save-continuity contract. The in-memory `GameState` remains the authority for
gameplay; SQLite stores explicit snapshots, not a second live world or a replay
stream.

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

An active preset has a separate, existing execution cutoff: if its derived
weight total is at or below `kProcessedMaterialComparisonEpsilon`, it allocates
zero processing capacity. This includes Stockpile Recovery when large processed
stockpiles make all six derived weights tiny. Effective and draft preset
percentages, and forecast output, use the same active cutoff. It does not make
small positive dormant Manual storage invalid; that storage still has checked,
normalized working shares. Active Manual instead requires a total above the
cutoff at admission.

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

## H1B: durable ordering

Order within `GameState` collections is part of continuation behavior. Mining
visits colonies and deposits in vector order. Shipyard orders consume each
colony's daily capacity in vector order, so ID order is not FIFO priority. Fleet
order and per-fleet ship-roster order can determine event sequencing and fuel
payment. Save derives ordering metadata from the current vectors without sorting
the live state, changing IDs, or adding ordinal fields to domain records.

A valid shipyard order is accepted even when its colony has zero capacity. The
active order remains in FIFO position without progress until capacity is available;
capacity waiting is derived from current state rather than persisted as a status.

Schema v11 stores a global zero-based `ordinal` for each of these durable
vectors:

| `GameState` vector | SQLite table |
| --- | --- |
| `starSystems` | `star_systems` |
| `institutions` | `institutions` |
| `people` | `people` |
| `bodies` | `bodies` |
| `colonies` | `colonies` |
| `mineralDeposits` | `mineral_deposits` |
| `shipClasses` | `ship_classes` |
| `shipyardOrders` | `shipyard_orders` |
| `ships` | `ships` |
| `fleets` | `fleets` |

Ordered child collections have their own scope. `appointments` keeps its global
ordinal; `colony_processing_allocations` keeps an ordinal per colony; and
`fleet_order_queue` keeps an ordinal per fleet. A Ship row has **two independent
positions**: `ships.ordinal` reconstructs `GameState::ships`, while
`ships.fleet_ordinal`, unique within its `fleet_id`, reconstructs that Fleet's
`shipIds` roster. Reconstructing the roster by global Ship order would lose a
different ordering contract. Resource and ship-class cost arrays remain keyed
by mineral/material enum index, while metadata and ID counters remain keyed by
name. `event_log` remains ordered by Event ID, with strictly increasing IDs and
nondecreasing event days validated; it has no second ordinal.

The v11 writer assigns contiguous ordinals beginning at zero. Schema
constraints require non-null integer, nonnegative, unique values in each scope;
the reader also checks storage type and contiguity before accepting a sequence.
Every ordered read uses explicit `ORDER BY`. Missing, duplicate, fractional,
negative, or gapped v11 order data is rejected rather than reconstructed in
legacy ID order. Empty collections are valid.

## Schema versions and destination policy

H1A wrote schema v10. H1B writes **v11 only** and reads validated v10 or v11.
A v10 Load is read-only: it applies the established legacy ID/child-ordinal
reconstruction and current domain validation, without upgrading the file or
inventing ordering data that v10 never stored. The authentic
[v10 fixture](../../tests/fixtures/README.md) demonstrates the limit: a valid
source shipyard FIFO `[2, 1]` reloads as `[1, 2]`, changing which order
completes on the next day. A v11 Load requires the v11 table, column, key, and
foreign-key shape and all its ordering checks. A version marker alone does not
make either structure valid. Unsupported, malformed, or mismatched versions
reject; there is no fallback from a failed v11 read to the v10 reader.

Destination recognition compares the user schema object set and each expected
table's `table_xinfo`, `foreign_key_list`, and index shape with a freshly built
schema reference. The v10 check uses the corresponding legacy columns and
indexes. Save rejects user triggers even when they target known tables, because
their write effects cannot be trusted as part of the snapshot contract.
Read-only Load may tolerate triggers attached to known tables; they cannot
change the reconstructed rows during that operation. A test-only build injects
a post-deletion failure to prove rollback without exposing a production failure
switch. The check does not require
byte-identical `CREATE TABLE` text or silently add missing columns.

Save accepts a new path, a schema-empty database, or an existing compatible,
valid v11 save. It gives v10 new-path guidance only after checking both the
legacy structure and complete logical snapshot; a marker of `10` alone does
not identify a valid v10 save. Unsupported, malformed, mismatched, or
unrecognized databases reject without that guidance or attempted repair.
A database with unrelated user schema objects is not empty.
There is no in-place v10 migration or automatic downgrade. A player may load a
valid v10 game and save its reconstructed state to a **new** v11 destination;
the original v10 ordering information that was never stored remains lost.

Save validates its input state before opening the destination. The connection
enables foreign keys before an immediate write transaction. On that same
connection and inside that transaction it classifies the destination, verifies
an existing v11 snapshot before replacement, creates v11 schema only if empty,
replaces rows, rereads the new snapshot, and commits only after validation.
Load opens read-only, checks version-specific structure and foreign keys, and
validates a detached snapshot within one read transaction before returning it.
A failed replacement rolls back the previous valid save's **logical** contents
and schema. A failed first save may leave an empty new file; neither path
promises byte-identical files after every open or recovery from power loss and
arbitrary filesystem failure. Failed application Load leaves the active world
and its workflows intact.

## Session data and continuation limits

`dailyEconomySnapshots` is current-session telemetry and is not saved. Loaded
games start with an empty telemetry vector until later days run; compare newly
emitted telemetry after the continuation point rather than expecting a loaded
copy of prior session samples. Selection, previews, world-generation UI
references, processing-editor drafts, and window geometry are also outside the
game snapshot. Successful Load replaces the world and clears old-world
interaction and editor state through the existing lifecycle.

The v11 ordering contract supports comparisons of durable state, ordered
children, counters, and meaningful event order when an unsaved and reloaded
simulation continue under the **same build and same inputs**. It does not
promise bitwise identical floating-point results across compilers, platforms,
or build flags, or unchanged outcomes after gameplay rules change. The H1A
processing checks still reject previously accepted malformed extreme-weight
saves; v10 read compatibility is for valid snapshots, not automatic repair.

## Review evidence

H1A evidence should include baseline reproduction of same-material and
cross-material overflow, direct allocation-rule cases at the epsilon boundary,
hand-calculated Manual shares and percentages, command rejection with only the
permitted audit effect, imported-state rejection, daily processing output, and
agreement of read-only query and forecast projections with independent expected
values. A valid near-maximum finite weight must retain finite shares and
percentages. Build and test results, native UI checks, and ordinary v10
Save/Load checks belong in the H1A review report with executed and unperformed
checks clearly distinguished. H1B evidence should include the pinned v10
baseline ordering failure, fixture provenance, current-format round trips and
same-build continuation, legacy read/no-upgrade behavior, rejection and
transaction preservation, and running-UI Save/Load checks. This document
states the contract; it does not itself certify that every check ran.
