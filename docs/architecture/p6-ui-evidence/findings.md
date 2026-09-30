# Native UI findings

## UI-000 — Integrated game workflow is difficult to operate through the current UI

**Class:** B

**Status:** Owner hypothesis decomposed into UI-001–UI-010 below.

This is not a standalone proven finding. The concrete records below identify
which visible conditions support the broader concern.

## UI-001 — Startup does not orient the player to current operational work

**Class:** B

**Severity:** Friction

**Exercises:** H0, H1

**Screenshots:** `screenshots/00-startup-overview.png`, `screenshots/00-startup-window-menu.png`, `screenshots/01-h1-waiting-intent-overview.png`

### Observed

The initial map, body table and empty Information rail expose the date and
object selection, but no active-program, attention or resource-constraint
summary. Loading a checkpoint with a waiting order leaves the same overview.
View lists the relevant operational panels in a flat menu.

### Expected and impact

A player should be able to establish what is currently authorized and what
needs attention before choosing a subsystem. The present view requires panel
exploration to establish those facts. The menu labels do make the subsystem
entry points discoverable once the menu is opened.

### Source knowledge required?

No source lookup was used. The initial screenshot alone cannot establish that
no active work or attention item exists.

### Reproduction

1. Launch at the reviewed head or load `01-waiting-intent.sqlite`.
2. Inspect the map, body overlay and Information rail.
3. Attempt to identify the standing order or current resource constraint.
4. Open View to discover the operational panels.

### Candidate direction and classification rationale

Consider a compact operational orientation surface with links to existing
work. This is explanation/navigation for existing data, hence Class B. A
broader redesign decision requires the remainder of the evidence pass.

## UI-002 — Dense tables truncate the labels needed to interpret state

**Class:** B

**Severity:** Friction

**Exercises:** H0, H1

**Screenshots:** `screenshots/00-startup-overview.png`, `screenshots/01-h1-standing-order.png`

### Observed

Body names, type/owner values and headings such as observation/assessment counts
are clipped in the body table. After enlarging Shipyard, the order status cell
still clips `Waiting for...`, while the cause occupies a narrow column with
many short wrapped lines. The original PNGs preserve the actual text density.

### Expected and impact

Status and cause need to be readable where the player compares work. Clipped
labels and very narrow explanations add interpretation effort even when ample
unused screen width exists elsewhere.

### Source knowledge required?

No. Hover-based recovery of every clipped field has not yet been tested; this
record establishes the default visible reading problem, not an accessibility
compliance conclusion.

### Reproduction

1. Open Bodies / System at startup.
2. Load H1 and open Shipyard / Production.
3. Enlarge the panel and scroll to Shipyard orders.
4. Read the Status and Explanation columns.

### Candidate direction and classification rationale

Prioritize semantic columns and readable status/cause text using existing data.
This is presentation of implemented mechanics, hence Class B.

## UI-003 — Existing shipyard orders are buried below the complete design editor

**Class:** B

**Severity:** Major

**Exercises:** H1

**Screenshots:** `screenshots/01-h1-waiting-intent-detail.png`, `screenshots/01-h1-standing-order.png`

### Observed

Opening Shipyard shows Build controls, a generic Ready line and a full new
revision/component editor. The waiting order appears only after resizing and
scrolling past the catalog. No visible orders shortcut or order count at the
top directs the player to that standing commitment.

### Expected and impact

The task is to understand an already authorized wait. Putting all design-entry
content before the backlog makes a basic status check require unrelated
navigation and makes the initial Ready line easy to misinterpret.

### Source knowledge required?

No. The order was found by expanding, resizing and scrolling visible controls.

### Reproduction

1. Load `01-waiting-intent.sqlite`.
2. Open View → Shipyard / Production and expand if collapsed.
3. Inspect the first visible content.
4. Resize and scroll down until Shipyard orders appears.

### Candidate direction and classification rationale

Give existing commitments a direct entry point or separate them from the
new-design editor. This concerns access to existing order data, hence Class B.

## UI-004 — Comparing designs requires remembering values across a long editor

**Class:** B

**Severity:** Major

**Exercises:** H2

**Screenshots:** `screenshots/02-h2-class-selector.png`, `screenshots/02-h2-established-design.png`, `screenshots/02-h2-design-comparison.png`

### Observed

Start from selects one class into the draft editor. Its derived totals are
below the full catalog and the selected class identity scrolls out of view.
Comparing Established and Precision therefore required two top/bottom scroll
cycles and memorizing totals. No side-by-side or delta comparison control was
visible in this inspected workflow. The catalog does expose real threshold,
power, space, cost and service consequences.

### Expected and impact

The player needs to compare at least two operational alternatives without
losing which class a total belongs to. Reusing a long editing workflow for
inspection increases memory load and makes ordinary comparison cumbersome.

### Source knowledge required?

No source lookup. The visible values support the documented tradeoffs; this
finding concerns the effort to compare them, not missing simulation data.

### Reproduction

1. Load `02-design-choice.sqlite` and open Shipyard / Production.
2. Select Established Characterization Cutter under Start from.
3. Scroll below the catalog and remember derived totals.
4. Return to Start from, choose Precision, and scroll down again.

### Candidate direction and classification rationale

Keep inspected class identity and comparable consequences together, potentially
with a delta view. This presents existing design data, hence Class B.

## UI-005 — Completed orders still display remaining construction demand

**Class:** B

**Severity:** Major

**Exercises:** H2

**Screenshots:** `screenshots/02-h2-established-design.png`, `screenshots/02-h2-design-comparison.png`

### Observed

The first order row reads Qty 2, Done 2, Ships Left 0, Complete and ETA 0d, while
BP Remaining reads 520 and Materials Remaining lists 250 Alloys, 100 Electronics,
20 Reactor Fuel and 60 Composites. Its explanation also describes remaining
order build points despite the completed status.

### Expected and impact

Remaining demand should communicate outstanding work. A completed row with
nonzero remaining demand creates uncertainty about whether resources are still
owed, whether construction really completed, or what those columns mean.

### Source knowledge required?

No. The contradiction is visible in one row. Physical resource corruption was
not established, so this is not classified as a simulation integrity defect.

### Reproduction

1. Load `02-design-choice.sqlite`.
2. Open Shipyard / Production and scroll to Shipyard orders.
3. Compare completed counts/status with remaining BP/material columns.

### Candidate direction and classification rationale

Clarify per-class reference demand versus outstanding order demand and show
consistent completed-state values. This is a misleading existing projection,
hence Class B pending a separate code diagnosis.

## UI-006 — A selected body does not carry into development authoring

**Class:** B

**Severity:** Friction

**Exercises:** H3

**Screenshots:** `screenshots/03-h3-target-context.png`, `screenshots/03-h3-context-not-carried.png`, `screenshots/03-h3-development-authoring.png`

### Observed

The Information rail identified Uninvestigated Ice Prospect and its unknown
knowledge state. Opening Sites / Development through View left Known body
Unassigned. The rail had no visible development action. The same body had to be
found again in a separately scrolling dropdown.

### Expected and impact

An action about the inspected target should retain or explicitly offer its
context. Repeating object selection breaks the flow from body inspection to
investment and creates a risk of authoring against a different body.

### Source knowledge required?

No. Visible menu labels and the full target name were enough. Once the duplicate
selection and support colony were supplied, the preview honestly exposed costs
and uncertainty and enabled authorization without survey evidence.

### Reproduction

1. Load `03-blind-development.sqlite`.
2. Select Uninvestigated Ice Prospect in Bodies / System.
3. Open Sites / Development from View.
4. Observe selected target in the rail but Known body Unassigned in the form.

### Candidate direction and classification rationale

Offer a contextual development action or prefill/confirm the selected public
body. This joins existing selection and authoring workflows, hence Class B.

## UI-007 — The evidence dossier requires long scans through repeated channel prose

**Class:** B

**Severity:** Friction

**Exercises:** H4

**Screenshots:** `screenshots/04-h4-raw-observation.png`, `screenshots/04-h4-assessment-water-ice.png`, `screenshots/04-h4-operating-evidence.png`

### Observed

The Evidence / Analysis view begins with a new-analysis form, then existing
commitments, then the body dossier. A raw observation lists fourteen channels;
the assessment repeats several provenance/date/method lines for each channel.
The reviewer resized the panel and used two long scroll segments to reach
Water Ice, with the target identity and assessment header moving out of view. The records themselves
are consistent: raw detection, assessment Indicated and site published Indicated.

### Expected and impact

The reviewer needs to distinguish raw evidence, interpretation and operating
results while retaining target and date context. Repetitive prose gives every
channel similar visual weight and makes the meaningful result harder to find.
The explicit limits and provenance are useful and should remain available.

### Source knowledge required?

No. An initial reviewer misreading was corrected by independent inspection of
the same PNGs. No Class A contradiction is supported or retained.

### Reproduction

1. Load `04-evidence-and-operation.sqlite`.
2. Open Evidence / Analysis and enlarge the panel.
3. Inspect raw batch #1 for Uninvestigated Ice Prospect.
4. Scroll through the repeated mineral blocks to Assessment #1's Water Ice claim.
5. Open Sites / Development → Operating evidence and scientific records to
   connect it to actual operation.

### Candidate direction and classification rationale

Consider a concise target/claim summary with expandable provenance and a clear
route to related operating evidence. This changes access and hierarchy for
existing records, hence Class B.

## UI-008 — A stopped time advance gives a cause without a route to the affected work

**Class:** B

**Severity:** Major

**Exercises:** H5

**Screenshots:** `screenshots/05-h5-limitation-overview.png`, `screenshots/05-h5-time-stopped.png`, `screenshots/05-h5-time-control.png`, `screenshots/05-h5-acknowledgment.png`

### Observed

The loaded pending-decision checkpoint had no visible attention marker on the
map overview. +1 stopped correctly and displayed a generic site zero-recovery
message in the top bar, with Dismiss but no affected-site name or navigation.
After using Time Control's own +1 button, it showed a clipped stop message and
Status: OK. The actionable
acknowledgment was found separately in Sites / Development.

### Expected and impact

A consequential stop should identify the affected commitment and provide a
clear route to its existing response. Disconnected local feedback makes the
player search panels to discover whether an action is needed and where it is.

### Source knowledge required?

No. The word "site" led to the correct panel. Once there, the named issue and
acknowledgment were understandable, and ten later days required no repeated
response to the accepted condition.

### Reproduction

1. Load `05-accepted-bottleneck.sqlite` with operational panels closed.
2. Click global +1 and observe zero elapsed days and the top-bar message.
3. Open Time Control, then inspect the affected site in Sites / Development.
4. Compare the feedback and availability of the acknowledgment action.

### Candidate direction and classification rationale

Connect current attention state, typed source and existing response navigation.
This is an existing-workflow explanation problem, hence Class B; it does not
require a new notification policy or simulation authority.

## UI-009 — The latest published report is reached through an oldest-first text history

**Class:** B

**Severity:** Friction

**Exercises:** H7

**Screenshots:** `screenshots/07-h7-period-reports.png`, `screenshots/07-h7-latest-report.png`

### Observed

After advancing from Day 190 to 280, Site operating reports opened as a prose
list beginning with Days 21–30. Reaching the latest Days 240–270 review required
scrolling through repeated policy/cost/stock lines. The current period did have
useful output, delivery and wait information once reached.

### Expected and impact

A strategic review should make the latest published period and material changes easy to
find while preserving older history. The current layout adds repeated scanning
to ordinary monthly or quarterly inspection.

### Source knowledge required?

No. The section label and dated rows were understandable. The native 90-day
interval itself completed without mandatory interventions or daily dispatch.

### Reproduction

1. Load `07-delegated-90-day.sqlite` and advance three times with +30.
2. Open Sites / Development → Site operating reports.
3. Find the latest quarterly review among the earlier prose blocks.

### Candidate direction and classification rationale

Give the latest review a direct summary/selection while retaining dated history.
This is access to existing reports, hence Class B.

## UI-010 — Workspace switching opens overlapping retained panel layouts

**Class:** B

**Severity:** Major

**Exercises:** Supplemental H0/H7 navigation check

**Screenshots:** `screenshots/08-workspace-menu.png`, `screenshots/08-production-workspace.png`

### Observed

After the documented review resizes, choosing Workspace → Production opened
Economy Forecast, Technical Development and Shipyard in overlapping retained
positions/sizes. Foreground panels obscured other headings and controls; dense
underlying text remained visible through panel backgrounds. The preset did
provide relevant tool entry points, but did not produce a readable arrangement
for this existing session layout.

### Expected and impact

A workspace switch should help the player establish a coherent working context.
Stacking several tools with retained geometry instead creates more window
management before the intended work can be read.

### Source knowledge required?

No. This is directly captured from a single visible workspace selection.
It is not a claim about a fresh install or an untouched default layout.

### Reproduction

1. Follow H1/H3's normal panel enlargement steps in this session.
2. Finish with the expanded Sites view open.
3. Choose Workspace → Production.
4. Observe the stacked Economy, Technical Development and Shipyard views.

### Candidate direction and classification rationale

Review how existing workspace choices restore geometry, select foreground work
and preserve context. This concerns current window/navigation behavior, hence
Class B. Any broader redesign remains an owner scope decision.
