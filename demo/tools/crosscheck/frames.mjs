// The page's frames, against the real plugin's, at the same settings and time.
//
//     node frames.mjs <page url> <out dir>
//
// Writes <out dir>/frames.txt (one line per frame: `time id=value ...`, every
// parameter every line, so neither side carries anything over), then loads the
// page in headless Chrome (over a pipe, no debugging port), waits for the demo
// pack to load, and for each line sets the page's parameters and reads back
// the frame at that time through window.gridironDemo.capture. The page's
// frames go to <out dir>/page.rgba in the format frames.mm writes the
// plugin's: premultiplied RGBA8, bottom row first.
//
// The page has to be served over http (it fetches ./logos/); crosscheck.sh
// --frames does that. Chrome renders through SwiftShader, so this needs no GPU.
import { spawn } from 'node:child_process';
import { writeFileSync, mkdtempSync, rmSync, openSync, writeSync, closeSync } from 'node:fs';
import { join } from 'node:path';

const [url, outDir] = process.argv.slice(2);
const WIDTH = 960;
const HEIGHT = 540;

// The defaults, as the host would send them (Gridiron.cpp's constructor).
const DEFAULTS = {
  folderOverride: null, reload: 0,
  columns: 6, rows: 4, fit: 0, cellPad: 0.12, showGrid: 0, gridWidth: 0.1, gridR: 1, gridG: 1, gridB: 1,
  seed: 1, varyNeighbours: 1, respectAspect: 1, equalAirtime: 1,
  hero: 0, heroColumns: 2, heroRows: 1, heroLogo: 0,
  mode: 0, speed: 0.5, scrollX: 0.5, scrollY: 0.5, gridRotation: 0.5, walk: 0, walkRate: 0.25, twinkle: 0, fade: 0,
  projection: 0, fov: 0.4, camYaw: 0.56, camPitch: 0.56,
  edge: 0, edgeScope: 0, edgeAmount: 1, edgeThreshold: 0.35,
  cellFill: 0, fillR: 0.1, fillG: 0.1, fillB: 0.1,
};

/** [name, time, changes]. Each one is a picture worth comparing. */
const FRAMES = [
  ['defaults: Fit', 0, {}],
  ['Trim margins', 0, { fit: 2 }],
  ['Fill', 0, { fit: 1 }],
  ['Stretch', 0, { fit: 3 }],
  ['seed 7, 5x3, padded', 0, { seed: 7, columns: 5, rows: 3, cellPad: 0.3, fit: 2 }],
  ['hero top, grid, solid fill', 0, { hero: 1, heroColumns: 2, heroRows: 1, heroLogo: 18, showGrid: 1, cellFill: 1, fit: 2 }],
  ['Burin on every logo', 0, { edge: 1, edgeScope: 1, fit: 2 }],
  ['Nib on the whole wall', 0, { edge: 2, edgeScope: 0, fit: 2 }],
  ['Vectrix on all but a middle hero', 0, { edge: 3, edgeScope: 3, hero: 2, heroColumns: 2, heroRows: 2, fit: 2 }],
  ['Rubik at rest, cube faces', 0, { mode: 2, cellFill: 2 }],
  ['Rubik mid-move', 3.3, { mode: 2, cellFill: 2 }],
  ['Rubik three moves in', 7.0, { mode: 2, cellFill: 1, fit: 2 }],
  ['Slot Machine', 2.0, { mode: 1, fit: 2 }],
  ['Twinkle', 1.3, { mode: 3, fit: 2 }],
  ['Fade Cycle', 5.1, { mode: 4, fit: 2 }],
  ['perspective wall', 0, { projection: 1, camYaw: 0.62, camPitch: 0.53, fit: 2 }],
  ['Scroll X', 1.7, { scrollX: 0.8, fit: 2 }],
  ['Logo Walk left', 3.0, { walk: 3, walkRate: 0.5, fit: 2 }],
  ['Grid Rotation', 1.0, { gridRotation: 0.7, fit: 2 }],
];

const lines = FRAMES.map(([, time, changes]) => {
  const values = { ...DEFAULTS, ...changes };
  const parts = Object.entries(values).filter(([, v]) => v !== null).map(([k, v]) => `${k}=${v}`);
  return `${time} ${parts.join(' ')}`;
});
writeFileSync(join(outDir, 'frames.txt'), `${lines.join('\n')}\n`);
writeFileSync(join(outDir, 'frames.json'), JSON.stringify(FRAMES.map(([name, time]) => ({ name, time }))));

const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const profile = mkdtempSync(join(outDir, 'chrome-'));
const chrome = spawn(CHROME, ['--headless=new', '--no-first-run', '--no-default-browser-check', '--hide-scrollbars',
  `--user-data-dir=${profile}`, '--remote-debugging-pipe', '--use-gl=angle', '--use-angle=swiftshader',
  '--enable-unsafe-swiftshader', '--window-size=1400,1000'], { stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe'] });

let id = 0;
const waiting = new Map();
const errors = [];
let buf = '';
chrome.stdio[4].on('data', (chunk) => {
  buf += chunk.toString('utf8');
  let at;
  while ((at = buf.indexOf('\0')) >= 0) {
    const msg = JSON.parse(buf.slice(0, at));
    buf = buf.slice(at + 1);
    if (msg.id && waiting.has(msg.id)) {
      const { resolve, reject } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) reject(new Error(JSON.stringify(msg.error)));
      else resolve(msg.result);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      errors.push(msg.params.exceptionDetails.exception?.description ?? msg.params.exceptionDetails.text);
    } else if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
      errors.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
    }
  }
});
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
  id += 1;
  waiting.set(id, { resolve, reject });
  chrome.stdio[3].write(`${JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) })}\0`);
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let status = 0;
try {
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  const s = (m, p) => send(m, p, sessionId);
  await s('Runtime.enable');
  const evaluate = async (expression) => {
    const r = await s('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
    return r.result.value;
  };
  await s('Page.navigate', { url: `${url}?size=${WIDTH}x${HEIGHT}` });
  const deadline = Date.now() + 60000;
  while (!(await evaluate('Boolean(window.gridironDemo && window.gridironDemo.finished())').catch(() => false))) {
    if (Date.now() > deadline) throw new Error('the page never finished loading the demo pack');
    await sleep(250);
  }

  // The page's decode of the demo pack, in the shape `frames geometry` prints.
  const geometry = await evaluate(`window.gridironDemo.logos().map((l) =>
    [l.name, l.aspect, l.trimX0, l.trimY0, l.trimX1, l.trimY1, l.trimmedAspect]
      .map((v, i) => (i === 0 ? v : v.toFixed(6))).join(' ')).join('\\n')`);
  writeFileSync(join(outDir, 'page-geometry.txt'), `${geometry}\n`);

  const sink = openSync(join(outDir, 'page.rgba'), 'w');
  for (const line of lines) {
    const [time, ...parts] = line.split(' ');
    const set = parts.map((kv) => {
      const [k, v] = kv.split('=');
      return `p.set(${JSON.stringify(k)}, ${['columns', 'rows', 'seed', 'heroColumns', 'heroRows', 'heroLogo'].includes(k) ? JSON.stringify(v) : Number(v)});`;
    }).join('');
    const b64 = await evaluate(`(async () => { const p = window.gridironDemo.params; ${set}
      await window.gridironDemo.capture(${time});
      const { width, height, pixels } = await window.gridironDemo.capture(${time});
      if (width !== ${WIDTH} || height !== ${HEIGHT}) throw new Error('captured ' + width + 'x' + height);
      let s = ''; for (let i = 0; i < pixels.length; i += 0x8000) s += String.fromCharCode.apply(null, pixels.subarray(i, i + 0x8000));
      return btoa(s); })()`);
    writeSync(sink, Buffer.from(b64, 'base64'));
  }
  closeSync(sink);
  if (errors.length) {
    console.error(`frames.mjs: the page logged ${errors.length} error(s): ${errors.join(' | ')}`);
    status = 1;
  }
} catch (error) {
  console.error(`frames.mjs: ${error.message}`);
  status = 1;
} finally {
  try { await send('Browser.close'); } catch { /* gone */ }
  chrome.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
process.exit(status);
