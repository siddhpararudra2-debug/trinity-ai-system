import { goto, sleep, waitUntil } from '../lib.mjs';

/**
 * DAG suite (M15.2 plan affordance + M15.3 productized panel).
 *
 * The full product walkthrough in CI's no-model truth: stream a plan from
 * the deterministic router (labeled verbatim), load it as the graph,
 * EDIT the node into a real CAD call (engine roster from the live
 * registry), execute, follow the artifact link, append a node, thread a
 * value forward from the observed run, and hit the remove guard. The
 * model branch and its delta frames stay pinned by backend route tests.
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

  const liCount = async () =>
    cdp.eval(`(() => { ${ROOT} return el.querySelectorAll('ol > li').length; })()`);
  const liText = async () =>
    cdp.eval(`(() => { ${ROOT} return [...el.querySelectorAll('ol > li')].map(li => li.innerText.replace(/\\s+/g, ' ')).join('~'); })()`);

  const defaultCount = Number(await liCount());
  check('default pipeline renders nodes', defaultCount >= 1);

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

  await cdp.eval(`(() => { ${ROOT} el.querySelector('.dag-plan-btn').click(); return 1; })()`);

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

  // ---- M15.3: edit the planned node into a real CAD call -------------
  const editorOpen = await cdp.eval(`(() => { ${ROOT}
    const li = el.querySelector('ol > li');
    const btn = li.querySelector('.dag-edit-btn');
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  check('EDIT opens the node editor', Boolean(editorOpen));

  const rosterOk = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const engine = el.querySelector('.dag-editor .dag-engine-select');
        if (!engine) return null;
        return engine.options.length >= 2
          ? [...engine.options].map(o => o.value).join(',')
          : null;
      })()`),
    (names) => /math/.test(names || '') && /cad/.test(names || ''),
    8000,
  );
  check('engine select is rostered from the live registry (math + cad)', /math/.test(rosterOk) && /cad/.test(rosterOk));

  await cdp.eval(`(() => { ${ROOT}
    const engine = el.querySelector('.dag-editor .dag-engine-select');
    engine.value = 'cad';
    engine.dispatchEvent(new Event('change', { bubbles: true }));
    return 1;
  })()`);
  const opsOk = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const ops = el.querySelector('.dag-editor .dag-operation-select');
        return ops ? [...ops.options].map(o => o.value).join(',') : null;
      })()`),
    (list) => /generate/.test(list || ''),
    5000,
  );
  check('operation select follows the chosen engine (generate present)', /generate/.test(opsOk || ''));

  // Offering the op is not using it: the op select defaults to `solve`, so
  // the edit must pick `generate` explicitly — the first version of this
  // suite skipped that and the node stayed math · solve, cascading four
  // downstream failures.
  const opPick = await cdp.eval(`(() => { ${ROOT}
    const sel = el.querySelector('.dag-editor .dag-operation-select');
    if (!sel) return null;
    sel.value = 'generate';
    sel.dispatchEvent(new Event('change', { bubbles: true }));
    return sel.value;
  })()`);
  check('generate is selected as the operation', opPick === 'generate');

  const paramsSet = await cdp.eval(`(() => { ${ROOT}
    const ta = el.querySelector('.dag-editor .dag-params-input');
    const setter = Object.getOwnPropertyDescriptor(
      window.HTMLTextAreaElement.prototype, 'value').set;
    setter.call(ta, '{"type": "enclosure", "parameters": {"length": 40, "width": 30, "height": 20}, "outputs": ["stl"]}');
    ta.dispatchEvent(new Event('input', { bubbles: true }));
    return ta.value.includes('enclosure');
  })()`);
  check('parameters JSON is accepted (no parse error)', Boolean(paramsSet));

  const threadEmpty = await cdp.eval(`(() => { ${ROOT}
    return el.querySelector('.dag-editor .dag-thread-none')?.innerText ?? '';
  })()`);
  check(
    'thread section states the single-node truth',
    /no other nodes/i.test(threadEmpty || ''),
  );

  const saved = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const btn = el.querySelector('.dag-editor .dag-save-btn');
        if (!btn) return 'gone';
        return btn.disabled ? 'disabled' : 'ready';
      })()`),
    (s) => s === 'ready',
    5000,
  );
  if (saved === 'ready') {
    await cdp.eval(`(() => { ${ROOT} el.querySelector('.dag-editor .dag-save-btn').click(); return 1; })()`);
  }
  const editedRow = await waitUntil(
    async () => liText(),
    (text) => /cad/i.test(text || '') && /generate/i.test(text || ''),
    5000,
  );
  check('SAVE replaces the node in the graph (cad · generate)', /cad/i.test(editedRow) && /generate/i.test(editedRow));

  // ---- explicit execution of the edited plan -------------------------
  const executed = await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => /EXECUTE PIPELINE/.test(b.textContent));
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  check('execute button is clickable with a loaded graph', Boolean(executed));

  const completedWithArtifact = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const rows = [...el.querySelectorAll('ol > li')].map(li => li.innerText).join('|');
        const art = el.querySelector('ol > li .dag-artifact');
        return { rows, href: art ? art.getAttribute('href') : null, label: art?.innerText ?? null };
      })()`),
    (s) =>
      /COMPLETED/.test(s.rows) &&
      s.href && s.href.includes('/artifacts/') &&
      /stl/i.test(s.label || ''),
    30000,
  );
  check(
    'the edited node executes to COMPLETED with a real artifact link',
    Boolean(completedWithArtifact) &&
      /artifacts\//.test(completedWithArtifact.href || ''),
  );

  // ---- append + grow + truthful completion ---------------------------
  const beforeGrow = Number(await liCount());
  await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => b.textContent.trim() === '+ NODE');
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  // React's re-render lands after the click turn — poll, never read in-step.
  const grew = await waitUntil(
    async () => liCount(),
    (count) => Number(count) > beforeGrow,
    5000,
  );
  check('+ NODE appends a node', Boolean(grew) && Number(grew) > beforeGrow);

  await cdp.eval(`(() => { ${ROOT}
    const btn = [...el.querySelectorAll('button')].find(b => /EXECUTE PIPELINE/.test(b.textContent));
    btn.click();
    return 1;
  })()`);
  const grownCount = Number(await liCount());
  const allDone = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        return {
          text: [...el.querySelectorAll('ol > li')].map(li => li.innerText).join('|'),
          count: el.querySelectorAll('ol > li').length,
        };
      })()`),
    (state) =>
      Number(state.count) === grownCount &&
      /COMPLETED/.test(state.text) &&
      !/FAILED|PENDING|BLOCKED/.test(state.text),
    30000,
  );
  check('a grown pipeline runs to completion', Boolean(allDone));

  // ---- threaded-ref helper from the observed run ---------------------
  const threadPath = await cdp.eval(`(() => { ${ROOT}
    const lis = [...el.querySelectorAll('ol > li')];
    const li = lis.find(x => x.innerText.startsWith('n2'));
    const btn = li?.querySelector('.dag-edit-btn');
    if (!btn) return 'no-button';
    btn.click();
    return 'opened';
  })()`);
  const threadSeen = await waitUntil(
    async () =>
      cdp.eval(`(() => { ${ROOT}
        const chips = [...el.querySelectorAll('.dag-editor .dag-thread-path')].map(b => b.title);
        const btn = [...el.querySelectorAll('.dag-editor .dag-thread-btn')].map(b => b.title);
        return { chips, btn, joined: chips.join('|') + '//' + btn.join('|') };
      })()`),
    (s) => /\{node:n1\}\.result\./.test(s.joined) && /triangle_count/.test(s.joined),
    5000,
  );
  check(
    'thread helper offers the sibling and its observed real paths',
    /\{node:n1\}\.result\./.test((threadSeen || {}).joined || '') &&
      /triangle_count/.test((threadSeen || {}).joined || ''),
  );

  if (threadPath !== 'no-button') {
    // Guard the chip lookup: an absent path must fail the check honestly,
    // not crash the run on `undefined.click()`.
    const inserted = await cdp.eval(`(() => { ${ROOT}
      const chip = [...el.querySelectorAll('.dag-editor .dag-thread-path')].find(b => /triangle_count/.test(b.title));
      if (!chip) return 'no-chip';
      const ta = el.querySelector('.dag-editor .dag-params-input');
      ta.selectionStart = ta.value.length;
      ta.selectionEnd = ta.value.length;
      chip.click();
      return ta.value.includes('{node:n1}.result.triangle_count');
    })()`);
    check(
      'clicking an observed path inserts the ref at the caret',
      inserted === true,
      inserted === 'no-chip' ? 'no triangle_count chip — the completed run did not expose that path' : 'insert failed',
    );
    await cdp.eval(`(() => { ${ROOT}
      el.querySelector('.dag-editor .dag-cancel-btn').click();
      return 1;
    })()`);
  } else {
    check('clicking an observed path inserts the ref at the caret', false, 'editor never opened for n2');
  }

  // ---- removal guard --------------------------------------------------
  await cdp.eval(`(() => { ${ROOT}
    const li = el.querySelector('ol > li');
    li.querySelector('.dag-remove-btn').click();
    return 1;
  })()`);
  const guarded = await cdp.eval(`(() => { ${ROOT}
    return el.querySelector('.dag-remove-error')?.innerText ?? '';
  })()`);
  check(
    'removing a depended-on node is refused, naming the dependent',
    /required by.*n2/i.test(guarded || ''),
  );

  await cdp.eval(`(() => { ${ROOT}
    const lis = [...el.querySelectorAll('ol > li')];
    const li = lis.find(x => x.innerText.startsWith('n2'));
    li.querySelector('.dag-remove-btn').click();
    return 1;
  })()`);
  const shrank = await waitUntil(
    async () => liCount(),
    (count) => Number(count) === 1,
    5000,
  );
  check('removing the leaf node shrinks back to the single edited node', Number(shrank) === 1);

  await sleep(300);
}
