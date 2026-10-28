// demo/port.js through the same scenario as printer.cpp, printing the same
// lines. The wiring below is the page's (demo/plugin.js), which is
// GridironPlugin's: FillGrid before the schedule, the hero off on a cube,
// Twinkle and Fade forced on by their modes, the camera pitch halved.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const P = await import(join(here, '..', '..', 'port.js'));
const { f } = P;

const view = new DataView(new ArrayBuffer(4));
const fromBits = (hex) => {
  view.setUint32(0, Number.parseInt(hex, 16));
  return view.getFloat32(0);
};
const bits = (x) => {
  view.setFloat32(0, x);
  return view.getUint32(0).toString(16).padStart(8, '0');
};
const mat = (m) => Array.from(m, bits).join(' ');

const lines = readFileSync(0, 'utf8').split('\n').filter(Boolean);
const out = [];
let logos = [];
let schedule = { steps: [] };
let columns = 1;
let rows = 1;

for (const line of lines) {
  const t = line.split(' ');
  if (t[0] === 'L') {
    const n = Number(t[1]);
    logos = [];
    let at = 2;
    for (let i = 0; i < n; i += 1) {
      const v = t.slice(at, at + 10).map(fromBits);
      const frames = Number(t[at + 10]);
      const frameMs = t.slice(at + 11, at + 11 + frames).map(Number);
      at += 11 + frames;
      logos.push({
        index: i, aspect: v[0], trimX0: v[1], trimY0: v[2], trimX1: v[3], trimY1: v[4], trimmedAspect: v[5],
        imageX0: v[6], imageY0: v[7], imageX1: v[8], imageY1: v[9], frameMs,
      });
    }
    out.push(`L ${n}`);
  } else if (t[0] === 'A') {
    const n = Number(t[1]);
    const maxSize = Number(t[2]);
    const maxLayers = Number(t[3]);
    const images = [];
    let at = 4;
    for (let i = 0; i < n; i += 1) {
      const width = Number(t[at]);
      const height = Number(t[at + 1]);
      const v = t.slice(at + 2, at + 8).map(fromBits);
      const frames = Number(t[at + 8]);
      const frameMs = t.slice(at + 9, at + 9 + frames).map(Number);
      at += 9 + frames;
      images.push({
        name: 'logo', width, height, aspect: v[0], trimX0: v[1], trimY0: v[2], trimX1: v[3], trimY1: v[4], trimmedAspect: v[5],
        // Planning reads sizes and delays, never pixels.
        frames: frames > 0 ? frameMs.map((delayMs) => ({ rgba: null, delayMs })) : [{ rgba: null, delayMs: 100 }],
      });
    }
    const plan = P.planAtlas(images, maxSize, maxLayers);
    out.push(`A ${plan.layerSize} ${plan.layers.length} ${plan.animated} ${plan.stilled} ${plan.note}`);
    for (const l of plan.logos) {
      const v = [l.aspect, l.imageX0, l.imageY0, l.imageX1, l.imageY1, l.trimX0, l.trimY0, l.trimX1, l.trimY1, l.trimmedAspect];
      out.push(`P ${l.index} ${l.layer} ${v.map(bits).join(' ')} ${l.frameMs.length}${l.frameMs.map((ms) => ` ${ms}`).join('')}`);
    }
    out.push(`Q${plan.layers.map((x) => ` ${x.image},${x.frame}`).join('')}`);
  } else if (t[0] === 'S') {
    const [cols, rws, mode, seed, noAdj, aspect, equal, hero, heroCols, heroRows, heroLogo] = t.slice(1).map(Number);
    columns = cols;
    rows = rws;
    const [fc, fr] = P.fillGrid(mode, cols, rws);
    schedule = P.buildSchedule(logos, fc, fr, {
      seed: seed >>> 0,
      noAdjacentRepeat: noAdj !== 0,
      aspectAware: aspect !== 0,
      equalAirtime: equal !== 0,
      hero: mode === P.MODE.Rubik ? P.HERO.Off : hero,
      heroCols,
      heroRows,
      heroLogo,
    });
    out.push(`S ${schedule.steps.length} ${schedule.ordinaryCells} ${schedule.exactParity ? 1 : 0} ${schedule.adjacentRepeats} ${schedule.note}`);
    for (const s of schedule.steps) {
      out.push(`T ${s.cells.map((p) => `${p.col},${p.row},${p.colSpan},${p.rowSpan},${p.logo},${p.hero ? 1 : 0}`).join(' ')}`);
    }
  } else if (t[0] === 'Y') {
    const [mode, projection, fit, walk, edge, edgeScope] = t.slice(1, 7).map(Number);
    const h = t.slice(7, 20).map(fromBits);
    const seed = Number(t[20]) >>> 0;
    const width = Number(t[21]);
    const height = Number(t[22]);
    const time = fromBits(t[23]);
    const lo = {
      mode, projection, fit, walk, edge, edgeScope,
      cellPad: P.CellPad(h[0]),
      speed: P.Speed(h[1]),
      scrollX: P.Scroll(h[2]),
      scrollY: P.Scroll(h[3]),
      gridRot: P.GridRot(h[4]),
      walkRate: P.WalkRate(h[5]),
      twinkle: h[6],
      fade: h[7],
      fov: P.Fov(h[8]),
      camYaw: P.CamAngle(h[9]),
      camPitch: f(P.CamAngle(h[10]) * 0.5),
      seed,
      outputAspect: f(width / height),
    };
    if (mode === P.MODE.Twinkle && lo.twinkle <= 0) lo.twinkle = 1;
    if (mode === P.MODE.FadeCycle && lo.fade <= 0) lo.fade = 1;

    const r = P.buildLayout(schedule, logos, columns, rows, time, lo);
    out.push(`Y ${r.cells.length} ${bits(P.EdgeThreshold(h[11]))} ${bits(P.GridWidth(h[12]))} ${mat(r.view)} ${mat(r.projection)}`);
    for (const c of r.cells) {
      out.push(`C ${c.logo} ${c.frame} ${c.hero ? 1 : 0} ${c.edge ? 1 : 0} ${mat(c.model)} ${[c.uvScaleX, c.uvScaleY, c.uvOffsetX, c.uvOffsetY, c.brightness, c.opacity].map(bits).join(' ')}`);
    }
  }
}

process.stdout.write(`${out.join('\n')}\n`);
