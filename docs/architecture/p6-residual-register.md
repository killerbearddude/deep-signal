# P6 residual register

This register separates defects in implemented P1–P5 behavior from new
simulation concepts. The P6 branch fixes only Class A/B defects supported by
reproducers. Class C rows remain decisions for a later milestone. Native review
findings must be appended here after H1–H7; they are not inferred from tests.

## Corrected implemented behavior

| ID | Class | Evidence and disposition |
| --- | --- | --- |
| P6-A1 / R11 | A | Closed SurveyPrograms formerly published future 30-day reports. A focused regression reproduced it, then the executor, validation and time preflight were corrected using existing audit/cursor state. Historical rows remain valid. Five-year soak kept the closed program's report count fixed. |
| P6-B1 | B | Two real technical programs shared one facility budget; the later program performed zero work while its query said ready. The shared readiness projection now explains full or partial next-opening contention. The executor's six-kind ordering and physical debit are unchanged. |

## Deferred scope entering and leaving P6

| ID | Residual | P6 disposition |
| --- | --- | --- |
| R01 | Staffed Mission Control and coordination workload | Class C — deferred. Existing bounded executors are proven without a new organization. |
| R02 | General multi-fleet expedition planner | Class C — deferred. P6 scenario scripting is developer test tooling only. |
| R03 | Remote tender support or rescue | Class C — deferred. |
| R04 | Finite scientific analysis and shared field/analysis team | Addressed earlier by P4A; retained in P6 evidence and tests. |
| R05 | Requirements/design scientific staffing across one shared person/team | Class C — deferred. Science and engineering remain distinct physical pools. |
| R06 | Evidence-responsive autonomous leader selection | Class C — deferred. No hidden deposit ranking or new planner. |
| R07 | Generic manufactured-equipment freight and lot genealogy | Class C — deferred. P5's one local prototype remains bounded. |
| R08 | Mass-sensitive transit, crew/stores and ship operating modes | Class C — deferred. P6 proves current transit and fuel accounting only. |
| R09 | Shipyard suspend/cancel lifecycle | Class C — deferred. P1 orders still lack that command. |
| R10 | Unleased manual transit-cancellation shortcut | Deferred. No P6 integrated integrity failure required a movement redesign. |
| R11 | Closed survey programs receiving future reports | Resolved narrowly as P6-A1. Native report comprehension remains H4/H7 pending. |
| R12 | Native human workflow/play review | Mandatory acceptance gate pending. The seven earned v18 checkpoints and H1–H7 worksheet are prepared. |
| R13 | Curated starting scientific archives | Class C — deferred. |
| R14 | Formal scientific interpretation of P4B operating evidence | Class C — deferred. Operating receipts remain distinct from P4A observations and assessments. |
| R15 | Rich site terrain, depth and hazards | Class C — deferred. |
| R16 | Broad research graph or research points | Class C — deferred. P5 remains one optional opportunity. |
| R17 | Generic prototype transport or refit | Class C — deferred. Prototype testing remains local. |
| R18 | Copying factory/process capability without local qualification | Class C — deferred. |
| R19 | Random research breakthroughs and failures | Class C — deferred. |
| R20 | Personnel sourcing, academies, recruitment and career pipeline | Class C — deferred. |
| R21 | Cumulative history scan cost in long sessions | Performance follow-up, nonblocking at five years. A fixed 365-day block rose from about 0.08 s in year one to 0.27 s in year five while annual event/receipt growth remained roughly linear. `siteDutySpent` scans all prior duty receipts during readiness, and site report publication scans prior receipt vectors. Optimize only after profiling a larger workload; do not add persisted cache state under P6. |

No new resource type, gameplay authority, scheduler, technology branch, routing
mode, refit, rescue system or schema v19 is introduced by P6. Further Class C
ideas from the native play session should be added with their observed context
and an explicit owner decision before implementation.
