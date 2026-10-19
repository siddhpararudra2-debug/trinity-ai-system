/**
 * E2E runner. Executes every suite in e2e/suites against a running stack.
 *
 * The UI had no automated coverage at all before this, which is how a badge
 * that lied about whether an exported file existed survived three commits.
 * These suites drive the real buttons in a real browser.
 *
 * Prerequisites, started by the caller:
 *   API  TRINITY_NATIVE_EXEC=<Trinity.exe> python -m uvicorn src.main:app --port 8000
 *   WEB  NEXT_PUBLIC_TRINITY_API_URL=http://127.0.0.1:8000/api npm start -- -p 3000
 *   EDGE launched with --headless=new --remote-debugging-port=9222
 *
 * Usage:  node e2e/run.mjs            # all suites
 *         node e2e/run.mjs pcb vision # selected suites
 */

import { readdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { openBrowser, makeChecker, sleep } from './lib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const WEB = process.env.E2E_WEB_URL ?? 'http://127.0.0.1:3000';

const available = readdirSync(join(HERE, 'suites'))
  .filter((f) => f.endsWith('.mjs'))
  .sort();

const wanted = process.argv.slice(2);
const suites = wanted.length
  ? available.filter((f) => wanted.some((w) => f.startsWith(w)))
  : available;

if (suites.length === 0) {
  console.error('no suites matched. available:', available.join(', '));
  process.exit(1);
}

const { check, state } = makeChecker();
const results = [];

for (const file of suites) {
  const suite = await import(`./suites/${file}`);
  console.log(`\n=== ${suite.name} ===`);
  const started = Date.now();
  try {
    // Each suite starts from the workspace so panel state is predictable.
    const cdp = await openBrowser();
    const { goto } = await import('./lib.mjs');
    await goto(cdp, WEB);
    await suite.run({ cdp, check, WEB });
    cdp.close();
  } catch (err) {
    check(`${suite.name} suite crashed`, false, err.message);
  }
  results.push({ name: suite.name, seconds: ((Date.now() - started) / 1000).toFixed(1) });
  await sleep(500);
}

console.log('\n--- summary ---');
for (const r of results) console.log(`  ${r.name.padEnd(12)} ${r.seconds}s`);
console.log(`\n  ${state.passes} passed, ${state.failures.length} failed`);
if (state.failures.length) {
  console.log('  failed:');
  for (const f of state.failures) console.log(`    - ${f}`);
}
process.exit(state.failures.length ? 1 : 0);