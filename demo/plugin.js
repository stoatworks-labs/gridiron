/**
 * Gridiron — the browser demo.
 *
 * What runs here, and what it is:
 *
 * **The shaders are the plugin's.** shaders.js is source/Shaders.cpp's four
 * bodies, spliced by demo/tools/check_shaders.py and compared character for
 * character by tools/verify.sh. They run in the same two passes as
 * Render.cpp's Renderer::Draw: one instanced draw of every cell into a
 * framebuffer of gridiron's own with a depth buffer, then a full-screen
 * composite (which carries the whole-wall edge treatment) into the output.
 *
 * **The CPU half is a port** (port.js): the folder rules, the trim, the
 * atlas geometry and its nearest-neighbour resample, the arrangement, the
 * layout and the cube, the hash, the matrices and Controls.h's mappings. The
 * wiring in this file is GridironPlugin::ProcessOpenGL's: the same caches on
 * the same keys, the same order, the same forced Twinkle and Fade.
 *
 * **The decoding is the browser's, not the plugin's.** stb_image and nanosvg
 * are not here. PNG, JPEG, GIF and BMP are decoded by the browser with colour
 * management and orientation turned off, which is what stb does; TGA and PSD
 * have no browser decoder. An SVG is drawn by the browser at the plugin's 512
 * px, which honours things nanosvg drops — except live text, which this page
 * removes first, because nanosvg draws none and that is the failure the plugin
 * warns about loudest.
 *
 * **The folder.** The plugin's Logos parameter is a file picker that reads
 * the directory around the file picked. A browser cannot see a picked file's
 * siblings, so the page asks for the folder itself (or several files, or a
 * drop) and applies the plugin's folder rules to what it is given.
 *
 * **The clock** is the kit's, in seconds, handed straight to the layout; the
 * plugin's seconds-or-milliseconds vote (Clock.cpp) is not ported.
 *
 * Nothing leaves the machine: a visitor's logos are decoded in the page.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer } from './vendor/gl.js';
import * as S from './shaders.js';
import {
  f, toOption, MODE, HERO,
  CellPad, GridWidth, Speed, Scroll, GridRot, WalkRate, Fov, CamAngle, EdgeThreshold,
  buildSchedule, buildLayout, fillGrid, mul,
  lowerExtension, scanFolder, scanUnsupported, computeTrim, svgRasterSize, svgNote, loaderNote,
  atlasGeometry, placeIn, blit,
} from './port.js';

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

/**
 * The demo pack: demo/logos, the folder the release puts in the download as
 * "demo logos". Listed because a static host has no directory listing;
 * demo/tools/check_pack.py fails verify.sh if this and the folder disagree.
 */
const DEMO_PACK = [
  'app-amber.png',
  'app-aquilon-vpu-map.png',
  'app-asciify.png',
  'app-atem-overseer.png',
  'resolume-arena.svg',
  'resolume-avenue.svg',
  'resolume-wire.svg',
  'sponsor-bit-depth-charge.png',
  'sponsor-codec-red-padded.png',
  'sponsor-colour-bars-and-grill.png',
  'sponsor-genlock-and-key.png',
  'sponsor-gobo-getters.png',
  'sponsor-haze-and-confused.png',
  'sponsor-par-can-do.png',
  'sponsor-safe-area-51-padded.png',
  'sponsor-truss-issues.png',
  'stoatworks-lockup.png',
  'stoatworks-mark.png',
  'stoatworks-stoat.svg',
];

/** What PollLoader hands Loader::Start: vectors rasterised at 512 px. */
const RASTER_PX = 512;

/** Gridiron.cpp's kUploadPixelBudget: four megapixels of layers a frame. */
const UPLOAD_PIXEL_BUDGET = 4 * 1024 * 1024;

//===========================================================================
// Decoding — the browser's, standing in for stb_image and nanosvg
//===========================================================================

/** The straight-alpha RGBA8 of a bitmap, row 0 at the top, as stb gives it. */
function pixelsOf(gl, source, width, height) {
  const max = gl.getParameter(gl.MAX_TEXTURE_SIZE);
  if (width <= max && height <= max) {
    const tex = gl.createTexture();
    const fbo = gl.createFramebuffer();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, source);
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    const out = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, out);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.deleteFramebuffer(fbo);
    gl.deleteTexture(tex);
    return out;
  }
  // Bigger than a texture may be: through a 2D canvas instead, which costs
  // the low-alpha precision a canvas's premultiplied storage loses.
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext('2d');
  ctx.drawImage(source, 0, 0);
  return new Uint8Array(ctx.getImageData(0, 0, width, height).data.buffer);
}

/** A CSS/SVG length in px at 96 dpi, as nanosvg's parseCoordinate reads one; 0 if it cannot. */
function svgLength(value) {
  const m = /^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)\s*(px|pt|pc|mm|cm|in)?\s*$/i.exec(value ?? '');
  if (!m) return 0;
  const n = Number(m[1]);
  const unit = (m[2] ?? 'px').toLowerCase();
  const per = { px: 1, pt: 96 / 72, pc: 96 / 6, mm: 96 / 25.4, cm: 96 / 2.54, in: 96 }[unit];
  return f(n * per);
}

async function decodeSvg(gl, entry, rasterPx) {
  const img = { name: entry.name, width: 0, height: 0, aspect: 1, rgba: null, note: '', vector: true };
  let text;
  try {
    text = await entry.blob.text();
  } catch {
    img.note = `${img.name}: cannot read`;
    return img;
  }

  img.dropped = scanUnsupported(text);

  const doc = new DOMParser().parseFromString(text, 'image/svg+xml');
  const root = doc.documentElement;
  if (doc.querySelector('parsererror') || root?.localName !== 'svg') {
    img.note = `${img.name}: not a usable SVG`;
    return img;
  }

  // The document's size: width and height, or the viewBox where they are
  // missing or relative, as nanosvg settles it.
  const vb = (root.getAttribute('viewBox') ?? '').trim().split(/[\s,]+/).map(Number);
  const hasViewBox = vb.length === 4 && vb.every(Number.isFinite) && vb[2] > 0 && vb[3] > 0;
  const svgW = svgLength(root.getAttribute('width')) || (hasViewBox ? f(vb[2]) : 0);
  const svgH = svgLength(root.getAttribute('height')) || (hasViewBox ? f(vb[3]) : 0);
  if (!(svgW > 0) || !(svgH > 0)) {
    img.note = `${img.name}: not a usable SVG`;
    return img;
  }

  // nanosvg draws no <text> at all. Taken out before the browser draws, so the
  // cell shows what the plugin's would: nothing where the live text was.
  for (const node of [...root.getElementsByTagNameNS('*', 'text')]) node.remove();

  const { width, height } = svgRasterSize(svgW, svgH, rasterPx);
  if (!hasViewBox) root.setAttribute('viewBox', `0 0 ${svgW} ${svgH}`);
  root.setAttribute('width', String(width));
  root.setAttribute('height', String(height));

  const url = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(doc)], { type: 'image/svg+xml' }));
  try {
    const el = new Image();
    el.src = url;
    await el.decode();
    const canvas = new OffscreenCanvas(width, height);
    const ctx = canvas.getContext('2d');
    ctx.drawImage(el, 0, 0, width, height);
    img.rgba = new Uint8Array(ctx.getImageData(0, 0, width, height).data.buffer);
  } catch {
    img.note = `${img.name}: rasteriser failed`;
    return img;
  } finally {
    URL.revokeObjectURL(url);
  }

  img.width = width;
  img.height = height;
  img.aspect = f(svgW / svgH);
  computeTrim(img);
  img.note = svgNote(img.name, img.dropped);
  return img;
}

async function decodeRaster(gl, entry) {
  const img = { name: entry.name, width: 0, height: 0, aspect: 1, rgba: null, note: '', vector: false };
  const ext = lowerExtension(entry.name);
  if (ext === 'tga' || ext === 'psd') {
    img.note = `${img.name}: no .${ext} decoder in this browser (the plugin's stb_image has one)`;
    return img;
  }
  let bitmap;
  try {
    bitmap = await createImageBitmap(entry.blob, {
      premultiplyAlpha: 'none',
      colorSpaceConversion: 'none',
      imageOrientation: 'none',
    });
  } catch {
    img.note = ext === 'gif' ? `${img.name}: GIF would not decode` : `${img.name}: would not decode`;
    return img;
  }
  img.width = bitmap.width;
  img.height = bitmap.height;
  img.rgba = pixelsOf(gl, bitmap, bitmap.width, bitmap.height);
  bitmap.close();
  img.aspect = f(img.width / img.height);
  computeTrim(img);
  return img;
}

function decode(gl, entry, rasterPx) {
  return lowerExtension(entry.name) === 'svg' ? decodeSvg(gl, entry, rasterPx) : decodeRaster(gl, entry);
}

const usable = (img) => img.width > 0 && img.height > 0 && img.rgba;

/**
 * Library.cpp's Loader, off the frame: Start() cancels and replaces, Ready()
 * goes true once, Take() hands the images over and clears it.
 */
class Loader {
  constructor(gl) {
    this.gl = gl;
    this.generation = 0;
    this.ready = false;
    this.images = [];
    this.note = '';
    this.running = false;
    this.onReady = null;
  }

  start(entries, rasterPx) {
    const generation = ++this.generation;
    this.ready = false;
    this.running = true;
    (async () => {
      const files = scanFolder(entries);
      const decoded = [];
      for (const file of files) {
        if (generation !== this.generation) return;
        decoded.push(await decode(this.gl, file, rasterPx));
      }
      if (generation !== this.generation) return;
      const loaded = decoded.filter(usable);
      this.images = loaded;
      this.note = loaderNote(files.length, loaded.length, decoded);
      this.running = false;
      this.ready = true;
      this.onReady?.();
    })();
  }

  take() {
    this.ready = false;
    const out = this.images;
    this.images = [];
    return out;
  }
}

//===========================================================================
// Atlas.cpp on WebGL2 — one GL_TEXTURE_2D_ARRAY layer per logo
//===========================================================================

class Atlas {
  constructor(gl) {
    this.gl = gl;
    this.texture = null;
    this.layers = 0;
    this.layerSize = 0;
    this.uploaded = 0;
    this.logos = [];
    this.scratch = null;
    this.note = '';
  }

  release() {
    if (this.texture) this.gl.deleteTexture(this.texture);
    this.texture = null;
    this.layers = 0;
    this.layerSize = 0;
    this.uploaded = 0;
    this.logos = [];
    this.scratch = null;
  }

  valid() { return Boolean(this.texture) && this.layers > 0; }

  uploading() { return Boolean(this.texture) && this.uploaded < this.layers; }

  begin(images) {
    const gl = this.gl;
    this.release();
    this.note = '';
    if (images.length === 0) {
      this.note = 'nothing to upload';
      return false;
    }

    const geo = atlasGeometry(images, gl.getParameter(gl.MAX_TEXTURE_SIZE), gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS));

    while (gl.getError() !== gl.NO_ERROR) { /* drained, as Atlas::Begin does */ }

    this.texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, this.texture);
    gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA8, geo.layerSize, geo.layerSize, geo.layers, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    const error = gl.getError();
    if (error !== gl.NO_ERROR) {
      gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);
      this.release();
      const mb = Math.floor((geo.layerSize * geo.layerSize * 4 * geo.layers) / (1 << 20));
      // "0x" then the code in decimal: Atlas.cpp's std::to_string, ported as it is.
      this.note = `the GPU refused a ${geo.layerSize}px array of ${geo.layers} layers (${mb} MB, GL error 0x${error})`;
      return false;
    }
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);

    this.logos = geo.logos;
    this.layers = geo.layers;
    this.layerSize = geo.layerSize;
    this.uploaded = 0;
    this.scratch = new Uint8Array(geo.layerSize * geo.layerSize * 4);
    this.note = geo.note;
    return true;
  }

  step(images, pixelBudget) {
    const gl = this.gl;
    if (!this.texture || this.uploaded >= this.layers) return true;
    if (images.length < this.layers) {
      this.note = 'the image list changed under the upload';
      this.release();
      return true;
    }

    const perLayer = this.layerSize * this.layerSize;
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, this.texture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);

    let spent = 0;
    while (this.uploaded < this.layers && (spent === 0 || spent + perLayer <= pixelBudget)) {
      const i = this.uploaded;
      const img = images[i];
      this.scratch.fill(0);
      const p = placeIn(img, this.layerSize);
      blit(img.rgba, img.width, img.height, this.scratch, this.layerSize, p.dx, p.dy, p.dw, p.dh);
      gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, i, this.layerSize, this.layerSize, 1, gl.RGBA, gl.UNSIGNED_BYTE, this.scratch);
      this.uploaded += 1;
      spent += perLayer;
    }

    const done = this.uploaded >= this.layers;
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, done ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    if (done) {
      gl.generateMipmap(gl.TEXTURE_2D_ARRAY);
      this.scratch = null;
    }
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);
    return done;
  }
}

//===========================================================================
// The parameters, in ParamId order, with the constructor's names, groups,
// element lists and defaults
//===========================================================================

const deg = (rad) => `${((rad * 180) / Math.PI).toFixed(1)}°`;
const signed = (n, d = 2) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;

/** FF_TYPE_INTEGER: a number field here, in the declared range (see below). */
const int = (id, name, group, def, min, max, hint) =>
  ({ id, name, group, type: 'text', default: String(def), int: { def, min, max }, hint });
const std = (id, name, group, def, display, hint) => ({ id, name, group, type: 'standard', default: def, display, hint });
const bool = (id, name, group, def, hint) => ({ id, name, group, type: 'boolean', default: def ? 1 : 0, hint });
const opt = (id, name, group, elements, hint) => ({ id, name, group, type: 'option', default: 0, elements, hint });
const rgb = (prefix, label, group, def) => ['Red', 'Green', 'Blue'].map((c, i) => ({
  id: `${prefix}${c[0]}`, name: `${label} ${c}`, group, type: 'colour', default: def[i],
}));

const PARAMS = [
  // PT_FOLDER_FILE (Logos, FF_TYPE_FILE) is the row this page adds above these.
  { id: 'folderOverride', name: 'Folder Override', group: 'Content', type: 'text', default: '',
    placeholder: 'a path on disk — a browser cannot read one',
    hint: 'FF_TYPE_TEXT in the plugin: a literal folder path, for anyone driving it from a script. It wins over Logos when set. A web page cannot open a path on your disk, so here it is shown and does nothing.' },
  bool('reload', 'Reload', 'Content', false,
    'FF_TYPE_EVENT: read the folder again. Here it re-reads the files you gave the page.'),

  int('columns', 'Columns', 'Grid', 6, 1, 32, 'FF_TYPE_INTEGER, 1–32. A real integer in the plugin: a slider that lands on 5 or 7 either side of the 6 the designer asked for is the wrong wall.'),
  int('rows', 'Rows', 'Grid', 4, 1, 32, 'FF_TYPE_INTEGER, 1–32.'),
  opt('fit', 'Fit', 'Grid', ['Fit', 'Fill', 'Trim margins', 'Stretch'],
    'What a logo does inside its cell. Trim margins measures where the opaque pixels are, discards the exported margin, then letterboxes, so a mixed pack reads at one optical size.'),
  std('cellPad', 'Cell Padding', 'Grid', 0.12, (v) => `${(CellPad(v) * 100).toFixed(1)}%`,
    'Padding as a share of the cell, 0 to 45%: a half would leave the cell no interior.'),
  bool('showGrid', 'Show Grid', 'Grid', false, 'A border drawn inside the cell shader. No extra geometry.'),
  std('gridWidth', 'Grid Width', 'Grid', 0.1, (v) => GridWidth(v).toFixed(3), 'Grid line width, as a fraction of the cell: 0.002 to 0.082.'),
  ...rgb('grid', 'Grid', 'Grid', [1, 1, 1]),

  int('seed', 'Seed', 'Fill', 1, 0, 9999, 'FF_TYPE_INTEGER, 0–9999. Fixes the arrangement: the same folder and the same seed always give the same wall.'),
  bool('varyNeighbours', 'Vary Neighbours', 'Fill', true, 'No logo touches a copy of itself, across or down.'),
  bool('respectAspect', 'Respect Aspect', 'Fill', true, 'A wordmark wider than 2.2:1 (trimmed) takes the cell to its right rather than being letterboxed into one.'),
  bool('equalAirtime', 'Equal Airtime', 'Fill', true, 'Every logo gets the same number of placements over a cycle; a wordmark across two cells counts once.'),

  opt('hero', 'Hero Block', 'Hero', ['Off', 'Top', 'Middle', 'Bottom'],
    'A merged block for the headline sponsor. Its cells leave the pool and its logo leaves the rotation. Ignored on the cube, which has no top.'),
  int('heroColumns', 'Hero Columns', 'Hero', 2, 1, 32, 'FF_TYPE_INTEGER, 1–32.'),
  int('heroRows', 'Hero Rows', 'Hero', 1, 1, 32, 'FF_TYPE_INTEGER, 1–32.'),
  int('heroLogo', 'Hero Logo', 'Hero', 0, 0, 255, 'FF_TYPE_INTEGER, 0–255: which logo, by position in the folder sorted by filename. 0 is the first file.'),

  opt('mode', 'Mode', 'Motion', ['Static', 'Slot Machine', 'Rubik', 'Twinkle', 'Fade Cycle'],
    'Static, reels that spin down onto whole cells, the wall on a cube turning a slice at a time, twinkling, or cells that fade and change logo while dark.'),
  std('speed', 'Speed', 'Motion', 0.5, (v) => `×${Speed(v).toFixed(2)}`, 'Master speed, cubed: the useful range is at the bottom.'),
  std('scrollX', 'Scroll X', 'Motion', 0.5, (v) => `${signed(Scroll(v))} cells/s`, 'Whole-grid scroll, wrapped into the wall.'),
  std('scrollY', 'Scroll Y', 'Motion', 0.5, (v) => `${signed(Scroll(v))} cells/s`, 'Whole-grid scroll, wrapped into the wall.'),
  std('gridRotation', 'Grid Rotation', 'Motion', 0.5, (v) => `${signed(GridRot(v))} rad/s`, 'Whole-grid rotation, in the view matrix.'),
  opt('walk', 'Logo Walk', 'Motion', ['Off', 'Up', 'Down', 'Left', 'Right'],
    'Logos moving through the grid while the cells stay put: content reassigned between cells, not a transform.'),
  std('walkRate', 'Walk Rate', 'Motion', 0.25, (v) => `${WalkRate(v).toFixed(2)} steps/s`, 'Cell steps per second.'),
  std('twinkle', 'Twinkle', 'Motion', 0, (v) => v.toFixed(2), 'Depth, 0 = off. Mode Twinkle turns it fully on when it is at 0.'),
  std('fade', 'Fade', 'Motion', 0, (v) => v.toFixed(2), 'Depth, 0 = off. Mode Fade Cycle turns it fully on when it is at 0.'),

  opt('projection', 'Projection', 'Camera', ['Flat', 'Perspective'], 'Orthographic, every cell coplanar, or a perspective camera. The cube is always perspective.'),
  std('fov', 'Field of View', 'Camera', 0.4, (v) => deg(Fov(v)), 'Vertical, 15° to 90°. Only consulted in perspective.'),
  std('camYaw', 'Camera Yaw', 'Camera', 0.56, (v) => deg(CamAngle(v)), 'Signed, ±180°.'),
  std('camPitch', 'Camera Pitch', 'Camera', 0.56, (v) => deg(f(CamAngle(v) * 0.5)), 'Signed; the plugin halves it, ±90°.'),

  opt('edge', 'Edge Treatment', 'Edge', ['Off', 'Burin (engraved)', 'Nib (inked)', 'Vectrix (traced)'],
    'The fleet’s three line-art looks: a dark engraved line, an inked contour in the mark’s own colour, or a bright traced line.'),
  opt('edgeScope', 'Edge Applies To', 'Edge', ['Whole wall', 'Every logo', 'Hero only', 'All but hero'],
    'Whole wall is a post pass on the finished wall. The other three run in the cell shader on one logo at a time, which stacking a plugin in Resolume cannot give you.'),
  std('edgeAmount', 'Edge Amount', 'Edge', 1.0, (v) => v.toFixed(2), 'How much of the treatment.'),
  std('edgeThreshold', 'Edge Threshold', 'Edge', 0.35, (v) => EdgeThreshold(v).toFixed(3), 'On the gradient magnitude; never zero, or every fragment is an edge.'),

  opt('cellFill', 'Cell Fill', 'Cell Fill', ['Off', 'Solid Colour', 'Cube Faces'],
    'Backs every cell with a colour so a logo with alpha does not make the wall, or the cube, see-through. Cube Faces picks the sticker colours from each face’s normal.'),
  ...rgb('fill', 'Fill', 'Cell Fill', [0.10, 0.10, 0.10]),
];

const INTS = new Map(PARAMS.filter((p) => p.int).map((p) => [p.id, p.int]));

/** An FF_TYPE_INTEGER's value, held to its declared range as the host holds it. */
function intOf(params, id) {
  const spec = INTS.get(id);
  const n = Number.parseInt(params.get(id), 10);
  if (!Number.isFinite(n)) return spec.def;
  return Math.min(spec.max, Math.max(spec.min, n));
}

//===========================================================================
// The plugin: GridironPlugin's wiring, around the ported parts
//===========================================================================

let instance = null;

function createRenderer(gl) {
  const wall = new Program(gl, S.WALL_VERTEX, S.WALL_FRAGMENT, 'wall', { attribs: {} });
  const composite = new Program(gl, S.COMPOSITE_VERTEX, S.COMPOSITE_FRAGMENT, 'composite', { attribs: {} });

  // Render.cpp's instance layout: four vec4s of matrix, the UV transform, and
  // layer/brightness/opacity/edge -- 24 floats, all divisor 1.
  const kFloatsPerInstance = 24;
  const vao = gl.createVertexArray();
  const instances = gl.createBuffer();
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, instances);
  for (let i = 0; i < 6; i += 1) {
    gl.enableVertexAttribArray(i);
    gl.vertexAttribPointer(i, 4, gl.FLOAT, false, kFloatsPerInstance * 4, i * 16);
    gl.vertexAttribDivisor(i, 1);
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  gl.bindVertexArray(null);
  let capacity = 0;

  // The composite draws a quad from gl_VertexID and reads no attributes.
  const emptyVao = gl.createVertexArray();

  // gridiron's own framebuffer: colour plus depth, which the cube needs.
  const target = new PassBuffer(gl, { depth: true });

  const loader = new Loader(gl);
  const atlas = new Atlas(gl);
  // A paused page draws only on request. The plugin's host calls it every
  // frame, so the folder lands and the layers arrive whether or not anything
  // moves; here the loader asks for the frame that picks it up.
  loader.onReady = () => mounted?.redraw();

  const st = {
    folder: null,          // { label, entries } -- what Logos points at
    reloads: 0,
    loadedKey: null,       // mLoadedFolder
    images: [],
    uploadPending: false,
    note: 'pick any image inside the folder of logos',
    schedule: { steps: [], note: '' },
    scheduleKey: '',
    reloadDown: false,
    capture: null,
    drawn: false,
  };

  function pollLoader() {
    const wanted = st.folder ? `${st.folder.key}#${st.reloads}` : '';
    if (wanted !== st.loadedKey) {
      st.loadedKey = wanted;
      if (wanted) {
        loader.start(st.folder.entries, RASTER_PX);
        st.note = 'loading...';
      } else {
        st.images = [];
        atlas.release();
        st.schedule = { steps: [], note: '' };
        st.note = 'pick any image inside the folder of logos';
      }
    }

    if (loader.ready) {
      st.images = loader.take();
      st.uploadPending = true;
      st.note = loader.note;
    }

    if (st.uploadPending) {
      atlas.begin(st.images);
      st.uploadPending = false;
      if (!atlas.valid()) st.note += ` -- ${atlas.note}`;
      st.scheduleKey = '';
    }

    if (atlas.uploading()) atlas.step(st.images, UPLOAD_PIXEL_BUDGET);
  }

  function rebuildScheduleIfNeeded(params) {
    const mode = toOption(params.get('mode'), 5);
    const [columns, rows] = fillGrid(mode, intOf(params, 'columns'), intOf(params, 'rows'));
    const o = {
      seed: Math.max(0, intOf(params, 'seed')) >>> 0,
      noAdjacentRepeat: params.get('varyNeighbours') > 0.5,
      aspectAware: params.get('respectAspect') > 0.5,
      equalAirtime: params.get('equalAirtime') > 0.5,
      hero: toOption(params.get('hero'), 4),
      heroCols: intOf(params, 'heroColumns'),
      heroRows: intOf(params, 'heroRows'),
      heroLogo: intOf(params, 'heroLogo'),
    };
    const key = JSON.stringify([columns, rows, atlas.layers, mode, o]);
    if (key === st.scheduleKey) return;
    st.scheduleKey = key;
    // The hero block is meaningless on a cube: there is no top of the wall.
    if (mode === MODE.Rubik) o.hero = HERO.Off;
    st.schedule = buildSchedule(atlas.logos, columns, rows, o);
  }

  function draw(layout, width, height, d) {
    const data = [];
    for (const c of layout.cells) {
      if (c.logo < 0 || c.logo >= atlas.layers) continue;
      for (let i = 0; i < 16; i += 1) data.push(c.model[i]);
      data.push(c.uvScaleX, c.uvScaleY, c.uvOffsetX, c.uvOffsetY);
      data.push(c.logo, c.brightness, c.opacity, c.edge ? 1 : 0);
    }
    const count = data.length / kFloatsPerInstance;
    if (count === 0) return;

    const floats = new Float32Array(data);
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, instances);
    if (floats.length > capacity) {
      gl.bufferData(gl.ARRAY_BUFFER, floats, gl.STREAM_DRAW);
      capacity = floats.length;
    } else {
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, floats);
    }

    // ---- the wall --------------------------------------------------------
    target.ensure(width, height);
    target.clearTo(0, 0, 0, 0);

    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.disable(gl.CULL_FACE);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);

    wall.use();
    gl.uniformMatrix4fv(wall.location('ViewProjection'), false, mul(layout.projection, layout.view));

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, atlas.texture);
    wall.setSampler('Logos', 0);

    const perCell = d.edgeScope !== 0;
    wall.setInt('EdgeMode', perCell ? d.edge : 0);
    wall.set('EdgeAmount', d.edgeAmount);
    wall.set('EdgeThreshold', d.edgeThreshold);
    wall.set('LayerTexel', atlas.layerSize > 0 ? f(1 / atlas.layerSize) : 0);
    wall.setInt('GridLines', d.gridLines ? 1 : 0);
    wall.set('GridWidth', d.gridWidth);
    wall.set('GridColour', d.gridR, d.gridG, d.gridB);
    wall.setInt('CellFill', d.cellFill);
    wall.set('FillColour', d.fillR, d.fillG, d.fillB);

    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, count);

    // ---- composite into the host's (here, the canvas) ---------------------
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, width, height);
    gl.disable(gl.DEPTH_TEST);

    composite.use();
    gl.bindVertexArray(emptyVao);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, target.texture);
    composite.setSampler('Wall', 0);
    composite.set('Texel', f(1 / width), f(1 / height));
    const global = d.edgeScope === 0;
    composite.setInt('EdgeMode', global ? d.edge : 0);
    composite.set('EdgeAmount', d.edgeAmount);
    composite.set('EdgeThreshold', d.edgeThreshold);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    // ---- hand the context back -------------------------------------------
    gl.disable(gl.BLEND);
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);
    gl.useProgram(null);
    gl.bindBuffer(gl.ARRAY_BUFFER, null);
    gl.bindVertexArray(null);
  }

  function render({ params, width, height, time }) {
    // FF_TYPE_EVENT: Reload on the rising edge, then let go, as a host does.
    const reloadDown = params.get('reload') > 0.5;
    if (reloadDown && !st.reloadDown) st.reloads += 1;
    st.reloadDown = reloadDown;
    if (reloadDown) queueMicrotask(() => params.set('reload', 0));

    pollLoader();
    rebuildScheduleIfNeeded(params);

    st.drawn = false;
    if (atlas.valid() && st.schedule.steps.length > 0) {
      const mode = toOption(params.get('mode'), 5);
      const real = (id) => f(params.get(id));
      const lo = {
        mode,
        projection: toOption(params.get('projection'), 2),
        fit: toOption(params.get('fit'), 4),
        walk: toOption(params.get('walk'), 5),
        edge: toOption(params.get('edge'), 4),
        edgeScope: toOption(params.get('edgeScope'), 4),
        cellPad: CellPad(real('cellPad')),
        speed: Speed(real('speed')),
        scrollX: Scroll(real('scrollX')),
        scrollY: Scroll(real('scrollY')),
        gridRot: GridRot(real('gridRotation')),
        walkRate: WalkRate(real('walkRate')),
        twinkle: real('twinkle'),
        fade: real('fade'),
        fov: Fov(real('fov')),
        camYaw: CamAngle(real('camYaw')),
        camPitch: f(CamAngle(real('camPitch')) * 0.5),
        seed: Math.max(0, intOf(params, 'seed')) >>> 0,
        outputAspect: f(width / height),
      };
      if (mode === MODE.Twinkle && lo.twinkle <= 0) lo.twinkle = 1;
      if (mode === MODE.FadeCycle && lo.fade <= 0) lo.fade = 1;

      const layout = buildLayout(st.schedule, atlas.logos, intOf(params, 'columns'), intOf(params, 'rows'), f(time), lo);

      draw(layout, width, height, {
        edge: lo.edge,
        edgeScope: lo.edgeScope,
        edgeAmount: real('edgeAmount'),
        edgeThreshold: EdgeThreshold(real('edgeThreshold')),
        gridLines: params.get('showGrid') > 0.5,
        gridWidth: GridWidth(real('gridWidth')),
        gridR: real('gridR'),
        gridG: real('gridG'),
        gridB: real('gridB'),
        cellFill: toOption(params.get('cellFill'), 3),
        fillR: real('fillR'),
        fillG: real('fillG'),
        fillB: real('fillB'),
      });
      st.drawn = true;
    }

    // Still filling the atlas a budget at a time: ask for the next frame even
    // when paused, as a host would call ProcessOpenGL anyway.
    if (atlas.uploading() || st.uploadPending) mounted?.redraw();

    // demo/tools/crosscheck reads a frame back here, in the frame that drew it:
    // the canvas does not preserve its buffer past this task.
    if (st.capture) {
      const out = new Uint8Array(width * height * 4);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, out);
      const resolve = st.capture;
      st.capture = null;
      resolve({ width, height, pixels: out });
    }
  }

  instance = {
    st,
    atlas,
    setFolder(label, entries) {
      st.folder = { label, entries, key: `${label}:${Date.now()}:${Math.random()}` };
    },
    finished: () => st.drawn && !loader.ready && !st.uploadPending && atlas.valid() && !atlas.uploading(),
  };

  return { render };
}

//===========================================================================
// The page
//===========================================================================

const mounted = mountDemo({
  name: 'Gridiron',
  pluginId: 'GR01',
  kind: 'source',
  tagline:
    'An animated step-and-repeat: the sponsor wall you stand in front of to be photographed, built from a folder of logos. No logo touches a copy of itself, every logo gets the same airtime, wordmarks take two cells, and the same seed always gives the same wall. Then it scrolls, spins reels, twinkles, fades, or wraps the lot onto a Rubik’s cube.',
  repo: 'https://github.com/stoatworks-labs/gridiron',
  page: 'https://stoatworks-labs.com/software/gridiron/',
  blurb:
    'It is Gridiron’s own GLSL, ported from the repository to WebGL2, drawing a wall laid out by a JavaScript port of the plugin’s arrangement and layout — same parameters, same rules, no install. Gridiron is a source: it reads no video, only a folder of logos, and this page starts on the demo pack that ships in the download. Drop your own logos on it; they never leave this page.',
  showBackdrop: true,
  sources: [],
  params: PARAMS,
  differences: [
    'The arrangement and the layout are a JavaScript port of the plugin’s C++ — the folder rules, the trim, the atlas geometry, Fill.cpp, Layout.cpp, Mat4.h, Hash.h and Controls.h — in float arithmetic, as the plugin does it. demo/tools/crosscheck in the repository runs the port and the plugin’s own Fill.cpp and Layout.cpp through the same two hundred walls and fails unless every schedule and every cell’s logo come out identical and every position within a millionth; that, and not this page, is the reason to believe the port.',
    'The decoding is the browser’s, not the plugin’s. PNG, JPEG, GIF and BMP are decoded by the browser with colour management and EXIF rotation off, as stb_image decodes them. TGA and PSD have no browser decoder, so here they are listed in the About line as not loaded; the plugin loads them.',
    'An SVG is drawn by the browser at the plugin’s 512 px, not by nanosvg. The browser honours clip paths, masks, filters, patterns, <use> and embedded images, all of which nanosvg drops, so an SVG can look better here than in the plugin. Live <text> is the exception: nanosvg draws none, so this page takes it out before drawing and the About line warns, as the plugin’s does. Trust the About line over the picture.',
    'An animated GIF shows its first frame. So does the plugin as built: its atlas holds one layer per logo, filled from frame 0, and nothing reads the frame index the layout computes.',
    'Logos in the plugin is a file picker that reads the folder around the file you pick. A browser cannot see a picked file’s neighbours, so this page asks for the folder itself, or several files, or a drop, and applies the plugin’s rules to what it gets: the top level only, the eight extensions, no dotfiles, sorted by name. Folder Override takes a path on disk, which a page cannot read, so it does nothing here. Reload reads your files again.',
    'The plugin starts with no folder and draws nothing; this page starts as if you had pointed it at the demo pack in the download. The Resolume marks in that pack are Resolume’s trademarks, included as sample content only; gridiron is independent of Resolume and implies no affiliation or endorsement.',
    'The clock is the page’s: seconds from frame deltas, handed straight to the layout. The plugin measures whether its host sends seconds or milliseconds (Resolume sends milliseconds) and that vote is not ported. Pause stops the wall; Step is 1/60 s.',
    'Columns, Rows, Seed and the hero’s three counts are FF_TYPE_INTEGER in the plugin and number fields here, held to their declared ranges. Reload is FF_TYPE_EVENT: a button. About is the plugin’s note field, shown read-only; the plugin’s log file (Diag.cpp) is not here.',
  ],
  createRenderer,
});

//===========================================================================
// The controls the kit has no type for, and the folder
//===========================================================================

function packEntries() {
  return Promise.all(DEMO_PACK.map(async (name) => {
    const response = await fetch(`./logos/${encodeURIComponent(name)}`);
    if (!response.ok) throw new Error(`${name}: ${response.status}`);
    return { name, blob: await response.blob() };
  }));
}

async function useDemoPack() {
  try {
    instance?.setFolder('the demo pack (demo/logos)', await packEntries());
  } catch (error) {
    instance?.setFolder('the demo pack (demo/logos)', []);
    // eslint-disable-next-line no-console
    console.error(error);
  }
  mounted?.redraw();
}

/** The top level of a picked folder, as the plugin's directory_iterator sees it. */
function folderFromInput(files) {
  const list = [...files];
  const label = list[0]?.webkitRelativePath?.split('/')[0] || 'your files';
  const top = list.filter((file) => !file.webkitRelativePath || file.webkitRelativePath.split('/').length === 2);
  return { label, entries: top.map((file) => ({ name: file.name, blob: file })) };
}

async function readDirectory(entry) {
  const reader = entry.createReader();
  const all = [];
  for (;;) {
    const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
    if (batch.length === 0) break;
    all.push(...batch);
  }
  const files = await Promise.all(all.filter((e) => e.isFile).map((e) => new Promise((resolve) => e.file(resolve, () => resolve(null)))));
  return files.filter(Boolean).map((file) => ({ name: file.name, blob: file }));
}

function adopt(label, entries) {
  instance?.setFolder(label, entries);
  mounted?.redraw();
}

if (mounted && instance) {
  const params = mounted.params;
  window.gridironDemo = {
    params,
    state: mounted.state,
    /** Has the folder loaded, uploaded, and drawn? For headless checks. */
    finished: () => instance.finished(),
    /** Render the frame at `time` seconds and hand back its pixels (bottom row first). */
    capture(time) {
      mounted.state.playing = false;
      mounted.state.time = time;
      return new Promise((resolve) => {
        instance.st.capture = resolve;
        mounted.redraw();
      });
    },
    schedule: () => instance.st.schedule,
    logos: () => instance.atlas.logos,
  };

  if (!embed) {
    // ---- the two kit controls a source has no use for ------------------
    // demo.sources is empty, so the kit builds an empty Clip dropdown and a
    // single-file picker that would load a clip nothing reads. A source has
    // no clip; the logos come in through the Logos row instead.
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') field.remove();
    }
    document.querySelector('.transport__file')?.remove();

    // By id where the kit gives the control one; the kit's toggles have none,
    // so by the label's text (the plugin's own name) otherwise.
    const rowOf = (id) => document.getElementById(`p-${id}`)?.closest('.prow')
      ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === PARAMS.find((p) => p.id === id)?.name);
    const button = (label, title) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.textContent = label;
      if (title) b.title = title;
      return b;
    };

    // ---- Logos (FF_TYPE_FILE): the folder ------------------------------
    const logosRow = document.createElement('div');
    logosRow.className = 'prow prow--text';
    const logosName = document.createElement('label');
    logosName.className = 'prow__name';
    logosName.textContent = 'Logos';
    logosName.title = 'FF_TYPE_FILE in the plugin: pick any image inside the folder and it reads the folder. A page cannot see a file’s neighbours, so pick the folder itself, or several files, or drop them on the picture.';
    const logosValue = document.createElement('output');
    logosValue.className = 'prow__value';
    logosValue.style.gridColumn = '1 / -1';
    logosValue.style.textAlign = 'left';
    logosValue.style.maxWidth = 'none';
    logosValue.style.whiteSpace = 'normal';
    const actions = document.createElement('div');
    actions.style.display = 'flex';
    actions.style.flexWrap = 'wrap';
    actions.style.gap = '6px';
    actions.style.gridColumn = '1 / -1';

    const folderInput = document.createElement('input');
    folderInput.type = 'file';
    folderInput.webkitdirectory = true;
    folderInput.hidden = true;
    const filesInput = document.createElement('input');
    filesInput.type = 'file';
    filesInput.multiple = true;
    filesInput.accept = 'image/*,.svg,.tga,.psd';
    filesInput.hidden = true;

    const folderButton = button('Folder…', 'A folder of logos from this machine. Read in the page; nothing is uploaded.');
    const filesButton = button('Files…', 'Several logo files, treated as one folder.');
    const packButton = button('Demo pack', 'Back to the logo pack that ships in the download.');
    folderButton.addEventListener('click', () => folderInput.click());
    filesButton.addEventListener('click', () => filesInput.click());
    packButton.addEventListener('click', () => useDemoPack());
    folderInput.addEventListener('change', () => {
      if (!folderInput.files?.length) return;
      const { label, entries } = folderFromInput(folderInput.files);
      adopt(label, entries);
      folderInput.value = '';
    });
    filesInput.addEventListener('change', () => {
      if (!filesInput.files?.length) return;
      adopt(`${filesInput.files.length} file${filesInput.files.length === 1 ? '' : 's'}`, [...filesInput.files].map((file) => ({ name: file.name, blob: file })));
      filesInput.value = '';
    });
    actions.append(folderButton, filesButton, packButton, folderInput, filesInput);
    logosRow.append(logosName, actions, logosValue);
    rowOf('folderOverride')?.before(logosRow);

    // ---- Folder Override: shown, inert -------------------------------------
    const override = document.getElementById('p-folderOverride');
    if (override) {
      override.readOnly = true;
      override.title = 'A path on disk. A web page cannot read one, so this does nothing here.';
    }

    // ---- Reload (FF_TYPE_EVENT): a press ------------------------------------
    const reloadToggle = rowOf('reload')?.querySelector('.prow__toggle');
    if (reloadToggle) {
      const press = button('Reload', 'Read the folder again.');
      press.className = 'prow__toggle';
      press.addEventListener('click', () => params.set('reload', 1));
      reloadToggle.replaceWith(press);
    }

    // ---- the integers: number fields in the declared range ----------------
    for (const [id, spec] of INTS) {
      const field = document.getElementById(`p-${id}`);
      if (!field) continue;
      field.type = 'number';
      field.min = String(spec.min);
      field.max = String(spec.max);
      field.step = '1';
      field.inputMode = 'numeric';
      field.addEventListener('change', () => {
        const held = intOf(params, id);
        if (String(held) !== field.value) params.set(id, String(held));
      });
    }

    // Which logo Hero Logo names, by its place in the sorted folder.
    const heroName = document.createElement('output');
    heroName.className = 'prow__value';
    heroName.style.gridColumn = '1 / -1';
    heroName.style.textAlign = 'left';
    heroName.style.maxWidth = 'none';
    rowOf('heroLogo')?.append(heroName);

    // ---- About (FF_TYPE_TEXT, no group): the plugin's note --------------
    const aboutSection = document.createElement('section');
    aboutSection.className = 'pgroup';
    const aboutRow = document.createElement('div');
    aboutRow.className = 'prow prow--text';
    const aboutLabel = document.createElement('label');
    aboutLabel.className = 'prow__name';
    aboutLabel.textContent = 'About';
    aboutLabel.htmlFor = 'p-about';
    const about = document.createElement('input');
    about.type = 'text';
    about.className = 'prow__text';
    about.id = 'p-about';
    about.readOnly = true;
    aboutRow.append(aboutLabel, about);
    aboutSection.append(aboutRow);
    document.querySelector('.inspector__body')?.append(aboutSection);

    // ---- the plugin's own words, under the picture ----------------------
    const stage = document.querySelector('.stage');
    const status = document.createElement('p');
    status.className = 'stage__status';
    status.id = 'gridiron-status';
    status.setAttribute('aria-live', 'polite');
    status.style.whiteSpace = 'pre-wrap';
    stage?.append(status);

    // ---- drop a folder or files on the picture --------------------------
    const canvasWrap = document.querySelector('.stage__canvas');
    canvasWrap?.addEventListener('dragover', (event) => {
      event.preventDefault();
      event.dataTransfer.dropEffect = 'copy';
    });
    canvasWrap?.addEventListener('drop', async (event) => {
      event.preventDefault();
      const items = [...(event.dataTransfer.items ?? [])].map((item) => item.webkitGetAsEntry?.()).filter(Boolean);
      const dirs = items.filter((e) => e.isDirectory);
      if (dirs.length === 1 && items.length === 1) {
        adopt(dirs[0].name, await readDirectory(dirs[0]));
        return;
      }
      const files = [...(event.dataTransfer.files ?? [])];
      if (files.length) adopt(`${files.length} dropped file${files.length === 1 ? '' : 's'}`, files.map((file) => ({ name: file.name, blob: file })));
    });

    let last = '';
    setInterval(() => {
      const st = instance.st;
      const label = st.folder ? st.folder.label : 'nothing yet';
      const logos = instance.atlas.logos;
      const heroIndex = Math.min(intOf(params, 'heroLogo'), Math.max(0, logos.length - 1));
      const heroText = logos.length ? `→ ${logos[heroIndex].name}` : '';
      const text = [
        `Logos: ${label}`,
        `About: ${st.note}`,
        st.schedule.note ? `The schedule (Fill.cpp’s own summary): ${st.schedule.note}` : '',
        heroText,
      ].join('\n');
      if (text === last) return;
      last = text;
      logosValue.textContent = label;
      about.value = st.note;
      about.title = st.note;
      heroName.textContent = heroText;
      status.textContent = [
        `About: ${st.note}`,
        st.schedule.note ? `Schedule: ${st.schedule.note}` : '',
      ].filter(Boolean).join('\n');
    }, 250);
  }

  useDemoPack();
}
