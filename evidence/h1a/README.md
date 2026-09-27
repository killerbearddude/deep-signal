# H1A review evidence: processing allocation integrity

This evidence accompanies the H1A implementation branch based on
`72f90baae66bfd00fd7810e8a93ad1fb90acc5df`. H1B save continuity remains
pending a separate review and instruction.

## Pinned-baseline reproduction

The small [baseline reproduction](baseline_repro.cpp) was compiled against an
archive of the exact base commit, not the modified worktree:

```sh
mkdir -p /tmp/deep-signal-h1a-baseline
git archive 72f90baae66bfd00fd7810e8a93ad1fb90acc5df src/sim |
  tar -x -C /tmp/deep-signal-h1a-baseline
g++ -std=c++20 -O0 -Wall -Wextra -Wpedantic -Wconversion \
  -I/tmp/deep-signal-h1a-baseline/src evidence/h1a/baseline_repro.cpp \
  /tmp/deep-signal-h1a-baseline/src/sim/GameStateValidation.cpp \
  /tmp/deep-signal-h1a-baseline/src/sim/Simulation.cpp \
  /tmp/deep-signal-h1a-baseline/src/sim/ScenarioFactory.cpp \
  /tmp/deep-signal-h1a-baseline/src/sim/TransitPlanning.cpp \
  -o /tmp/deep-signal-h1a-baseline/repro
/tmp/deep-signal-h1a-baseline/repro
```

| Case | Pinned baseline observation | H1A observation |
|---|---|---|
| Two `DBL_MAX` Electronics rows | Command and state validation accepted; after one day Electronics stockpile `500 -> NaN` | Command rejected before Colony mutation |
| `DBL_MAX` Alloys and Electronics | Command and state validation accepted; both daily shares collapsed to zero | Command rejected before Colony mutation |
| One `DBL_MAX` Electronics row | Accepted; Electronics stockpile `500 -> 550` | Still accepted with finite full share and the same output |
| Overflowing dormant rows under Balanced | State validation accepted | State validation rejects the unrepresentable aggregate |

The corrected run linked this same source to the H1A `deep_signal_sim` library.
The separate baseline query reproduction also found `inf` stored, effective,
and draft percentages for one otherwise valid `DBL_MAX` row; the H1A query
regression now verifies finite `100%` at all three projection points.

## Automated execution

Fresh CMake/Ninja directories were configured for core-only, app/save, and
UI-enabled builds. The final sequential `ctest --output-on-failure -j 1` runs
passed **6/6**, **12/12**, and **17/17**, respectively. The new rule tests cover
both overflow shapes, invalid rows, zero and epsilon boundaries, repeated
sub-epsilon rows with a valid combined total, duplicates, hand-calculated
ratios, and valid near-maximum weights. Simulation, validation, query, forecast,
and editor tests exercise their respective boundaries. The UI build emitted
deprecated-enum warnings from bundled ImPlot; project-owned sources compiled
without warnings.

These tests prove the exercised inputs and side effects. The focused command
source review establishes that no gameplay mutation occurs before validation,
and its prepared Manual vector and success result precede the nonthrowing
configuration commit. The rejection test checks the selected Colony's full
configuration, inventory, and capacities, gameplay counters and collection
sizes, prior audit entry, and exactly one new rejection event. It does not
compare every field of every unrelated entity record.

## Agent-driven native UI checks

The native SDL3/ImGui application was operated on an isolated Xvfb display:

- [Manual applied, day 1](manual-applied-day1.png): Mars Naval Yards changed to
  Manual and advanced one day with finite stockpiles.
- [Preset retains Manual draft](preset-retains-manual.png): Fuel Focus was
  applied; reopening and switching the draft to Manual still showed the six
  prior `1.0` weights.
- [Schema-v10 Load succeeded](v10-load-success.png): an ordinary game was saved
  through the UI, reloaded through the UI, and continued to day 2. The test
  owned save's `schema_version` was `(1, 10)` and its six Manual rows remained.
- [Large valid allocation](large-allocation.png): a disposable v10 fixture with
  one `DBL_MAX` Electronics row was loaded through the UI. The editor displayed
  the weight compactly as `1.8e+308` and showed a finite `100.0%` Electronics
  share with `25.0` nominal units/day.
- [Clean-source smoke launch](clean-smoke.png): after deleting the disposable
  save and rebuilding the final source, the application launched normally with
  the standard initial scenario.

The extreme fixture was derived from the UI-created test save by replacing only
Mars Naval Yards' processing policy and Manual rows in disposable SQLite data.
It passed the normal native Load validation. The fixture and ordinary save were
removed from the repository after the check. These screenshots document
agent-driven input, not human verification or broader save-continuity proof.

H1A leaves the write/read format at v10. It does not repair malformed extreme
saves, prove ordering across Save/Load, or implement schema v11. Those are
separate H1B concerns.
