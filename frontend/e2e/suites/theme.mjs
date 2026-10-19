import { goto, waitUntil, sleep } from '../lib.mjs';

/** Theme toggle: the switch must actually flip the document theme. */
export const name = 'theme';

export async function run({ cdp, check, WEB }) {
  await goto(cdp, `${WEB}/`);
  await sleep(1200);

  const themeNow = () =>
    cdp.eval(`document.documentElement.getAttribute('data-theme')`);

  const before = await themeNow();
  check('theme attribute is set', before === 'light' || before === 'dark', String(before));

  await cdp.eval(`
    (() => {
      const b = document.querySelector('.theme-toggle');
      if (b) b.click();
      return true;
    })()
  `);

  const flipped = await waitUntil(
    themeNow,
    (t) => t !== before && (t === 'light' || t === 'dark'),
    6000,
  );
  check(
    'toggle flips the theme',
    flipped !== before && (flipped === 'light' || flipped === 'dark'),
    `${before} -> ${flipped}`,
  );

  // It must persist, or the toggle is a toy.
  await goto(cdp, `${WEB}/`);
  await sleep(1500);
  const persisted = await themeNow();
  check(
    'choice persists across navigation',
    persisted === flipped,
    `${flipped} -> ${persisted}`,
  );
}