# decided.md — Stage 11: cross-checks + verified report

The plan of record for the next build: a shared **design record** (parts, nets
and pins, board size, holes and connectors, firmware pin map), ~10
deterministic **cross-checks** across CAD / PCB / firmware / BOM, a
**verified report** page with export, and the storage + eval layer around it.

How to use this file: like `docs/PROCESS.md`'s settled decisions, this file
settles things so they stop being re-opened. When reality disagrees with this
file, reality wins — but the *change* is decided here first, in an edit to
this file, never improvised in a code session. Milestones follow the project
conventions: one concern per commit, ticked in `work.md` / narrated in
`opencodeworks.md`, verified before advertised, no faked capabilities.

Status: **Phase 0 decided** (the rule table below is the deliverable a plan
asked for: the rules a design must pass to be VERIFIED, and the pass/warn/fail
verdict for every rule). Phases 1–4 are open work; Phase 5 is optional.

Baseline at decision time: commit `734ac59` (M18-tick-books) on `main`,
Python suite 488 passed / 40 skipped, ruff clean.

---

## Phase 0 — Define "verified" (~1 day) — M19, **decided here**

Write the list of rules a design must pass to be VERIFIED and decide
pass / warn / fail for each rule. That list is this section. It is settled
against the engine surface that actually exists today (pcb:
`validate / generate / export / add_net / add_track`; firmware:
`create / select_mcu / configure_pin / configure_peripheral / generate /
validate / build`; cad: `generate / families / nest / drawings`; the registry
honesty rule governs anything new the record needs).

### Status semantics (settled)

- A **check** returns exactly one of **PASS / WARN / FAIL**, always with a
  human-readable `reason` and a `location` (net, part ref, pin, hole, edge —
  wherever the problem is). No check ever returns a bare verdict.
- **WARN never blocks a repair and never fails the report — but WARN never
  grants VERIFIED.** "passes" below means a PASS verdict.
- Overall record status extends the existing per-artifact chain
  (`GENERATED | VALIDATED | VERIFIED | FAILED` in `src/models/schemas.py`)
  with a report-level triad, decided as:
  - **VERIFIED** — every cross-check PASS **and** every engine check on the
    record's constituent artifacts green (the gate below).
  - **WARNING** — no FAIL, at least one WARN.
  - **FAILED** — any check FAILed, or any engine check FAILED.
- A check whose required input is **absent** (record lacks a section, engine
  answer has no such field) reports **WARN `cannot_verify` with the missing
  input named — never a PASS by default**. Nothing is verified that was never
  checked; this is the phantom-capability rule applied to verdicts.

### The rules

Gates and cross-checks C01–C11. C00 is the engine-check gate.

| ID | Rule | PASS when | WARN when | FAIL when |
|---|---|---|---|---|
| C00 | **Engine checks on constituents** — every engine `validate` on each artifact the record was built from (pcb DRC/validate, firmware validate, cad geometry checks) | every engine check VALIDATED/VERIFIED | — | any engine check FAILED (the report surfaces each per engine, verbatim) |
| C01 | **Record complete** — record carries every required section: `parts[]`, `nets[]` (each with `pins[]`), `board` (size + outline), `holes[]`, `connectors[]`, `enclosure` (cavity + standoff points + cutouts), `firmware` (mcu + pin map + peripherals); `bom` optional until Phase 2 | all present and schema-valid | — | any required section missing or malformed; reason names it |
| C02 | **MCU matches the board** — the firmware-record MCU is the MCU populated on the PCB | same part named by both records | same family, package string differs from the populated package | board has no MCU part or names a different one |
| C03 | **Firmware pins exist on the board** — every firmware-mapped pin resolves to a (part, pin) on the board | every mapped pin resolves to a populated pad | pad exists but is attached to no net (floating) | unknown part ref, or pin not on that part model |
| C04 | **Pins match named nets** — firmware-driven pins sit on nets the PCB actually declares | every driven pin sits on a named net with ≥2 attachments | its net has a single attachment (connects to nothing) | the net name firmware quotes is absent from the PCB netlist |
| C05 | **Peripherals on the board** — every firmware peripheral (uart / i2c / spi / pwm) has a physical counterpart | counterpart part/connector on the board spans the peripheral's mapped pins | counterpart present, pin assignment differs from the firmware config | peripheral with no counterpart |
| C06 | **Voltage domains compatible** — power-net classes coherent across firmware rails and board power nets | every power net carries exactly one class; the MCU's expected rail exists on the board | power net present but unclassified | two classes on one net (e.g. 5 V onto 3V3), or the MCU's rail is missing from the board |
| C07 | **Current budget** — per-rail draw vs capacity | every rail's summed draw ≤ 80% of capacity | 81–100%, or parts on the rail missing draw data | summed draw exceeds capacity |
| C08 | **Board fits the enclosure** — board outline inside the enclosure cavity | fully inside with ≥0.5 mm clearance | inside with <0.5 mm clearance | any overlap or outline outside the cavity (reason names the violating edge/corner) |
| C09 | **Mounting holes line up** — enclosure standoffs ↔ board holes | every standoff has a board hole within 0.2 mm | nearest hole drifts >0.2 mm | a standoff with no board hole |
| C10 | **Connectors exposed** — external connectors within enclosure cutouts | every external connector lies within a cutout window with clearance | inside the cutout but <0.5 mm edge distance | a connector no cutout covers (blocked by the enclosure wall) |
| C11 | **BOM matches parts** (activates in Phase 2) — BOM lines ↔ record parts | exact match on refs, quantity, value, package | refs match 1:1 but an attribute differs | missing or extra reference lines on either side |

**VERIFIED ⇒ C01–C11 all PASS and C00 green.** Anything less is WARNING or
FAILED, with the count of each on the report.

Fix hints are deterministic per check id (each check owns its hint text); no
check ever ships a hint generated by a model (see Phase 4 for where the LLM
plugs in — it never plugs in here).

### Evidence sources (what each check can read today, verified against the IRs)

Phase 0 was verified against the engine surface it will run on:
`app/native/include/trinity/pcb/PcbDesign.hpp` and
`app/native/include/trinity/firmware/FirmwareProject.hpp` (both
JSON-round-trip), plus the advertised op lists (pcb:
`validate / generate / export / add_net / add_track`; firmware:
`create / select_mcu / configure_pin / configure_peripheral / generate /
validate / build`).

| Evidence class | Checks | Evidence |
|---|---|---|
| **IR today** | C00, C02, C03, C04, C05 (counterpart side), C08/C10 (board side), the assembly half of C01 | `PcbDesign`: `Component{ref, value, footprint, type: Mcu\|Regulator\|Connector\|..., pins[] {number, name, electrical: PowerIn\|PowerOut\|..., posMm, net}}`, `Net{id, name, pins[] as "REF.NUM"}`, `BoardOutline{widthMm, heightMm, thicknessMm, originMm}`, `Placement{ref, xMm, yMm, rotationDeg}`, `Footprint`/`FootprintPad{posMm, sizeMm, kind: smd\|thru_hole}`, `TrackSegment`, `DesignRule`. `FirmwareProject`: `MCU{family, model, pins[], peripherals[]}`, `PinMapping{mcuPin, function, peripheral, direction}`, `FirmwareRequirement{kind: Gpio\|Pwm\|Uart\|I2c\|Spi}` |
| **Record-side annotation** (M20.1) | C01 (sections), C06 classes, C07 draw, C09 standoffs, C10 cutouts, C11 BOM | no IR field exists; the record carries these as explicit supplied sections, and an absent one is the WARN `cannot_verify` with the section named — never an invented default |
| **Needs an engine extension** | C09 (PCB NPTH mounting holes — the IR has `thru_hole` pads only), C08/C10 (enclosure cavity/cutout spec from CAD) | the only two seams where M20 may grow a field, decided per-milestone with the IR's round-trip-test convention, never silently |

Conventions pinned so M20 (and beyond) starts on the same page — items 1–2
were decided before M20.2, items 3–5 were pinned *by* M20.2 as they became
concrete:

1. **Match key (C03/C04):** firmware `PinMapping.mcuPin` is an MCU *pin
   name* (`GPIO21`), and it matches the PCB MCU component's `Pin.name`.
   C04's `location` quotes the pin and the net name it expected.
2. **Holes and enclosure stay record-side** until an M-milestone
   deliberately extends an engine; a missing section is always reported,
   never guessed.
3. **C02 verdict keys.** PASS = the exact part name on both sides
   (case-insensitive). WARN = one name is a *prefix* of the other — same
   part line at different package granularity (`ESP32` vs
   `ESP32-WROOM-32`). FAIL = a different member of the family
   (`ESP32-C6` vs `ESP32-WROOM-32`), which is a different part, not a
   package difference.
4. **Netlist refs are the single source of truth for connectivity.**
   C05's counterpart test reads who is on a net from `Net.pins`, never
   from pad-level `net` strings; C04 verifies each driven pad is itself
   listed by the net its pad claims. When those two views disagree, that
   is a reported failure, not a truth to pick from.
5. **Driven floating pins are double-reported on purpose** (C03's
   pin-layer WARN + C04's net-layer FAIL): C03 verifies pins exist and
   resolve, C04 verifies the nets those pins ride. Fixing the net clears
   both.
6. **Enclosure geometry lives in one mm frame, stated not guessed**
   (M20.3). The board corner is `pcb.board.origin_mm`;
   `enclosure.cavity_mm` must carry its own `origin_mm` explicitly — an
   absent origin is a WARN `cannot_verify`, never a default (0, 0).
   Record-side enclosure/holes/connectors fields use `_mm`-suffixed
   names (`cavity_mm`, `pos_mm`, `width_mm`, `height_mm`,
   `diameter_mm`). Bands: C08/C10 clearances below 0.5 mm WARN
   (`ENCLOSURE_CLEARANCE_MM`), any negative clearance FAIL (the inner
   escapes the window); C09 pairs each standoff with its nearest board
   hole — within 0.2 mm drift PASS (`HOLE_DRIFT_MM`), within 1.0 mm
   WARN, beyond that the standoff simply has no hole (FAIL;
   `HOLE_CAPTURE_MM = 1.0`). C10's exposing cutout must fully contain
   the connector rect to count; a connector no cutout contains FAILs
   with its closest window named.
7. **The gate and the electrical annotations are record-side (M20.4).**
   `engine_checks[]` stores each constituent artifact's engine-validate
   verdict verbatim (`{artifact, op, status, detail}`); statuses are the
   `src/models/schemas.py` chain — VALIDATED/VERIFIED close the C00
   gate, FAILED fails it with the detail verbatim, any unfinished status
   (e.g. GENERATED) is WARN cannot_verify (an engine check the record
   never made is never passed by default). The `power` section carries
   `rails[]` (`{net, class, capacity_ma, draws[]}`) and `mcu_rail`
   (`{class}`): C06 wants exactly **one** declared class per *board
   power net* (discovered from `power_in`/`power_out` pads; a
   present-but-unclassified net WARNs; two classes on one net FAIL; the
   MCU's rail absent from the board FAILs); C07 weighs sum(draws)
   against capacity_ma — ≤ 80% PASS (`RAIL_WARN_FRACTION = 0.8`),
   81–100% WARN, over capacity FAIL; a rider (any pad on the rail net,
   the MCU included) without draw data WARNs, and a rail without a
   usable capacity is not weighed (WARN — nothing unweighed passes
   silently). Both sections stay optional in record schema v1: absent =
   WARN cannot_verify (adding optional keys does not bump
   RECORD_SCHEMA_VERSION; an older build refuses the new keys by name
   via the existing unknown-key refusal).
8. **The overall triad is computed, never carried.**
   `overall_status()` folds a run: VERIFIED = every check PASS (the C00
   gate included); WARNING = no FAIL but ≥1 WARN; FAILED = any FAIL.
   WARN never grants VERIFIED, so a cannot_verify WARN denies it too
   (`status_counts()` carries the tally the report shows).

---

## Phase 1 — Cross-check + verified report (2–3 weeks) — M20

One shared design record: parts, nets and pins, board size, holes and
connectors, firmware pin map. The record is assembled from the engine
answered envelopes/artifacts on either backend (native `Trinity.exe` or the
Python fallbacks, per the bridge's preference) — a cross-check is only as
real as the data the record was actually built from.

Increments (one commit each, done-when per increment at the end):

- **M20.1 design record** — schema + builder + storage under
  `TRINITY_STORAGE_ROOT/projects/<id>/*.json` (files, not DB rows — no M17
  schema migration in this phase; snapshot history in Phase 2 rides the same
  dir). Schema tests pin every required section and every refusal path.
- **M20.2 cross-check framework + C01–C05** — the check-runner shape
  (`{check_id, name, status, reason, location, evidence}`), the registry-level
  honest refusal rules, and the connectivity half of the plan: pins vs nets,
  peripherals vs board. **Done-when: a wrong pin gives FAILED with the correct
  reason.** That is the phase-level acceptance test in miniature.
- **M20.3 enclosure checks C08–C10** — board outline vs cavity, holes vs
  standoffs, connectors vs cutouts, with the tolerances from the rule table.
- **M20.4 electrical checks C06–C07 + the C00 gate assembly** — overall
  status computed from gate + checks, exactly the VERIFIED/WARNING/FAILED
  semantics above.
- **M20.5 verified report** — assembly + the report page: overall status,
  per-check list, fix hints; backend route returns a self-contained HTML
  report. **HTML is the Phase 1 format.** PDF ships only when a real
  generator is vendored — until then the report page honestly says "print to
  PDF" rather than shipping a worse file and calling it a PDF.
- **M20.6 fixtures** — 3–5 good designs and 3–5 broken ones; every broken
  design targets one specific check (the wrong-pin fixture FAILs C04 and does
  not need to blanket-fail everything; cascades are diagnosed, not shipped).

In parallel (per the plan): **M20.e — the first 10 eval prompts** seeded into
`data/evals/` (see Phase 3 for the runner; the seeds land here).

Done when: a wrong pin gives FAILED with the correct reason.

---

## Phase 2 — BOM + local projects (1–2 weeks) — M21

- **M21.1 BOM export** — ref, part, value, quantity, package. CSV first
  (stdlib, round-trippable, tested by parse-back). XLSX ships only behind a
  real reader/writer library, verified by reading the file back — never
  hand-rolled bytes called an xlsx.
- **M21.2 local project open/save** — the design record saved and loaded
  locally (the Phase 1 storage layout gains its user entry points).
- **M21.3 snapshots** — every generate snapshots the record, with a history
  list, restore, and a compare view (diff of two snapshots: geometry, nets,
  pins, firmware map).

Done when: you can generate, change, restore an old version, and the BOM
matches.

---

## Phase 3 — Eval set (1 week, then ongoing) — M22

- Grow to **30–50 prompts** across CAD, PCB, firmware, math and mixed.
- Use the **verified report as the grader** wherever the prompt's truth is a
  record property (wrong-pin prompts grade themselves: the report must say
  FAILED, naming the check and the pin).
- **One-command runner** (`python tools/eval/run.py`) that prints a pass/fail
  table.
- **Ongoing rule:** run before every change; **add a prompt for every bug you
  fix** — the eval set is the regression suite the report sits on top of.

---

## Phase 4 — Link-up docs + Inspect mode (2–3 days) — M23

- **LLM plug-in points** marked with code comments + one markdown doc for the
  other developer. The seam rule this project already settled, applied here:
  - a model may **propose** record edits, write or rephrase report prose/note
    fields, and plan multi-step flows through engines;
  - a model may **never** set a check verdict, alter `location`/`reason`
    evidence, or mark a record VERIFIED. Verification is deterministic-only;
    the checks module reads no model output.
- **Inspect mode**: read-only — loads an existing record, runs the checks,
  shows the report, mutates nothing.

---

## Phase 5 (optional) — Data layer (3–4 weeks) — M24

- **Reference library**: ~10 designs that pass the verified report, tagged;
  similar ones retrieved at generate time; a **"correct this"** button saves
  fixes as *candidates*, which a human reviews and merges by hand before
  they enter the library. No auto-promotion, ever.
- **India parts and fabs**: 100–200 common parts with INR price and a
  "last updated" date (staleness is displayed, never hidden); 10–15 fabs with
  city and capabilities. Prices feed the BOM export as an INR column where
  the part has price data — absent elsewhere, never guessed.

---

## Parallel track — the 10 eval seeds (M20.e)

Seeded in Phase 1, grown in Phase 3. Each seed names its expected truth:

1. CAD — "Generate a quadcopter frame, 50 mm overall, export STL + JSON" →
   cad generate success, artifact links, VALIDATED.
2. CAD — "Nest 6× 20×30 brackets on a 100×200 plate with 2 mm spacing" →
   nesting layout with the pairwise overlap/spacing proof.
3. MATH — "Solve x^2 − 5x + 6 = 0" → solution set {2, 3}, VERIFIED by
   back-substitution.
4. MATH — "Differentiate sin(t) · t² with respect to t" → symbolic result
   with the derivative_check stamp.
5. FIRMWARE — "Create an ESP32 firmware project; uart0 on GPIO1/GPIO3;
   i2c0 pins GPIO21/GPIO22; generate sources" → generated sources +
   per-pin mappings, VALIDATED.
6. FIRMWARE — same request with one wrong pin name → honest refusal naming
   the pin and the MCU.
7. PCB — "Create a board; add nets VCC/GND/UART_TX with assigned pins;
   validate" → DRC checks green, VALIDATED.
8. PCB — gerber export of a validated board → F_Cu + Edge_Cuts artifacts,
   self-verification inside the export.
9. MIXED — firmware pin map on a board whose netlist differs by one pin →
   the record is **FAILED by C04, reason names the pin and the net** (the
   plan's done-when, graded by the report).
10. MIXED — a good multi-engine design (frame + board + firmware map) →
   record VERIFIED, report lists every check PASS.

---

## Settled decisions (in order of appearance)

1. **VERIFIED is conjunctional.** Every cross-check PASS + every engine check
   green. WARN never grants VERIFIED; WARN never fails the report.
2. **`cannot_verify` is a WARN, never a PASS.** A missing input is reported
   honestly with its name. This is the phantom-capability rule for verdicts.
3. **Every check carries `reason` + `location`.** A bare status is not a
   check result.
4. **The design record is Python-side and backend-agnostic**, assembled from
   whatever the engines actually answered; it persists as JSON files under
   `TRINITY_STORAGE_ROOT/projects/` (no DB schema migration until the data
   needs query semantics).
5. **HTML first for the report;** PDF only with a vendored generator, else
   the print path, stated as such.
6. **CSV first for the BOM;** XLSX only behind a real library verified by
   read-back.
7. **Models never grade.** Check statuses, reasons and locations are
   deterministic; model surfaces may propose and phrase, nothing more.
8. **Candidates, not auto-promotion** (Phase 5): machine-suggested fixes
   queue for human review; the reference library only admits reviewed
   designs that pass the verified report.

## Open questions (decide before the increment that hits them)

- Which enclosure/CAD fields the record builder needs beyond size —
  decided during M20.1, and if the CAD engine answer lacks a field the plan
  needs, the honest options are: extend the engine, or the check reports
  `cannot_verify`. Inventing a default is never an option.
- How connector positions/hole positions enter the PCB record (IR extension
  vs export artifact parse-back) — decided in M20.1 with the native IR
  owner's convention: extend the IR and its round-trip tests, never parse
  anything a structured answer already carries.
- Report route shape (`/api/projects/{id}/report` vs the engine op) —
  decided in M20.5 with route-test coverage like the other schema-edge
  rules.
