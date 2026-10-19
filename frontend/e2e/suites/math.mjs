import { goto, sleep, waitFor } from '../lib.mjs';

/**
 * Math, driven through the chat prompt.
 *
 * This is the only engine with no console of its own, so it is exercised the
 * way a user actually reaches it: by typing "solve ...". It is also the one
 * engine that was returning a fabricated answer. The router sent it to
 * math.solve, sympy solved it, and the UI -- which assumed every result was
 * CAD -- rendered "CAD part generated" and discarded the answer entirely.
 *
 * So the assertions here are deliberately about the absence of a lie as
 * much as the presence of a number.
 */
export const name = 'math';

const send = async (cdp, text) => {
  await cdp.eval(`(() => {
    const el = document.querySelector('.chat-input');
    const setter = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(el), 'value').set;
    setter.call(el, ${JSON.stringify(text)});
    el.dispatchEvent(new Event('input', { bubbles: true }));
    return true;
  })()`);
  await sleep(600);
  await cdp.eval(`(() => { document.querySelector('.chat-send-btn').click(); return true; })()`);
};

const botCount = (cdp) => cdp.eval(`document.querySelectorAll('.chat-message.bot').length`);

const lastBot = (cdp) =>
  cdp.eval(`(() => {
    const m = document.querySelectorAll('.chat-message.bot');
    return m[m.length - 1] ? m[m.length - 1].innerText : '';
  })()`);

export async function run({ cdp, check, WEB }) {
  await goto(cdp, `${WEB}/chat`);
  await sleep(1500);

  const before = await botCount(cdp);

  // --- x**2 - 4 = 0 has two roots, -2 and 2.
  await send(cdp, 'Solve x**2 - 4 = 0');

  let grew = false;
  for (let i = 0; i < 40 && !grew; i += 1) {
    grew = (await botCount(cdp)) > before;
    if (!grew) await sleep(300);
  }
  check('math prompt produces a reply', grew, 'no new bot message');

  const reply = await waitFor(
    async () => {
      const t = await lastBot(cdp);
      return /=/.test(t) && !/solving|thinking/i.test(t) ? t : null;
    },
    null,
    20000,
  ) ?? (await lastBot(cdp));

  check(
    'the solution is shown, not discarded',
    /x\s*=\s*-2\D+2/.test(reply),
    `reply was: ${JSON.stringify(reply)}`,
  );
  check(
    'no fabricated CAD part is claimed',
    !/CAD part/i.test(reply),
    `reply contained a CAD claim: ${JSON.stringify(reply)}`,
  );
  check(
    'the equation is echoed back',
    /x\*\*2\s*-\s*4\s*=\s*0/.test(reply),
    `reply was: ${JSON.stringify(reply)}`,
  );
  check(
    'the root count is stated',
    /2 solutions/i.test(reply),
    `reply was: ${JSON.stringify(reply)}`,
  );

  // --- the "understood as" panel must not label an equation a CAD part
  const understood = await cdp.eval(`(() => {
    const el = document.querySelector('.understood-panel');
    return el ? el.innerText : '';
  })()`);
  check(
    'the understood panel names the equation, not a part',
    Boolean(understood) && !/CAD Part/i.test(understood),
    `understood panel: ${JSON.stringify(understood)}`,
  );

  // --- a linear equation has exactly one root; -3 is the whole answer.
  const before2 = await botCount(cdp);
  await send(cdp, 'solve 2*x + 6 = 0');
  let grew2 = false;
  for (let i = 0; i < 40 && !grew2; i += 1) {
    grew2 = (await botCount(cdp)) > before2;
    if (!grew2) await sleep(300);
  }
  check('a second prompt is answered', grew2);

  const reply2 = await waitFor(
    async () => {
      const t = await lastBot(cdp);
      return /x\s*=/.test(t) ? t : null;
    },
    null,
    20000,
  ) ?? (await lastBot(cdp));

  check(
    'the linear root is correct',
    /x\s*=\s*-3/.test(reply2),
    `reply was: ${JSON.stringify(reply2)}`,
  );
  check(
    'a single root is called one solution',
    /1 solution\b/i.test(reply2) && !/2 solutions/i.test(reply2),
    `reply was: ${JSON.stringify(reply2)}`,
  );

  // --- nonsense must fail honestly rather than return a fabricated part
  const before3 = await botCount(cdp);
  await send(cdp, 'solve $$$ not an equation $$$');
  let grew3 = false;
  for (let i = 0; i < 30 && !grew3; i += 1) {
    grew3 = (await botCount(cdp)) > before3;
    if (!grew3) await sleep(300);
  }
  const reply3 = await lastBot(cdp);
  check('a malformed equation still gets a reply', grew3);
  check(
    'a malformed equation does not report a CAD part',
    !/CAD part/i.test(reply3),
    `reply was: ${JSON.stringify(reply3)}`,
  );
}