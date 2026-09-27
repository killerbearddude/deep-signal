# H1B review evidence: deterministic save continuity

H1B starts from the merged H1A `master` commit
`39fa62883cea85b3865e26ab871a326c4951094f`. This report records
executed checks for the separate schema-v11 save-continuity slice. It does not
claim a v10 migration or a change to gameplay rules.

## Pinned v10 failure and authentic fixture

The [fixture provenance](../../tests/fixtures/README.md) records the exact
baseline source archive and generator commands for the test-owned
`schema_v10_39fa628.sqlite`. Its valid source state has one colony and two
shipyard orders in vector order `[ID 2, ID 1]`. The pinned v10 writer persisted
no shipyard-order position; its reader reconstructed `[ID 1, ID 2]`.

| Executed path | First order completed after one day |
|---|---:|
| Pinned baseline, uninterrupted | ID 2 |
| Pinned baseline, v10 Save/Load first | ID 1 |

Both runs emitted event ID 1, but its `ShipCompletedEvent.orderId` differed.
The fixture is an actual v10 save written by the pinned repository, not a v11
file relabeled as v10. Its SHA-256 is
`74ff82c82117795dd70408f08b444983f049da25401b5b1781777256e46e9b44`.
H1B's legacy reader retains the established v10 `[1, 2]` reconstruction; it
cannot recover the lost `[2, 1]` order.

## Ordering contract and executed continuation

Schema v11 writes a zero-based global ordinal for `star_systems`,
`institutions`, `people`, `bodies`, `colonies`, `mineral_deposits`,
`ship_classes`, `shipyard_orders`, `ships`, and `fleets`. Existing appointment,
Manual-row, and queued-move ordinals retain their global or parent-scoped
meanings. `ships.fleet_ordinal` separately records each Fleet's `shipIds`
roster; global Ship order is independent. Events remain in Event-ID order.

The [continuation tests](../../tests/save_continuity_tests.cpp) compare every
durable field and vector/child order, counters, plans, inventories, and event
payloads at checkpoints. Pre-save daily telemetry is excluded because it is
session-only; newly emitted telemetry is compared after continuation. Tests
give independent expected outcomes for:

- two same-body colonies competing for one deposit in non-ID vector order;
- FIFO shipyard orders whose IDs oppose their vector priority;
- per-hull move fuel payment with global Ship order differing from Fleet roster;
- same-day non-ID-ordered fleet arrivals, event IDs, and queued-leg promotion;
- mixed non-ID ordering across all ten global collections and ordered children;
- mid-leg Save/Load through arrival and the next queued departure, including
  `+N` versus `N` separate `+1` advances.

The [contract tests](../../tests/save_contract_tests.cpp) exercise new and
schema-empty v11 destinations, compatible replacement, authentic v10 read and
no-upgrade behavior, rejection of v10 overwrite, a new v11 path for a loaded
v10 game, unsupported and unrecognized destinations, version/structure
mismatches, strict ordinal failures, writer contention, and rollback after
old-row deletion and the first new version-row insertion. The rollback hook is
compiled only into a test-specific save target; the production save library has
no failure switch. A known-table trigger can be read safely but is rejected
before Save changes the destination. The test also accepts a structurally
equivalent v11 declaration with harmless SQL whitespace changes.

## Native application checks

Agent-driven SDL3/ImGui checks used isolated Xvfb displays and disposable
ignored `saves/` paths:

- [v11 Load succeeded](v11-loaded.png): an ordinary Manual policy was applied,
  advanced to day 1, saved through File, and reloaded. The saved file's version
  was 11 and Mars retained six stored Manual rows.
- [v11 continuation](v11-continued.png): the reloaded game advanced to day 2.
- [authentic v10 Load](v10-loaded.png): the checked-in v10 fixture loaded through
  the native panel without upgrading it.
- [v10 overwrite rejection](v10-overwrite-rejected.png): Save at the v10 path
  reported the new-path requirement. The copied v10 file retained the fixture's
  exact SHA-256 after the attempt.
- [new v11 destination](v10-new-v11.png): changing the editable path and saving
  the v10-loaded game succeeded. The new file reported version 11; its two
  shipyard orders had ordinals `[0, 1]` in the legacy-reconstructed order.
- [malformed Load status](malformed-load-status.png) and
  [retained draft](malformed-load-draft-retained.png): a disposable copy with an
  unsupported version failed Load, leaving the selected Colony and unapplied
  Fuel Focus editor draft intact.
- [successful Load clears editor](v11-success-clears-editor.png): a valid v11
  reload cleared the old-world editor and main selection.
- [clean-source smoke launch](clean-smoke.png): after deleting every disposable
  save and rebuilding, the native executable launched with its standard world.

No human UI verification is claimed. Screenshots do not prove the per-hull
fuel or event-order results; those use headless state comparisons.

## Build and limits

Fresh CMake/Ninja directories were used for core-only (`build-h1b-sim`),
app/save (`build-h1b`), and UI-enabled (`build-h1b-ui`) configurations, with
the local SQLite 3.37.2 development headers and SDL3 dependency prefix. Final
sequential CTest runs passed **6/6**, **14/14**, and **19/19**, respectively.
Project-owned sources compiled without warnings; the fresh UI build emitted
bundled ImPlot deprecated-enum warnings. No sanitizer run or human check is
claimed.

The new reader requires v11 ordinals to be integer, nonnegative, unique, and
contiguous in their scope. Load does not modify a v10 file. Save classifies an
existing destination and verifies its logical v11 snapshot inside one write
transaction before row replacement. A failed replacement preserves its prior
logical rows and schema; a failed first save can leave an empty file. This does
not promise byte-identical SQLite files after every open, power-loss recovery,
cross-compiler bitwise determinism, or continuity after gameplay rules change.
