# Simulation state contract

This document records the H1A processing-configuration, H1B save-continuity,
P1 shipyard-intent, P2 vessel-design, P3A delegated-survey and P3B freight contracts.
The in-memory `GameState` remains the authority for
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
| `freightPrograms` (v14) | `freight_programs` |
| `shipClasses` | `ship_classes` |
| `shipyardOrders` | `shipyard_orders` |
| `ships` | `ships` |
| `fleets` | `fleets` |

Ordered child collections have their own scope. `appointments` keeps its global
ordinal; `colony_processing_allocations` keeps an ordinal per colony; and
`fleet_order_queue` keeps an ordinal per fleet. P3A target, receipt, and report
rows keep an ordinal per program. P3B manifests, transfer receipts, and freight
reports also retain per-program ordinals. The optional `freight_shipments` row
holds the committed participants and limits; `ship_cargo` holds the one physical
lot on each carrying ship. These lots reference the exact program and active
shipment number. A Ship row has **two independent
positions**: `ships.ordinal` reconstructs `GameState::ships`, while
`ships.fleet_ordinal`, unique within its `fleet_id`, reconstructs that Fleet's
`shipIds` roster. Reconstructing the roster by global Ship order would lose a
different ordering contract. Resource and component cost arrays remain keyed
by mineral/material enum index, while metadata and ID counters remain keyed by
name. `event_log` remains ordered by Event ID, with strictly increasing IDs and
nondecreasing event days validated; it has no second ordinal.

The current v14 writer assigns contiguous ordinals beginning at zero. Schema
constraints require non-null integer, nonnegative, unique values in each scope;
the reader also checks storage type and contiguity before accepting a sequence.
Every ordered read uses explicit `ORDER BY`. Missing, duplicate, fractional,
negative, or gapped v14 order data is rejected rather than reconstructed in
legacy ID order. Empty collections are valid.

## Schema versions and destination policy

H1A wrote schema v10, H1B wrote v11, P2 wrote v12, and P3A wrote v13. P3B writes and reads
**v14 only**. Deep Signal is in active pre-release development: development
save files are disposable, and compatibility across schema versions is not
guaranteed unless a future milestone explicitly establishes it. This is the
current development policy, not a permanent release policy. An older file,
including v13, fails with an unsupported-schema error before gameplay
reconstruction. Load opens it read-only and does not modify it. Save refuses to
overwrite older or unknown schemas. No automatic migration or in-place repair
is performed.

The v14 reader requires the current table, column, key, and foreign-key shape,
complete component and material-cost rows, class revision identity, ordered
installations, program/team references, scoped target/receipt/report ordinals,
freight commitment/custody/history references, and all H1B ordering checks. New
freight numeric values and enums use strict SQLite storage-type readers. A
version marker alone does not make a file valid.
Destination recognition compares the user schema object set and each table's
`table_xinfo`, `foreign_key_list`, and index shape with a freshly built v14
reference. Save rejects user triggers even on known tables because their write
effects are not trusted. Read-only Load may tolerate triggers on known tables.
The check does not require byte-identical `CREATE TABLE` text or silently add
missing columns.

Save accepts a new path, a schema-empty database, or an existing compatible,
valid v14 save. It validates its input state before opening the destination.
The connection enables foreign keys before an immediate write transaction.
Inside that transaction it verifies an existing v14 snapshot before replacement,
creates v14 schema only if empty, replaces rows, rereads the new snapshot, and
commits only after validation. Load opens read-only, checks v14 structure and
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

The H1B ordering contract, retained in v14, supports comparisons of durable state, ordered
children, counters, and meaningful event order when an unsaved and reloaded
simulation continue under the **same build and same inputs**. It does not
promise bitwise identical floating-point results across compilers, platforms,
or build flags, or unchanged outcomes after gameplay rules change. The H1A processing checks remain in force for current v14 snapshots.

## Review evidence

H1A processing-allocation and H1B durable-ordering tests remain part of the
current suite. Current persistence evidence covers v14 round trips,
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
in v14 alongside survey and freight program state. Old v10/v11/v12 fixtures remain useful for
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

## P3B: delegated processed-material freight

A freight charter identifies one source colony, one distinct destination colony,
and one processed material. Its finite quantity is cumulative delivery intent.
Authorizing it does not reserve stock, fuel tanks, or acquire busy assets. Missing
fleet, leader, stock, tankage or handling remains a visible waiting condition.
The amendment command contains only name, quantity, requested fleet/leader and
resource policies: route and commodity require a different program. Historical
delivery and current shipment commitments may exceed a later lowered target.

`Ship::cargo` is the only onboard cargo inventory. An optional positive lot
identifies its freight program, shipment, material and quantity. Engine fuel is
separate, including when the cargo is Propellant. There is no cargo-to-tank
conversion or destination refueling. Cargo uses normalized units: one unit of
any processed material occupies one unit of cargo capacity. Loaded mass does
not change the retained prototype transit/fuel equations.

The shared ship evaluator derives cargo capacity and installed handling rate.
Each hull must have enough generation for its own total power demand to operate
its handling equipment. Another hull cannot lend it handling power or unload its
hold. Transfer limits apply per hull in stored roster order. Each debit has an
equal physical credit; positive dated receipts distinguish cargo load, delivery,
source return and operating fuel. Cumulative loaded cargo equals delivered plus
returned plus currently aboard. Destination consumption does not undo delivery.

The reference freighter installs the existing hull, reactor, tank and general
systems plus two Standard Cargo Bays. Each bay contributes 100 cargo units and
25 handling units/day, with mass 60, volume 200, demand 20, BP 60 and construction
cost 40 Alloys / 10 Electronics / 20 Composites. The resulting hull derives mass
590, volume 670/1000, generation/demand 120/60, tankage 1000, cargo 200, handling
50/day and 550 BP. It has no survey equipment. Existing cutter totals remain.

### Shared control and day phases

`ProgramController` distinguishes typed survey and freight IDs even when their
numeric values coincide. Reverse fleet ownership is derived from canonical
program leases. All manual movement, queue edits, cancellation and survey guards
use that same ownership boundary; internal departures verify their actual owner
before using common route planning and engine-fuel payment.

Opening dispatch is a stable merge of the stored survey and freight vectors.
Only their next unvisited heads are compared by creation day; equal days visit
survey first. Neither vector is sorted. Existing leases occupy assets for the
whole opening phase, so release cannot enable a second controller that day.
A temporary per-colony/material budget captures actual opening stock. Each
withdrawal reduces actual stock and budget; inbound credit increases only actual
stock. Every program therefore waits until the next opening to spend new inbound
goods. Mining, processing, shipyards and movement then run in their existing
order; industry may use goods actually unloaded earlier that day. End-of-day
arrival/issue/report bookkeeping never dispatches another physical action.

### Repeated trips, resource limits and disposition

One fleet action per opening day can refill tanks, load, unload, depart, or
perform survey work. Last loading, departure, arrival, unloading and return
departure cannot be collapsed into one recursive action. Distinct colonies on
the same body skip transit but retain separate handling days.

Batch planning bounds the next shipment by unmet demand, real available stock
and operational per-hull capacity. It records finite per-hull planned limits,
which are not inventory reservations. Fuel estimates include rate-limited
loading, a required refueling day, outward transit, rate-limited unloading and
next-day return departure. Return fuel uses that projected date and the existing
commander modifier. Actual departures reprice and pay their actual leg.
The deterministic search evaluates at most 14 candidates: the maximum, a
fuel-adjusted smaller candidate when Propellant is shared, then bounded halves.
It is a readiness estimate, not an optimizer or guaranteed completion ETA.

Source-only operating fuel has priority over payload, is limited by actual tank
room, stock and remaining lifetime allowance, and is counted only when actually
transferred. If payload is Propellant, its stock floor and operating fuel floor
use their maximum, not their sum. With 120 stock, floor 20 and operating transfer
10, at most 90 remains for payload. Floors constrain each program's withdrawals;
they are not global reservations against unrelated consumers.

Suspension preserves cargo and paid transit, stops transfers/departures, and
retains custody while any cargo is aboard. Empty stationary fleets can release
safely without refunding engine fuel. A retained return task must reacquire the
same fleet. Fleet amendments take effect at an empty source planning boundary.
Cancellation stops future pickups and authorizes only bounded settlement:
unshipped source cargo returns through real handling days; already dispatched
cargo reaches its original destination and unloads there, with no new mandatory
empty return. An already active empty leg reaches its paid destination. Cargo is
never erased, teleported, or counted as delivered at departure/arrival.

Normal completion requires actual delivery, empty holds and physical source
return before release. Unfinished completion return and cancellation settlement
remain actionable through amendment, suspension/resumption and acknowledgment.
Suspending freight retains cancellation intent so resumption cannot restart
pickups. `Closed` is terminal and has `closedDay`; periodic freight reports stop
after closure. A scheduled report due on closure day is still published once.
Reports snapshot their resource floors, operating allowance and contingency;
later charter amendments do not change those historical limits.
P3A's existing post-closure report policy is retained separately.

Freight does not lease a survey team. An already embarked unleased team follows
its physical fleet. No remote support/rescue, cargo market, crew system, density
model, final propulsion, tender maintenance, P4 or P5 mechanics are introduced.
