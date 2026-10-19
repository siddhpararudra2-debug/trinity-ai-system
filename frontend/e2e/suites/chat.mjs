import { goto, sleep } from '../lib.mjs';

/** Chat page: catalog-driven form mode, output checkboxes, STEP availability. */
export const name = 'chat';

export async function run({ cdp, check, WEB }) {
  await goto(cdp, `${WEB}/chat`);
  await sleep(1500);

  check(
    'chat renders',
    await cdp.eval(`document.body.innerText.includes('Trinity')`),
  );

  // --- prompt mode is the default
  const tabs = await cdp.eval(`
    [...document.querySelectorAll('.input-tab')].map(b => b.textContent.trim())
  `);
  check('prompt and form tabs present', Array.isArray(tabs) && tabs.length === 2, String(tabs));

  // --- switch to form mode
  await cdp.eval(`
    (() => {
      const b = [...document.querySelectorAll('.input-tab')]
        .find(x => x.textContent.trim().toLowerCase() === 'form');
      if (b) b.click();
      return true;
    })()
  `);
  await sleep(1200);

  const hasPartSelect = await cdp.eval(`
    [...document.querySelectorAll('select')]
      .some(s => [...s.options].some(o => o.value === 'quadcopter_frame'))
  `);
  check('form mode exposes the CAD catalog', Boolean(hasPartSelect));

  // --- outputs: STEP must reflect live catalog availability
  const outputs = await cdp.eval(`
    (() => {
      const fieldset = document.querySelector('.cad-outputs');
      if (!fieldset) return null;
      return [...fieldset.querySelectorAll('label')].map(l => ({
        text: l.innerText.replace(/\\s+/g, ' ').trim(),
        disabled: l.querySelector('input')?.disabled ?? false,
      }));
    })()
  `);
  check('output selector present', Array.isArray(outputs) && outputs.length >= 3, JSON.stringify(outputs));
  const step = (outputs ?? []).find((o) => /STEP/i.test(o.text));
  check('STEP is offered', Boolean(step), 'no STEP checkbox');
  if (step) {
    // Either enabled (kernel present) or explicitly disabled with a reason.
    check(
      'STEP state is deliberate',
      step.disabled ? /no kernel/i.test(step.text) : true,
      step.text,
    );
  }
}