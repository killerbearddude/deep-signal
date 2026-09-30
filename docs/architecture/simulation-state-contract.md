# Simulation state contract

This document records the H1A processing-configuration, H1B save-continuity,
P1 shipyard-intent, P2 vessel-design, P3A delegated-survey, P3B freight, P3C service,
P4A scientific evidence, P4B site development, and P5 technical-development contracts.
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
| `siteModuleCatalog` (v17) | `site_module_catalog` |
| `resourceSites` (v17) | `resource_sites` |
| `siteDevelopmentPrograms` (v17) | `site_development_programs` |
| `technologyOpportunities` (v18) | `technology_opportunities` |
| `technicalFacilities` (v18) | `technical_facilities` |
| `technicalDevelopmentPrograms` (v18) | `technical_development_programs` |
| `prototypeDesigns` / `prototypeComponentUnits` (v18) | `prototype_designs` / `prototype_component_units` |
| `technicalTestRecords` / `developedComponentRevisions` (v18) | `technical_test_records` / `developed_component_revisions` |
| `componentProductionCapabilities` / `supportQualificationRecords` (v18) | `component_production_capabilities` / `support_qualifications` |
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

The current v18 writer assigns contiguous ordinals beginning at zero. Schema
constraints require non-null integer, nonnegative, unique values in each scope;
the reader also checks storage type and contiguity before accepting a sequence.
Every ordered read uses explicit `ORDER BY`. Missing, duplicate, fractional,
negative, or gapped v18 order data is rejected rather than reconstructed in
legacy ID order. Empty collections are valid.

## Schema versions and destination policy

H1A wrote schema v10, H1B wrote v11, P2 wrote v12, P3A wrote v13 and P3B wrote v14. P3C wrote v15, P4A wrote v16, and P4B wrote v17. P5 writes and reads
**v18 only**. Deep Signal is in active pre-release development: development
save files are disposable, and compatibility across schema versions is not
guaranteed unless a future milestone explicitly establishes it. This is the
current development policy, not a permanent release policy. An older file,
including v17, fails with an unsupported-schema error before gameplay
reconstruction. Load opens it read-only and does not modify it. Save refuses to
overwrite older or unknown schemas. No automatic migration or in-place repair
is performed.

The v18 reader requires the current table, column, key, and foreign-key shape,
complete component and material-cost rows, class revision identity, ordered
installations, program/team references, scoped target/receipt/report ordinals,
freight commitment/custody/history references, and all H1B ordering checks. New
freight numeric values and enums use strict SQLite storage-type readers. A
version marker alone does not make a file valid.
Destination recognition compares the user schema object set and each table's
`table_xinfo`, `foreign_key_list`, and index shape with a freshly built v18
reference. Save rejects user triggers even on known tables because their write
effects are not trusted. Read-only Load may tolerate triggers on known tables.
The check does not require byte-identical `CREATE TABLE` text or silently add
missing columns.

Save accepts a new path, a schema-empty database, or an existing compatible,
valid v18 save. It validates its input state before opening the destination.
The connection enables foreign keys before an immediate write transaction.
Inside that transaction it verifies an existing v18 snapshot before replacement,
creates v18 schema only if empty, replaces rows, rereads the new snapshot, and
commits only after validation. Load opens read-only, checks v18 structure and
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

The H1B ordering contract, retained in v18, supports comparisons of durable state, ordered
children, counters, and meaningful event order when an unsaved and reloaded
simulation continue under the **same build and same inputs**. It does not
promise bitwise identical floating-point results across compilers, platforms,
or build flags, or unchanged outcomes after gameplay rules change. The H1A processing checks remain in force for current v18 snapshots.

## Review evidence

H1A processing-allocation and H1B durable-ordering tests remain part of the
current suite. Current persistence evidence covers v18 round trips,
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
in v17 alongside program state, physical instrument condition and scientific records. Old v10/v11/v12 fixtures remain useful for
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
job ETA is shown only with positive capacity/authority and a full projected
opening allocation; zero and partial laboratory shares have no completion ETA.
It is conditional on that allocation continuing, not a completion forecast for
future follower inputs.

`AnalysisReadiness` separates eligibility, typed wait cause, explanatory text,
selected input and the transient work quantum. The executor consumes that result
against its actual opening occupancy, delivered-input set and remaining lab
budget; it never compares display strings. Live queries and report waiting reasons
project one next-opening analysis pass in the existing head-only order, using
current canonical leases and already acquired inputs available by D+1. Earlier
eligible work consumes only local scratch throughput and scientist occupancy in
that projection. Zero share reports laboratory contention; a reduced positive
share remains executable and reports its partial allocation. Drafts have no
stored dispatch position and use the shared mechanical eligibility rules.
The projection does not simulate future survey movements, arrivals or releases,
allocate jobs, reserve resources, or persist a calculated share. The unused
survey-only `surveyProgramCondition` helper was removed; the authoritative survey
execution explanation retains the shared scientific-team owner lookup.

P4A implements only the bounded field/analysis team tradeoff. Requirements/design
staffing, staffed Mission Control, remote tender support, final propulsion, site
development, research and the remaining P3 residuals are not implemented here.
The accepted historical [P3 residual register](p3-closeout-and-residual-register.md)
remains the record of the pre-P4A scope decision; R04 is addressed by this section.

## P4B: v17 persistence boundary

P4B introduced the v17 snapshot described in this historical subsection. P5
supersedes it with the current v18-only boundary below; v17 and older files are
rejected before gameplay reconstruction or replacement. No migration, synthetic
site/technology conversion, or in-place repair is provided. Historical rejection
fixtures remain in use and have not been removed.

`SiteSchema.cpp` and `SitePersistence.cpp` add focused relational records:

| Stored authority | Tables |
| --- | --- |
| Authored module definitions, exact version/kind, costs and vector order | `site_module_catalog` |
| Typed construction-family capability binding | `site_construction_binding` |
| Cumulative actual colony processor output by processed channel | `colony_processing_totals` |
| Registered location/body, separate raw/processed stores, standing operating policy/revision, report cursor and issue episode | `resource_sites` |
| Frozen construction route/package version, requested/leased/task assets, work/commissioning status, fuel and policy/history cursors | `site_development_programs` |
| Ordered package rows, exact counts, partial work, pinned workshop and consumed channels | `site_development_rows` |
| Earned groups tied to their originating program/package row and commissioning day | `site_installed_modules` |
| Actual engineering participants, dated work and consumed inputs | `site_development_work` |
| Dated construction summaries, policy snapshots and publication audit cutoff | `site_development_reports` |
| Actual supported duty and supply debits with historical policy/equipment cutoff | `site_duty_receipts` |
| Dated attempted/recovered Ice and observed limitations, without hidden reserve/accessibility | `site_extraction_receipts` |
| Operating totals, raw occupancy/capacity, export/delivery, historical policy and publication cutoff | `site_operating_reports` |

Every ordered child retains a contiguous zero-based ordinal in its parent
scope; loading never sorts the reconstructed domain vectors by identity.
Construction package rows and their partial-work rows share the same stored
ordinal. Fixed material arrays use explicitly indexed columns in these new
records, and colony production totals require exactly one row for every material
channel. A partially NULL material-allowance set is malformed. SQLite numerical
storage, finite values, integer range, flags, ordinals and final domain/accounting
invariants are checked before a detached snapshot can become the active world.

Freight charters, shipments, lots and receipts now persist explicit commodity
and stock-location kind/value pairs. Processed versus raw and Colony versus Site
are distinct namespaces even when values overlap. The actual operating base has
a Colony foreign key. Polymorphic endpoints are checked by strict tag decoding
and domain reference validation, rather than aliased to a Colony foreign key.
Ship cargo remains the single physical per-hull inventory. New audit envelopes
preserve construction participants/row identity and typed site-operating issue
episodes; they do not masquerade as P4A scientific observations.

Site/development child records are cleared before their referenced parents.
Snapshot writes insert ordinary assets and engineering records before site and
construction children, with foreign keys enabled throughout. Existing schema
recognition, input preflight, transactional replacement, reread-before-commit,
rollback, and failed-Load world preservation remain in force. No projected
capacity, reverse controller, receiving-space reservation, opening stock/handling
budget, or copied geological reserve is persisted.

## P4B: unrestricted site development and Ice supply

A `ResourceSite` is a typed working location, not a colony. Atomic new-site
registration plus development authorization creates no inventory, installed
capacity, fleet, or workforce. A known public body is sufficient; observations,
assessments, true deposit existence, reserve and accessibility are not admission
inputs. A development's site, support colony, catalog version and ordered
package remain fixed. Assignment and authority amendments preserve sunk work,
material debits and the original physical participants until safe return.

The authored v1 module catalog defines extraction, power, handling, storage and
automation. The reference package costs 240 Structural Alloys, 70 Electronics
and 60 Industrial Composites, with 14 engineer-workdays of proportional assembly
and two further commissioning workdays. Zero capability and inadequate power
are valid package consequences. Each opening selects at most one executable
assembly row in stored package order; an input-blocked row does not block an
independent later row. Positive work debits proportional real site materials,
bounded by current stock, opening stock, floors and lifetime material authority.
The bounded rounding adjustment applies only to an already positive final paid
step, never to a zero-input completion.

Field construction uses a powered installed workshop with an explicit authored
family binding and a qualified real `MaintenanceTeam`. Ship role grants no
work. A row pins its workshop hull. Embark, operating-fuel loading, departure,
work, commissioning, return and disembark are separate physical actions. A
cancelled project keeps sunk work and physically returns its participants;
completed equipment is not removed by cancelling builder closeout. Construction
closure ends its future reports while independent site operations continue.
Additive developments pay their own material/labor cost, append installed groups
once, and preserve prior operating history and policy.

`ProgramController` has distinct Survey, Freight, Maintenance, Analysis and
Development alternatives. Equal numeric IDs from different kinds do not alias.
The opening dispatcher still merges only the unvisited head of each stored
vector by creation date; ties follow that order. Maintenance and Development
share one engineering workforce. Fleet/team/site construction leases remain on
the owning program, and reverse ownership is derived. A site's operating
responsibility is a separate `DecisionSource`, never a fleet-owning program.
All time entry points use the same interruption-aware runner.

### Typed freight and cold starts

`Commodity` distinguishes processed materials from raw minerals, and
`StockLocation` distinguishes colonies from sites. Tag plus value is identity;
matching ordinals alone are not interchangeable. Cargo stays in one real lot on
each hull and is never a second engine tank. A freight charter fixes both
endpoints, commodity and a colony operating base that must be one endpoint.
Site-to-site freight and remote engine refuelling are outside this increment.

Processed packages use powered ship handling to land at a completely cold site.
Raw transfer requires commissioned site storage and paid site handling. A
source-base delivery keeps the previous loaded-out/empty-home cycle. A
destination-base collection fuels at that base, travels empty to the source,
loads a manifest supported by actual source stock, and carries it home. Completed
collection closes at the base. Committing a manifest does not reserve inventory:
if competing work spends it before arrival, the retained shipment waits. It does
not shrink itself to predicted yield or acquire fuel from its payload.

Cancellation settles unshipped cargo back to the source using real stock space
and handling, or delivers dispatched cargo to the committed destination. Paid
transit and cargo custody survive suspension and amendments. Distinct storage
locations on the same body still require transfers but no artificial journey.

### Daily eligibility and support

1. The new day snapshots all real opening raw/processed stocks, existing leases,
   eligible installed groups and raw receiving space.
2. Sites pay supported duty from eligible opening Reactor Fuel and Industrial
   Composites. Hardware commissioned today is not eligible. Paid duty funds one
   shared site raw-handling pool.
3. The stable program merge runs. Inbound stock is not added to opening spending
   authority. Raw incoming freight consumes opening receiving room; outgoing
   transfers do not replenish that room during the opening.
4. Colony mining runs first, then sites extract in stored site order using actual
   remaining raw room and the handler left after freight.
5. Colony processing runs, followed by shipyards and fleet movement, then reports
   and observed decisions. Raw unloaded this opening can feed this processing
   phase; its resulting Propellant can fuel a program only at a later opening.

All eligible installed equipment is active. Supported duty requires power and
automation with enough total power for the whole installed set. Duty is bounded
by one day, remaining lifetime duty authority and actual above-floor support
stocks. It consumes Reactor Fuel and Industrial Composites even when storage is
full, the extraction target is zero, or a genuine attempt recovers nothing.
Passive storage remains available without supported duty; active raw handling
does not. There is no local site refinery.

The only site physical-geology reader is the extraction boundary. With positive room and handling, nominal attempted work is supported duty times
the lesser of rated extraction and the daily target. Recovery is bounded by true
remaining Ice, nominal times accessibility clamped to [0,1], actual free raw room
and remaining daily handling. Actual recovered
Ice debits that deposit, credits site raw stock and consumes the shared handler.
Unrepresentable coupled transfers do not manufacture inventory or a false
negative observation. A zero-duty/full-bin/no-handler day is not an attempt.
Records expose dated nominal work and recovered output, never true reserve,
accessibility or proof of absence. These operating observations do not create
P4A batches, findings or assessments.

Five genuine zero-recovery attempts create an observed dated issue. Explicit
acknowledgment neither stops operation nor grants resources. The episode uses
recorded attempts and audit acknowledgments; pauses do not repeatedly announce
the same negative result. Loss of previously used support and exhausted duty
authority are distinct operational decisions. Reports retain dated policy,
hardware cutoff, support cost, attempts, yield and raw freight flows. Net exports
may be negative in a period that returns cargo loaded in an earlier period.

Colony `processedProductionTotals` records actual gross recipe output only;
authored stocks, freight and commissioning transfers do not increment it. The
existing recipe remains 1 Water Ice + 0.5 Volatiles per Propellant unit, subject
to the existing allocation and capacity rules. These totals do not claim that
mixed inventory can be attributed to an individual site after delivery.

## P5: optional technical development and reusable capability

The ordinary scenario exposes one public `TechnologyOpportunity`: Precision
Characterization Array. Its target threshold is public; the deterministic
candidate threshold is separate authored truth. Opportunity lists, authoring,
readiness, estimates, component catalogs and controls never read candidate truth.
Only completed physical prototype test work creates `TechnicalTestRecord`
evidence. Three repeated tests establish the measured threshold without improving
it. The demonstrated measurement profile and component are allocated once from
that acquired result; the existing Specialist Survey Array and Characterization
profile remain ordinary established catalog records.

Technical development reuses `MaintenanceTeam` as the finite engineering
workforce but keeps `EngineeringQualification::PrototypeInstrumentation`
separate from service-family qualifications. Maintenance, SiteDevelopment and
TechnicalDevelopment derive one shared engineering owner. A local
`TechnicalFacility` supplies a transient opening work budget. Equal-day program
ties retain Survey → Freight → Maintenance → Analysis → SiteDevelopment →
TechnicalDevelopment. Requested identities confer no ownership, and the team
must be physically at the fixed development colony. Missing people, facility,
throughput, stock, or authority are valid waits.

The reference stages are sequential, with at most one positive engineering
action per program opening:

| Stage | Workdays | Processed material | Durable result |
| --- | ---: | --- | --- |
| Concept engineering | 5 | 10 Electronics + 5 Composites | Immutable prototype design |
| Prototype fabrication | 4 | 80 Electronics + 20 Composites | One physical local prototype unit |
| Prototype testing | 3 | 15 Electronics + 6 Composites | Three dated tests and demonstrated component/profile |
| Local process qualification | 4 | 40 Alloys + 30 Electronics + 20 Composites | Colony/facility-local serial process, available D+1 |
| Support qualification | 2 | 10 Electronics + 10 Composites | Exact team gains Specialist Survey Instruments, usable D+1 |

Every positive step is bounded by remaining stage work, team throughput,
unspent opening facility capacity, actual/opening colony stock, floors and
lifetime authority. Consumption is proportional and representable. Freight
unloaded during the opening and material made in the later processing phase are
first eligible on the next opening. Completed artifacts survive cancellation;
a new program resumes at the first missing completed artifact and does not
inherit cancelled partial work. Closed technical programs publish no later
period reports.

The candidate mechanical design is known after concept work, while sensitivity
remains unestablished. Its immutable demonstrated component has mass 35, volume
100, power demand 55, survey capability 1, 90 BP, and serial cost 60 Electronics
+ 10 Composites. Its service profile uses Specialist Survey Instruments, 90
duty, 0.25 team-workdays/restored duty and 0.75 Electronics + 0.75 Composites
per restored duty. The measurement threshold comes from the three tests. A miss
of the public target is a typed observed decision; acknowledgment permits the
real component to continue toward production.

### Prototype and serial shipyard supply

A demonstrated component is immediately a normal immutable design option.
Saving a class and authorizing an order do not require a production process.
Only developed components consult P5 supply; established catalog components
retain their existing manufacturability.

When the next hull first receives positive yard capacity, its developed
component supply is planned atomically. A complete local path is either an
effective serial process or the exact number of available local prototype units.
Zero capacity does not reserve a prototype. An incomplete path yields zero build
progress and holds local FIFO. The resulting current-hull plan is durable and
frozen until completion, so a process becoming available later cannot replace a
reserved prototype in work.

Prototype-backed requirements subtract only the component's embodied serial
cost and component BP from that hull. Prototype/tooling overhead remains sunk
development expenditure. Completion consumes each reserved unit once and links
it to the produced ship; the next quantity receives a new plan. Serial hulls pay
the full component cost/BP. Production capability is local to its exact colony
and facility and becomes effective on the opening after qualification. P5 does
not provide prototype freight, generic equipment inventory, refits or process
copying.

Ships containing the component use the normal power, survey exposure,
observation, duty and maintenance paths. Threshold 6 detects normalized signal
7 where established threshold 10 does not; a demonstrated threshold 9 does not.
There is no target-based or P5-specific survey bonus. Developing the component
does not train service skill. Support qualification changes only the exact real
team; actual maintenance still needs compatible powered workshop hardware,
co-location, parts and elapsed work.

### v18 persistence boundary

Schema v18 persists public opportunity and hidden truth separately, technical
facilities and engineering qualifications, complete program work/report history,
design/prototype/test/developed-component provenance, local process and support
records, dynamic catalog/profile identities, frozen current-hull supply plans,
prototype reservations/consumption receipts, audit events and counters. Reverse
owners and daily facility budgets remain derived. Validation reconstructs
stage material/work totals, test-derived component/profile values, D+1 dates,
locality, support family, and effective prototype cost/BP credits. v17 and older
development files are rejected read-only and are never migrated or overwritten.

P5 does not introduce a generic technology graph, research points, random
breakthroughs, equipment freight/warehouse genealogy, broad education,
Mission Control, formal analysis of P4B operating records, local site refining,
final propulsion or P6 proving mechanics.
