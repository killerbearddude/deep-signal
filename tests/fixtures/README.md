# Schema v10 compatibility fixture

`schema_v10_39fa628.sqlite` is a small, valid SQLite save written by
`SaveGameRepository::save()` from the exact pre-H1B commit
`39fa62883cea85b3865e26ab871a326c4951094f` (the H1A merge baseline).
It was not produced by editing a schema marker or converting a v11 database.
The fixture contains one system, body, colony, and ship class, plus two active
shipyard orders. It contains no personal game data.

The detached source state deliberately stores same-colony shipyard orders in
FIFO vector order **[ID 2, ID 1]**. Both need ten build points and the colony
has a ten-point daily pool. `validateGameState()` accepts the state. Schema v10
has no order ordinal for `shipyard_orders`, so its loader's `ORDER BY id`
reconstructs **[ID 1, ID 2]**. The original order cannot be recovered from this
file. Compatibility tests should verify truthful legacy reconstruction, not
claim that a v10 load recovers the lost vector order.

## Provenance and reproduction

The checked-in [generator](generate_schema_v10_39fa628.cpp) is a disposable
standalone probe. Compile it against the pinned baseline source and libraries,
then direct its output to a temporary path. These are the executed commands
with the local SQLite dependency paths represented by task-specific variables:

```sh
repo_root="$(git rev-parse --show-toplevel)"
: "${H1B_SQLITE_INCLUDE_DIR:?Set this to the directory containing sqlite3.h}"
: "${H1B_SQLITE_LIBRARY:?Set this to a compatible SQLite shared library}"
mkdir -p /tmp/deep-signal-h1b-baseline-39fa628
git -C "$repo_root" archive 39fa62883cea85b3865e26ab871a326c4951094f | tar -x -C /tmp/deep-signal-h1b-baseline-39fa628
cmake -S /tmp/deep-signal-h1b-baseline-39fa628 -B /tmp/deep-signal-h1b-baseline-build -G Ninja -DDEEP_SIGNAL_BUILD_UI=OFF -DSQLite3_INCLUDE_DIR="$H1B_SQLITE_INCLUDE_DIR" -DSQLite3_LIBRARY="$H1B_SQLITE_LIBRARY"
cmake --build /tmp/deep-signal-h1b-baseline-build --target deep_signal_save_load_tests -j 4
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -I/tmp/deep-signal-h1b-baseline-39fa628/src -I"$H1B_SQLITE_INCLUDE_DIR" "$repo_root/tests/fixtures/generate_schema_v10_39fa628.cpp" /tmp/deep-signal-h1b-baseline-build/libdeep_signal_save.a /tmp/deep-signal-h1b-baseline-build/libdeep_signal_sim.a "$H1B_SQLITE_LIBRARY" -o /tmp/deep-signal-h1b-v10-probe
/tmp/deep-signal-h1b-v10-probe /tmp/deep-signal-h1b-v10-fixture.sqlite
sha256sum /tmp/deep-signal-h1b-v10-fixture.sqlite
```

The local run used SQLite 3.37.2 headers and the system `libsqlite3.so.0`
runtime library. No repository dependency was installed or changed.

The compiler emitted no warnings. The probe output was:

```text
before_save day=0 order_ids=2(0/1,bp=0) 1(0/1,bp=0) nextFleetId=1 nextShipId=1 nextEventId=1
after_load day=0 order_ids=1(0/1,bp=0) 2(0/1,bp=0) nextFleetId=1 nextShipId=1 nextEventId=1
uninterrupted_day1 day=1 order_ids=2(1/1,bp=0) 1(0/1,bp=0) nextFleetId=2 nextShipId=2 nextEventId=2
resumed_day1 day=1 order_ids=1(1/1,bp=0) 2(0/1,bp=0) nextFleetId=2 nextShipId=2 nextEventId=2
uninterrupted_events count=1 event_id=1 completed_order=2
resumed_events count=1 event_id=1 completed_order=1
```

The fixture is **180,224 bytes**, SHA-256
`74ff82c82117795dd70408f08b444983f049da25401b5b1781777256e46e9b44`.
A read-only SQLite check returned `schema_version = 10`,
`PRAGMA integrity_check = ok`, and no `PRAGMA foreign_key_check` rows.
`shipyard_orders` has no `ordinal` column. The probe also successfully loaded
the generated save through the baseline repository and advanced both simulations
one day. Rebuilding with a different SQLite release may yield different file
bytes; compare the schema and logical contents as well as the probe output.
