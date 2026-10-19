/**
 * Shared browser-driving helpers for the e2e suites.
 *
 * Zero dependencies: Node's built-in WebSocket speaks the Chrome DevTools
 * Protocol directly, so there is nothing to install and no browser to
 * download. The suite attaches to a browser the caller already launched.
 *
 * Every query that touches a console panel must go through `panel()`.
 * The workspace command bar has its own "GENERATE ->" button that sits
 * earlier in the DOM than the PCB panel, and an unscoped selector drives
 * the wrong control -- that bug cost a whole test run once already.
 */

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Minimal CDP client. */
export class Cdp {
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

  close() {
    try {
      this.ws.close();
    } catch {
      /* already gone */
    }
  }
}

/**
 * Connect to the debuggable browser and open a page.
 * Ignores Edge's own chrome pages — a first-run sync dialog can hold a tab,
 * and driving that tests nothing.
 */
export async function openBrowser(port = 9222) {
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page =
    targets.find((t) => t.type === 'page' && !t.url.startsWith('edge://')) ??
    targets.find((t) => t.type === 'page');
  if (!page) {
    throw new Error(
      `no page target on :${port}. Start a browser with --remote-debugging-port=${port}`,
    );
  }
  const cdp = await Cdp.attach(page.webSocketDebuggerUrl);
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');
  return cdp;
}

/** Navigate and wait for the app shell to be interactive. */
export async function goto(cdp, url) {
  await cdp.send('Page.navigate', { url });
  await sleep(3500);
}

/**
 * A scoped handle on one console panel, matched by its heading text.
 * Every method is scoped to that <article>, so nothing can reach the
 * command bar by accident.
 */
export function panel(cdp, headingPattern) {
  const root = `
    const __root = [...document.querySelectorAll('h2')]
      .find(e => ${headingPattern}.test(e.textContent));
    const el = __root ? __root.closest('article') : null;
    if (!el) throw new Error('panel not found: ${headingPattern}');
  `;

  return {
    text: () => cdp.eval(`(() => { ${root} return el.innerText; })()`),

    async click(label) {
      const ok = await cdp.eval(`(() => { ${root}
        const b = [...el.querySelectorAll('button')]
          .find(x => x.textContent.trim().toUpperCase().startsWith(${JSON.stringify(label)}));
        if (!b) return false;
        if (b.disabled) return 'disabled';
        b.click();
        return true;
      })()`);
      if (ok !== true) throw new Error(`${label}: ${ok === 'disabled' ? 'disabled' : 'not found'}`);
    },

    select(value, labelMatch) {
      return cdp.eval(`(() => { ${root}
        const s = [...el.querySelectorAll('select')]
          .find(x => ${labelMatch ? `x.closest('label')?.innerText.includes(${JSON.stringify(labelMatch)}) && ` : ''}[...x.options].some(o => o.value === ${JSON.stringify(value)}));
        if (!s) return false;
        s.value = ${JSON.stringify(value)};
        s.dispatchEvent(new Event('change', {bubbles:true}));
        return true;
      })()`);
    },

    fill(labelText, value) {
      return cdp.eval(`(() => { ${root}
        const input = [...el.querySelectorAll('label')]
          .find(l => l.innerText.trim().toUpperCase().startsWith(${JSON.stringify(labelText.toUpperCase())}))
          ?.querySelector('input');
        if (!input) return false;
        const setter = Object.getOwnPropertyDescriptor(
          window.HTMLInputElement.prototype, 'value').set;
        setter.call(input, ${JSON.stringify(value)});
        input.dispatchEvent(new Event('input', {bubbles:true}));
        return true;
      })()`);
    },

    scrollIntoView() {
      return cdp.eval(`(() => { ${root} el.scrollIntoView({block:'start'}); return true; })()`);
    },
  };
}

/** Poll `read` until `needle` appears, or give up. Returns the text. */
export async function waitFor(read, needle, ms = 30000) {
  const deadline = Date.now() + ms;
  let last = '';
  while (Date.now() < deadline) {
    last = await read();
    if (typeof last === 'string' && last.includes(needle)) return last;
    await sleep(400);
  }
  return null;
}

/**
 * Poll until `predicate(read)` is truthy. For conditions that are not a
 * substring match — waitFor's needle API coerces null to the string "null"
 * and silently never matches, which cost a whole suite once.
 */
export async function waitUntil(read, predicate, ms = 10000) {
  const deadline = Date.now() + ms;
  let last;
  while (Date.now() < deadline) {
    last = await read();
    if (predicate(last)) return last;
    await sleep(300);
  }
  return last;
}

/** Assert helper with a running failure tally. */
export function makeChecker() {
  const state = { failures: [], passes: 0 };
  return {
    state,
    check(name, ok, detail = '') {
      if (ok) {
        state.passes += 1;
        console.log(`  PASS  ${name}`);
      } else {
        state.failures.push(name);
        console.log(`  FAIL  ${name}${detail ? `  (${detail})` : ''}`);
      }
      return ok;
    },
  };
}