# LLM seams (M23)

The link-up doc for the other developer: where a model may plug in,
what it may do there, and what is structurally out of reach. The rule
this whole system already settled and M23 codifies (`decided.md`,
Phase 4):

- a model may **propose** record edits, write or rephrase report
  prose/note fields, and plan multi-step flows through engines;
- a model may **never** set a check verdict, alter `location` /
  `reason` evidence, or mark a record VERIFIED. Verification is
  deterministic-only: the checks module reads no model output.

## The seams, one by one

### 1. Proposed record edits — `PUT /api/projects/{id}`

A model proposes an edit as a full candidate record body (the stored
shape). The route validates it with the very same
`DesignRecord.from_dict` load uses, so a record this build cannot
later read back cannot be saved — proposed or human. The edit then
meets the report like any other: the checks run fresh against the
stored record at report time. Proposing is the model's whole reach
here; nothing about the edit can set a verdict, because verdicts are
computed, never carried — `assemble_report` runs `run_checks` fresh
on every request (the report is generated, never stored).

### 2. Report prose and notes — `src/engines/crosscheck/report.py`

The report carries prose (the page's footer notes, the JSON rows'
`hint`) that a model may rephrase or write — constrained to words that
restate what the checker already said. What the prose may not do:
invent a new tolerance, new semantics, or reword a `reason` in a way
that changes its truth, because those columns are the evidence. The
shipped `hint` column is repo-owned text keyed per check id for
exactly this reason (the Phase 0 decision in the module docstring):
a hint a model generated can say things the system does not mean.

### 3. Multi-step flows — `/intelligence/stream-plan` + `/workflows/execute`

A model proposes a plan: a list of engine calls with parameter
threading (`{node:<id>}.result.<path>`) whose shape
`src/intelligence/plan.py` validates (roster-only engines, acyclic,
bounded). The workflow executor owns execution: each node's engine
runs, and its `EngineResult` lands as a new `engine_checks` entry on
the design record. The verdict comes afterwards, from the crosscheck
module reading the recorded engine result — the model that proposed
the flow never decides whether the result passed.

## The never-side

One rule, three shapes, each enforced by a different layer:

| The model's temptation | Who enforces the refusal |
|---|---|
| Set a check verdict | `check.py` — verdicts are computed from the record; no input field carries one. |
| Alter `location` / `reason` evidence | The writes go only through `PUT`, which validates the record's shape, never its verdicts; the report recomputes verdicts at read time, so stored prose cannot smuggle evidence in. |
| Mark a record VERIFIED | `overall_status` — VERIFIED is the conjunctional fold (every check PASS, C00 gate included); it is not stored anywhere for anyone to set. |

Adding a new seam later means stating, in code where the proposer
sits, what it may propose and which layer enforces the never-side —
this doc gets one more row when it does.
