# Project state

Reviewed 2026-10-02 against `master` at `ab234ab`. This is a compact status
snapshot, not a gameplay contract or a record of every change. Read the
[README](../README.md) for build and run commands, [AGENTS.md](../AGENTS.md) for
repository working rules, and the
[simulation state contract](architecture/simulation-state-contract.md) for
accepted behavior and persistence boundaries.

## Current stage

- **CONFIRMED:** The repository contains the P1–P5 gameplay systems, integrated
  P6 automated whole-loop proving, and the optional SDL3 / Dear ImGui desktop
  shell. The current save format is v18; older development saves are unsupported.
  The README summarizes the implemented features.
- **CONFIRMED:** The P6 implementation and later Shipyard readability and
  design-comparison changes are merged on `master`. The
  [P6 proving report](architecture/p6-proving-report.md) and
  [UI readability reports](architecture/ui-readability/) record the evidence and
  limits of their respective revisions; their branch and PR status statements
  describe those historical checkpoints.
- **PENDING IN TRACKED EVIDENCE:** The
  [P6 H1–H7 native play worksheet](architecture/p6-human-play-review.md) has no
  completed exercise results. Automated suites and focused native UI captures
  do not establish whole-loop human usability.

## Known limits and open questions

- **CONFIRMED:** The [P6 residual register](architecture/p6-residual-register.md)
  retains deferred mechanics and a nonblocking long-session performance
  follow-up. Its deferred rows are not implementation requirements.
- **UNKNOWN:** Whether H1–H7 review or final owner acceptance of the latest UI
  changes occurred outside the tracked evidence. A merge alone does not answer
  those review questions.

## Next useful boundary

- **PROPOSED:** Run and record the native H1–H7 review against an identified
  current build and review pack. Triage observed findings against the existing
  integrity, explanation, and new-mechanic classes before selecting the next
  implementation unit.

Update this page when the review status or next accepted implementation boundary
changes. Keep detailed rules in the state contract and historical validation in
the phase reports.
