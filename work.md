### Stage 12 (M25–M29): make usable, then add the data layer

M25 backend gaps (2–3 days). [x] DONE
- `GET /api/projects/{id}/bom.csv` — export priced BOM as CSV; 404 if record missing, 409 if no BOM data
- `POST /api/projects/{id}/bom/rebuild` — rebuild BOM from PCB design fragment via save_record
- `POST /api/projects/from-design` — build a new design record from a request body via DesignRecord.from_dict()

M26 projects UI (1–1.5 wks) [x] DONE
- `/projects` list (unreadable records show the refusal)
- `/projects/view` tabs:
  - Report: badge, tally, check table (status, reason, location, hint)
  - Record: JSON textarea plus Save
  - History: snapshots, Restore (confirm), Compare
  - BOM: table, CSV download, Rebuild from PCB (confirm)
- E2E suite projects: seed good_esp32_i2c and break_c03_unknown_pin; assert VERIFIED vs FAILED, C03 at U1.GPIO39, fix-and-save flips status, restore, API-down error
- Done when: create, break, fix, VERIFIED, restore, download BOM, all in the UI

M27 reference library (~1 wk) [x] DONE
- Audit library.py against convention 9. Fixed tag vocabulary; rank by tag matches, then id; recompute verified on read
- Grow to ~10 designs (drift test, all-VERIFIED test)
- Routes: /api/reference, /match?tags=, POST /api/projects/from-reference (copy, never alias), /api/reference/candidates
- No promote route; CLI tools/reference/review.py
- UI: start from reference, submit as candidate

M28 India data (1–2 wks) [x] DONE
- Fix convention 9: parts get bom_value, bom_package, source (required with a price), price_note (required when null)
- Match (value, package) first; value alone only if exactly one part has it; ambiguous means blank
- Build strict src/engines/parts/catalog.py, parts.json (100-200 generic parts, prices null), fabs.json (supplied entries only)
- export_priced_bom_csv() (adds price_inr, price_updated_on)
- tools/parts/import_prices.py
- routes /api/parts, /api/fabs, /api/projects/{id}/bom-priced.csv
- Parts/Fabs pages

M29 close-out (2–3 days) [x] DONE
- Eval set to ~38 prompts (growth from 30). Update README, MILESTONES.md, work.md
- Run full e2e

---

### Stage 13 — Accounts, Billing, Trust Plans

M30 accounts (1.5 wks). [ pending ]
- Signup, verify, login, refresh, logout, reset, delete my account
- In-app login screen; signed-out use stays on the free tier
- Tests: lockout, expired tokens, replayed reset links

M31 trust pages (1 wk). [ pending ]
- Privacy, terms, refund and cancellation, design ownership
- Linked in the footer (About and Contact exist)
- Draft from the user's facts only; mark each "DRAFT - needs lawyer review"
- Record policy version and time at signup
- No training on user designs unless the user decides otherwise

M32 billing (1.5 wks). [ pending ]
- Plans from config
- Checkout, webhook with signature check, deduped by event id
- States: trialing, active, past_due, cancelled, expired
- Cancel and refund follow the refund page
- Tests from sample webhooks: duplicate, out-of-order, bad signature

M33 plans and limits (1 wk). [ pending ]
- Signed plan token, offline verify, grace
- Meter LLM calls per day, exports, saved designs
- Over a limit, show a plain message naming the plan and limit, never a silent failure
- The free tier keeps all Stage 12 features working

M34 business basics (1 wk). [ pending ]
- Error reports (opt-in, no design contents)
- Minimal events with a visible opt-out
- In-app feedback button (text plus app version) posting to the cloud, and a CLI to read it

M35 launch check (3-4 days). [ pending ]
- Staging run in test mode: signup, verify, subscribe, webhook, token, cancel, offline
- Backups, secrets, auth review, installer pointing at the production API
- Production keys only after the user confirms legal sign-off