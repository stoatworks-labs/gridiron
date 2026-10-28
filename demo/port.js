/**
 * Gridiron's CPU half, ported to JavaScript. NOTHING HERE IS THE PLUGIN.
 *
 * The plugin is GLSL plus a good deal of C++ that decides what the GLSL draws:
 * which files in the folder count and in what order (Library.cpp), where the
 * opaque part of each logo is (Library.cpp's trim), how each logo sits in its
 * texture-array layer (AtlasPlan.cpp), which logo goes in which cell and when
 * (Fill.cpp), and where every cell is at a given moment, flat or on a cube
 * (Layout.cpp, Mat4.h, Hash.h), with the host's 0..1 turned into real units by
 * Controls.h. None of that can run in a browser as it stands, so it is
 * translated here, function for function, in the C++'s order and with its
 * names. It is a hand translation: a reader checks it, and
 * demo/tools/crosscheck/ holds Fill and Layout to the plugin's own C++ number
 * for number (see that script for what it found). The decoding is not here at
 * all -- the page uses the browser's decoders, and plugin.js says how that
 * differs from stb_image and nanosvg.
 *
 * Arithmetic is float, as the plugin's is. `f` is Math.fround, applied after
 * every operation the C++ does in float, so a tie between two logos breaks the
 * way it does in the plugin rather than the way doubles would break it -- the
 * seed is the whole of "same folder, same seed, same wall", and a port that
 * broke ties differently would draw a different wall from the same seed.
 *
 * No DOM in this file. demo/plugin.js imports it for the page and
 * demo/tools/crosscheck/run.mjs imports it under node.
 */

export const f = Math.fround;

//===========================================================================
// Controls.h -- the parameter ids' meanings and the 0..1 mappings
//===========================================================================

export const FIT = { Fit: 0, Fill: 1, Trim: 2, Stretch: 3 };
export const HERO = { Off: 0, Top: 1, Middle: 2, Bottom: 3 };
export const MODE = { Static: 0, SlotMachine: 1, Rubik: 2, Twinkle: 3, FadeCycle: 4 };
export const WALK = { Off: 0, Up: 1, Down: 2, Left: 3, Right: 4 };
export const PROJECTION = { Flat: 0, Perspective: 1 };
export const EDGE = { Off: 0, Burin: 1, Nib: 2, Vectrix: 3 };
export const EDGE_SCOPE = { Global: 0, EveryCell: 1, HeroOnly: 2, ExceptHero: 3 };

const PI = f(3.14159265);

export const CellPad = (v) => f(f(v) * f(0.45));
export const GridWidth = (v) => f(f(0.002) + f(f(v) * f(0.08)));
export const Speed = (v) => f(f(f(f(v) * f(v)) * f(v)) * 8);
export function Scroll(v) {
  const s = f(f(f(v) * 2) - 1);
  return f(f(f(s * s) * s) * 4);
}
export function GridRot(v) {
  const s = f(f(f(v) * 2) - 1);
  return f(f(f(s * s) * s) * PI);
}
export const WalkRate = (v) => f(f(0.05) + f(f(f(v) * f(v)) * 6));
export const Fov = (v) => f(f(f(f(15) + f(f(v) * 75)) * PI) / 180);
export const CamAngle = (v) => f(f(f(f(v) * 2) - 1) * PI);
export const EdgeThreshold = (v) => f(f(0.01) + f(f(f(v) * f(v)) * f(0.6)));

/** Gridiron.cpp's ToOption: an option parameter's value back to an index. */
export function toOption(v, count) {
  v = f(v);
  if (count <= 1) return 0;
  const i = v <= 1 && count > 2 && v !== Math.floor(v)
    ? Math.trunc(f(f(v * (count - 1)) + 0.5))
    : Math.trunc(f(v + 0.5));
  return Math.min(Math.max(i, 0), count - 1);
}

//===========================================================================
// Hash.h -- Thomas Wang's integer hash, the source of every seeded choice
//===========================================================================

export function hashU32(x) {
  x >>>= 0;
  x = ((x ^ 61) ^ (x >>> 16)) >>> 0;
  x = (x + (x << 3)) >>> 0;
  x = (x ^ (x >>> 4)) >>> 0;
  x = Math.imul(x, 0x27d4eb2d) >>> 0;
  x = (x ^ (x >>> 15)) >>> 0;
  return x;
}

export const hash2 = (a, b) => hashU32((Math.imul(a >>> 0, 0x9e3779b9) ^ hashU32(b)) >>> 0);

/** 0..1, from two integers, as a float: static_cast<float>(hash) / 2^32. */
export const hash01 = (a, b) => f(f(hash2(a, b)) * f(1 / 4294967296));

//===========================================================================
// Mat4.h -- column-major, m[column * 4 + row], like OpenGL
//===========================================================================

const at = (c, r) => c * 4 + r;

export function identity() {
  const m = new Float32Array(16);
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}

const sub = (a, b) => ({ x: f(a.x - b.x), y: f(a.y - b.y), z: f(a.z - b.z) });
const cross = (a, b) => ({
  x: f(f(a.y * b.z) - f(a.z * b.y)),
  y: f(f(a.z * b.x) - f(a.x * b.z)),
  z: f(f(a.x * b.y) - f(a.y * b.x)),
});
const dot = (a, b) => f(f(f(a.x * b.x) + f(a.y * b.y)) + f(a.z * b.z));
function normalise(v) {
  const len = f(Math.sqrt(dot(v, v)));
  if (len <= f(1e-8)) return { x: 0, y: 0, z: 0 };
  return { x: f(v.x / len), y: f(v.y / len), z: f(v.z / len) };
}

/** `a * b`: transforms by b, then by a. */
export function mul(a, b) {
  const r = new Float32Array(16);
  for (let c = 0; c < 4; c += 1) {
    for (let row = 0; row < 4; row += 1) {
      let sum = 0;
      for (let k = 0; k < 4; k += 1) sum = f(sum + f(a[at(k, row)] * b[at(c, k)]));
      r[at(c, row)] = sum;
    }
  }
  return r;
}

export function transformPoint(a, p) {
  return {
    x: f(f(f(f(a[at(0, 0)] * p.x) + f(a[at(1, 0)] * p.y)) + f(a[at(2, 0)] * p.z)) + a[at(3, 0)]),
    y: f(f(f(f(a[at(0, 1)] * p.x) + f(a[at(1, 1)] * p.y)) + f(a[at(2, 1)] * p.z)) + a[at(3, 1)]),
    z: f(f(f(f(a[at(0, 2)] * p.x) + f(a[at(1, 2)] * p.y)) + f(a[at(2, 2)] * p.z)) + a[at(3, 2)]),
  };
}

export function translate(x, y, z) {
  const r = identity();
  r[at(3, 0)] = x;
  r[at(3, 1)] = y;
  r[at(3, 2)] = z;
  return r;
}

export function scale(x, y, z) {
  const r = identity();
  r[at(0, 0)] = x;
  r[at(1, 1)] = y;
  r[at(2, 2)] = z;
  return r;
}

export function rotateX(a) {
  const r = identity();
  const s = f(Math.sin(a));
  const c = f(Math.cos(a));
  r[at(1, 1)] = c;
  r[at(1, 2)] = s;
  r[at(2, 1)] = -s;
  r[at(2, 2)] = c;
  return r;
}

export function rotateY(a) {
  const r = identity();
  const s = f(Math.sin(a));
  const c = f(Math.cos(a));
  r[at(0, 0)] = c;
  r[at(0, 2)] = -s;
  r[at(2, 0)] = s;
  r[at(2, 2)] = c;
  return r;
}

export function rotateZ(a) {
  const r = identity();
  const s = f(Math.sin(a));
  const c = f(Math.cos(a));
  r[at(0, 0)] = c;
  r[at(0, 1)] = s;
  r[at(1, 0)] = -s;
  r[at(1, 1)] = c;
  return r;
}

export function rotateAxis(axis, angle) {
  const a = normalise(axis);
  const s = f(Math.sin(angle));
  const c = f(Math.cos(angle));
  const t = f(1 - c);
  const r = identity();
  r[at(0, 0)] = f(f(f(t * a.x) * a.x) + c);
  r[at(0, 1)] = f(f(f(t * a.x) * a.y) + f(s * a.z));
  r[at(0, 2)] = f(f(f(t * a.x) * a.z) - f(s * a.y));
  r[at(1, 0)] = f(f(f(t * a.x) * a.y) - f(s * a.z));
  r[at(1, 1)] = f(f(f(t * a.y) * a.y) + c);
  r[at(1, 2)] = f(f(f(t * a.y) * a.z) + f(s * a.x));
  r[at(2, 0)] = f(f(f(t * a.x) * a.z) + f(s * a.y));
  r[at(2, 1)] = f(f(f(t * a.y) * a.z) - f(s * a.x));
  r[at(2, 2)] = f(f(f(t * a.z) * a.z) + c);
  return r;
}

export function ortho(l, r, b, t, n, fr) {
  const o = identity();
  o[at(0, 0)] = f(2 / f(r - l));
  o[at(1, 1)] = f(2 / f(t - b));
  o[at(2, 2)] = f(-2 / f(fr - n));
  o[at(3, 0)] = f(-f(r + l) / f(r - l));
  o[at(3, 1)] = f(-f(t + b) / f(t - b));
  o[at(3, 2)] = f(-f(fr + n) / f(fr - n));
  return o;
}

export function perspective(fovY, aspect, n, fr) {
  const tanHalf = f(Math.tan(f(fovY * 0.5)));
  const p = new Float32Array(16);
  p[at(0, 0)] = f(1 / f(aspect * tanHalf));
  p[at(1, 1)] = f(1 / tanHalf);
  p[at(2, 2)] = f(-f(fr + n) / f(fr - n));
  p[at(2, 3)] = -1;
  p[at(3, 2)] = f(-f(f(2 * fr) * n) / f(fr - n));
  return p;
}

export function lookAt(eye, centre, up) {
  const fw = normalise(sub(centre, eye));
  const s = normalise(cross(fw, up));
  const u = cross(s, fw);
  const r = identity();
  r[at(0, 0)] = s.x;
  r[at(1, 0)] = s.y;
  r[at(2, 0)] = s.z;
  r[at(0, 1)] = u.x;
  r[at(1, 1)] = u.y;
  r[at(2, 1)] = u.z;
  r[at(0, 2)] = -fw.x;
  r[at(1, 2)] = -fw.y;
  r[at(2, 2)] = -fw.z;
  r[at(3, 0)] = -dot(s, eye);
  r[at(3, 1)] = -dot(u, eye);
  r[at(3, 2)] = dot(fw, eye);
  return r;
}

//===========================================================================
// Fill.cpp -- which logo goes in which cell, and when it changes
//===========================================================================

/** Above this trimmed width-to-height ratio a logo may claim the next cell. */
const WIDE_ASPECT = f(2.2);

function chooseLogo(remaining, left, up, hero, cellKey, seed) {
  let best = -1;
  let bestOwed = -1;
  let bestBreak = -1;

  for (let i = 0; i < remaining.length; i += 1) {
    if (remaining[i] <= 0) continue;
    const logo = i;
    if (logo === left || logo === up || logo === hero) continue;

    const tie = hash01(cellKey, (seed ^ Math.imul(logo, 0x85ebca6b)) >>> 0);
    if (remaining[i] > bestOwed || (remaining[i] === bestOwed && tie > bestBreak)) {
      best = logo;
      bestOwed = remaining[i];
      bestBreak = tie;
    }
  }

  if (best < 0) {
    for (let i = 0; i < remaining.length; i += 1) {
      if (remaining[i] > 0 && (best < 0 || remaining[i] > remaining[best])) best = i;
    }
  }
  return best;
}

function countAdjacentRepeats(cells) {
  let n = 0;
  for (let i = 0; i < cells.length; i += 1) {
    const a = cells[i];
    if (a.logo < 0 || a.hero) continue;
    for (let j = 0; j < cells.length; j += 1) {
      if (j === i || cells[j].logo !== a.logo) continue;
      const dc = cells[j].col - a.col;
      const dr = cells[j].row - a.row;
      if ((dc === 0 && (dr === 1 || dr === -1)) || (dr === 0 && (dc === 1 || dc === -1))) {
        n += 1;
        break;
      }
    }
  }
  return n;
}

export function heroBlock(columns, rows, o) {
  const r = { x: 0, y: 0, w: 0, h: 0 };
  if (o.hero === HERO.Off) return r;
  const w = Math.min(Math.max(o.heroCols, 1), columns);
  const h = Math.min(Math.max(o.heroRows, 1), rows);
  if (w * h >= columns * rows) return r;
  r.w = w;
  r.h = h;
  r.x = Math.trunc((columns - w) / 2);
  if (o.hero === HERO.Top) r.y = 0;
  else if (o.hero === HERO.Middle) r.y = Math.trunc((rows - h) / 2);
  else if (o.hero === HERO.Bottom) r.y = rows - h;
  return r;
}

const valid = (r) => r.w > 0 && r.h > 0;

function gcd(a, b) {
  while (b) [a, b] = [b, a % b];
  return a;
}

const placement = (col, row) => ({ col, row, colSpan: 1, rowSpan: 1, logo: -1, hero: false });

/**
 * BuildSchedule. `o` carries FillOptions: seed, noAdjacentRepeat, aspectAware,
 * equalAirtime, hero, heroCols, heroRows, heroLogo.
 */
export function buildSchedule(logos, columns, rows, o) {
  const out = { steps: [], ordinaryCells: 0, exactParity: true, adjacentRepeats: 0, note: '' };
  columns = Math.max(columns, 1);
  rows = Math.max(rows, 1);
  const seed = o.seed >>> 0;

  const hero = heroBlock(columns, rows, o);

  const blank = [];
  for (let row = 0; row < rows; row += 1) {
    for (let col = 0; col < columns; col += 1) {
      if (valid(hero) && col >= hero.x && col < hero.x + hero.w && row >= hero.y && row < hero.y + hero.h) continue;
      blank.push(placement(col, row));
    }
  }

  const C = blank.length;
  const L = logos.length;
  out.ordinaryCells = C;

  if (L === 0 || C === 0) {
    out.note = L === 0 ? 'no images in folder' : 'no ordinary cells: the hero block covers the wall';
    return out;
  }

  const heroLogo = valid(hero) ? Math.min(Math.max(o.heroLogo, 0), L - 1) : -1;

  let eligible = [];
  for (let i = 0; i < L; i += 1) if (i !== heroLogo) eligible.push(i);
  if (eligible.length === 0) eligible = [...Array(L).keys()];

  const E = eligible.length;
  const g = gcd(E, C);
  const perLogo = o.equalAirtime ? C / g : Math.max(1, Math.trunc((C + E - 1) / E));

  const gridIndex = new Array(columns * rows).fill(-1);
  blank.forEach((p, i) => { gridIndex[p.row * columns + p.col] = i; });

  const remaining = new Array(L).fill(0);
  const total = new Array(L).fill(0);
  for (const i of eligible) remaining[i] = perLogo;

  const kMaxSteps = 4096;

  for (let step = 0; step < kMaxSteps; step += 1) {
    const cells = blank.map((p) => ({ ...p }));
    const occupying = new Array(blank.length).fill(-1);

    for (let i = 0; i < cells.length; i += 1) {
      if (occupying[i] >= 0) continue;

      if (!remaining.some((r) => r > 0)) {
        remaining.fill(0);
        for (const e of eligible) remaining[e] = perLogo;
      }

      const p = cells[i];

      let left = -1;
      let up = -1;
      if (p.col > 0) {
        const li = gridIndex[p.row * columns + (p.col - 1)];
        if (li >= 0) left = occupying[li];
      }
      if (p.row > 0) {
        const ui = gridIndex[(p.row - 1) * columns + p.col];
        if (ui >= 0) up = occupying[ui];
      }

      const heroBar = o.noAdjacentRepeat && valid(hero) ? heroLogo : -1;
      const cellKey = (Math.imul(step, 7919) + i) >>> 0;
      const logo = o.noAdjacentRepeat
        ? chooseLogo(remaining, left, up, heroBar, cellKey, seed)
        : chooseLogo(remaining, -1, -1, -1, cellKey, seed);
      if (logo < 0) break;

      remaining[logo] -= 1;
      total[logo] += 1;
      p.logo = logo;
      occupying[i] = logo;

      if (o.aspectAware && logos[logo].trimmedAspect >= WIDE_ASPECT && p.col + 1 < columns) {
        const ri = gridIndex[p.row * columns + (p.col + 1)];
        if (ri >= 0 && occupying[ri] < 0) {
          p.colSpan = 2;
          occupying[ri] = logo;
          cells[ri].logo = -1;
          cells[ri].colSpan = 0;
        }
      }
    }

    out.adjacentRepeats += countAdjacentRepeats(cells);

    if (valid(hero)) {
      cells.push({ col: hero.x, row: hero.y, colSpan: hero.w, rowSpan: hero.h, hero: true, logo: heroLogo });
    }

    out.steps.push({ cells });

    if (!remaining.some((r) => r > 0)) break;
  }

  let minTotal = total.length ? total[0] : 0;
  let maxTotal = minTotal;
  for (const t of total) {
    minTotal = Math.min(minTotal, t);
    maxTotal = Math.max(maxTotal, t);
  }
  out.exactParity = o.equalAirtime && minTotal === maxTotal;

  let n = `${L} logos${heroLogo >= 0 ? ' (1 in the hero)' : ''}, ${C} cells, ${out.steps.length} step cycle`;
  if (o.equalAirtime) n += `, ${maxTotal} placements each`;
  if (!out.exactParity) n += `; PARITY NOT EXACT (${minTotal}..${maxTotal})`;
  if (out.adjacentRepeats > 0) n += `; ${out.adjacentRepeats} adjacent repeats unavoidable`;
  if (valid(hero)) n += `; hero ${hero.w}x${hero.h}`;
  out.note = n;

  return out;
}

//===========================================================================
// Layout.cpp -- where every cell is, in three dimensions, at a given moment
//===========================================================================

const kPi = f(3.14159265358979);
const kHalfPi = f(kPi * 0.5);
export const kMoveCount = 16;

const fract = (x) => f(x - Math.floor(x));

function wrapCentred(v, period) {
  if (period <= 0) return v;
  return f(v - f(period * Math.floor(f(f(v / period) + 0.5))));
}

function easeInOut(t) {
  t = Math.min(Math.max(t, 0), 1);
  return t < 0.5
    ? f(f(f(4 * t) * t) * t)
    : f(1 - f(f(Math.pow(f(f(-2 * t) + 2), 3)) * 0.5));
}

function easeOut(t) {
  t = Math.min(Math.max(t, 0), 1);
  const u = f(1 - t);
  return f(1 - f(f(u * u) * u));
}

function layerOf(p, axis, n) {
  const v = axis === 0 ? p.x : axis === 1 ? p.y : p.z;
  const l = Math.floor(f(f(v + 0.5) * n));
  return Math.min(Math.max(l, 0), n - 1);
}

const axisVector = (axis) => (axis === 0 ? { x: 1, y: 0, z: 0 } : axis === 1 ? { x: 0, y: 1, z: 0 } : { x: 0, y: 0, z: 1 });

function faceRotation(face) {
  switch (face) {
    case 0: return identity();
    case 1: return rotateY(kHalfPi);
    case 2: return rotateY(kPi);
    case 3: return rotateY(-kHalfPi);
    case 4: return rotateX(-kHalfPi);
    default: return rotateX(kHalfPi);
  }
}

/**
 * FitUv. Fit, Fill and Stretch start from the artwork's box in its layer, not
 * the whole square layer -- mapping the whole layer at the logo's aspect is what
 * drew a 4:1 wordmark at 16:1 up to the plugin's v0.2.3. A logo without the
 * box fills its layer, as the C++ Logo's defaults say.
 */
function fitUv(fit, cellAspect, logo, out) {
  let rx0 = logo.imageX0 ?? 0;
  let ry0 = logo.imageY0 ?? 0;
  let rx1 = logo.imageX1 ?? 1;
  let ry1 = logo.imageY1 ?? 1;
  let contentAspect = logo.aspect;

  if (fit === FIT.Trim) {
    rx0 = logo.trimX0;
    ry0 = logo.trimY0;
    rx1 = logo.trimX1;
    ry1 = logo.trimY1;
    contentAspect = logo.trimmedAspect;
  }

  const rw = f(rx1 - rx0);
  const rh = f(ry1 - ry0);

  if (fit === FIT.Stretch) {
    out.uvScaleX = rw;
    out.uvScaleY = rh;
    out.uvOffsetX = rx0;
    out.uvOffsetY = ry0;
    return;
  }

  const cover = fit === FIT.Fill;

  let fx = 1;
  let fy = 1;
  if (contentAspect > cellAspect) fy = f(cellAspect / contentAspect);
  else fx = f(contentAspect / cellAspect);

  if (cover) {
    const k = f(1 / Math.min(fx, fy));
    fx = f(fx * k);
    fy = f(fy * k);
  }

  out.uvScaleX = f(rw / fx);
  out.uvScaleY = f(rh / fy);
  out.uvOffsetX = f(rx0 - f(f(f(1 - fx) * 0.5) * out.uvScaleX));
  out.uvOffsetY = f(ry0 - f(f(f(1 - fy) * 0.5) * out.uvScaleY));
}

export const cubeSize = (columns, rows) => Math.min(Math.max(Math.max(columns, rows), 2), 8);

/** The virtual grid Fill is given: the operator's, or N by 6N on a cube. */
export function fillGrid(mode, columns, rows) {
  if (mode === MODE.Rubik) {
    const n = cubeSize(columns, rows);
    return [n, n * 6];
  }
  return [columns, rows];
}

/**
 * FrameAt: which frame of an animated logo shows at `timeSeconds`, from its own
 * delays (`frameMs`), looped. Whole milliseconds and integer arithmetic from
 * there, as the C++ does, so the frame boundaries fall in the same places.
 */
export function frameAt(logo, timeSeconds) {
  const frameMs = logo.frameMs ?? [];
  if (frameMs.length < 2) return 0;
  let cycle = 0;
  for (const ms of frameMs) cycle += Math.max(ms, 1);
  const now = Math.floor(f(f(timeSeconds) * 1000));
  let into = ((now % cycle) + cycle) % cycle;
  for (let i = 0; i < frameMs.length; i += 1) {
    const ms = Math.max(frameMs[i], 1);
    if (into < ms) return i;
    into -= ms;
  }
  return frameMs.length - 1;
}

export function moveSequence(seed, n) {
  const half = kMoveCount / 2;
  const moves = [];
  for (let i = 0; i < half; i += 1) {
    moves.push({
      axis: hash2(i, seed) % 3,
      layer: hash2(i, (seed ^ 0x1234567) >>> 0) % n,
      turns: hash01(i, (seed ^ 0x89abcdef) >>> 0) < 0.5 ? 1 : -1,
    });
  }
  for (let i = half - 1; i >= 0; i -= 1) moves.push({ ...moves[i], turns: -moves[i].turns });
  return moves;
}

function newCell(hero) {
  return {
    model: identity(),
    logo: -1,
    uvScaleX: 1,
    uvScaleY: 1,
    uvOffsetX: 0,
    uvOffsetY: 0,
    brightness: 1,
    opacity: 1,
    frame: 0,
    hero,
    edge: false,
  };
}

/**
 * BuildLayout. `columns` and `rows` are the operator's grid, never the
 * virtual one -- the plugin derives that here, once, and so does this.
 * `o` carries LayoutOptions, every float already a float.
 */
export function buildLayout(schedule, logos, columns, rows, timeSeconds, o) {
  const out = { cells: [], view: identity(), projection: identity() };
  if (schedule.steps.length === 0 || logos.length === 0) return out;

  columns = Math.max(columns, 1);
  rows = Math.max(rows, 1);
  timeSeconds = f(timeSeconds);
  const seed = o.seed >>> 0;

  const stepCount = schedule.steps.length;
  const t = f(timeSeconds * o.speed);

  // ---- camera ----------------------------------------------------------
  if (o.projection === PROJECTION.Perspective || o.mode === MODE.Rubik) {
    const radius = o.mode === MODE.Rubik ? f(0.86602540) : 0.5;
    const dist = f(f(radius / f(Math.tan(f(o.fov * 0.5)))) * f(1.15));
    const eye = {
      x: f(f(dist * f(Math.sin(o.camYaw))) * f(Math.cos(o.camPitch))),
      y: f(dist * f(Math.sin(o.camPitch))),
      z: f(f(dist * f(Math.cos(o.camYaw))) * f(Math.cos(o.camPitch))),
    };
    out.view = lookAt(eye, { x: 0, y: 0, z: 0 }, { x: 0, y: 1, z: 0 });
    out.projection = perspective(o.fov, o.outputAspect, f(0.05), 100);
  } else {
    const halfW = f(o.outputAspect * 0.5);
    out.view = identity();
    out.projection = ortho(-halfW, halfW, -0.5, 0.5, -10, 10);
  }

  if (o.gridRot !== 0) out.view = mul(out.view, rotateZ(f(o.gridRot * timeSeconds)));

  const cube = o.mode === MODE.Rubik;

  const n = cubeSize(columns, rows);
  const [gridColumns, gridRows] = fillGrid(o.mode, columns, rows);

  const wallH = 1;
  const wallW = o.outputAspect;
  const cellW = f(wallW / gridColumns);
  const cellH = f(wallH / gridRows);
  const pad = o.cellPad;
  const cellAspect = cube ? 1 : f(cellW / cellH);

  const moves = cube ? moveSequence(seed, n) : [];

  const mt = f(t * 0.5);
  let moveIndex = 0;
  let moveFrac = 0;
  if (cube) {
    moveIndex = Math.floor(mt) % kMoveCount;
    moveFrac = fract(mt);
    if (moveIndex < 0) moveIndex += kMoveCount;
  }

  let globalStep = 0;
  if (stepCount > 1) {
    globalStep = cube
      ? Math.floor(f(mt / kMoveCount)) % stepCount
      : Math.floor(f(t * 0.25)) % stepCount;
    if (globalStep < 0) globalStep += stepCount;
  }

  let walkC = 0;
  let walkR = 0;
  if (o.walk !== WALK.Off) {
    const steps = Math.floor(f(timeSeconds * o.walkRate));
    if (o.walk === WALK.Left) walkC = steps;
    else if (o.walk === WALK.Right) walkC = -steps;
    else if (o.walk === WALK.Up) walkR = steps;
    else if (o.walk === WALK.Down) walkR = -steps;
  }

  const step = schedule.steps[globalStep];
  const ghosts = [];

  for (const p of step.cells) {
    if (p.colSpan === 0) continue;

    const c = newCell(p.hero);

    // ---- which logo ---------------------------------------------------
    let logo = p.logo;
    let cellStep = globalStep;

    if (o.walk !== WALK.Off && !p.hero) {
      const sc = (((p.col + walkC) % gridColumns) + gridColumns) % gridColumns;
      const sr = (((p.row + walkR) % gridRows) + gridRows) % gridRows;
      for (const q of step.cells) {
        if (q.col === sc && q.row === sr && q.colSpan !== 0) {
          logo = q.logo;
          break;
        }
      }
    }

    let fadeOpacity = 1;
    if (o.fade > 0 && !p.hero) {
      const phase = hash01(p.row * gridColumns + p.col, (seed ^ 0x51ed270b) >>> 0);
      const u = f(f(t * f(0.3)) + phase);
      cellStep = Math.floor(u) % stepCount;
      if (cellStep < 0) cellStep += stepCount;

      const fr = fract(u);
      const v = fr < f(0.15) ? f(fr / f(0.15)) : (fr > f(0.85) ? f(f(1 - fr) / f(0.15)) : 1);
      fadeOpacity = f(1 - f(o.fade * f(1 - v)));

      for (const q of schedule.steps[cellStep].cells) {
        if (q.col === p.col && q.row === p.row && q.colSpan !== 0) {
          logo = q.logo;
          break;
        }
      }
    }

    if (logo < 0 || logo >= logos.length) continue;
    c.logo = logo;

    // ---- brightness and opacity ---------------------------------------
    c.opacity = fadeOpacity;
    if (o.twinkle > 0 && !p.hero) {
      const key = p.row * gridColumns + p.col;
      const phase = hash01(key, (seed ^ 0x2545f491) >>> 0);
      const rate = f(f(0.6) + f(hash01(key, (seed ^ 0x7feb352d) >>> 0) * f(0.8)));
      const s = f(0.5 + f(0.5 * f(Math.sin(f(f(f(f(t * rate) + phase) * 2) * kPi)))));
      c.brightness = f(1 - f(o.twinkle * f(1 - s)));
    }

    // ---- where it is ---------------------------------------------------
    const spanW = p.colSpan;
    const spanH = p.rowSpan;

    // The flat wall's cell centre and size, kept for the wrap-around copies
    // made once the cell is finished.
    let cx = 0;
    let cy = 0;
    let scaleX = 0;
    let scaleY = 0;

    if (cube) {
      const face = Math.trunc(p.row / n);
      const faceRow = p.row % n;
      const faceCol = p.col;

      const cs = f(1 / n);
      const x = f(f(f(faceCol + f(0.5 * spanW)) * cs) - 0.5);
      const y = f(0.5 - f(f(faceRow + f(0.5 * spanH)) * cs));

      const faceRot = faceRotation(face);
      const base = mul(mul(faceRot, translate(x, y, 0.5)),
        scale(f(f(cs * spanW) * f(1 - pad)), f(f(cs * spanH) * f(1 - pad)), 1));

      // Slice membership by the centre of the placement's FIRST cell: a
      // two-cell wordmark's own centre is on a slice boundary, and the
      // plugin's arm64 and x86_64 builds once rounded it to different slices.
      const sx = f(f(f(faceCol + 0.5) * cs) - 0.5);
      const sy = f(0.5 - f(f(faceRow + 0.5) * cs));
      let pos = transformPoint(faceRot, { x: sx, y: sy, z: 0.5 });
      let accum = identity();

      for (let i = 0; i < moveIndex; i += 1) {
        const m = moves[i];
        if (layerOf(pos, m.axis, n) !== m.layer) continue;
        const r = rotateAxis(axisVector(m.axis), f(m.turns * kHalfPi));
        accum = mul(r, accum);
        pos = transformPoint(r, pos);
      }

      const active = moves[moveIndex];
      if (layerOf(pos, active.axis, n) === active.layer) {
        accum = mul(rotateAxis(axisVector(active.axis), f(f(active.turns * kHalfPi) * easeInOut(moveFrac))), accum);
      }

      c.model = mul(accum, base);
    } else {
      cx = f(f(f(p.col + f(0.5 * spanW)) * cellW) - f(wallW * 0.5));
      cy = f(f(wallH * 0.5) - f(f(p.row + f(0.5 * spanH)) * cellH));

      if (o.mode === MODE.SlotMachine && !p.hero) {
        const key = p.col;
        const cycle = 6;
        const u = fract(f(t / cycle));
        const stop = f(f(0.45) + f(hash01(key, (seed ^ 0x68e31da4) >>> 0) * f(0.45)));
        const trips = gridRows * (2 + Math.trunc(f(hash01(key, (seed ^ 0xb5297a4d) >>> 0) * 4)));

        const travel = f(trips * easeOut(Math.min(f(u / stop), 1)));
        const shift = fract(travel);
        const whole = Math.floor(travel);

        cy = f(cy - f(shift * cellH));
        if (cy < f(f(-wallH * 0.5) - f(cellH * 0.5))) cy = f(cy + f(wallH + cellH));

        const sr = (((p.row + whole) % gridRows) + gridRows) % gridRows;
        for (const q of step.cells) {
          if (q.col === p.col && q.row === sr && q.colSpan !== 0) {
            c.logo = q.logo;
            break;
          }
        }
      }

      if (o.scrollX !== 0) cx = wrapCentred(f(cx + f(f(o.scrollX * cellW) * timeSeconds)), wallW);
      if (o.scrollY !== 0) cy = wrapCentred(f(cy + f(f(o.scrollY * cellH) * timeSeconds)), wallH);

      scaleX = f(f(cellW * spanW) * f(1 - pad));
      scaleY = f(f(cellH * spanH) * f(1 - pad));

      c.model = mul(translate(cx, cy, 0), scale(scaleX, scaleY, 1));
    }

    // ---- which frame, for an animated logo -----------------------------
    // After the slot machine, which can hand the cell a different logo. A
    // reel can only have picked up a logo that exists, so the guard never
    // fires; it is here so a mistake in the port throws nothing.
    const logoData = logos[c.logo];
    if (logoData) c.frame = frameAt(logoData, timeSeconds);

    // ---- fit ------------------------------------------------------------
    const thisCellAspect = f(cellAspect * f(spanW / spanH));
    if (logoData) fitUv(o.fit, thisCellAspect, logoData, c);

    // ---- does the edge treatment reach this cell ------------------------
    switch (o.edgeScope) {
      case EDGE_SCOPE.EveryCell: c.edge = o.edge !== EDGE.Off; break;
      case EDGE_SCOPE.HeroOnly: c.edge = o.edge !== EDGE.Off && p.hero; break;
      case EDGE_SCOPE.ExceptHero: c.edge = o.edge !== EDGE.Off && !p.hero; break;
      default: c.edge = false; break;
    }

    out.cells.push(c);

    // ---- wrap-around copies, for whole-grid scroll ----------------------
    // Copied from the FINISHED cell, as the plugin now copies them. Up to its
    // v0.2.3 they were copied before the fit, the frame and the edge flag were
    // set, so the mark entering at each scrolling edge was the whole layer
    // stretched over the cell, with no edge treatment.
    if (!cube && (o.scrollX !== 0 || o.scrollY !== 0)) {
      for (let gy = -1; gy <= 1; gy += 1) {
        for (let gx = -1; gx <= 1; gx += 1) {
          if (gx === 0 && gy === 0) continue;
          if (gx !== 0 && o.scrollX === 0) continue;
          if (gy !== 0 && o.scrollY === 0) continue;

          const gcx = f(cx + f(gx * wallW));
          const gcy = f(cy + f(gy * wallH));

          if (f(Math.abs(gcx) - f(scaleX * 0.5)) > f(wallW * 0.5)) continue;
          if (f(Math.abs(gcy) - f(scaleY * 0.5)) > f(wallH * 0.5)) continue;

          ghosts.push({ ...c, model: mul(translate(gcx, gcy, 0), scale(scaleX, scaleY, 1)) });
        }
      }
    }
  }

  out.cells.push(...ghosts);
  return out;
}

//===========================================================================
// Library.cpp -- the folder rules, the SVG scan, the trim
//===========================================================================

export const EXTENSIONS = ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'tga', 'psd', 'svg'];

export function lowerExtension(path) {
  const dot = path.lastIndexOf('.');
  if (dot < 0) return '';
  return path.slice(dot + 1).replace(/[A-Z]/g, (ch) => ch.toLowerCase());
}

export const isSupported = (path) => EXTENSIONS.includes(lowerExtension(path));

const utf8 = new TextEncoder();
function byteCompare(a, b) {
  const x = utf8.encode(a);
  const y = utf8.encode(b);
  const n = Math.min(x.length, y.length);
  for (let i = 0; i < n; i += 1) if (x[i] !== y[i]) return x[i] - y[i];
  return x.length - y.length;
}

/**
 * ScanFolder, given the folder's files rather than a path: the supported
 * extensions, no dotfiles (a Mac's ._Foo.png), sorted by name as std::sort
 * sorts std::string -- byte by byte -- because the order IS a logo's identity:
 * the hero index and the atlas layer are both positions in it.
 */
export function scanFolder(entries) {
  return entries
    .filter((e) => isSupported(e.name) && !e.name.startsWith('.'))
    .sort((a, b) => byteCompare(a.name, b.name));
}

function countElement(text, name) {
  const open = `<${name}`.toLowerCase();
  const lower = text.replace(/[A-Z]/g, (ch) => ch.toLowerCase());
  let count = 0;
  let from = 0;
  for (;;) {
    const found = lower.indexOf(open, from);
    if (found < 0) break;
    const after = found + open.length;
    if (after >= text.length) break;
    const c = text[after];
    if (c === '>' || c === '/' || c === ' ' || c === '\t' || c === '\n' || c === '\r') count += 1;
    from = after;
  }
  return count;
}

/** ScanUnsupported: the elements nanosvg silently drops, counted in the source. */
export function scanUnsupported(text) {
  const u = {
    text: countElement(text, 'text') + countElement(text, 'tspan'),
    image: countElement(text, 'image'),
    use: countElement(text, 'use'),
    clips: countElement(text, 'clipPath') + countElement(text, 'mask'),
    filters: countElement(text, 'filter'),
    pattern: countElement(text, 'pattern'),
  };
  u.any = Boolean(u.text || u.image || u.use || u.clips || u.filters || u.pattern);
  return u;
}

/**
 * TrimBounds: the box of pixels with alpha of at least 8/255 in ANY of the
 * frames (each an RGBA array), or null. Every frame, as the plugin now measures
 * it (up to v0.2.3 it measured frame 0), so an animated GIF's trim holds all of
 * it.
 */
export function trimBounds(frames, w, h) {
  const kAlphaFloor = 8;
  let minX = w;
  let minY = h;
  let maxX = -1;
  let maxY = -1;
  for (const rgba of frames) {
    for (let y = 0; y < h; y += 1) {
      for (let x = 0; x < w; x += 1) {
        if (rgba[(y * w + x) * 4 + 3] < kAlphaFloor) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }
  if (maxX < minX || maxY < minY) return null;
  return { x0: f(minX / w), y0: f(minY / h), x1: f((maxX + 1) / w), y1: f((maxY + 1) / h) };
}

/**
 * ComputeTrim, onto an image record {width, height, aspect, frames}, where
 * `frames` is [{rgba, delayMs}], one entry for a still.
 */
export function computeTrim(img) {
  const b = trimBounds(img.frames.map((fr) => fr.rgba), img.width, img.height) ?? { x0: 0, y0: 0, x1: 1, y1: 1 };
  img.trimX0 = b.x0;
  img.trimY0 = b.y0;
  img.trimX1 = b.x1;
  img.trimY1 = b.y1;
  const tw = f(f(img.trimX1 - img.trimX0) * img.width);
  const th = f(f(img.trimY1 - img.trimY0) * img.height);
  img.trimmedAspect = th > 0 ? f(tw / th) : img.aspect;
  return img;
}

/** RasteriseSvg's output size: the longer side at `rasterPx`. */
export function svgRasterSize(svgWidth, svgHeight, rasterPx) {
  rasterPx = Math.min(Math.max(rasterPx, 1), 4096);
  const longSide = Math.max(svgWidth, svgHeight);
  const s = f(rasterPx / longSide);
  return {
    width: Math.max(1, Math.trunc(f(f(svgWidth * s) + 0.5))),
    height: Math.max(1, Math.trunc(f(f(svgHeight * s) + 0.5))),
  };
}

/** Decode's note for an SVG, from its scan. Empty when nothing is dropped. */
export function svgNote(name, dropped) {
  if (dropped.text > 0) {
    return `${name}: ${dropped.text} live text element(s) WILL NOT DRAW -- convert text to outlines`;
  }
  if (dropped.any) return `${name}: some elements dropped (image/use/clip/filter/pattern)`;
  return '';
}

/** The Loader's note: counts, the live-text warning, then every complaint. */
export function loaderNote(fileCount, loadedCount, decoded) {
  let problems = '';
  let textWarnings = 0;
  for (const img of decoded) {
    if (!img.note) continue;
    if (img.dropped?.text > 0) textWarnings += 1;
    if (problems.length < 600) problems += (problems ? '; ' : '') + img.note;
  }
  let note = fileCount === 0 ? 'no supported images in that folder' : `${loadedCount} of ${fileCount} loaded`;
  if (textWarnings > 0) note += `; ${textWarnings} SVG(s) have live text that will not draw`;
  if (problems) note += ` -- ${problems}`;
  return note;
}

//===========================================================================
// Atlas.cpp and AtlasPlan.cpp -- every logo, a layer a frame, in a texture array
//===========================================================================

export function chooseLayerSize(cellPixels) {
  let size = 64;
  while (size < cellPixels && size < 2048) size *= 2;
  return size;
}

/** PlaceIn: where one image sits in a square layer, keeping its aspect. */
export function placeIn(img, layerSize) {
  let dw = layerSize;
  let dh = layerSize;
  if (img.aspect >= 1) dh = Math.max(1, Math.trunc(f(f(layerSize / img.aspect) + 0.5)));
  else dw = Math.max(1, Math.trunc(f(f(layerSize * img.aspect) + 0.5)));
  dw = Math.min(dw, layerSize);
  dh = Math.min(dh, layerSize);
  return { dx: Math.trunc((layerSize - dw) / 2), dy: Math.trunc((layerSize - dh) / 2), dw, dh };
}

/**
 * AtlasPlan.h's kFrameBudgetBytes: the bytes of layer that animation frames
 * past the first may claim between them.
 */
export const FRAME_BUDGET_BYTES = 256 * 1024 * 1024;

/**
 * PlanAtlas: the layer size, what goes in each layer, and every logo's
 * geometry in layer space -- the arithmetic between Atlas::Begin and Step. No
 * GL; plugin.js allocates the array. An animated logo is one layer per frame,
 * straight after its first, while the GPU's layer limit and the frame budget
 * last, handed out in folder order; one that does not fit is drawn still.
 */
export function planAtlas(images, maxSize, maxLayers) {
  const plan = { layerSize: 0, logos: [], layers: [], animated: 0, stilled: 0, note: '' };
  if (images.length === 0) return plan;
  if (maxSize <= 0) maxSize = 2048;
  if (maxLayers <= 0) maxLayers = 256;

  let longest = 0;
  for (const i of images) longest = Math.max(longest, Math.max(i.width, i.height));
  plan.layerSize = Math.min(chooseLayerSize(longest), maxSize);

  let count = images.length;
  if (count > maxLayers) {
    plan.note = `folder has ${count} images but this GPU allows ${maxLayers} layers; the rest are ignored`;
    count = maxLayers;
  }

  const perLayer = plan.layerSize * plan.layerSize * 4;
  let total = count;
  let spent = 0;
  const plays = new Array(count).fill(false);
  for (let i = 0; i < count; i += 1) {
    const img = images[i];
    if (img.frames.length < 2) continue;
    const extra = img.frames.length - 1;
    const bytes = extra * perLayer;
    if (total + extra <= maxLayers && spent + bytes <= FRAME_BUDGET_BYTES) {
      plays[i] = true;
      total += extra;
      spent += bytes;
      plan.animated += 1;
    } else {
      plan.stilled += 1;
    }
  }

  for (let i = 0; i < count; i += 1) {
    const img = images[i];
    const p = placeIn(img, plan.layerSize);
    const fx = f(p.dw / plan.layerSize);
    const fy = f(p.dh / plan.layerSize);
    const ox = f(p.dx / plan.layerSize);
    const oy = f(p.dy / plan.layerSize);
    const logo = {
      index: i,
      name: img.name,
      aspect: img.aspect,
      imageX0: ox,
      imageX1: f(ox + fx),
      imageY0: oy,
      imageY1: f(oy + fy),
      trimX0: f(ox + f(img.trimX0 * fx)),
      trimX1: f(ox + f(img.trimX1 * fx)),
      trimY0: f(oy + f(img.trimY0 * fy)),
      trimY1: f(oy + f(img.trimY1 * fy)),
      trimmedAspect: img.trimmedAspect,
      layer: plan.layers.length,
      frameMs: [],
    };
    const frames = plays[i] ? img.frames.length : 1;
    for (let k = 0; k < frames; k += 1) {
      plan.layers.push({ image: i, frame: k });
      if (frames > 1) logo.frameMs.push(img.frames[k].delayMs);
    }
    plan.logos.push(logo);
  }

  if (!plan.note) {
    plan.note = `${count} logos at ${plan.layerSize}px`;
    if (plan.animated > 0) plan.note += `, ${plan.animated} animated (${plan.layers.length} layers)`;
  }
  if (plan.stilled > 0) {
    const one = plan.stilled === 1;
    plan.note += `; ${plan.stilled} animated GIF${one ? ' is' : 's are'} drawn still: the atlas has no room for ${one ? 'its' : 'their'} frames`;
  }
  return plan;
}

/** Blit: nearest-neighbour, one image into its rectangle of a layer. */
export function blit(src, sw, sh, dst, layerSize, dx, dy, dw, dh) {
  for (let y = 0; y < dh; y += 1) {
    const sy = Math.min(sh - 1, Math.trunc((y * sh) / Math.max(dh, 1)));
    for (let x = 0; x < dw; x += 1) {
      const sx = Math.min(sw - 1, Math.trunc((x * sw) / Math.max(dw, 1)));
      const s = (sy * sw + sx) * 4;
      const d = ((dy + y) * layerSize + (dx + x)) * 4;
      dst[d] = src[s];
      dst[d + 1] = src[s + 1];
      dst[d + 2] = src[s + 2];
      dst[d + 3] = src[s + 3];
    }
  }
}
