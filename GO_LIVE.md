# GO LIVE — Remaining Items

Everything below is **verified evidence from the current build**, followed by
the items still open before a real production launch.

## Verified in this build (dummy mode)

- **One prompt, whole pipeline** — `python tools/e2e_smoke.py` drives it over
  the public API: `POST /api/requirements/execute` (single-domain routing),
  `POST /api/workflows/execute` (math → CAD → PCB → firmware → PCB export →
  verification), `GET /api/projects/{id}/inspect`, and artifact downloads.
  Result: 18/18 artifacts downloadable (STL/GLB/JSON + 15 PCB
  gerber/drill files). The verification node truthfully reports UNVERIFIED —
  no datasheets exist in dummy data yet.
- **10 engines registered**: math, cad, pcb, firmware, verification, vision,
  research, simulation, robotics, crosscheck. On a machine without the
  native binary, pcb/firmware run as pure-Python fallbacks and honestly
  label themselves `status: "scaffolded"`.
- **Full pytest suite: 724 passed / 26 skipped** (the 26 tests are the
  native-binary-dependent ones; they skip with reasons, never fake).
- **Eval baseline saved** at `data/eval/results.json`
  (`python -m tools.eval.run`, environment-conditional):
  **25 PASS / 0 FAIL / 13 SKIP of 38** with the STEP kernel installed;
  **24 PASS / 0 FAIL / 14 SKIP of 38** without it (the CI backend env).
  Every SKIP names why (native binary absent / fixture not in build).
- **`.env.dummy` drives behaviour with zero code edits** — auth toggle,
  plans, GST rates, trust-page copy, feedback endpoint, eval paths, part
  library paths are all read from env at startup.

## Real LLM
- Currently using the keyless local adapter behind
  `LLM_BASE_URL=http://localhost:11434/v1/chat/completions`
  (`LLM-LINK:` seam documented in `LLM_INTEGRATION.md`).
- Real LLM endpoint integration needed (OpenAI, Anthropic, etc.)
- Swap `LLM_BASE_URL` and `LLM_MODEL_NAME` in `.env.dummy` once provisioned.

## Datasheets
- Parts databases (India fabs, reference designs) use local JSON data files
  with no datasheet references yet — that is why the verification engine
  reports UNVERIFIED with reasons rather than a fake pass.
- Real manufacturer datasheets would replace `data/reference/library/` and
  `data/india_parts.json`; swap via `REF_DESIGN_LIBRARY_PATH` etc. in
  `.env.dummy`.

## Windows fixture run (native binary)
- pcb/firmware currently run as Python fallbacks (`status: "scaffolded"`).
- Building the native binary (`app/native/`, CMake/Qt) and running
  `TRINITY_REQUIRE_NATIVE=1 python -m tools.eval.run` on Windows upgrades
  the 13 SKIPs to graded verdicts.

## Hosting
- Single-process uvicorn server (`src/main.py`) serves the API at
  `http://localhost:8000`. Production needs gunicorn/uvicorn workers, SSL,
  and `TRINITY_ENV=production` with explicit `TRINITY_CORS_ORIGINS`.

## Business details
- Company: Trinity AI Systems; domain: example.com; emails:
  admin@example.com / support@example.com — all in `.env.dummy`, swappable
  with no code edits.

## Razorpay KYC
- Test-mode keys only: `RAZORPAY_KEY_ID=rzp_test_dummy`,
  `RAZORPAY_KEY_SECRET=rzp_test_secret_dummy`. Plans Trial(₹0, 1 job) /
  Monthly(₹299, 5 jobs) / Annual(₹3299, 10 jobs) with 18% GST split as
  CGST+SGST or IGST. No real payments processed. KYC + live keys needed.

## Domain/email
- All email addresses and domain names come from `.env.dummy`
  (`TRINITY_ADMIN_EMAIL`, `TRINITY_SUPPORT_EMAIL`, `TRINITY_DOMAIN`).
  Real domain + mailbox provisioning still open.

## Lawyer review
- Trust pages display "DRAFT, not legally reviewed" banner. Content comes
  from `.env.dummy` (`TRINITY_ABOUT_TEXT`, `TRINITY_PRIVACY_POLICY`,
  `TRINITY_TERMS_OF_SERVICE`, `TRINITY_DESIGN_OWNERSHIP`). Legal review is
  still required before production.

## Known dummy-mode boundary (by design, not hidden)
- The mixed prompt "Solve ..., design ..., generate PCB ..., create firmware
  ..." routed to ONE domain honestly errors at that domain; multi-domain
  execution goes through the plan → `POST /api/workflows/execute` DAG path
  (see the smoke). A real LLM attached at the `LLM-LINK:` seam decomposes
  the prompt into that multi-node plan automatically.
