import { writeFileSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { deflateSync } from 'node:zlib';
import { panel, waitFor, sleep } from '../lib.mjs';

/** Vision: upload, then run every operation the bridge exposes. */
export const name = 'vision';

let crcTable = null;
function crc32(buf) {
  if (!crcTable) {
    crcTable = new Int32Array(256);
    for (let n = 0; n < 256; n += 1) {
      let c = n;
      for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c;
    }
  }
  let c = -1;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return c ^ -1;
}

/** A valid 8x6 RGB PNG, built here so the suite needs no committed fixtures. */
function pngBytes() {
  const rows = [];
  for (let y = 0; y < 6; y += 1) {
    const px = [];
    for (let x = 0; x < 8 * 3; x += 1) px.push((x * 17) % 256);
    rows.push(Buffer.from([0, ...px]));
  }
  const chunk = (tag, payload) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(payload.length);
    const body = Buffer.concat([Buffer.from(tag, 'ascii'), payload]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body) >>> 0);
    return Buffer.concat([len, body, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(8, 0);
  ihdr.writeUInt32BE(6, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(Buffer.concat(rows))),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

export async function run({ cdp, check }) {
  const dir = join(tmpdir(), 'trinity-e2e');
  mkdirSync(dir, { recursive: true });
  const good = join(dir, 'e2e-board.png');
  const bad = join(dir, 'e2e-not-an-image.exe');
  const empty = join(dir, 'e2e-empty.png');
  const tiff = join(dir, 'e2e-photo.tif');
  writeFileSync(good, pngBytes());
  writeFileSync(bad, Buffer.from('MZ\x90\x00'));
  writeFileSync(empty, Buffer.alloc(0));
  writeFileSync(tiff, Buffer.from('II*\x00\x08\x00\x00\x00'));

  const p = panel(cdp, /VISION/i);
  await p.scrollIntoView();

  const setFile = async (path) => {
    const { root } = await cdp.send('DOM.getDocument');
    const { nodeId } = await cdp.send('DOM.querySelector', {
      nodeId: root.nodeId,
      selector: 'input[type=file]',
    });
    if (!nodeId) throw new Error('no file input in the vision panel');
    await cdp.send('DOM.setFileInputFiles', { files: [path], nodeId });
    await sleep(2500);
  };

  /**
 * Read a row out of the result spec table, by dt/dd text.
 *
 * Scraping the panel's innerText does not work: "WIDTH_PX" is also a form
 * label and appears in an OPERATION option, so the first regex match was
 * the form, not the result. Inputs contribute no innerText at all, so the
 * form label was followed by whatever label came next.
 */
  const field = (key) =>
    cdp.eval(`(() => {
      const h = [...document.querySelectorAll('h2')].find(e => /VISION/i.test(e.textContent));
      const specs = [...h.closest('article').querySelectorAll('.ws-spec')];
      const dl = specs[specs.length - 1];
      if (!dl) return null;
      for (const row of dl.querySelectorAll('div')) {
        const dt = row.querySelector('dt');
        const dd = row.querySelector('dd');
        if (dt && dt.textContent.trim() === ${JSON.stringify(key)}) return dd.textContent.trim();
      }
      return null;
    })()`);

  /** Href of the current result's first artifact link. */
  const artifactHref = () =>
    cdp.eval(`(() => {
      const h = [...document.querySelectorAll('h2')].find(e => /VISION/i.test(e.textContent));
      const a = [...h.closest('article').querySelectorAll('a')]
        .find(x => (x.getAttribute('href') || '').includes('/api/artifacts/'));
      return a ? a.getAttribute('href') : null;
    })()`);

  /** The panel's rendered error line, if any. Not the footnote. */
  const panelError = () =>
    cdp.eval(`(() => {
      const h = [...document.querySelectorAll('h2')].find(e => /VISION/i.test(e.textContent));
      const e = h.closest('article').querySelector('.ws-error');
      return e ? e.textContent.trim() : null;
    })()`);

  // Wait on a field reaching a specific value rather than on a label
  // appearing: "WIDTH_PX" is in the OPERATION option label and "CHANNELS"
  // survives from the previous result, so both match before anything runs.
  const waitField = async (key, want, ms = 40000) => {
    const deadline = Date.now() + ms;
    let seen = null;
    while (Date.now() < deadline) {
      seen = await field(key);
      if (seen === String(want)) return seen;
      await sleep(300);
    }
    return seen;
  };

  const runOp = async (op, label, fills = {}) => {
    await p.select(op, 'OPERATION');
    await sleep(500);
    for (const [k, v] of Object.entries(fills)) await p.fill(k, v);
    if (Object.keys(fills).length) await sleep(300);
    const before = await artifactHref();
    await p.click(label);
    return before;
  };

  await setFile(good);
  const uploaded = await waitFor(p.text, 'CHECKSUM', 30000);
  check('upload returns a checksum', Boolean(uploaded));
  check('upload is tracked as an artifact', Boolean(uploaded) && /ARTIFACT/.test(uploaded));

  // --- inspect. Asserting real values, not just that the labels rendered:
  // an earlier version of this suite passed against metadata that was all
  // zeros, because it only checked the label was on screen.
  await runOp('image_inspect', 'INSPECT');
  check('inspect decodes the real width', (await waitField('WIDTH_PX', '8')) === '8');
  check('inspect decodes the real height', (await waitField('HEIGHT_PX', '6')) === '6');
  check('inspect decodes the format', (await waitField('FORMAT', 'png')) === 'png');
  check('inspect decodes the channel count', (await waitField('CHANNELS', '3')) === '3');

  // --- resize, chained onto the upload
  await runOp('image_resize', 'RESIZE', {
    WIDTH_PX: '16',
    HEIGHT_PX: '12',
  });
  check('resize produces the requested width', (await waitField('WIDTH_PX', '16')) === '16');
  check('resize produces the requested height', (await waitField('HEIGHT_PX', '12')) === '12');
  check('resize records what it started from', (await waitField('WAS_WIDTH_PX', '8')) === '8');
  check('resize emits an image artifact', Boolean(await artifactHref()), 'no artifact link');

  // --- grayscale, chained onto the resized image. Canny is what first
  // triggers OpenCV's parallel backend, whose plugin_loader chatter on
  // stdout used to make the bridge report "unparseable output".
  await runOp('image_grayscale', 'GRAYSCALE');
  check('grayscale collapses to one channel', (await waitField('CHANNELS', '1')) === '1');
  check('grayscale keeps the chained dimensions', (await waitField('WIDTH_PX', '16')) === '16');

  // --- edge detect. Assert the artifact *changes*, not that the download
// count grows: Artifacts renders only the current result, so every op after
// the first shows exactly one link and a count check reads as "no output".
  const edgesBefore = await runOp('image_edges', 'EDGE DETECT');
  let edgesHref = await artifactHref();
  // Poll for a href that is both present and different. Running an
  // operation clears the previous result, so a poll landing mid-request
  // sees no artifact link at all -- an early exit there reads as "this op
  // produced nothing" and failed this suite for exactly that reason.
  for (let i = 0; i < 40 && (!edgesHref || edgesHref === edgesBefore); i += 1) {
    await sleep(300);
    edgesHref = await artifactHref();
  }
  // A null href means the op returned no artifact at all, which is a
  // failure -- but only the engine knows why, so carry its message.
  const edgesError = await panelError();
  check(
    'edge detect emits a new image artifact',
    Boolean(edgesHref) && edgesHref !== edgesBefore,
    edgesError ? `engine said: ${edgesError}` : `before=${edgesBefore} after=${edgesHref}`,
  );
  check('edge detect did not report a parse failure', !/unparseable/i.test((await p.text()) ?? ''), 'stdout noise broke the envelope');

  // --- statistics
  await runOp('image_stats', 'STATISTICS');
  let mean = null;
  for (let i = 0; i < 40; i += 1) {
    mean = await field('OVERALL_MEAN');
    if (mean && /^[\d.]+$/.test(mean)) break;
    await sleep(300);
  }
  check(
    'statistics report a numeric overall mean',
    Boolean(mean) && /^[\d.]+$/.test(mean),
    `got ${JSON.stringify(mean)}`,
  );
  check('statistics report per-channel means', /MEAN/.test((await p.text()) ?? ''));

  // --- rejections must happen at the edge, with a reason
  await setFile(bad);
  check(
    'non-image upload is refused',
    /Unsupported image type|supported/i.test((await p.text()) ?? ''),
  );

  await setFile(empty);
  check('empty upload is refused', /empty/i.test((await p.text()) ?? ''));

  // tiff/webp are offered by no control here and are not in the native
  // engine's supportedFormats(), so they must be refused at upload.
  await setFile(tiff);
  check(
    'tiff is refused rather than stored for later failure',
    /Unsupported image type|supported/i.test((await p.text()) ?? ''),
  );
}