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
    for (let i = 0; i < n; i += 1) {
      const v = t.slice(2 + i * 6, 8 + i * 6).map(fromBits);
      logos.push({ index: i, aspect: v[0], trimX0: v[1], trimY0: v[2], trimX1: v[3], trimY1: v[4], trimmedAspect: v[5] });
    }
    out.push(`L ${n}`);
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
