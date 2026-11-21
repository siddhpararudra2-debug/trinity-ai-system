# GO LIVE — Remaining Items

Everything below is **verified evidence from the current build**, followed by
the items still open before a real production launch.

## Verified in this build (dummy mode, as of `f00786c`)

- **One prompt, whole pipeline** — `python tools/e2e_smoke.py` drives it over
  the public API: `POST /api/requirements/execute` (single-domain routing),
  `POST /api/workflows/execute` (math → CAD → PCB → firmware → PCB export →
  verification), `GET /api/projects/{id}/inspect`, and artifact downloads.
  Result: 18/18 artifacts downloadable (STL/GLB/JSON + 15 PCB
  gerber/drill files). The verification node truthfully reports UNVERIFIED —
  no datasheets exist in dummy data yet.
- **10 engines registered**: math, cad, pcb, firmware, verification, vision,
  research, simulation, robotics, crosscheck. On a machine without the
  native binary, pcb and firmware run as real Python fallbacks (no status
  label): pcb places, routes and checks a board; firmware generates a
  compilable C skeleton, validates pins/peripherals against an MCU
  database, and reports `COMPILED` only from a real toolchain run —
  otherwise `"static check only, not compiled"`.
- **Full pytest suite: 823 passed / 24 skipped** with the `[cad]` and
  `[vision]` extras installed (2 skip without live LLM API keys, 22
  without the native binary; they skip with reasons, never fake).
  Without those extras the same suite is 805 passed / 42 skipped. The
  six LLM transport contract tests require the declared `openai` and
  `anthropic` SDKs; with them absent the suite reports 6
  `ModuleNotFoundError` failures, so install `requirements.txt` first.
- **Eval baseline saved** at `data/eval/results.json`
  (`python -m tools.eval.run`, environment-conditional):
  **25 PASS / 0 FAIL / 13 SKIP of 38** with the STEP kernel installed;
  **24 PASS / 0 FAIL / 14 SKIP of 38** without it (the CI backend env).
  Every SKIP names why (native binary absent / fixture not in build).
- **`.env.dummy` drives behaviour with zero code edits** — auth toggle,
  plans, GST rates, trust-page copy, feedback endpoint, eval paths, part
  library paths are all read from env at startup.

## Real LLM
- The keyless local adapter reads `TRINITY_MODEL_ENDPOINT` (full
  OpenAI-compatible `chat/completions` URL) and `TRINITY_MODEL_NAME`
  (`LLM-LINK:` seam documented in `LLM_INTEGRATION.md`). With no
  endpoint configured it refuses truthfully instead of pretending a
  model is attached.
- Real LLM endpoint integration needed (OpenAI, Anthropic, etc.).
- Set `TRINITY_MODEL_ENDPOINT` / `TRINITY_MODEL_NAME` (and optionally
  `TRINITY_MODEL_API_KEY`) in the environment once provisioned. The
  `.env.dummy` keys `LLM_BASE_URL` / `LLM_API_KEY` are not read by the
  client today; `LLM_MODEL_NAME` is only a fallback model name.

## Datasheets
- Parts databases (India fabs, reference designs) use local JSON data files
  with no datasheet references yet — that is why the verification engine
  reports UNVERIFIED with reasons rather than a fake pass.
- Real manufacturer datasheets would replace `data/reference/library/` and
  `data/india_parts.json`; swap via `REF_DESIGN_LIBRARY_PATH` etc. in
  `.env.dummy`.

## Windows fixture run (native binary)
- pcb and firmware run as real Python fallbacks (no status label);
  their native-grade limits (real gerber content, real cross-compiles)
  are stated in each engine's module docstring.
- Building the native binary (`app/native/`, CMake/Qt) and running
  `TRINITY_REQUIRE_NATIVE=1 python -m tools.eval.run` (or the same
  command with `--require-native`) on Windows upgrades the
  native-dependent SKIPs to graded verdicts — 13 with the STEP kernel
  installed, 14 without it.

## Hosting
- `start.ps1` launches uvicorn with `--workers $TRINITY_WEB_CONCURRENCY`
  (default 1; production sets 2+ — safe because records and artifacts
  live in SQLite with WAL, so state is shared across workers, not
  per-process) plus `--proxy-headers --forwarded-allow-ips 127.0.0.1`
  for a TLS-terminating reverse proxy on the same host. The bind
  address is `-ApiHost` (default 127.0.0.1).
- Production checklist: `TRINITY_ENV=production` (startup refuses
  wildcard/empty `TRINITY_CORS_ORIGINS` — set explicit origins, as
  `.env.dummy` already does), TLS certificate at the reverse proxy,
  and real Razorpay keys after KYC. `.env.dummy` is the template;
  every value is swappable with no code edits.
- Verified: `TRINITY_WEB_CONCURRENCY=2` boots two uvicorn workers and
  `/api/health` answers on both.

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
