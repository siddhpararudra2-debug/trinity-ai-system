import { panel, waitFor, waitUntil } from '../lib.mjs';

/** Firmware: the native chain walked end to end, then a truthful build attempt. */
export const name = 'firmware';

export async function run({ cdp, check }) {
  const p = panel(cdp, /FIRMWARE/i);
  await p.scrollIntoView();

  await p.fill('PROJECT_NAME', 'e2e_rover');
  await p.select('ESP32', 'MCU');
  await p.click('RUN PIPELINE');

  // Wait on the validate step's own log line. "VALIDATED" is an always-present
  // Spec row label, so matching it returned before the pipeline had run.
  const done = await waitFor(p.text, 'all rules passed', 60000);
  check('pipeline reached validation', Boolean(done));
  for (const step of ['create', 'select_mcu', 'configure_peripheral', 'generate', 'validate']) {
    check(`step ran: ${step}`, Boolean(done) && done.includes(step));
  }
  check('ESP32 recorded', Boolean(done) && /ESP32/.test(done));
  check('generated sources listed', Boolean(done) && /main\.cpp/.test(done));

  // The step log lands before the summary row, so wait on the row itself
  // rather than racing it.
  const settled = await waitUntil(
    p.text,
    (t) => typeof t === 'string' && /VALIDATED\s*\n\s*(true|false)/i.test(t),
    15000,
  );
  check(
    'VALIDATED row reflects the real outcome, not a dash',
    Boolean(settled) && /VALIDATED\s*\n\s*true/i.test(settled),
    settled ? 'row did not settle on true' : 'row never rendered a verdict',
  );

  // Build either succeeds or refuses with the toolchain named. Both are
  // correct; silently claiming success would not be.
  await p.click('BUILD');
  // waitFor takes a substring, not a regex — matching needs waitUntil.
  const after = await waitUntil(
    p.text,
    (t) => typeof t === 'string' && /toolchain|executed|build/i.test(t),
    40000,
  );
  check('build answered', Boolean(after));
  check(
    'build refusal names the toolchain',
    Boolean(after) && /xtensa-esp32-elf-g\+\+|executed|arm-none-eabi|TOOLCHAIN/i.test(after),
    after ? 'no toolchain detail' : 'never answered',
  );
}