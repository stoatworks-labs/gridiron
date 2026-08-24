# Gridiron — architecture

An animated step-and-repeat: a press wall / sponsor board built from a folder of
logos, filled intelligently, and animated as a grid.

FFGL source plugin, id `GR01`. An effect variant `GR02` is reserved but not built
— it would feed the incoming clip in as cell content or as the wall behind.

---

## The decision everything else follows from

**Rubik's cube mode ships in v1, so the renderer is 3D-native throughout.**

Every cell is a textured quad with a model matrix in world space. The flat wall is
not a separate code path; it is an orthographic camera with every quad coplanar.
Whole-grid scroll and rotation fall out of the view matrix for free, and the cube
is a different set of model matrices rather than a different renderer.

Retrofitting 3D onto a 2D quad-blitter later would have meant rewriting layout,
rendering and every animation mode. Doing it the other way costs a matrix
multiply per cell on the flat path, which is nothing.

### The consequence that is not obvious: we need our own FBO

`ProcessOpenGLStruct` gives us `HostFBO`, and the SDK comment is explicit that
this exists **so the plugin can restore that binding after using its own FBOs**.
It is a colour target. Nothing in FFGL promises it carries a depth attachment,
the SDK has no FBO helper of any kind, and a cube with six faces and rotating
slices is unrenderable without depth.

So: gridiron allocates an FBO with a depth buffer, renders the wall into it, and
composites into `HostFBO`. This is required for the cube and harmless for the
flat wall, so there is one path, always.

---

## Pipeline

    folder scan → decode (async) → atlas → fill → layout → render → post

### 1. Library — the folder

**FFGL has no folder picker.** The parameter types stop at `FF_TYPE_FILE` (14),
which is a *file* dialog with an extension whitelist, via `SetFileParamInfo`.
There is no directory type and no way to add one.

So the operator picks **any one image inside the folder**, and gridiron loads the
containing directory. One click, the real native picker, no path typing. A
`FF_TYPE_TEXT` field overrides it for anyone driving the plugin from a script.

Accepted content:

| kind | decoder | notes |
|---|---|---|
| png jpg jpeg bmp tga psd | stb_image | straight-alpha RGBA8 |
| gif | stb_image | animated; frames become a per-cell playhead |
| svg | nanosvg | **rasterised at cell size**, see below |

**Vector logos rasterise at the size the cell actually is**, and re-rasterise when
the cell footprint changes. This is burin's central trick and it matters more here
than it did there: a press wall is the one place where the same logo is drawn at
wildly different sizes depending on grid density, and resampling a fixed-size PNG
is exactly what makes a sponsor board look cheap. Real sponsor packs are vector.

> **nanosvg drops `<text>` entirely.** No font loading, no glyph outlines, not
> even a stub — an SVG whose wordmark is live text parses without error and draws
> nothing. burin's `ScanUnsupported` counts the dropped elements by scanning the
> source, and gridiron reuses it to warn per file. The operator-facing fix is
> "convert text to outlines before export"; it belongs in the README and in the
> plugin's own note field, because silently losing a sponsor's name from a
> sponsor wall is the worst failure this plugin has.

Decoding runs on a worker thread. A folder of sixty logos cannot be decoded inside
a render callback, and the first frame after a folder change must not stall.

### 2. Atlas

Logos go into a **texture array**, one layer per logo, not a packed atlas.

An atlas would be smaller, but a grid wants a uniform slot per cell and an atlas
bleeds at layer edges under filtering — which on a step-and-repeat shows up as a
halo of the neighbouring sponsor's logo, the single most visible possible artefact.
An array costs padding and gives clean edges and per-layer mips.

Layers share dimensions, so each logo is padded into a common layer; fit / fill /
crop / stretch is then a texture-coordinate transform *within* the layer. Guard
`GL_MAX_TEXTURE_SIZE` conservatively at 8192 per side as flipbook does, because
`glTexImage3D` failing gives you black, which looks identical to a folder that did
not load.

### 3. Fill — "intelligently filled"

Three rules, all three chosen deliberately:

- **No adjacent repeat.** A logo never touches a copy of itself horizontally or
  vertically. This is what real press walls do, and it is the rule that makes a
  repeated small pack look designed rather than looped.
- **Aspect-aware.** Wide logos land in wide or spanned cells, square logos in
  square cells, rather than being letterboxed into a slot that fights them.
- **Equal airtime.** With more logos than cells, the schedule gives every logo an
  equal *amount of on-screen time* across a full cycle — not merely an equal
  number of placements. Sponsors pay for parity and it is auditable.

- **Reproducible.** A seed parameter fixes the arrangement. The same folder and
  the same seed always produce the same wall, so an operator can shuffle until
  they like one and then keep it — and an approved wall is still the approved
  wall after a reload. Without this, reopening a composition silently gives a
  different arrangement than the one that was signed off, and equal-airtime
  parity cannot be reproduced or audited after the show.

The hero block removes its cells from the pool before filling; fill works around
the hole.

### 4. Layout

Emits, per cell: a model matrix, a content slot, and a phase offset.

- **Flat wall** — orthographic camera, coplanar quads.
- **Cube** — six faces, animated slice rotations, one slice at a time.
- **Whole-grid** — scroll and rotate, both in the view matrix.
- **Visible / invisible grid** — a border drawn inside the cell shader. No extra
  geometry; a grid line is a function of cell-local UV.

**Logo movement through the grid is not a transform.** A logo walking up, down,
left or right through the wall is *content reassignment over time* — the cells
stay put and the slots move between them. Keeping this distinct from the matrix
path is what stops the two interfering.

### 5. Motion

Per-cell state: brightness, opacity, content slot, phase.

- **Twinkle** — per-cell phase derived from a hash of the cell index, so no two
  cells are ever in the same state and nothing beats in lockstep.
- **Fade** — cells fade out and in, swapping content at the trough.
- **Slot machine** — per-column scroll velocity, optional spin-down easing that
  lands on a cell boundary.
- **Rubik** — slice rotation, sequenced one at a time.

### 6. Edge FX

Shaders from the fleet's three edge-detection plugins: burin, nib, vectrix.
Wired both **globally and per-cell**, because per-cell is the only mode that
stacking those plugins over gridiron in Resolume cannot already give you — the
hero logo solid while the rest of the wall is line art.

Per-cell means the edge kernel runs **in the cell shader on the atlas sample**,
not as a full-screen post pass. Its neighbourhood taps must be masked to the
layer's own padding, or every logo gets outlined against its padding and the wall
fills with rectangles.

---

## Traps this plugin is already known to be walking into

Carried from the fleet; each has bitten a sibling repo.

- **Resolume sends `SetTime` in milliseconds**, not seconds. Repaired across
  fourteen repos after the fact. Get it right on day one.
- **Factory presets must not be copy-based.** Resolume ignores value events, so a
  copy-based apply loses to the host's echo. All seven fleet plugins were fixed;
  gridiron's four presets use the fixed pattern from the start.
- **`SetParamInfo` clamps a ranged default into 0..1 before `SetParamRange` can
  be called**, and there is no `SetParamDefault` — so a default of 5 becomes 1,
  silently. **The clamp is guarded on `FF_TYPE_STANDARD` only**; an
  `FF_TYPE_INTEGER` default passes through untouched. Continuous parameters are
  therefore 0..1 and mapped in `Controls.h`, but the counts — Columns, Rows, hero
  span, seed — are declared as real integers with real ranges and real defaults.
  They have to be: this plugin is laying out somebody's sponsor board, and a
  slider that lands on 5 or 7 either side of 6 is unusable.
- **`SetParamGroup` collapses *runs* of same-group parameters**, so the
  declaration order in `ParamId` is load-bearing. Reordering an id silently
  splits one group in the inspector into two.
- **`cmake/InfoOFX.plist.in` must stay parameterised.** A hardcoded
  `CFBundleExecutable` builds, loads, and renders correctly, then fails codesign
  with a message naming neither the plist nor the cause. This repo's copy came
  from flipbook, which is the fixed one.
- **`vcpkg.json` is load-bearing and invisible.** GLEW reaches the Windows build
  through it; nothing in `CMakeLists.txt` points at it, so losing it breaks only
  Windows, only at configure time.
- **`oxbow selftest` is the only real instantiation check.** A clean build, a
  passing test run and a manual smoke test all pass on a bundle that registers no
  plugin at all.
- **Anything the release job can do locally belongs in `tools/verify.sh`.** A
  check that only runs in CI after a tag is a check that catches you after the tag.

## State of the repo

Building and verified as far as offline checks reach. `ctest` runs four suites —
arrangement, geometry, decoding, and a real render in a headless GL context —
and `oxbow selftest` confirms the bundle registers and instantiates.

**Never run in Resolume.**

### What testing actually caught

Every one of these built cleanly, and none would have been visible without the
check that found it.

- **Shuffle-then-repair could not solve the small folder.** Two sponsors on a 6x4
  wall have a perfect checkerboard available and it left fifteen logos touching
  themselves. Replaced with constructive choice.
- **Wide-logo spanning lost placements.** Six wordmarks that should each have been
  placed four times came out 2, 1, 3, 2, 2, 2.
- **`SetFileParamInfo` ends in an unconditional `params.push_back()`.** Pairing it
  with a `SetParamInfo` for the same index registered the parameter twice and left
  a phantom trailing parameter with type `0xFFFFFFFF` and a NaN default — which is
  what made `FF_INSTANTIATE_GL` fail. Only `oxbow selftest` showed it.
- **`Loader::Take()` moved out of its member without clearing `Ready()`**, so the
  plugin took the folder on the frame it landed and took an empty vector on every
  frame after, wiping the atlas. The wall drew once and then vanished, which looks
  exactly like a rendering bug and is not.
- **`BuildLayout` derived the cube size from whatever grid it was handed.** The
  test passed the operator's grid, the plugin passed the already-transformed one,
  and `CubeSize(6, 36)` answered 8 — an 8-cube built from a 6-cube's schedule,
  drawing nothing. The function now derives the virtual grid itself.
- **The camera distance was one hard-coded constant for both projections**, which
  framed the cube at three percent of the frame: a correct render of an unreadable
  picture.

`tools/verify.sh` is gridiron's own, and runs the release job's macOS checks
locally. `.github/workflows/release.yml` arrived from flipbook expecting two
plugins and an OFX bundle, neither of which gridiron builds; both were removed
before the first tag, and the demo logo pack now ships in place of flipbook's
example sheet — a step-and-repeat with no logos to hand draws nothing on first
run, which reads as a broken install rather than a missing input.
