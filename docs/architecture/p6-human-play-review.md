# P6 native human play review

**Status: awaiting native interactive review.** The implementation environment
has no `DISPLAY` or `WAYLAND_DISPLAY`. UI compilation and automated ImGui
submission cannot establish that a human can understand and operate the slice.
This page is a concise review worksheet; the package's
`Deep_Signal_P6_Human_Play_Review.md` remains
the full exercise specification.

## Review setup

Use a native SDL3/Dear ImGui session, the final P6 PR head, and the seven
command-earned v18 saves from the generated review pack. Generate a fresh pack
into a new directory with:

```sh
build-p6/deep_signal_cli --write-p6-human-review-pack /tmp/deep-signal-p6-native-review
```

The command refuses an existing destination. It saves, reloads, validates,
checks SQLite foreign keys and continues each checkpoint through its next
modeled day or issue response before publishing the directory. The pack's
`REVIEW_MANIFEST.md` maps files to exercises; filenames and in-game text do not
disclose hidden geology or technical candidate truth. Existing local packs
can be used if their generating commit is recorded below. The implementation
commit `1c17df893b84203b6ce984faf99cbf5287884173` generated a verified
pack at `/tmp/deep-signal-p6-review-1c17df8`; regenerate it if this temporary
artifact is unavailable at review time.

Record the review commit SHA, OS/session/display backend, UI build settings,
screen resolution/scaling, reviewer, date and whether each exercise used New
Game or a generated checkpoint. Target one focused 45–90 minute session; the
five simulated years are covered by automated soak, not manual clicking.

| Environment item | Actual review value |
| --- | --- |
| Commit SHA | Pending |
| Reviewer and date | Pending |
| OS and native session | Pending |
| Display backend | Pending |
| Resolution and scaling | Pending |
| UI build configuration | Pending |
| Review pack path / generating SHA | Pending |

## H1–H7 worksheet

For each exercise, record **Pass**, **Friction** or **Blocker**; what the
reviewer expected, what the UI communicated, the action taken, whether source
code knowledge was needed, and any finding ID with Class A/B/C triage.

| Exercise | Suggested checkpoint | Result | Notes and finding IDs |
| --- | --- | --- | --- |
| H1 — identify a durable wait, its actual cause and available response | `01-waiting-intent.sqlite` | Pending | |
| H2 — compare established and earned Precision ship designs, then explain two operational tradeoffs | `02-design-choice.sqlite` | Pending | |
| H3 — authorize or preview unknown-site development without hidden suitability advice | `03-blind-development.sqlite` | Pending | |
| H4 — distinguish observation, analyzed assessment and operating result | `04-evidence-and-operation.sqlite` | Pending | |
| H5 — acknowledge a real limitation and advance without routine repeat prompts | `05-accepted-bottleneck.sqlite` | Pending | |
| H6 — identify an earlier decision's lasting supply or technical consequence | `06-lasting-capability.sqlite` | Pending | |
| H7 — advance at least 90 simulated days under delegated work and count interventions | `07-delegated-90-day.sqlite` | Pending | |

After H7, answer in plain language whether the UI distinguishes authorized,
requested and physically executing work; names real waits; permits action
under uncertainty; helps interpret provenance and reports; and supports
strategic management by exception. Record the number of player interventions,
any daily dispatch chores, the most confusing control, and desired new
mechanics separately.

## Finding disposition

Class A covers a violation of an existing integrity or information contract.
Class B covers a material explanation or access problem with an existing
workflow. Class C requests a new mechanic and belongs in the residual register.
Do not merge P6 as complete while an applicable Class A or material Class B
finding remains unresolved. Do not turn a Class C idea into a P6 feature.

The automated P6 report records the code, test and checkpoint evidence. This
worksheet remains pending until the native H1–H7 review is actually performed.
