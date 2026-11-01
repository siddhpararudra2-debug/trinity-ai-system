import { goto, sleep, waitUntil } from '../lib.mjs';

/**
 * Agent suite (M15.4 agent surface).
 *
 * CI's no-model truth: the workbench offers the two real roles, the run
 * button only enables after a real prompt, and RUNNING against a modelless
 * API renders the backend's `capability_unavailable` refusal verbatim —
 * the attach env var named, the role named, and never a synthetic step
 * list or summary. The model branch itself is pinned backend-side by the
 * scripted-fake loop tests (CI never attaches a model).
 */
export const name = 'agent';

// Statements that resolve the panel; every eval wraps them in its own IIFE.
const ROOT = `
  const h3 = [...document.querySelectorAll('h3')].find(e => e.textContent.includes('AGENT WORKBENCH'));
  const el = h3 ? h3.closest('section') : null;
  if (!el) throw new Error('panel not found: AGENT WORKBENCH');
`;

export async function run({ cdp, check, WEB }) {
  await goto(cdp, WEB);
  await sleep(1200);

  const mounted = await waitUntil(
    async () => cdp.eval(`(() => { ${ROOT} return 1; })()`),
    Boolean,
    15000,
  );
  check('agent workbench is mounted', Boolean(mounted));

  // ---- roles are the route contract, exactly --------------------------
  const roles = await cdp.eval(`(() => { ${ROOT}
    const sel = el.querySelector('.agent-role-select');
    return sel ? [...sel.options].map(o => o.value) : null;
  })()`);
  check(
    'role select offers exactly coder and researcher',
    Array.isArray(roles) && roles.length === 2 &&
      roles.includes('coder') && roles.includes('researcher'),
  );

  // ---- the run button cannot lie about readiness ---------------------
  const disabledEmpty = await cdp.eval(`(() => { ${ROOT}
    return el.querySelector('.agent-run-btn').disabled;
  })()`);
  check('run button is disabled while the prompt is empty', Boolean(disabledEmpty));

  const filled = await cdp.eval(`(() => { ${ROOT}
    const ta = el.querySelector('.agent-prompt-input');
    if (!ta) return false;
    const setter = Object.getOwnPropertyDescriptor(
      window.HTMLTextAreaElement.prototype, 'value').set;
    setter.call(ta, 'build a firmware binary for the spec');
    ta.dispatchEvent(new Event('input', { bubbles: true }));
    return true;
  })()`);
  check('prompt input takes the request', Boolean(filled));

  // React flips the disabled state after the input event flushes; poll.
  const clickable = await waitUntil(
    async () => cdp.eval(`(() => { ${ROOT}
      return !el.querySelector('.agent-run-btn').disabled;
    })()`),
    Boolean,
    5000,
  );
  check('run button becomes enabled after the prompt flush', Boolean(clickable));

  // ---- the no-model refusal, verbatim --------------------------------
  await cdp.eval(`(() => { ${ROOT} el.querySelector('.agent-run-btn').click(); return 1; })()`);
  const status = await waitUntil(
    async () => cdp.eval(`(() => { ${ROOT}
      const st = el.querySelector('.agent-status');
      return st ? st.innerText : '';
    })()`),
    (text) => /REFUSED/.test(text || ''),
    15000,
  );
  check(
    'no model attached: the status says REFUSED verbatim',
    /REFUSED/.test(status || ''),
  );

  const refusal = await cdp.eval(`(() => { ${ROOT}
    const block = el.querySelector('.agent-refusal');
    const message = el.querySelector('.agent-refusal-message');
    return {
      text: block ? block.innerText.replace(/\\s+/g, ' ') : '',
      message: message ? message.innerText : '',
    };
  })()`);
  check(
    'the refusal is capability_unavailable and names the attach env var',
    /capability_unavailable/.test(refusal.text || '') &&
      /TRINITY_MODEL_ENDPOINT/.test(refusal.text || ''),
  );
  check(
    'the refusal names the agent role it was asked for',
    /coder agent cannot run/.test(refusal.message || ''),
  );

  // ---- nothing fake rides along a refusal ----------------------------
  const fakes = await cdp.eval(`(() => { ${ROOT}
    return {
      summary: Boolean(el.querySelector('.agent-summary')),
      steps: el.querySelectorAll('.agent-step').length,
    };
  })()`);
  check(
    'a refusal renders no summary and no synthesized steps',
    fakes && !fakes.summary && Number(fakes.steps) === 0,
  );

  // ---- the role select acts on the run (offered is not selected) -----
  const rolePick = await cdp.eval(`(() => { ${ROOT}
    const sel = el.querySelector('.agent-role-select');
    sel.value = 'researcher';
    sel.dispatchEvent(new Event('change', { bubbles: true }));
    return sel.value;
  })()`);
  check('the selected role updates the control', rolePick === 'researcher');

  await cdp.eval(`(() => { ${ROOT} el.querySelector('.agent-run-btn').click(); return 1; })()`);
  const researcher = await waitUntil(
    async () => cdp.eval(`(() => { ${ROOT}
      const message = el.querySelector('.agent-refusal-message');
      return message ? message.innerText : '';
    })()`),
    (text) => /researcher agent cannot run/.test(text || ''),
    15000,
  );
  check(
    'the researcher run is refused in the researcher truth',
    /researcher agent cannot run/.test(researcher || ''),
  );

  await sleep(300);
}
