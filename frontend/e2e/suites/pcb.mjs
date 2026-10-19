import { panel, waitFor, sleep } from '../lib.mjs';

/**
 * PCB: generate -> auto-net -> validate -> export KiCad.
 *
 * The last assertion is the regression this suite exists for: the badge and
 * download link used to be derived from the current result, so a later
 * VALIDATE erased them. It caught that on its first run.
 */
export const name = 'pcb';

export async function run({ cdp, check }) {
  const p = panel(cdp, /PCB/i);
  await p.scrollIntoView();

  await p.select('esp32', 'COMPONENTS');
  await sleep(300);

  await p.click('GENERATE');
  // Wait on the summary line, not the COMPONENTS field label, which is
  // always present in the form and matches before anything has run.
  const generated = await waitFor(p.text, '1 COMPONENTS');
  check('generate produces a board', Boolean(generated));
  check(
    'auto-netting wired the power pins',
    Boolean(generated) && /NET 3V3/.test(generated),
    generated ? (generated.match(/NET [^\n]*/g) ?? []).join(', ') : 'never rendered',
  );

  await p.click('VALIDATE');
  const validated = await waitFor(p.text, 'RULES PASSED');
  check('validate passes after auto-netting', Boolean(validated));

  await p.click('EXPORT KICAD');
  const exported = await waitFor(p.text, 'KICAD EXPORTED');
  check('export produces a KiCad file', Boolean(exported));
  check(
    'export badge shows a size',
    Boolean(exported) && /KICAD EXPORTED \(\d/.test(exported),
  );

  await p.click('VALIDATE');
  await sleep(2500);
  const after = await p.text();
  check('badge survives a later VALIDATE', /KICAD EXPORTED/.test(after));
  check('download link survives a later VALIDATE', /DOWNLOAD/.test(after));

  const href = await cdp.eval(`
    (() => {
      const h = [...document.querySelectorAll('h2')].find(e => /PCB/i.test(e.textContent));
      const a = [...h.closest('article').querySelectorAll('a')]
        .find(x => (x.getAttribute('href') || '').includes('/api/artifacts/'));
      return a ? a.getAttribute('href') : '';
    })()
  `);
  check('artifact link points at the API', Boolean(href) && href.includes('/api/artifacts/'), href);
}