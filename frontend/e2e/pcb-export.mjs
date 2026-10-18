/**
 * End-to-end check of the PCB console in a real browser.
 *
 * The UI had no automated coverage at all: it was verified by `next build`
 * and by reading screenshots, which is how a stale "exported" badge
 * survived three commits. This drives the actual buttons through the
 * Chrome DevTools Protocol using Node's built-in WebSocket, so there is
 * nothing to install — no Playwright, no puppeteer, no browser download.
 *
 * Requires the API and the Next server to already be running:
 *   API   NEXT_PUBLIC_TRINITY_API_URL=http://127.0.0.1:8000/api
 *   WEB   http://127.0.0.1:3000
 *
 * Usage:  node e2e/pcb-export.mjs
 */

const WEB = process.env.E2E_WEB_URL ?? 'http://127.0.0.1:3000';
const PORT = 9222;

const log = (...a) => console.log('   ', ...a);
let failures = 0;

function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures += 1;
  return ok;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Minimal CDP client over the built-in WebSocket. */
class Cdp {
  constructor(ws) {
    this.ws = ws;
    this.id = 0;
    this.pending = new Map();
    ws.addEventListener('message', (event) => {
      const msg = JSON.parse(event.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) {
          reject(new Error(JSON.stringify(msg.error)));
        } else {
          resolve(msg.result);
        }
      }
    });
  }

  static async attach(wsUrl) {
    const ws = new WebSocket(wsUrl);
    await new Promise((resolve, reject) => {
      ws.addEventListener('open', resolve, { once: true });
      ws.addEventListener('error', reject, { once: true });
    });
    return new Cdp(ws);
  }

  send(method, params = {}) {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }

  /** Evaluate an expression in the page and return its JSON value. */
  async eval(expression) {
    const res = await this.send('Runtime.evaluate', {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (res.exceptionDetails) {
      throw new Error(res.exceptionDetails.exception?.description ?? 'eval failed');
    }
    return res.result.value;
  }
}

async function fetchJson(url) {
  const res = await fetch(url);
  return res.json();
}

async function main() {
  // ---------------------------------------------------------------- setup
  const targets = await fetchJson(`http://127.0.0.1:${PORT}/json/list`);
  // Ignore Edge's own chrome pages — a first-run sync dialog can be sitting
  // on another tab, and driving that tests nothing.
  const page =
    targets.find((t) => t.type === 'page' && !t.url.startsWith('edge://')) ??
    targets.find((t) => t.type === 'page');
  if (!page) throw new Error('no page target: is Edge running with --remote-debugging-port?');

  const cdp = await Cdp.attach(page.webSocketDebuggerUrl);
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');

  log(`browser: ${page.url}`);
  await cdp.send('Page.navigate', { url: WEB });
  await sleep(3500);

  // The panel is inside the app scroller, so scroll it into view first.
  await cdp.eval(`
    (() => {
      const h = [...document.querySelectorAll('h2')].find(e => /PCB/i.test(e.textContent));
      if (h) h.scrollIntoView({block:'start'});
      return true;
    })()
  `);
  await sleep(400);

  // ------------------------------------------------------------- helpers
  // Every query is scoped to the PCB panel. The workspace command bar has its
  // own "GENERATE ->" button that appears earlier in the DOM, so an unscoped
  // query silently drives the wrong control.
  const SCOPED = `
    const panel = [...document.querySelectorAll('h2')]
      .find(e => /PCB/i.test(e.textContent)).closest('article');
  `;

  const clickButton = async (label) => {
    const clicked = await cdp.eval(`(() => { ${SCOPED}
      const b = [...panel.querySelectorAll('button')]
        .find(x => x.textContent.trim().toUpperCase().startsWith(${JSON.stringify(label)}));
      if (!b) return false;
      b.click();
      return true;
    })()`);
    if (!clicked) throw new Error(`button not found in PCB panel: ${label}`);
  };

  const panelText = () => cdp.eval(`(() => { ${SCOPED} return panel.innerText; })()`);

  const waitFor = async (needle, ms = 30000) => {
    const deadline = Date.now() + ms;
    while (Date.now() < deadline) {
      const text = await panelText();
      if (text.includes(needle)) return text;
      await sleep(400);
    }
    return null;
  };

  // --------------------------------------------------------------- tests
  const landed = await cdp.eval(`document.querySelectorAll('h2').length > 0`);
  check('workspace renders', landed);

  await cdp.eval(`(() => { ${SCOPED}
      const s = [...panel.querySelectorAll('select')]
        .find(e => [...e.options].some(o => o.value === 'esp32'));
      if (s) { s.value = 'esp32'; s.dispatchEvent(new Event('change', {bubbles:true})); }
      return true;
    })()`);
  await sleep(300);

  await clickButton('GENERATE');
  // Wait on the summary line, not the "COMPONENTS" field label, which is
  // always present in the form and matches before anything has run.
  const generated = await waitFor('1 COMPONENTS');
  check('generate produces a board', Boolean(generated));
  if (process.env.E2E_DEBUG) console.log('---- panel after generate ----\n', generated);
  check('auto-netting wired power pins', /NET 3V3/.test(generated ?? ''),
    (generated ?? '').match(/NET [^\n]*/g)?.slice(0, 3).join(', '));
  check('bare-safe: a component board is not netless', !/NET \(none\)/.test(generated ?? ''));

  await clickButton('VALIDATE');
  const validated = await waitFor('RULES PASSED');
  check('validate passes after auto-netting', Boolean(validated));

  await clickButton('EXPORT KICAD');
  const exported = await waitFor('KICAD EXPORTED');
  check('export produces a KiCad file', Boolean(exported));
  check('export badge shows a size', /KICAD EXPORTED \(\d/.test(exported ?? ''),
    (exported ?? '').match(/KICAD EXPORTED[^\\n]*/)?.[0]);

  // The regression this suite exists for: the badge used to be derived from
  // the current result, so running VALIDATE afterwards erased it.
  await clickButton('VALIDATE');
  await sleep(2500);
  const afterRevalidate = await panelText();
  check('badge survives a later VALIDATE', /KICAD EXPORTED/.test(afterRevalidate));

  const links = await cdp.eval(`(() => { ${SCOPED}
    return [...panel.querySelectorAll('a')]
      .map(a => a.getAttribute('href') || '')
      .filter(h => h.includes('/api/artifacts/'));
  })()`);
  check('kicad artifact download link present', links.length > 0, links[0]);

  console.log('');
  console.log(failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('E2E ERROR:', err.message);
  process.exit(1);
});