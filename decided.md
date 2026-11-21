## Current status (as of `f00786c`, 2026-11-17)

Stage 12 (M25-M29), Stage 13 (M30-M33), Stage 14 (A1-A7, B1-B3), and
Stage 15 items 1-2 (real PCB and firmware fallbacks) are complete.
M34 is partially implemented (feedback route + analytics flag; error
reports, events and the feedback CLI are still open). M35 (launch
check) and Stage 15's remaining Fix-mode/embeddings work are open.

## Stage 12 (M25-M29) - Complete

### Data Layer Implementation Status

**All M25-M29 milestones completed:**

- **M25**: 3 API routes verified (`bom.csv` 404/409 handling, `bom/rebuild` via save_record, and the `from-design` create-record path). Note: the `POST /api/projects/from-design` route is **not registered** in the current build — the dangling decorator was removed (see `tests/test_route_table.py`); create records via `POST /api/projects/from-reference/{design_id}` or `PUT /api/projects/{id}`.
- **M26**: BOM CSV export and rebuild functional; status differentiation via GPIO39 C03 pin mapping verified. The `/designs` and `/designs/view` pages described here are **not present** in the current frontend; design records are API-only, and the `/projects` page is the artifacts & jobs view.
- **M27**: 10 library designs (grown from 3); 4 API routes (`reference`, `match?tags=`, `projects/from-reference`, `candidates/{id}`); convention 9 fields on all 162 parts; `lookup_by_value_package()` matching logic in `catalog.py`; `tools/reference/review.py` CLI for promote/demote/search created
- **M28**: 3 API routes (`/parts` — 162 parts, `/fabs` — 15 fabs, `/projects/{id}/bom-priced.csv` — priced BOM CSV); convention 9 fields on all 162 parts; `lookup_by_value_package()` matching logic; `tools/parts/import_prices.py` CLI. The Parts/Fabs frontend pages named here are not present in the current frontend; the data is served by the API and the `/projects` page reads the CAD catalog.
- **M29**: Eval set grown from 30→38 prompts (8 new added); `MILESTONES.md` created; `README.md` updated; `work.md` updated with M25-M29 milestones; 4 test assertion fixes applied

### Key Metrics (current, `f00786c`)
- **823 pytest tests pass** (24 skipped: 2 need live LLM API keys, 22 need the native binary) with the `[cad]` + `[vision]` extras; without them, 805 passed / 42 skipped.
- **0 test failures** in a full-extras environment; the six LLM transport contract tests need the declared `openai`/`anthropic` SDKs, else they fail with `ModuleNotFoundError`.
- **0 ruff errors** (`ruff check src tests`).

### API Routes Verified
- M25: `bom.csv`, `bom/rebuild` (the `from-design` route is not registered — see above)
- M27: `reference`, `match?tags=`, `projects/from-reference`, `candidates/{id}`
- M28: `/parts`, `/fabs`, `/projects/{id}/bom-priced.csv`

### Frontend TypeScript Status
The pre-existing TypeScript/module-resolution errors described in the original Stage 12 notes were fixed in `57feafc` and `4074c5a`; CI is green. A local `npm run build` still needs `npm ci` in `frontend/` first because no `node_modules` is committed. The `fabs_page.tsx` / `parts_page.tsx` pages mentioned here are not part of the current frontend.

### Stage 13 — Accounts, Billing, Trust Plans (done)

> Status: M30-M33 are complete. The `[NEEDS: ...]` notes below are the
> original pre-work requirements, not open items (passwords are scrypt,
> not argon2; no Windows Credential Manager dependency was added).

**M30 accounts (1.5 wks).** Signup, verify, login, refresh, logout, reset, delete my account. In-app login screen; signed-out use stays on the free tier. Tests: lockout, expired tokens, replayed reset links. [NEEDS: user email/domain, password hash config (argon2), Windows Credential Manager setup]

**M31 trust pages (1 wk).** Privacy, terms, refund and cancellation, design ownership, linked in the footer (About and Contact exist). Draft from the user's facts only and mark each "DRAFT - needs lawyer review" until the user removes it. Record policy version and time at signup. No training on user designs unless the user decides otherwise. [NEEDS: lawyer review, CA for GST invoicing, legal text templates]

**M32 billing (1.5 wks).** Plans from config. Checkout, webhook with signature check, deduped by event id. States: trialing, active, past_due, cancelled, expired. Cancel and refund follow the refund page. Tests from sample webhooks: duplicate, out-of-order, bad signature. [NEEDS: Razorpay account with KYC, plan names/prices in INR, config file]

**M33 plans and limits (1 wk).** Signed plan token, offline verify, grace. Meter LLM calls per day, exports, saved designs. Over a limit, show a plain message naming the plan and limit, never a silent failure. The free tier keeps all Stage 12 features working. [NEEDS: plan definitions with daily/export/saved design limits]

**M34 business basics (1 wk).** Error reports (opt-in, no design contents). Minimal events with a visible opt-out. In-app feedback button (text plus app version) posting to the cloud, and a CLI to read it. [NEEDS: domain, sender email address, opt-out mechanism config]

**M35 launch check (3-4 days).** Staging run in test mode: signup, verify, subscribe, webhook, token, cancel, offline. Backups, secrets, auth review, installer pointing at the production API. Production keys only after the user confirms legal sign-off. [NEEDS: production API endpoint, secrets env vars, legal sign-off]

### Stage 14 - Remaining Gaps (all closed)

> Status: A1-A7 and B1-B3 are complete (see `work.md` and
> `opencodeworks.md` for the landed commits and proof runs). The
> `[NEEDS: ...]` notes below are the original pre-work requirements,
> not open items.

**A1 XLSX BOM (2 days).** `GET /api/projects/{id}/bom.xlsx` via openpyxl, same columns as the CSV plus price columns when present. Second sheet: record id, snapshot id, report status. No formulas. Test: reopen, compare to CSV. [NEEDS: XLSX library, test data with price columns]

**A2 Enclosure lid (3-4 days).** Add a lid parameter set (`lid`: none or removable, thickness, fit clearance, screw posts yes/no). Output a separate lid part (STEP/STL). Update the board-fits-enclosure check to use inner height with the lid fitted. The skipped eval prompt must now pass. [NEEDS: lid design datasheets, enclosure fit measurements]

**A3 Skipped eval prompts (3 days).** Save engine outputs from a Windows run as fixtures. Without the native binary the runner replays them, labelled REPLAYED, never PASS. Add `--require-native` so a Windows run fails instead of skipping. [NEEDS: Windows run outputs, fixture data, --require-native flag] **__(CHOSEN SMALLER OPTION for Stage 14 first implementation)__**

**A4 CAD catalog audit (1 wk).** Write `docs/cad-catalog-audit.md`: per part, parameters, whether it builds at default/min/max, STEP export, test, and dimension source. Add a test that builds every part at default/min/max. Flag any dimension without a datasheet or standard as UNVERIFIED. Never correct dimensions from memory. [NEEDS: CAD part datasheets, build test infrastructure]

**A5 Literature RAG (2 wks).** Chunk the existing documents with source and page. Local embedding model by default (offline), named in config. Keep keyword search as fallback. Show only retrieved passages with source; below a score threshold, say "not found". Add ~15 retrieval evals. [NEEDS: embedding model choice, document chunking infrastructure, ~15 retrieval evals]

**A6 Router (1 wk, needs the LLM link-up).** One `ModelClient` interface over the existing clients. Model per task from config, fallback order, timeout, and a log of which model answered (no design content). The rule-based router stays first; tests assert zero model calls on matched requests, and cover timeout, error and fallback with fake clients. [NEEDS: LLM link-up from other developer, model config, fake client infrastructure]

**A7 Explain and Fix (2 wks, after A6).**
- **Explain:** read-only. Every claim cites a check id or record field from the report. Test that each cited id exists. [NEEDS: report data structure, citation validation]
- **Fix:** takes a FAILED report and returns a record patch. The server validates the patch with the normal loaders and shows a diff. Only after the user confirms does it save (a snapshot is made first), re-run Inspect, and show before and after. If a new FAIL appears, offer a one-click restore. Max 3 attempts. Tests: good patch, invalid patch refused, patch that makes things worse. [NEEDS: patch validation, snapshot infrastructure, UI buttons]
- **UI (not shipped):** "Explain" and "Propose fix" buttons on the Report tab were planned but are absent from the current frontend; the API endpoints above are what landed.

**M30 accounts (1.5 wks).** Signup, verify, login, refresh, logout, reset, delete my account. In-app login screen; signed-out use stays on the free tier. Tests: lockout, expired tokens, replayed reset links. [NEEDS: already complete from Stage 13]

**M31 trust pages (1 wk).** Privacy, terms, refund and cancellation, design ownership, linked in the footer (About and Contact exist). Draft from the user's facts only and mark each "DRAFT - needs lawyer review" until the user removes it. Record policy version and time at signup. No training on user designs unless the user decides otherwise. [NEEDS: already complete from Stage 13]

**M32 billing (1.5 wks).** Plans from config. Checkout, webhook with signature check, deduped by event id. States: trialing, active, past_due, cancelled, expired. Cancel and refund follow the refund page. Tests from sample webhooks: duplicate, out-of-order, bad signature. [NEEDS: already complete from Stage 13 plan definitions]

**M33 plans and limits (1 wk).** Signed plan token, offline verify, grace. Meter LLM calls per day, exports, saved designs. Over a limit, show a plain message naming the plan and limit, never a silent failure. The free tier keeps all Stage 12 features working. [NEEDS: already complete from Stage 13 plan definitions]

**M34 business basics (1 wk).** Error reports (opt-in, no design contents). Minimal events with a visible opt-out. In-app feedback button (text plus app version) posting to the cloud, and a CLI to read it. [NEEDS: domain, sender email address, opt-out mechanism config]

**M35 launch check (3-4 days).** Staging run in test mode: signup, verify, subscribe, webhook, token, cancel, offline. Backups, secrets, auth review, installer pointing at the production API. Production keys only after the user confirms legal sign-off. [NEEDS: production API endpoint, secrets env vars, legal sign-off]

Track B runs only if the user goes fully hosted.

One concern per commit. Tick `work.md`, log in `opencodeworks.md`.

### Objective Rule Compliance (current, `f00786c`)
- `ruff`: ✅ All checks passed (`src tests`), 0 errors.
- `pytest`: ✅ 823 passed / 24 skipped with the `[cad]` + `[vision]` extras (2 skips need live LLM keys, 22 need the native binary). Without the extras: 805 passed / 42 skipped. The six LLM transport tests need the declared `openai`/`anthropic` SDKs; with them absent they fail with `ModuleNotFoundError`.
- `tools/eval/run.py`: ✅ Processes all 38 prompts — 25 PASS / 0 FAIL / 13 SKIP with the STEP kernel; 24 PASS / 0 FAIL / 14 SKIP without it.
- `tsc`/`build`/e2e: ✅ The frontend TypeScript/lint failures described in earlier stages were fixed in `57feafc` and `4074c5a`; CI is green. A local `npm run build` still requires `npm ci` in `frontend/` first (no `node_modules` is committed).

The Trinity Stage 12 data layer (M25-M29) is **complete and verified**, and Stages 13-15 have since landed as described above.

---
**Stage 15** in progress; M34/M35 pending. One concern per commit. Tick `work.md`, log in `opencodeworks.md`.