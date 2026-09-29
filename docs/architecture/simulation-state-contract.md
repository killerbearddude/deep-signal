# Simulation state contract

This document records the H1A processing-configuration, H1B save-continuity,
P1 shipyard-intent, P2 vessel-design, and P3A delegated-survey contracts. The in-memory `GameState` remains the authority for
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

Schema v11 introduced a global zero-based `ordinal` for each of these durable
vectors:

| `GameState` vector | SQLite table |
| --- | --- |
| `starSystems` | `star_systems` |
| `institutions` | `institutions` |
| `people` | `people` |
| `bodies` | `bodies` |
| `colonies` | `colonies` |
| `mineralDeposits` | `mineral_deposits` |
| `shipComponents` (v12) | `ship_components` |
| `surveyTeams` (v13) | `survey_teams` |
| `surveyPrograms` (v13) | `survey_programs` |
| `shipClasses` | `ship_classes` |
| `shipyardOrders` | `shipyard_orders` |
| `ships` | `ships` |
| `fleets` | `fleets` |

Ordered child collections have their own scope. `appointments` keeps its global
ordinal; `colony_processing_allocations` keeps an ordinal per colony; and
`fleet_order_queue` keeps an ordinal per fleet. P3A target, receipt, and report
rows keep an ordinal per program. A Ship row has **two independent
positions**: `ships.ordinal` reconstructs `GameState::ships`, while
`ships.fleet_ordinal`, unique within its `fleet_id`, reconstructs that Fleet's
`shipIds` roster. Reconstructing the roster by global Ship order would lose a
different ordering contract. Resource and component cost arrays remain keyed
by mineral/material enum index, while metadata and ID counters remain keyed by
name. `event_log` remains ordered by Event ID, with strictly increasing IDs and
nondecreasing event days validated; it has no second ordinal.

The current v13 writer assigns contiguous ordinals beginning at zero. Schema
constraints require non-null integer, nonnegative, unique values in each scope;
the reader also checks storage type and contiguity before accepting a sequence.
Every ordered read uses explicit `ORDER BY`. Missing, duplicate, fractional,
negative, or gapped v13 order data is rejected rather than reconstructed in
legacy ID order. Empty collections are valid.

## Schema versions and destination policy

H1A wrote schema v10, H1B wrote v11, and P2 wrote v12. P3A writes and reads
**v13 only**. Deep Signal is in active pre-release development: development
save files are disposable, and compatibility across schema versions is not
guaranteed unless a future milestone explicitly establishes it. This is the
current development policy, not a permanent release policy. An older file,
including v12, fails with an unsupported-schema error before gameplay
reconstruction. Load opens it read-only and does not modify it. Save refuses to
overwrite older or unknown schemas. No automatic migration or in-place repair
is performed.

The v13 reader requires the current table, column, key, and foreign-key shape,
complete component and material-cost rows, class revision identity, ordered
installations, program/team references, scoped target/receipt/report ordinals,
and all H1B ordering checks. A version marker alone does not make a file valid.
Destination recognition compares the user schema object set and each table's
`table_xinfo`, `foreign_key_list`, and index shape with a freshly built v13
reference. Save rejects user triggers even on known tables because their write
effects are not trusted. Read-only Load may tolerate triggers on known tables.
The check does not require byte-identical `CREATE TABLE` text or silently add
missing columns.

Save accepts a new path, a schema-empty database, or an existing compatible,
valid v13 save. It validates its input state before opening the destination.
The connection enables foreign keys before an immediate write transaction.
Inside that transaction it verifies an existing v13 snapshot before replacement,
creates v13 schema only if empty, replaces rows, rereads the new snapshot, and
commits only after validation. Load opens read-only, checks v13 structure and
foreign keys, and validates a detached snapshot within one read transaction.
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

The H1B ordering contract, retained in v13, supports comparisons of durable state, ordered
children, counters, and meaningful event order when an unsaved and reloaded
simulation continue under the **same build and same inputs**. It does not
promise bitwise identical floating-point results across compilers, platforms,
or build flags, or unchanged outcomes after gameplay rules change. The H1A processing checks remain in force for current v13 snapshots.

## Review evidence

H1A processing-allocation and H1B durable-ordering tests remain part of the
current suite. Current persistence evidence covers v13 round trips,
same-build continuation, malformed-state rejection, transactional rollback,
and explicit rejection of older development schemas without changing their
source files. Historical v10/v11 fixtures remain documented as evidence of
prior development behavior; they no longer establish a current gameplay load
contract. Native UI checks must be reported separately from automated tests.

## P2: component designs and commissioning

`GameState::shipComponents` is the authoritative catalog. Each immutable
`ShipClass` revision stores an ordered list of component IDs and quantities, a
role label, and optional base-class lineage. `ShipDesignRules` derives dry mass,
internal volume, power, tankage, survey equipment, processed-material hull cost,
and build points from those installations. The role label supplies no capability.
A draft preview is read-only; saving a complete revision allocates a new class
ID and emits a revision event without changing the source class or inventory.
Duplicate installation rows reject. A design with volume overflow can be saved
and ordered but cannot accrue build points; it holds its FIFO position and the
backlog names the physical constraint. Power deficit, missing sensor, and zero
tankage are warnings, not construction blockers.

At completion, the shipyard pays the derived hull materials, then transfers up
to the derived tank capacity from the colony's actual processed Propellant stock.
The reference Survey Cutter's former 150 Propellant build-cost entry is removed
because filling a tank is a separate transfer. A hull can commission with an
empty or partial tank; P2 has no later refueling command. Powered installed
survey equipment is required for a fleet to survey; survey role alone is not
sufficient. P2 does not change sustained-burn transit physics.

P2's v12 schema replaced class aggregate cost/BP/tank columns with component
tables, ordered installations, and revision lineage. P3A retains those records
in v13 alongside new program state. Old v10/v11/v12 fixtures remain useful for
proving current rejection is clean and leaves source files unchanged.

## P3A: delegated home-supported survey programs

A `SurveyProgram` is durable player intent: its charter requests a home colony,
optional fleet/leader/team, ordered target pass quotas, and a bounded fuel
policy. Requested IDs do not grant control. A program obtains one exclusive
fleet/team lease only when those physical assets can be acquired together.
`SurveyTeam` has one real location, either at a colony or aboard a fleet.
The fleet keeps its actual transit plan; suspending or cancelling a program
never teleports it, refunds departure fuel, or moves an embarked team remotely.
An amendment records a new charter revision. A home change during committed
work remains pending until a safe boundary; prior receipts remain historical.
`Closing / Completed` means the final visit is done while physical return is
still pending. It remains an actionable decision point: an accepted amendment
reopens `Authorized`, and suspension records `Suspended`, both without replacing
the committed return task or route. `Closed / Completed` requires the home return.

The first executor runs one target pass per home-supported sortie. Leader
approach determines target order from public charter priorities and completed
pass counts. Target ranking cannot read hidden deposits. Each pass takes five
qualifying workdays in the dedicated proof fixture; no work occurs on an
arrival day. Powered installed survey equipment and an embarked team are
required for progress. A valid visit can produce zero new information without
claiming a body is barren. Manual survey remains immediate for unleased fleets.

Refueling transfers only real Propellant above the charter's home-stock floor
and within its remaining additional-fuel authorization, tank room, and planned
sortie need. Hulls fill in persisted fleet-roster order. One opening-day
physical action per fleet is allowed: transfer, departure, or survey workday.
Program movement uses the same route planner and adjusted fuel-cost rule as
manual movement. Mining, processing, shipyards, and movement retain their
relative daily order after the program opening phase. End-of-day bookkeeping
observes arrivals, publishes due reports, and raises consequential issues;
it never launches a second action.

The integer-day clock stops after a completed day when an unacknowledged
consequential issue is raised. All advance entry points share that runner and
report actual elapsed days. Global multiples of 30 publish durable reports;
multiples of 90 mark a review. Reports use durable program counters and
receipts, not session-only economy snapshots. Acknowledgment permits the
same known limitation to continue waiting without inventing fuel or repeating
an interruption every day.

P3A does not model cargo freight, tender maintenance, final propulsion,
scientific analysis/claims, research, or site development. A successful
program closes only after its requested visits and home return. The existing
manual transit cancellation shortcut remains unchanged for unleased fleets.
