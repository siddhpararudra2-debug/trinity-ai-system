import { goto, sleep, waitFor, waitUntil } from '../lib.mjs';

/**
 * CAD suite: requirement-driven generation on the workspace, 3D geometry viewer
 * interactions, artifact receipts, and parametric form generation on /chat.
 */
export const name = 'cad';

export async function run({ cdp, check, WEB }) {
  // =========================================================================
  // 1. Workspace CAD generation: natural language -> CAD IR -> mesh -> verdict
  // =========================================================================
  await goto(cdp, WEB);
  await sleep(1500);

  // Check initial command bar state
  const reqText = await cdp.eval(`(() => {
    const el = document.querySelector('#trinity-requirement');
    return el ? el.value : '';
  })()`);
  check('workspace command input is present', Boolean(reqText));

  // Trigger generation
  const clickedGenerate = await cdp.eval(`(() => {
    const btn = document.querySelector('.btn-generate');
    if (!btn || btn.disabled) return false;
    btn.click();
    return true;
  })()`);
  check('generate button clickable', Boolean(clickedGenerate));

  // Wait for the specification panel to receive the live result
  const specText = await waitFor(
    async () => {
      return cdp.eval(`(() => {
        const p = document.querySelector('.spec-panel');
        return p ? p.innerText : '';
      })()`);
    },
    'QUADCOPTER FRAME',
    35000,
  );
  check('specification panel renders quadcopter frame', Boolean(specText) && /QUADCOPTER FRAME/i.test(specText));
  check('specification shows overall size', Boolean(specText) && /Overall Size/i.test(specText));
  check('specification reports triangle count', Boolean(specText) && /Triangle count/i.test(specText));

  // =========================================================================
  // 2. Validation Engine: independent checks and verdict
  // =========================================================================
  const validationText = await cdp.eval(`(() => {
    const p = document.querySelector('.validation-panel');
    return p ? p.innerText : '';
  })()`);
  check('validation verdict is VALIDATED', Boolean(validationText) && /VALIDATED/.test(validationText));
  check('dimensional accuracy check passed', Boolean(validationText) && /Dimensional accuracy\s*PASS/i.test(validationText));
  check('topology check passed', Boolean(validationText) && /Topology\s*PASS/i.test(validationText));
  check('symmetry check passed', Boolean(validationText) && /Symmetry\s*PASS/i.test(validationText));
  check('manufacturability check passed', Boolean(validationText) && /Manufacturability\s*PASS/i.test(validationText));

  // =========================================================================
  // 3. 3D Geometry Viewer: Three.js Canvas & Interactive Controls
  // =========================================================================
  const viewerInfo = await cdp.eval(`(() => {
    const v = document.querySelector('.viewer-panel');
    if (!v) return null;
    const canvas = v.querySelector('canvas');
    const controls = [...v.querySelectorAll('.ctrl-btn')].map(b => ({
      text: b.textContent.trim(),
      active: b.classList.contains('is-active'),
      pressed: b.getAttribute('aria-pressed'),
    }));
    return {
      hasCanvas: Boolean(canvas),
      text: v.innerText,
      controls,
    };
  })()`);

  check('3D geometry canvas is mounted', Boolean(viewerInfo?.hasCanvas));
  check('viewer displays model dimensions', Boolean(viewerInfo?.text) && /\d+(\.\d+)?\s*×\s*\d+(\.\d+)?.*MM/i.test(viewerInfo.text));
  check('viewer displays triangle count', Boolean(viewerInfo?.text) && /TRIANGLES:\s*[\d,]+/i.test(viewerInfo.text));

  // Toggle wireframe mode
  await cdp.eval(`(() => {
    const btn = [...document.querySelectorAll('.ctrl-btn')].find(b => b.textContent.includes('WIREFRAME'));
    if (!btn) return false;
    btn.click();
    return true;
  })()`);
  const wireframeToggled = await waitUntil(
    async () => cdp.eval(`(() => {
      const btn = [...document.querySelectorAll('.ctrl-btn')].find(b => b.textContent.includes('WIREFRAME'));
      return Boolean(btn && (btn.classList.contains('is-active') || btn.getAttribute('aria-pressed') === 'true'));
    })()`),
    Boolean,
    2000,
  );
  check('wireframe control toggles active', Boolean(wireframeToggled));

  // Toggle back to solid
  await cdp.eval(`(() => {
    const btn = [...document.querySelectorAll('.ctrl-btn')].find(b => b.textContent.includes('SOLID'));
    if (btn) btn.click();
    return true;
  })()`);

  // =========================================================================
  // 4. Artifact receipts: STL, GLB, JSON artifacts with SHA-256 and API links
  // =========================================================================
  const artifacts = await cdp.eval(`(() => {
    const section = document.querySelector('#artifacts');
    if (!section) return null;
    const cards = [...section.querySelectorAll('.artifact-card')];
    return cards.map(c => {
      const a = c.querySelector('a.btn-download');
      return {
        text: c.innerText,
        href: a ? a.getAttribute('href') : null,
      };
    });
  })()`);

  check('artifacts section populated', Array.isArray(artifacts) && artifacts.length >= 3);
  const stlArtifact = (artifacts ?? []).find(a => /STL/i.test(a.text));
  const glbArtifact = (artifacts ?? []).find(a => /GLB/i.test(a.text));
  check('STL artifact generated', Boolean(stlArtifact));
  check('GLB artifact generated', Boolean(glbArtifact));
  check(
    'artifact links point at /api/artifacts/',
    Boolean(stlArtifact?.href && stlArtifact.href.includes('/api/artifacts/')),
    stlArtifact?.href ?? 'no STL download link',
  );

  // =========================================================================
  // 5. Parametric CAD Form Mode on /chat
  // =========================================================================
  await goto(cdp, `${WEB}/chat`);
  await sleep(1500);

  // Switch to form mode tab
  await cdp.eval(`(() => {
    const b = [...document.querySelectorAll('.input-tab')]
      .find(x => x.textContent.trim().toLowerCase() === 'form');
    if (b) b.click();
    return true;
  })()`);
  await sleep(1000);

  // Verify CAD catalog is loaded in form mode
  const formReady = await waitUntil(
    async () => {
      return cdp.eval(`(() => {
        const sel = document.querySelector('.cad-part-select select');
        return sel ? sel.options.length > 0 : false;
      })()`);
    },
    Boolean,
    10000,
  );
  check('parametric form catalog loaded', Boolean(formReady));

  // Select mounting_plate from the catalog
  await cdp.eval(`(() => {
    const sel = document.querySelector('.cad-part-select select');
    if (!sel) return false;
    sel.value = 'mounting_plate';
    sel.dispatchEvent(new Event('change', { bubbles: true }));
    return true;
  })()`);
  await sleep(600);

  // Verify mounting_plate fields appeared
  const fields = await cdp.eval(`(() => {
    const inputs = [...document.querySelectorAll('.cad-fields-grid input')];
    return inputs.map(i => i.name);
  })()`);
  check(
    'mounting plate fields displayed',
    Array.isArray(fields) && fields.includes('width') && fields.includes('length'),
    String(fields),
  );

  // Record bot message count before generating
  const botBefore = await cdp.eval(`document.querySelectorAll('.chat-message.bot').length`);

  // Submit CAD form
  await cdp.eval(`(() => {
    const btn = document.querySelector('.cad-generate-btn');
    if (btn && !btn.disabled) btn.click();
    return true;
  })()`);

  // Wait for the request to finish, not merely for the transient processing
  // bubble.  The loading bubble is itself a bot message, so counting bot
  // nodes reports success before the API response has been rendered.
  const lastBotMessage = await waitUntil(
    async () => cdp.eval(`(() => {
      const msgs = [...document.querySelectorAll('.chat-message.bot')];
      const last = msgs[msgs.length - 1];
      if (!last || last.innerText.includes('PROCESSING')) return null;
      const links = [...last.querySelectorAll('a')].map(a => a.innerText.trim());
      return { text: last.innerText, links, count: msgs.length };
    })()`),
    (message) => Boolean(message && message.count > botBefore),
    30000,
  );
  check('form submission produces bot response', Boolean(lastBotMessage));

  check(
    'response confirms mounting plate generation',
    Boolean(lastBotMessage?.text) && /Mounting Plate generated/i.test(lastBotMessage.text),
    lastBotMessage?.text ?? 'no message',
  );
  check(
    'response includes triangle count and status',
    Boolean(lastBotMessage?.text) && /triangles/i.test(lastBotMessage.text),
  );
  check(
    'response offers file download links',
    Array.isArray(lastBotMessage?.links) && lastBotMessage.links.length > 0,
    JSON.stringify(lastBotMessage?.links),
  );
}
