# Native session 01

Source gameplay/UI SHA: `f0f11672238b5edc70f72a607644b2d2abc447dd`.
Fresh pack: `/tmp/deep-signal-p6-ui-evidence-f0f1167`.
No source code was used to locate a control during this walkthrough.

## H0 — orientation

Initial normal launch at 1280 × 720 client. Date Day 0 and time controls were
clear. Strategic Map and a floating Bodies/System table were visible; the
Information rail said No selection. Controlled assets, active programs,
attention items and stock constraints were not summarized on this screen.
The table clipped headers and values, and overlaid central map content.

Actions: captured initial window; maximized the native app; opened View.
The flat View menu revealed named entry points for shipyard, survey, evidence,
site development, freight and technical development. No source lookup needed.
Major operational panel transitions: 0; one menu exploration plus native
maximization. Next action was to choose a panel; no strategic priority was
established by the initial view.

Result: Friction. Findings: UI-001, UI-002. Positive: date, map interaction hints,
honest No selection, and explicit unmeasured reserves/non-detection caveat.
Evidence: `00-startup-overview.png`, `00-startup-window-menu.png`.

## H1 — durable waiting intent

Loaded `01-waiting-intent.sqlite` through View → Save / Load by entering the
supplied absolute path. Load succeeded, but no order/attention overview appeared.
Closed the Save / Load and Bodies overlays; opened View → Shipyard / Production.
The panel appeared collapsed in this session. Expanded it, enlarged it using
its visible corner grip, then scrolled past the full component editor to reach
the order table. Source/layout origin of the initial collapsed state was not
investigated.

The order table showed one Survey Cutter order at Terra, Done 0, one ship left,
500 BP remaining and ETA `---`. Its narrow explanation column stated that no
positive colony shipyard capacity exists and ETA cannot be estimated. Thus the
standing intention and actual capacity cause could be understood after hunting.
The status cell itself was clipped to `Waiting fo...`. The top of the panel
instead showed generic `Ready` and build/editor controls, without an immediate
route to the existing order.

Major panel transitions after load: 1 (Shipyard); additional actions: dismiss
two overlays, expand, resize, scroll. Source knowledge required: No. Next
relevant navigation action was not obvious until exploring the panel's long
scroll content. Enabling positive colony yard capacity is understandable from
the eventual explanation; no progress or ETA was fabricated.

Result: Friction, with a Major discoverability finding UI-003 and table-reading
friction UI-002. Evidence: `01-h1-waiting-intent-overview.png`,
`01-h1-panel-collapsed.png`, `01-h1-waiting-intent-detail.png`,
`01-h1-standing-order.png`.

## H2 — ship design choice

Loaded `02-design-choice.sqlite` (Day 25). The Shipyard panel retained its
previous scroll position. Returned to its top, opened Start from, selected
Established Characterization Cutter, then scrolled to totals. Repeated that
navigation for Precision Characterization Cutter. No revision was saved and
no hull was ordered.

Visible Established totals: mass 500, used volume 350/1000, power demand 60,
margin 60, 500 BP, 80 Electronics and 50 Composites. Precision totals: mass 505,
used volume 370, demand 75, margin 45, 520 BP, 100 Electronics and 60 Composites.
The catalog exposed threshold 10 versus 6; duty capacity 120 versus 90 and
0.200 versus 0.250 team-workdays per restored duty. Thus the improved detection
threshold carries space/power/material and service tradeoffs; established
equipment remains available and constructible.

There is no visible comparison workspace in the inspected panel. Comparing
class totals required remembering the previous values across two selector and
scroll cycles. The selected class identity scrolls away from its totals. The
catalog provides helpful method/prototype/process explanations but mixes them
with editable quantities and unrelated components. Service family is shown
as a numeric ID.

The completed Precision order row simultaneously showed Qty 2, Done 2,
Ships Left 0, Status Complete, ETA 0d, **BP Remaining 520** and nonempty
Materials Remaining. This is a visible contradiction in the status display;
no claim about incorrect physical stock accounting is inferred from it.

Major panel transitions: 2 (Load then back to Shipyard); two long scroll and
class-selection cycles. Source lookup required: No. Result: Friction, with
Major findings UI-004 and UI-005. Evidence: `02-h2-design-choice-overview.png`,
`02-h2-class-selector.png`, `02-h2-established-design.png`,
`02-h2-design-comparison.png`.

## H3 — blind development

Loaded `03-blind-development.sqlite` (Day 0), with no operational panel open.
The central map labels overlapped, so used View → Bodies / System to select
the visible truncated unknown-prospect row. The Information rail resolved its
full name and showed Observation batches 0, Assessments 0, Reserve Unmeasured,
Site suitability Unassessed. It contained no visible development action.

Opened View → Sites / Development. Known body remained Unassigned despite the
selected prospect in the rail. Enlarged the authoring panel, entered draft
names, scrolled the public-body dropdown and selected that same prospect again,
then selected P4B Supply Base. Left builder, engineering team and leader
unassigned. The preview became actionable and showed real construction inputs,
14+2 workdays, power, nominal extraction/handling/storage and duty supplies.
It explicitly said yield/reserve lifetime are not established and survey,
staffing and delivered supplies are not admission gates. Authorize was enabled.
No authorization was committed; this exercise used draft preview only.

Major panel transitions after load: 2 (Bodies, Sites). Additional effort:
resize, duplicate target selection and dropdown scrolling. Source knowledge
required: No. Result: Friction (UI-006). No hidden suitability leak or mandatory
survey gate was observed. Evidence: `03-h3-blind-development-overview.png`,
`03-h3-target-context.png`, `03-h3-context-not-carried.png`,
`03-h3-development-authoring.png`.

## H4 — observation, assessment and operating result

Loaded `04-evidence-and-operation.sqlite` (Day 100). Opened Evidence / Analysis,
enlarged it, and navigated below the new-analysis form. The Body evidence dossier
selected Uninvestigated Ice Prospect. Raw batch #1 showed acquisition day 18,
field window 14–18, availability day 19, fleet #7, survey #1, scientist #2,
Characterization v1 threshold 10 and exposure 5/5. Water Ice was Detected with
High accessibility; other channels explicitly said not detected within method
limits, without establishing absence.

Scrolled to Assessment #1 revision 1, published day 21 by job #1 from batch #1.
Its Water Ice headline says **Indicated; accessibility High**, consistent with
its method provenance **Detected indication; High** and the Sites summary
**Published Water Ice indication: Indicated**. The primary reviewer initially
misread the dense screenshot; independent screenshot inspection and a second
full-resolution review corrected that reading before publication. No scientific
contradiction finding is retained.

Switched to Sites / Development, collapsed the unrelated authoring form, and
expanded Operating evidence and scientific records. It explicitly distinguishes
dated operating evidence from scientific assessments and reserve lifetime.
Visible operation: 21 observed attempts, 210 Ice recovered, 200 raw Ice held,
27 Reactor Fuel + 27 Composites paid, storage currently full. Per-day rows show
attempted/recovered amounts. These records can be understood as actual local
operation, not a geological reserve measurement.

Major panel transitions after load: 2 (Evidence, Sites). Additional effort:
resize, two long scroll segments through repeated channel/provenance rows,
collapse authoring, expand operating evidence. Source knowledge used: No.
Result: Friction (UI-007). The evidence distinctions and limits are present and
consistent once located. Evidence:
`04-h4-evidence-overview.png`, `04-h4-raw-observation.png`,
`04-h4-assessment.png`, `04-h4-assessment-water-ice.png`,
`04-h4-observation-assessment-operation.png`, `04-h4-operating-evidence.png`.

## H5 — acknowledge an observed limitation

Loaded `05-accepted-bottleneck.sqlite` at Day 58. The map overview showed no
pending-decision marker. Clicking global +1 advanced zero days and exposed the
message that five or more genuine site attempts recovered no Water Ice. The
message did not name the site or link to its response. Opened Time Control to
look for that context; a second +1 attempt showed a clipped version of the
same stop message and Status: OK.

Opened Sites / Development. Its named site section showed the observed issue
and Acknowledge operating issue. Clicked once at Day 58. The button disappeared;
raw Ice remained zero and the already-paid support/attempt counts remained
five. Advanced +5 to Day 63 and +5 to Day 68. Both requests completed, no repeat
acknowledgment was needed, and support/attempt counts reached 15 while recovery
remained zero. Builders completed their physical return during the follow-up.

| Day | Source/message | Action | Type / discoverability |
| --- | --- | --- | --- |
| 58 | Existing site zero-recovery limitation | Two diagnostic +1 attempts stopped at 0 days while locating response | No source shortcut in global feedback; Time Control added no route to the affected site. |
| 58 | Named site issue in Sites / Development | Acknowledge operating issue once | Strategic acceptance; clear once the site was found. |
| 63 | Same physical limitation persists | +5 completed | No mandatory intervention. |
| 68 | Same physical limitation persists | +5 completed | No mandatory intervention; no fabricated output. |

Major diagnostic panel transitions: 2 (Time Control, Sites). Source knowledge
required: No. Result: Friction (UI-008), with the acknowledgment and physical
continuation behavior observed to work. Evidence: `05-h5-limitation-overview.png`,
`05-h5-time-stopped.png`, `05-h5-time-control.png`,
`05-h5-acknowledgment.png`, `05-h5-acknowledged-state.png`,
`05-h5-follow-up.png`.

## H6 — lasting capability

Loaded `06-lasting-capability.sqlite` (Day 100). Opened Sites / Development,
collapsed the long operating trace, then expanded Ice delivery and colony
processing and Related freight commitments. These sections supplied a useful
causal explanation without visiting separate colony/freight panels.

The UI showed the development Closed / Completed with builders returned and
the commissioned site still enabled. The site had recovered 210 Ice, retained
200, and delivered 10 to P4B Supply Base. The processing section showed the
1 Ice + 0.5 Volatiles → 1 Propellant recipe, raw Ice 5, gross Propellant produced
5 and current Propellant inventory 3005. It explicitly separated delivery,
gross production, inventory and engine-fuel transfers. Related freight names,
routes and statuses connected the site to its supplies and standing collection.

Major panel transitions after load: 1 (Sites). Two section expansions and one
scroll. Source knowledge required: No. Result: Pass for this exercise; the
joined site/freight/processing explanation is a strength to preserve. The
initial map still has UI-001 orientation friction, but the requested lasting
consequence could be explained from the existing panel. Evidence:
`06-h6-lasting-capability-overview.png`, `06-h6-supply-consequence.png`,
`06-h6-related-freight.png`.

## H7 — delegated 90-day operation

Loaded `07-delegated-90-day.sqlite` at Day 190 with operational panels closed.
Used the global +30 control three times, inspecting each result: Day 220,
Day 250 and Day 280. Every request advanced all 30 days. There were zero
mandatory interventions, zero repeated accepted-condition prompts, zero routine
dispatch commands and zero diagnostic panel switches during this interval.
The three time requests themselves were deliberate strategic advancement.

| Start → end | Native action/result | Mandatory response | Diagnostic panel switches |
| --- | --- | ---: | ---: |
| 190 → 220 | +30; Advanced 30 day(s) | 0 | 0 |
| 220 → 250 | +30; Advanced 30 day(s) | 0 | 0 |
| 250 → 280 | +30; Advanced 30 day(s) | 0 | 0 |

Afterward opened Sites / Development and expanded Site operating reports.
Reports appear as an oldest-first prose list; scrolling past prior periods was
needed to inspect Days 240–270. Its quarterly marker, paid duty, recovered and
delivered quantities, historical policy and ordinary storage wait were visible.
The processing section showed 930 Ice delivered and 770 gross Propellant by
Day 280, with inventory separately labeled. The loop continued without daily
player dispatch. Finding UI-009 concerns report access/readability, not the
delegation result.

Major panel transitions after the interval: 1 (Sites), plus report expansion
and scrolling. Source knowledge required: No. Result: Friction, with successful
90-day native advancement and no intervention episode to screenshot. The three
dated result images document that absence rather than inventing an example.
Evidence: `07-h7-delegated-overview.png`, `07-h7-day220.png`,
`07-h7-day250.png`, `07-h7-day280.png`, `07-h7-period-reports.png`,
`07-h7-latest-report.png`.

## Supplemental workspace check

After H7, inspected Workspace as an alternative navigation route. It offers
System, Economy, Production, Fleets, Intelligence and History. Selected
Production once. In this session, after the documented panel enlargements,
the preset opened Economy Forecast, Technical Development and Shipyard in
overlapping retained positions/sizes. Their content obscured one another and
the previously visible work; the screenshot records the resulting state.
This is not claimed to be a pristine/default preset layout. Finding UI-010
records the observed transition, with no functional changes attempted.
Evidence: `08-workspace-menu.png`, `08-production-workspace.png`.

## Session closeout

PNG capture window: 2026-09-30 09:37:33–10:07:45 America/Denver (30 minutes,
12 seconds). The audit's native app instance closed normally at 10:10:43.
No checkpoint was overwritten; the seven input save modification times still
precede the first screenshot. H3 changed an unsaved draft only. H5/H7 used
ordinary commands in the live session, then those in-memory worlds were
replaced by Load or discarded on exit.

All H0–H7 tasks were reached. No task required opening source to locate a
control. The reviewer has prior project context, so this is not claimed to be
a novice study. An independent screenshot check corrected the H4 reading
before any evidence commit. No Class A contradiction remains in the findings.

The detailed panels can distinguish requested, actual and historical work,
and the native loop supported ordinary strategic time advancement. The weak
points are initial orientation, carrying object context into actions, finding
an existing commitment below authoring forms, comparing designs, routing a
time stop to its response and reaching the latest report. The strongest joined
view was H6's site/freight/processing consequence. Unknown-site preview remained
available and did not expose hidden suitability. No new mechanic was requested
or implemented during this pass.

Immediate recommendation: **the evidence shows cross-window information
architecture failure**, with additional local presentation defects. Use the
existing data and H6's relationship cues as a basis for an owner-scoped UI
remediation handoff. This session does not authorize that remediation or claim
P6 acceptance.
