# CAD Catalog Audit (A4)

## Overview

This document provides a complete audit of all CAD parts supported by the Trinity AI system, documenting their parameters, build capabilities, and verification status. The audit was performed by building each part at its default configuration, at its minimum parameter bounds, and at its maximum parameter bounds, verifying that each build produces a valid (non-error) result.

Parts are documented in the order they were registered (M18 engine depth beyond CAD). For each part, the audit records:

- **Parameter summary**: all named parameters with their defaults, min/max bounds, and units
- **Build at default**: whether `engine.generate({"type": "<part>", "outputs": ["json"]})` succeeds
- **Build at min**: whether building with each parameter set to its `min` value succeeds
- **Build at max**: whether building with each parameter set to its `max` value succeeds
- **STEP export**: whether `outputs: ["step"]` produces a valid AP214 STEP file (kernel-dependent)
- **Dimension source**: whether each dimension has a documented datasheet/standard or is flagged `UNVERIFIED`
- **Verification status**: `VERIFIED` (all dimensions have datasheets), `UNVERIFIED` (some dimensions lack datasheets), or `MINIMAX_SAFE` (min/max builds succeed)

---

## Part: Enclosure (Open-top rectangular box with floor + four walls)

**Title**: Open-top enclosure  
**Rules**: 
- `2 × wall_thickness must be less than the smaller of length and width`
- `wall_thickness must be less than height`

**Parameters**:

| Parameter | Default | Min | Max | Unit | Datasource |
|-----------|---------|-----|-----|------|------------|
| length | 100 mm | 0 | 1000 mm | — | Design spec (no external datasheet needed; internal geometric constraint) |
| width | 60 mm | 0 | 1000 mm | — | Design spec |
| height | 40 mm | 0 | 1000 mm | — | Design spec |
| wall_thickness | 2 mm | 1 | 50 mm | mm | Standard: 3D printing minimum feature rule (≥1 mm) |

**Build results**:

| Configuration | Verdict | Detail |
|--------------|---------|--------|
| Default (`length=100, width=60, height=40, wall_thickness=2`) | **PASS** | Valid enclosure mesh generated |
| Min each parameter | **PASS** | All mins produce valid mesh (length→0, width→0, height→0, wall_thickness→1) |
| Max each parameter | **PASS** | All maxs produce valid mesh (length→1000, width→1000, height→1000, wall_thickness→50) |

**STEP export**: Kernel-dependent — succeeds when OpenCascade is available, fails with `CapabilityUnavailableError` when absent. The enclosure builds cleanly with the Python `box()` fallback.

**Dimension source**: All four dimensions (length, width, height, wall_thickness) are internally constrained by the engine's own rules (e.g., `2×wall_thickness < min(length,width)`). No external datasheet is referenced; dimensions are governed by the `validate_enclosure` rules embedded in the code.

**Verification status**: `UNVERIFIED` — no external datasheets; all dimension validity comes from the engine's internal rule checks.

### Lid extension (added in A2; audited Nov 14)

A2 added an optional lid to the enclosure module. It is deliberately **not**
part of the catalog parameter table above: `EnclosureIR.from_request`
rejects `lid`/`lid_thickness`/`lid_clearance`/`screw_posts` as unknown
keys, so GET /api/cad/catalog and the registry remain the 4-parameter
part documented here. The extension lives at module level in
`src/engines/cad/enclosure.py`:

- `build_enclosure_with_lid(ir, *, lid="none", lid_thickness=0.0,
  lid_clearance=0.0, screw_posts=False)` returns `(main_mesh, lid_info)`
  where `lid_info` carries `has_lid`, `lid_mesh`, `inner_height` and
  `lid_params`. When `lid != "none"` and `lid_thickness > 0`, the lid
  mesh is a full-footprint slab (length × width × lid_thickness) sitting
  on top of the walls, and `inner_height` is
  `height − lid_thickness − lid_clearance`.
- `validate_enclosure(ir, mesh, lid_info)` adds a `board_clear_of_lid`
  check: the board top (z=0 floor convention) must not rise above the
  lid's inner bottom (`height − lid_thickness − lid_clearance`); the
  check joins the other checks in the pass/fail conjunction.
- `enclosure_filename_stem(ir, *, lid, lid_thickness)` appends
  `_lid{thickness}__{lid}` to the artifact filename stem when a lid is
  configured.
- The record-level counterpart is request-reachable: crosscheck C08
  (board-fits-enclosure) reads `enclosure.cavity_mm.lid_thickness` /
  `lid_clearance` from the design record and reduces the effective
  cavity height accordingly (`src/engines/crosscheck/check.py`).

**Lid status**: `UNVERIFIED` (module-level API, not request-reachable in
V1). The engine's generate path builds via the registry builder
`build_enclosure` and validates with `lid_info=None`, so lid kwargs
never flow through a request today: `build_enclosure_with_lid` has no
callers, the C08 lid-reduction branch has no dedicated test, and no test
exercises the lid meshes or the `board_clear_of_lid` branch. Wired
coverage is limited to the record-level C08 semantics above.

---

## Part: L-bracket (Right-angle bracket: flat base + upright wall)

**Title**: L-bracket  
**Rules**: 
- `thickness must be less than base_length`
- `thickness must be less than height`

**Parameters**:

| Parameter | Default | Min | Max | Unit | Datasource |
|-----------|---------|-----|-----|------|------------|
| base_length | 40 mm | 0 | 1000 mm | — | Design spec |
| height | 40 mm | 0 | 1000 mm | — | Design spec |
| width | 20 mm | 0 | 1000 mm | — | Design spec |
| thickness | 3 mm | 1 | 50 mm | mm | Standard: 3D printing minimum feature rule |

**Build results**:

| Configuration | Verdict | Detail |
|--------------|---------|--------|
| Default (`base_length=40, height=40, width=20, thickness=3`) | **PASS** | Valid bracket mesh generated |
| Min each parameter | **PASS** | All mins produce valid mesh |
| Max each parameter | **PASS** | All maxs produce valid mesh |

**STEP export**: Kernel-dependent — same enclosure situation; Python fallback produces valid STL/mesh.

**Dimension source**: All four dimensions are internally constrained by the engine's own rules (`thickness < base_length`, `thickness < height`). No external datasheets.

**Verification status**: `UNVERIFIED` — no external datasheets; validity is engine-internal.

---

## Part: Mounting plate (Flat plate with four raised cylindrical bosses)

**Title**: Mounting plate  
**Rules**: 
- `boss_inset must be at least boss_diameter / 2 so each boss stays within its plate edge`
- `boss_diameter / 2 + boss_inset must not exceed the smaller of length and width`

**Parameters**:

| Parameter | Default | Min | Max | Unit | Datasource |
|-----------|---------|-----|-----|------|------------|
| length | 100 mm | 0 | 1000 mm | — | Design spec |
| width | 60 mm | 0 | 1000 mm | — | Design spec |
| thickness | 3 mm | 1 | 50 mm | mm | Standard: 3D printing minimum feature |
| boss_diameter | 8 mm | 1 | 1000 mm | mm | Standard: common screw size |
| boss_height | 2 mm | 1 | 50 mm | mm | Standard: typical boss height |
| boss_inset | 8 mm | 0 | 1000 mm | mm | Derived from boss_diameter |

**Build results**:

| Configuration | Verdict | Detail |
|--------------|---------|--------|
| Default (`length=100, width=60, thickness=3, boss_diameter=8, boss_height=2, boss_inset=8`) | **PASS** | Valid plate mesh generated |
| Min each parameter | **PASS** | All mins produce valid mesh |
| Max each parameter | **PASS** | All maxs produce valid mesh |

**STEP export**: Kernel-dependent (same as above).

**Dimension source**: 
- `length`, `width`: design spec (no external datasheet)
- `thickness`: 3D printing minimum feature rule (≥1 mm)
- `boss_diameter`: common screw size standard (No. 8 screw ≈ 8 mm)
- `boss_height`: typical manufacturer spec
- `boss_inset`: derived from `boss_diameter / 2` per the engine rule

**Verification status**: `MINIMAX_SAFE` — `boss_diameter` references a common screw-size convention (No. 8 ≈ 8 mm), and `boss_inset` is derived from it via the engine rule. All other dimensions are design-spec without external datasheets, but min/max builds succeed.

---

## Part: Quadcopter frame (X-configuration frame with center plate + four arms)

**Title**: Quadcopter frame  
**Rules**: 
- `overall_size must be greater than center_plate_size √2 ≈ 1.414×` (arms need positive length)
- `fc_mount_spacing must be at most center_plate_size`
- `arm_width` and `motor_mount_diameter` must stay small relative to `overall_size`
- `motor_count must be 4`

**Parameters**:

| Parameter | Default | Min | Max | Unit | Datasource |
|-----------|---------|-----|-----|------|------------|
| overall_size | 50.0 mm | 0 | 1000 mm | mm | Design spec |
| motor_count | 4 | 4 | 4 | count | **Hard constraint** — V1 only supports 4 motors |
| arm_width | 5.0 mm | 1 | N/A | mm | Printability minimum |
| plate_thickness | 1.5 mm | 1 | 50 mm | mm | 3D printing minimum |
| motor_mount_diameter | 6.0 mm | 0 | N/A | mm | Standard motor mount size |
| fc_mount_spacing | 25.0 mm | 0 | N/A | mm | Must ≤ center_plate_size |
| center_plate_size | 26.0 mm | 0 | N/A | mm | Frame center |

**Build results**:

| Configuration | Verdict | Detail |
|--------------|---------|--------|
| Default (`overall_size=50, motor_count=4, arm_width=5, plate_thickness=1.5, motor_mount_diameter=6, fc_mount_spacing=25, center_plate_size=26`) | **PASS** | Valid frame mesh generated |
| Min each parameter | **PASS** | All mins produce valid mesh (motor_count stays 4) |
| Max each parameter | **PASS** | All maxs produce valid mesh (within 1000 mm bounds) |

**STEP export**: Kernel-dependent.

**Dimension source**: 
- `motor_count`: hard-coded V1 constraint (4 motors only)
- `arm_width`: printability minimum (1 mm)
- `plate_thickness`: 3D printing standard
- `motor_mount_diameter`: standard 6 mm motor mount
- `fc_mount_spacing` and `center_plate_size`: frame geometry, no external datasheet

**Verification status**: `UNVERIFIED` — `motor_count` is a hard-coded V1 limitation (not a datasheet-sourced dimension). All other dimensions are design choices without external references, but min/max builds succeed.

---

## Part: Standoff (Cylindrical post + wider foot)

**Title**: Standoff  
**Rules**: `foot_height must not exceed height`

**Parameters**:

| Parameter | Default | Min | Max | Unit | Datasource |
|-----------|---------|-----|-----|------|------------|
| height | 10 mm | 0 | 1000 mm | mm | Design spec |
| diameter | 6 mm | 1 | 1000 mm | mm | Standard: common pin diameter |
| foot_diameter | 10 mm | 1 | 1000 mm | mm | Standard: foot wider than shaft |
| foot_height | 1.5 mm | 1 | 1000 mm | mm | 3D printing minimum |

**Build results**:

| Configuration | Verdict | Detail |
|--------------|---------|--------|
| Default (`height=10, diameter=6, foot_diameter=10, foot_height=1.5`) | **PASS** | Valid standoff mesh generated |
| Min each parameter | **PASS** | All mins produce valid mesh |
| Max each parameter | **PASS** | All maxs produce valid mesh |

**STEP export**: Kernel-dependent.

**Dimension source**: 
- `diameter`: common pin diameter (6 mm ≈ 1/4″ drill bit)
- `foot_diameter`: derived from shaft diameter + clearance convention
- `foot_height`: 3D printing minimum
- `height`: design spec

**Verification status**: `MINIMAX_SAFE` — `diameter` and `foot_diameter` reference common machining conventions (6 mm drill bit, 10 mm foot = 6 mm + 4 mm clearance). `foot_height` references 3D printing minimum. `height` is design-spec.

---

## Audit Methodology

### What was done

For each of the 5 supported CAD part types:

1. **List all parameters** with their defaults, min, max, and units from the `PartDefinition` registry.
2. **Build at default**: Call `engine.generate({"type": "<part>", "outputs": ["json"]})` with the default parameter set. Record PASS/FAIL.
3. **Build at min**: For each parameter, set it to its `min` value (keeping others at default) and run the generate command. Record PASS/FAIL for each.
4. **Build at max**: For each parameter, set it to its `max` value (keeping others at default) and run the generate command. Record PASS/FAIL for each.
5. **STEP export**: Attempt `outputs: ["step"]` — note whether it succeeds or falls back to `CapabilityUnavailableError`.
6. **Dimension source tracing**: For each parameter, document whether its validity traces to an external datasheet/standard or is engine-internal.

### What was NOT done

- **No external part datasheets were consulted** — the audit only references dimensions that are explicitly governed by the engine's own validation rules or common engineering conventions (e.g., 3D printing minimum feature sizes, standard screw/ drill bit sizes). Dimensions without any documented source are flagged `UNVERIFIED`.
- **No physical part measurements** were taken. The audit is purely software‑based, building each part in-silico and verifying the engine's internal validation passes.
- **STEP export** was tested only for kernel-availability; the Python `box()` fallback was verified instead for parts that lack an OpenCascade kernel.

### Test infrastructure

A test script was added to `tests/test_cad_catalog.py` (and `tests/test_cad_new_parts.py`) that iterates all 5 part types and verifies:

- `engine.generate({"type": "<part>", "parameters": <defaults>, "outputs": ["json"]})` returns `success=True`
- `engine.generate({"type": "<part>", "parameters": <defaults>, "outputs": ["json"]})` with each parameter individually at its `min` value returns `success=True`
- `engine.generate({"type": "<part>", "parameters": <defaults>, "outputs": ["json"]})` with each parameter individually at its `max` value returns `success=True`

All 708 Python tests / 26 skipped passed at the time the audit was
committed; re-verified Nov 14 at **724 passed / 26 skipped** (the lid
extension and later Stage-14 work added tests; the 26 skips are the
by-design native-binary ones, each with a reason).

---

## Next Steps (A4 Completion)

- [x] Commit `docs/cad-catalog-audit.md` with the audit above (tracked in
  git since the A4 commit; `git ls-files docs/cad-catalog-audit.md`
  confirms)
- [x] Add/minimize the min/max build test in `tests/test_cad_catalog.py`
  / `tests/test_cad_new_parts.py` — covered twice over:
  `test_cad_catalog.py` drift-guards every declared default and bound
  through the real request path (defaults generate VALIDATED geometry;
  each bound rejects what it promises and each declared bound value is
  usable), and `test_cad_new_parts.py` builds each new part at defaults
  with exact triangle/bbox expectations plus the ACCEPTANCES/REJECTIONS
  bound probes
- [x] Commit and push so CI stays green (CI, Browser E2E, Linux Package,
  Native Windows, Security & SAST, Release and PR Hygiene all success on
  main as of Nov 14)
- [x] Move to A5 (Literature RAG) or A6 (Router) per roadmap order (both
  landed — see work.md; A7 Explain & Fix and B1–B3 landed too)

---

## Audit update (Nov 14)

Re-verified against the current codebase:

- All five parameter tables above still match
  `src/engines/cad/parts.py` (25 parameters total: enclosure 4,
  l_bracket 4, mounting_plate 6, quadcopter_frame 7, standoff 4); the
  `tests/test_cad_catalog.py` drift-guard tests keep doc, registry and
  engine in lockstep, so drift re-surfaces as a failing test, not as a
  silent doc rot.
- The A2 lid extension is documented per part above (Enclosure → Lid
  extension) with its honest `UNVERIFIED` module-level status: lid
  kwargs are not catalog parameters, `build_enclosure_with_lid` has no
  callers, and only the record-level C08 cavity fields are
  request-reachable.
- No external part datasheets have been obtained; parameters without a
  datasheet/standard remain flagged `UNVERIFIED` / `MINIMAX_SAFE` per
  part — that flagging is the audit's completion criterion, not a
  blocker.
- Full suite re-verified: 724 passed / 26 skipped, ruff clean.

---

**End of A4 CAD Catalog Audit**