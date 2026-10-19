import { panel, waitFor } from '../lib.mjs';

/** Simulation console: deterministic linear motion with CSV + JSON artifacts. */
export const name = 'simulation';

export async function run({ cdp, check }) {
  const p = panel(cdp, /SIMULATION/i);
  await p.scrollIntoView();

  await p.fill('DURATION_S', '1.0');
  await p.click('SIMULATE');

  const text = await waitFor(p.text, 'METHOD');
  check('result reports the integrator', Boolean(text) && /closed_form|semi_implicit/.test(text));
  check('result reports step count', Boolean(text) && /STEPS/.test(text));
  check(
    'final position is shown',
    Boolean(text) && /FINAL_X_M/.test(text),
    text ? 'no FINAL_X_M row' : 'never rendered',
  );
  check(
    'artifacts are downloadable',
    Boolean(text) && /CSV/iu.test(text) && /JSON/iu.test(text),
  );
}