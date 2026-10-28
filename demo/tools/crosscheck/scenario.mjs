// The walls both sides are asked to build. Prints scenario lines on stdout;
// printer.cpp (the plugin's C++) and run.mjs (demo/port.js) each read them
// and print what they make of them. See printer.cpp for the line format.
//
// Deterministic: a fixed-seed generator picks the logo sets and options, so
// the same scenario is checked every run and a difference is reproducible.

const f = Math.fround;
const view = new DataView(new ArrayBuffer(4));
const bits = (x) => {
  view.setFloat32(0, x);
  return view.getUint32(0).toString(16).padStart(8, '0');
};

let state = 0x6d2b79f5;
function rand() {
  state = (state + 0x6d2b79f5) >>> 0;
  let t = state;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const pick = (list) => list[Math.floor(rand() * list.length)];

/**
 * A logo as the atlas would describe it: an aspect, a trim box and the
 * artwork's box in layer space, and its frame delays (none for a still).
 */
function logo(aspect, margin = 0, frameMs = []) {
  aspect = f(aspect);
  // Where the artwork sits in a square layer, as PlaceIn puts it.
  const fx = aspect >= 1 ? 1 : aspect;
  const fy = aspect >= 1 ? 1 / aspect : 1;
  const ox = (1 - fx) / 2;
  const oy = (1 - fy) / 2;
  const x0 = f(ox + margin * fx);
  const x1 = f(ox + (1 - margin) * fx);
  const y0 = f(oy + margin * 0.5 * fy);
  const y1 = f(oy + (1 - margin * 0.5) * fy);
  const trimmed = f((aspect * (1 - 2 * margin)) / (1 - margin));
  return { floats: [aspect, x0, y0, x1, y1, trimmed, f(ox), f(oy), f(ox + fx), f(oy + fy)], frameMs };
}

const GIF_DELAYS = [11, 20, 40, 70, 100, 120, 500];
const animated = (n) => Array.from({ length: n }, () => pick(GIF_DELAYS));

const SETS = [
  [logo(1)],
  [logo(1), logo(1.3, 0, [120, 120, 120, 120])],
  [logo(4), logo(1), logo(0.7, 0.1, [100, 200, 300])],
  // the shape of the demo pack: panels, wordmarks, roundels, padded squares
  [logo(16 / 9), logo(16 / 9), logo(16 / 9), logo(16 / 9), logo(1.1348), logo(1, 0.06), logo(1, 0.05),
    logo(4.1538, 0.25), logo(1, 0.1), logo(4.6154, 0.26), logo(3.8462, 0.24), logo(1), logo(4.0385, 0.25),
    logo(1), logo(1, 0.4), logo(1), logo(4.2153), logo(1.2924), logo(0.7683)],
];
for (const n of [5, 9, 40]) {
  const set = [];
  for (let i = 0; i < n; i += 1) {
    const frames = rand() < 0.15 ? animated(2 + Math.floor(rand() * 8)) : [];
    set.push(logo(pick([0.5, 0.8, 1, 1, 1.5, 2.2, 2.5, 4, 6]), pick([0, 0, 0.1, 0.3]), frames));
  }
  SETS.push(set);
}

const TIMES = [0, 0.37, 1.999, 3.9, 17.25, 123.4, 1000.5];
const GRIDS = [[1, 1], [2, 2], [6, 4], [7, 5], [12, 8], [3, 9]];
// The largest wall the plugin allows, for one small folder only: a 32x32 wall
// is a thousand cells a step and its cycle can run to hundreds of steps, and
// the adjacency count is quadratic in the cells, so a few of these are most
// of the run time on both sides.
const out = [];

function L(set) {
  const each = set.map((l) => [...l.floats.map(bits), l.frameMs.length, ...l.frameMs].join(' '));
  out.push(`L ${set.length} ${each.join(' ')}`);
}

/**
 * AtlasPlan: images as the decoder hands them over -- a pixel size, an aspect,
 * a trim box in image space and frame delays -- and a GPU's two limits.
 */
function A(images, maxSize, maxLayers) {
  const each = images.map((i) => [i.width, i.height, ...[i.aspect, ...i.trim, i.trimmed].map(bits), i.frameMs.length, ...i.frameMs].join(' '));
  out.push(`A ${images.length} ${maxSize} ${maxLayers} ${each.join(' ')}`);
}

function image(width, height, frameMs = [], margin = 0) {
  const aspect = f(width / height);
  const trim = [f(margin), f(margin * 0.5), f(1 - margin), f(1 - margin * 0.5)];
  const trimmed = f(f(f(trim[2] - trim[0]) * width) / f(f(trim[3] - trim[1]) * height));
  return { width, height, aspect, trim, trimmed, frameMs };
}

function S(cols, rows, mode, seed, noAdj, aspect, equal, hero, hc, hr, hl) {
  out.push(`S ${cols} ${rows} ${mode} ${seed} ${noAdj} ${aspect} ${equal} ${hero} ${hc} ${hr} ${hl}`);
}

function Y(mode, host, ints, size, time) {
  const h = [host.cellPad, host.speed, host.scrollX, host.scrollY, host.gridRot, host.walkRate, host.twinkle,
    host.fade, host.fov, host.camYaw, host.camPitch, host.edgeThreshold, host.gridWidth].map((v) => bits(f(v)));
  out.push(`Y ${mode} ${ints.projection} ${ints.fit} ${ints.walk} ${ints.edge} ${ints.scope} ${h.join(' ')} ${ints.seed} ${size[0]} ${size[1]} ${bits(f(time))}`);
}

const DEFAULT_HOST = {
  cellPad: 0.12, speed: 0.5, scrollX: 0.5, scrollY: 0.5, gridRot: 0.5, walkRate: 0.25, twinkle: 0, fade: 0,
  fov: 0.4, camYaw: 0.56, camPitch: 0.56, edgeThreshold: 0.35, gridWidth: 0.1,
};

SETS.forEach((set, index) => {
  L(set);
  for (const [cols, rows] of index === 2 ? [...GRIDS, [32, 32]] : GRIDS) {
    for (const mode of [0, 1, 2, 3, 4]) {
      if (cols === 32 && mode !== 0 && mode !== 2) continue;
      // An 8-cube is 384 cells a step, and like a big flat wall its cycle
      // often runs to BuildSchedule's 4,096-step cap; one folder is enough.
      if (mode === 2 && Math.max(cols, rows) > 7 && index !== 2) continue;
      const seed = pick([0, 1, 7, 42, 9999, 1234567]);
      const noAdj = rand() < 0.8 ? 1 : 0;
      const aspect = rand() < 0.8 ? 1 : 0;
      const equal = rand() < 0.8 ? 1 : 0;
      const hero = pick([0, 0, 1, 2, 3]);
      S(cols, rows, mode, seed, noAdj, aspect, equal, hero, pick([1, 2, 3, 40]), pick([1, 2]), pick([0, 1, 5, 300]));

      for (let k = 0; k < (cols === 32 ? 1 : 4); k += 1) {
        const host = { ...DEFAULT_HOST };
        if (rand() < 0.5) host.cellPad = rand();
        if (rand() < 0.5) host.speed = rand();
        if (rand() < 0.4) host.scrollX = rand();
        if (rand() < 0.4) host.scrollY = rand();
        if (rand() < 0.3) host.gridRot = rand();
        if (rand() < 0.5) host.walkRate = rand();
        if (rand() < 0.3) host.twinkle = rand();
        if (rand() < 0.3) host.fade = rand();
        if (rand() < 0.5) host.fov = rand();
        if (rand() < 0.5) host.camYaw = rand();
        if (rand() < 0.5) host.camPitch = rand();
        host.edgeThreshold = rand();
        host.gridWidth = rand();
        const ints = {
          projection: rand() < 0.3 ? 1 : 0,
          fit: pick([0, 1, 2, 3]),
          walk: rand() < 0.4 ? pick([1, 2, 3, 4]) : 0,
          edge: pick([0, 1, 2, 3]),
          scope: pick([0, 1, 2, 3]),
          seed,
        };
        const size = pick([[960, 540], [1920, 1080], [1280, 1024], [1080, 1920], [333, 777]]);
        Y(mode, host, ints, size, pick(TIMES) + (rand() < 0.5 ? rand() * 10 : 0));
      }
    }
  }
});

// The atlas plan: frames as layers, the GPU's layer limit and the frame budget.
for (let k = 0; k < 24; k += 1) {
  const images = [];
  const count = 1 + Math.floor(rand() * 12);
  for (let i = 0; i < count; i += 1) {
    const longest = pick([64, 120, 200, 512, 800, 1500, 2000]);
    const aspect = pick([0.25, 0.5, 0.8, 1, 1, 1.5, 2.2, 4, 8]);
    const width = aspect >= 1 ? longest : Math.max(1, Math.round(longest * aspect));
    const height = aspect >= 1 ? Math.max(1, Math.round(longest / aspect)) : longest;
    const frames = rand() < 0.3 ? animated(2 + Math.floor(rand() * 30)) : [];
    images.push(image(width, height, frames, pick([0, 0, 0.1, 0.3])));
  }
  const total = images.reduce((n, i) => n + Math.max(i.frameMs.length, 1), 0);
  A(images, pick([0, 1024, 2048, 8192]), pick([0, 2048, count, count + 3, total, total - 1, Math.max(1, count - 2)]));
}
// Two GIFs at 2048px: the first fits the 256 MB of frames, the second does not.
A([image(2000, 500), image(300, 300, animated(10)), image(300, 300, animated(10))], 8192, 2048);

process.stdout.write(`${out.join('\n')}\n`);
