import { writeFileSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { deflateSync } from 'node:zlib';
import { panel, waitFor, sleep } from '../lib.mjs';

/** Vision: upload an image, inspect it, and confirm bad uploads are refused. */
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
  writeFileSync(good, pngBytes());
  writeFileSync(bad, Buffer.from('MZ\x90\x00'));
  writeFileSync(empty, Buffer.alloc(0));

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

  await setFile(good);
  const uploaded = await waitFor(p.text, 'CHECKSUM', 30000);
  check('upload returns a checksum', Boolean(uploaded));
  check('upload is tracked as an artifact', Boolean(uploaded) && /ARTIFACT/.test(uploaded));

  await p.click('INSPECT');
  const inspected = await waitFor(p.text, 'WIDTH_PX', 40000);
  check('inspect reports dimensions', Boolean(inspected) && /WIDTH_PX/.test(inspected));
  check('inspect reports the decoded format', Boolean(inspected) && /FORMAT/.test(inspected));

  await setFile(bad);
  check(
    'non-image upload is refused',
    /Unsupported image type|supported/i.test((await p.text()) ?? ''),
  );

  await setFile(empty);
  check('empty upload is refused', /empty/i.test((await p.text()) ?? ''));
}