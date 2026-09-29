# Simulation state contract

This document records the H1A processing-configuration, H1B save-continuity,
P1 shipyard-intent, P2 vessel-design, P3A delegated-survey, P3B freight, P3C service and P4A scientific-evidence contracts.
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
| `equipmentFamilies` (v15) | `equipment_families` |
| `maintenanceTeams` (v15) | `maintenance_teams` |
| `maintenancePrograms` (v15) | `maintenance_programs` |
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

The current v16 writer assigns contiguous ordinals beginning at zero. Schema
constraints require non-null integer, nonnegative, unique values in each scope;
the reader also checks storage type and contiguity before accepting a sequence.
Every ordered read uses explicit `ORDER BY`. Missing, duplicate, fractional,
negative, or gapped v16 order data is rejected rather than reconstructed in
legacy ID order. Empty collections are valid.

## Schema versions and destination policy

H1A wrote schema v10, H1B wrote v11, P2 wrote v12, P3A wrote v13 and P3B wrote v14. P3C wrote v15. P4A writes and reads
**v16 only**. Deep Signal is in active pre-release development: development
save files are disposable, and compatibility across schema versions is not
guaranteed unless a future milestone explicitly establishes it. This is the
current development policy, not a permanent release policy. An older file,
including v15, fails with an unsupported-schema error before gameplay
reconstruction. Load opens it read-only and does not modify it. Save refuses to
overwrite older or unknown schemas. No automatic migration or in-place repair
is performed.

The v16 reader requires the current table, column, key, and foreign-key shape,
complete component and material-cost rows, class revision identity, ordered
installations, program/team references, scoped target/receipt/report ordinals,
freight commitment/custody/history references, and all H1B ordering checks. New
freight numeric values and enums use strict SQLite storage-type readers. A
version marker alone does not make a file valid.
Destination recognition compares the user schema object set and each table's
`table_xinfo`, `foreign_key_list`, and index shape with a freshly built v16
reference. Save rejects user triggers even on known tables because their write
effects are not trusted. Read-only Load may tolerate triggers on known tables.
The check does not require byte-identical `CREATE TABLE` text or silently add
missing columns.

Save accepts a new path, a schema-empty database, or an existing compatible,
valid v16 save. It validates its input state before opening the destination.
The connection enables foreign keys before an immediate write transaction.
Inside that transaction it verifies an existing v16 snapshot before replacement,
creates v16 schema only if empty, replaces rows, rereads the new snapshot, and
commits only after validation. Load opens read-only, checks v16 structure and
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

The H1B ordering contract, retained in v16, supports comparisons of durable state, ordered
children, counters, and meaningful event order when an unsaved and reloaded
simulation continue under the **same build and same inputs**. It does not
promise bitwise identical floating-point results across compilers, platforms,
or build flags, or unchanged outcomes after gameplay rules change. The H1A processing checks remain in force for current v16 snapshots.

## Review evidence

H1A processing-allocation and H1B durable-ordering tests remain part of the
current suite. Current persistence evidence covers v16 round trips,
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
tables, ordered installations, and revision lineage. P3C retains those records
in v16 alongside program state, physical instrument condition and scientific records. Old v10/v11/v12 fixtures remain useful for
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

## P3C: colony-supported survey-instrument maintenance

Only survey instruments have managed condition in this slice. The authoritative
equipment-family catalog identifies Standard and Specialist Survey Instruments.
A component service profile names its family, per-unit duty capacity, engineering
work per restored duty, and processed-material recipe. A `Ship` stores exactly
one condition row per managed class installation, in installation order. Its
`usedDuty` is per unit; installation quantity multiplies restoration work and
materials. Conditions never reside on a shared class revision. New hulls use
the shared healthy initializer; Load reads exact current rows and rejects missing,
duplicate, extra or mismatched conditions.

One duty unit is one qualifying instrument survey workday. The nominal design
evaluator remains independent from wear. The action-specific evaluator reports
nominal, powered and usable capability. Each timed day debits one duty from each
contributing managed row. The result helper debits none, so the final pass day
is charged once. A valid zero-information pass still consumes duty. Unpowered,
exhausted, idle, waiting, suspended and transiting equipment earns no survey work
and consumes no duty. The retained immediate manual survey requires five duty
units for the whole action; its preview and command use that same rule.

Profiles default to 120 duty, 0.2 maintenance-team workdays and 0.5 Electronics
plus 0.5 Industrial Composites per restored duty per installed unit. The dedicated
proof scenario uses ten-duty profiles. Its six five-day passes consume 30 duty,
restore 20 in four service workdays, consume ten units of each recipe material,
and finish with ten used duty. It does not request a final unsolicited overhaul.

### Workshops, teams and service authority

Workshop family entries are unique within a definition. Compatible rates add
within a powered hull; reactors cannot lend power between hulls. A program picks
the first eligible hull in the tender's persisted roster for an unstarted group
and pins that workshop during the group's actual work. Losing readiness pauses
that group. Completing it can select another group/hull only on a later day.

A finite maintenance team has explicit family qualifications, a nonnegative
workdays/day rate and one physical location: colony or fleet. A standing
maintenance charter fixes its service colony and requests an existing tender,
team, leader, ordered clients and material policy. Missing readiness is accepted
intent. Non-null references and policy values must be valid. Tender/team leases
are acquired together at the actual service colony. No team is created by ship
design, borrowed from surveying, or teleported between locations.

The provider leases only its tender. Its service job records the requesting
survey, original client/tender/team/leader, charter revisions, ordered worn
ShipId/component targets, and the current pinned workshop. The client retains
its survey movement lease. Targets are full-service requests, not a second
condition inventory. Daily receipts record actual before/after duty, quantity,
restoration, work, materials, workshop and applicable revisions. Job history
distinguishes completed full service from explicit withdrawal.

`ProgramController` now distinguishes Survey, Freight and Maintenance. The
stable head-only merge compares creation day with that tie order, preserving all
within-kind vector order and prior Survey/Freight behavior. Maintenance-team
occupancy is separate from survey-team occupancy. Actual ownership is used by
manual guards, checked movement, diagnostic labels and typed interruptions.

### Client requests, detours and daily accounting

Survey support is optional. At a stationary home boundary, selected support is
requested when a worn nominally contributing row reaches the remaining-duty
trigger, or no powered row can sustain the next complete pass or retained
partial pass. The default trigger is 0.25. A fresh design with too short an
envelope produces an explanation rather than zero-work jobs. Removing the
policy permits actual remaining duty to be used and restores nothing.

Actual service requires survey client control and its team, a provider that
authorizes that fleet, and stationary client/tender/team at the service colony.
Its body must match the client's home body. Distinct same-body colonies retain
separate stockpiles: repair uses the service colony and refueling uses home.
An exhausted partial field visit can make a real funded maintenance return,
retain its target/participants/workdates, receive service, and resume through
real outbound movement. Insufficient return fuel remains a physical wait.

The opening context captures active jobs and eligible pending requests as
transient client holds. A newly acquired client requests support before any fuel
or dispatch action; provider work waits for the next opening snapshot. Holds
last for the full phase, including when an older provider finishes before its
client's dispatch turn. No repair occurs on arrival day, and no repaired client
refuels, departs or surveys later in that same opening.

For one installation group with quantity N, used duty U, labor coefficient L,
recipe M, powered compatible workshop W, qualified team T and authorized real
supply S, restoration is bounded by `min(U, min(W,T)/(N*L), S[j]/(N*M[j]))` for
positive recipe entries. One provider repairs one group per opening, without
spending leftover capacity on another group/client. Every withdrawal uses the
common opening stock budget. Freight cargo is unavailable until actually
unloaded, and incoming/industry-produced stock becomes program-spendable only
next opening. There is no onboard repair inventory or additional cargo charge.

Comparison tolerance is absolute `1e-9` plus relative `1e-10` for finite history
reconciliation. Execution's final-step normalization uses relative scale only,
so tiny genuine shortages cannot be mistaken for complete repairs. Relatively
equivalent recipe/throughput rounding is capped at actual available resources.
Unrepresentable positive debits or condition changes remain named execution
limits. Materials, condition and receipt storage are prepared before mutation;
the engine's existing exception guarantees remain in force.

### Stops, reports and current snapshots

Provider suspension/cancellation withdraws its active job before safe tender/team
release; actual partial restoration and spent parts remain. Resume makes a new
job from current wear when the client policy requests it. Client stops or support
changes withdraw service before client lease release. Resource replacement or
removing the active client ends the old job; later work has new provenance.
Limits-only amendments preserve the job and historical spending. Lower caps
never undo expenditure or prohibit Amend/Suspend/Cancel.

Ordinary missing supply and periodic service are waits, not daily interruption
spam. Changed consequential causes after work use stable typed issues through
the common advance runner. Acknowledgment does not relax any physical limit.
Standing providers report on global 30-day boundaries and mark 90-day reviews;
reports snapshot policy and actual work. An audit watermark preserves the
publication boundary: commands later on the same integer date are counted in
the next report rather than rewriting the prior one. Closure records its final
history, publishes a due report once and stops future provider reports. P3A's
pre-existing closed-survey report behavior is unchanged.

Schema v15 stores families/profiles/recipes/workshops, physical condition, finite
teams/qualifications, provider/client policies, leases, ordered jobs/targets,
receipts/materials, reports/policy snapshots/audit boundaries, issues and detours.
Opening budgets, pending request lists, service holds and nominal totals are not
saved. Strict readers and graph/accounting validation preserve ordering and
same-build continuation. No v14 reader or migration is provided.

This adds no engine/reactor/hull/cargo wear, random failures, remote tender
deployment/rendezvous, rescue, shipboard supplies, cargo-to-tank conversion,
refitting, recruitment, crew economy, final propulsion, P4 or P5 systems.


## P4A: observations, finite analysis and dated assessments

Physical `MineralDeposit` stores only body/mineral identity, remaining units and
accessibility. Acquisition does not change those quantities. Confidence and its
partition helpers are removed. Only `ObservationAcquisition` and the tightly
scoped `sampleObservationChannels` bridge may read physical geology for science;
`AssessmentRules` takes sealed records and completed findings without GameState.

The immutable measurement catalog has Reconnaissance (threshold 50, no
accessibility measurement) and Characterization (threshold 10, coarse
accessibility). Both declare all 14 minerals in enum order. The existing standard
and specialist arrays reference these profiles independently of maintenance
family. Signal is `remaining * min(accessibility, 1)` in fictional normalized
units. The signal and physical inputs are never returned. A detection has
accessibility Low `[0, .25)`, Moderate `[.25, .75)`, or High `[.75, unbounded)`
only for a characterization profile. Unbounded is a tag, not numeric infinity.

Every qualifying instrument installation records actual exposure dates and its
exact ship/class/component/profile identity. Managed and unmanaged contributors
are both recorded. Quantity does not accelerate acquisition. Five contributions
are required for a full-profile reading; shorter exposure produces an explicit
InsufficientExposure result for every declared channel. A pass samples once at
its completion boundary. Partial visits and service detours preserve their real
work dates. One completed pass has exactly one sealed batch and publication audit.
Immediate manual action still costs five duty units, has one actual date and no
invented scientific team or five elapsed days, and never completes analysis.

A batch acquired on D is readable raw data immediately and available to analysis
at opening D+1. This directorate-wide delivery is an explicit distance-independent
information abstraction. People, cargo and fleets retain their physical locations.

An AnalysisProgram has a fixed laboratory colony and either one FollowSurvey
source or an ordered, unique, nonempty FixedBatches source. Names, requested
scientists, responsible leaders and lifetime work allowance can be amended.
Zero or missing readiness preserves intent. The home scenario authors 1.0
scientific team-workday/day at Terra; other colonies default to zero. Proof
fixtures explicitly author one laboratory at their existing fixed survey base.
One existing SurveyTeam supplies at most 1.0 team-workday/day. Analysis never
leases a fleet or borrows a deployed team. The derived scientific-team owner
covers Survey and Analysis using typed identities; maintenance engineers remain
in their separate workforce.

One batch requires exactly 3.0 scientific team-workdays. One program performs
at most one job's work per opening. Work is bounded by remaining job demand,
team rate, remaining opening lab throughput and remaining lifetime allowance.
Progress and spent allowance derive from positive dated receipts, with actual
scientist/leader/colony/charter revision and historical installed lab capacity.
A partially worked job may retain a team while capacity is temporarily absent.
Completion releases it; an idle follower does not hoard scientists. Released
teams remain occupied for that opening, including across program kinds.

Common dispatch remains a head-only merge of stored program vectors, with equal
dates ordered Survey, Freight, Maintenance, Analysis. Transient opening snapshots
also hold information availability and finite per-colony lab budgets. Mining,
processing, shipyards and movement keep their predecessor relative ordering.
No lab work consumes fuel, repair parts or instrument duty.

Completed jobs publish immutable findings and one immutable assessment revision.
Assembly includes only already completed findings. Claims retain method-specific
alternatives and use latest applicable acquisition dates, independently for
indication and accessibility. An older observation analyzed later cannot displace
newer science. Repetition adds dated history without increased certainty. A newer
non-detection after an earlier indication states both facts without claiming
absence or depletion. Reserve quantity is Unmeasured; site suitability is
Unassessed. There is no construction-permission flag.

Suspension releases stationary analysts and retains documented work. Assignment
amendments release the old team before later reacquisition; previous receipts
remain attributed to their actual participants. Cancellation closes unfinished
jobs without publishing them and retains raw records and completed assessments.
A follower completes only after its source is Closed and all acquired inputs
are analyzed; Closing/Completed physical return is still an open source.
Empty canceled sources close with no observations, not a barren-body conclusion.

Analysis issues use the shared interruption runner. Known insufficient work
allowance is acknowledged at authorization/amendment. A new exhausted allowance
after real work and newly acquired backlog can interrupt; acknowledgment does
not fabricate capacity or repeat work. Reports publish at global 30-day/90-day
boundaries, snapshot authority/source/work and an audit cutoff, and stop after
analysis closure. P3A closed-survey report behavior remains a separate residual.

Schema v16 adds 26 focused scientific tables to the retained 50-table snapshot.
It stores ordered profiles/channels, component links, laboratory capacities,
active exposure and dates, sealed batches/results, sources, jobs, labor receipts,
findings, assessments/claim provenance, reports, issues and counters. Persisted
profile references reconstruct immutable method snapshots without resampling.
There are no saved reverse owners, latest-assessment caches, input queues or
opening budgets. Current-only structural checks, foreign keys, strict numeric
readers, graph/accounting validation and transactional replacement remain.

Preparation allocates observations before duty is charged, and prepares completed
findings/assessment storage before the last labor receipt. Tested identity-limit
failures charge no missing duty/work and publish no free result. These are
focused guarantees; an unexpected exception can still leave the global day
advanced, consistent with the existing nontransactional simulation tick contract.
Save/Load remains transactional and never silently repairs scientific records.

Ordinary body/detail/intelligence previews enumerate public bodies and declared
channels, never hidden deposit existence. Stock and output telemetry remain
visible. Geological lifetime forecasts report insufficient evidence. A current
job ETA is shown only with positive capacity/authority and no current competing
ready analyst; it is conditional on those resources remaining unchanged, not a
completion forecast for future follower inputs.

P4A implements only the bounded field/analysis team tradeoff. Requirements/design
staffing, staffed Mission Control, remote tender support, final propulsion, site
development, research and the remaining P3 residuals are not implemented here.
The accepted historical [P3 residual register](p3-closeout-and-residual-register.md)
remains the record of the pre-P4A scope decision; R04 is addressed by this section.
