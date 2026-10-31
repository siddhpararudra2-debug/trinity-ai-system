import { goto, sleep, waitUntil } from '../lib.mjs';

/**
 * DAG suite (M15): the pipeline panel renders truthful node outcomes, and
 * the PLAN affordance streams a request into a loaded, explicitly executed
 * graph. CI runs without a model endpoint, so the deterministic-router
 * branch — labeled verbatim in the UI — is the branch under test here; the
 * model branch and its delta frames are pinned by backend route tests.
 */
export const name = 'dag';

// Statements that resolve the panel; every eval wraps them in its own IIFE.
const ROOT = `
  const h3 = [...document.querySelectorAll('h3')].find(e => e.textContent.includes('DAG EXECUTION PIPELINE'));
  const el = h3 ? h3.closest('section') : null;
  if (!el) throw new Error('panel not found: DAG EXECUTION PIPELINE');
`;

export async function run({ cdp, check, WEB }) {
  await goto(cdp, WEB);
  await sleep(1200);

  const mounted = await waitUntil(
    async () => cdp.eval(`(() => { ${ROOT} return 1; })()`),
    Boolean,
    15000,
  );
  check('dag pipeline panel is mounted', Boolean(mounted));

  const nodeCount = async () =>
    cdp.eval(`(() => { ${ROOT} return el.querySelectorAll('ol > li').length; })()`);

  const before = Number(await nodeCount());
  check('default pipeline renders nodes', before >= 1);

  // ---- plan from a natural-language request --------------------------
  const filled = await cdp.eval(`(() => { ${ROOT}
    const input = el.querySelector('.dag-plan-input');
    if (!input) return false;
    const setter = Object.getOwnPropertyDescriptor(
      window.HTMLInputElement.prototype, 'value').set;
    setter.call(input, 'Solve x**2 - 4 = 0');
    input.dispatchEvent(new Event('input', { bubbles: true }));
    return true;
  })()`);
  check('plan input takes the request', Boolean(filled));

  // React flips the button's disabled state after the input event flushes;
  // poll for it instead of clicking in the same tick.
  const clickable = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT} return !el.querySelector('.dag-plan-btn').disabled; })()`),
    Boolean,
    5000,
  );
  check('plan button becomes clickable after the input flush', Boolean(clickable));

  await cdp.eval(`(() => { ${ROOT}
    el.querySelector('.dag-plan-btn').click();
    return 1;
  })()`);

  // No model endpoint in CI: the fallback plan must say so, verbatim.
  const status = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const st = el.querySelector('.dag-plan-status');
        return st ? st.innerText : '';
      })()`),
    (text) => /DETERMINISTIC ROUTER/i.test(text || ''),
    30000,
  );
  check(
    'plan source is labeled verbatim (no model attached)',
    /DETERMINISTIC ROUTER/i.test(status || ''),
  );

  // The plan replaced the graph with the routed call: one math node.
  const planned = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const items = [...el.querySelectorAll('ol > li')].map(li => li.innerText);
        return { count: items.length, text: items.join('|') };
      })()`),
    (state) => Number(state.count) === 1 && /math/i.test(state.text) && /solve/i.test(state.text),
    10000,
  );
  check(
    'the streamed plan loads as the pipeline graph',
    Boolean(planned) && planned.count === 1,
  );

  // ---- explicit execution of the loaded plan -------------------------
  const executed = await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => /EXECUTE PIPELINE/.test(b.textContent));
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  check('execute button is clickable with a loaded graph', Boolean(executed));

  const completed = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        return [...el.querySelectorAll('ol > li')].map(li => li.innerText).join('|');
      })()`),
    (text) => /COMPLETED/.test(text || ''),
    30000,
  );
  check('the planned node executes to a truthful COMPLETED badge', Boolean(completed));

  // ---- the demo affordance keeps working next to the plan output -----
  const beforeGrow = await cdp.eval(`(() => { ${ROOT}
    return el.querySelectorAll('ol > li').length;
  })()`);
  const clicked = await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => b.textContent.trim() === '+ NODE');
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  // React's re-render lands after the click turn — poll, never read in-step.
  const grew = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        return el.querySelectorAll('ol > li').length;
      })()`),
    (count) => Number(count) > Number(beforeGrow) && clicked,
    5000,
  );
  check('+ NODE appends a node', Boolean(grew));

  // One more run through the workflow endpoint with the grown graph: every
  // node must land COMPLETED — FAILED or BLOCKED rows are never acceptable.
  const grownCount = await cdp.eval(`(() => { ${ROOT}
    return el.querySelectorAll('ol > li').length;
  })()`);
  await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => /EXECUTE PIPELINE/.test(b.textContent));
    btn.click();
    return 1;
  })()`);
  const allDone = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        return {
          text: [...el.querySelectorAll('ol > li')].map(li => li.innerText).join('|'),
          count: el.querySelectorAll('ol > li').length,
        };
      })()`),
    (state) =>
      Number(state.count) === Number(grownCount) &&
      /COMPLETED/.test(state.text) &&
      !/FAILED|PENDING|BLOCKED/.test(state.text),
    30000,
  );
  check('a grown pipeline runs to completion', Boolean(allDone));

  await sleep(400);
}
