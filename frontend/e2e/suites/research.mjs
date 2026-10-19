import { panel, waitFor } from '../lib.mjs';

/** Research console: search over the native index. */
export const name = 'research';

export async function run({ cdp, check }) {
  const p = panel(cdp, /RESEARCH/i);
  await p.scrollIntoView();

  await p.fill('QUERY', 'indexes documents');
  await p.click('SEARCH');

  const text = await waitFor(p.text, 'HITS');
  check('search returns a hit count', Boolean(text) && /HITS?/.test(text));
  check(
    'result states the truth about an empty index',
    Boolean(text) && /Empty index/i.test(text),
    text ? 'no empty-index notice' : 'never rendered',
  );
  check('no error banner', !/could not complete|Refusing|unavailable/i.test(text ?? ''));
}