import { goto, sleep } from '../lib.mjs';

/**
 * The engine registry as the UI presents it.
 *
 * This is the one place in the app whose entire job is stating what the
 * running system can do, and it carried three separate false claims:
 * a hardcoded fallback table marking pcb and vision as scaffolds, a guess
 * that painted six working bridged engines as SCAFFOLD until the API
 * answered (permanently, since the fetch had no rejection path), and a
 * "· 501" suffix printed next to every bridged engine -- which is the one
 * status that does not mean unimplemented.
 */
export const name = 'registry';

export async function run({ cdp, check, WEB }) {
  // --- /systems strip
  await goto(cdp, `${WEB}/systems`);
  await sleep(3500);

  const strip = await cdp.eval(`(() => {
    const el = document.querySelector('.engine-strip-grid');
    return el ? el.innerText : null;
  })()`);

  check('the engine strip renders', Boolean(strip), 'no .engine-strip-grid');
  check(
    'bridged engines are not labelled 501',
    Boolean(strip) && !/BRIDGED · 501/.test(strip),
    strip ? 'a bridged engine was marked 501' : 'nothing rendered',
  );
  check(
    'working engines show real capabilities',
    Boolean(strip) && /PCB/.test(strip) && /VALIDATE/.test(strip),
    strip ? 'no pcb capabilities in the strip' : 'nothing rendered',
  );
  check(
    'the removed phantom capabilities do not reappear',
    Boolean(strip) && !/OCR|GEOMETRY|DOCUMENT_PARSE|\bINSPECT · 501/.test(strip),
    strip ? 'a phantom capability is visible again' : 'nothing rendered',
  );

  // --- /services grid
  await goto(cdp, `${WEB}/services`);
  await sleep(3500);

  const cards = await cdp.eval(`[...document.querySelectorAll('.ws-panel')]
    .map(p => {
      const h2 = p.querySelector('h2');
      if (!h2) return null;
      return {
        head: h2.innerText.replace(/\\s+/g, ' ').trim(),
        pills: [...p.querySelectorAll('.status-pill')].map(s => s.innerText.trim()),
      };
    })
    .filter(Boolean)`);

  check('the engine grid renders cards', Array.isArray(cards) && cards.length >= 6, JSON.stringify(cards?.length));

  const byName = (n) => (cards ?? []).find((c) => c.head.toUpperCase().startsWith(n));

  const pcb = byName('PCB');
  check('pcb is shown as bridged, not scaffolded', Boolean(pcb) && pcb.pills.includes('BRIDGED'), JSON.stringify(pcb?.pills));
  check(
    'pcb advertises only its four real ops',
    Boolean(pcb) && !pcb.pills.some((p) => /INSPECT/.test(p)),
    JSON.stringify(pcb?.pills),
  );

  const vision = byName('VISION');
  check(
    'vision advertises only its five real ops',
    Boolean(vision) && !vision.pills.some((p) => /OCR|GEOMETRY|DOCUMENT/.test(p)),
    JSON.stringify(vision?.pills),
  );

  const firmware = byName('FIRMWARE');
  check(
    'firmware advertises no unreachable ops',
    Boolean(firmware) && !firmware.pills.some((p) => /TEST|COMPILE/.test(p)),
    JSON.stringify(firmware?.pills),
  );

  // Every bridged engine must show capabilities that came from the API.
  // An empty chip row means the grid fell back to its own guesses.
  const emptyCaps = (cards ?? []).filter(
    (c) => c.pills.includes('BRIDGED') && c.pills.length < 2,
  );
  check(
    'no bridged engine renders an empty capability list',
    emptyCaps.length === 0,
    emptyCaps.map((c) => c.head).join(', '),
  );
}