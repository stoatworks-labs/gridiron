# Working on gridiron

An FFGL **source** plugin (`GR01`) that builds an animated step-and-repeat — a
press wall / sponsor board — from a folder of logos.

Read `docs/ARCHITECTURE.md` first. It carries the design, the reasoning, and the
list of fleet traps this plugin is known to be walking into.

## Status

v0.2.3 is the current release (v0.1.0, the first, was **released** on
2026-08-24), and it is in other people's hands. Builds on macOS; `ctest` runs
four suites and `oxbow selftest` confirms the bundle registers, instantiates and
renders. Windows is built by CI only, never locally, and outside users run it in
Resolume on Windows 11 (gridiron#1, #4–#6). There is no Linux build.

**The Windows binary had never been run by anyone when it shipped.** The first
outside report (gridiron#1) was a black wall in Resolume on Windows 11, and it
arrived with no log because there was no logging. Two real defects came out of
chasing it, both now fixed and both covered in `gdtest_render`:

- `Atlas::Upload` read a GL error the *host* had left in the queue as its own
  failure and abandoned the atlas for the session. A plugin must drain the queue
  before it asks whether its own call worked.
- Whole-grid scroll translated by `scroll * timeSeconds` with no wrap, in world
  units rather than cells. Any scroll off centre slid the wall out of frame
  within seconds and it never returned.

`Render_test.mm` is Objective-C++ against CGL, so **there is no pixel-level
coverage on Windows at all**. Weigh that before trusting a green run.

## `oxbow selftest` will always report FAIL, and that is correct

It renders 120 frames and fails a plugin that lights no pixels. gridiron with no
folder set draws nothing, deliberately — and oxbow cannot set a folder, because
`--set` drives float parameters while a `FF_TYPE_FILE` path arrives through
`SetTextParameter`.

So read oxbow's output, not its verdict. What it uniquely proves is the part no
other check can reach:

    name / id / type / params   the bundle registers a plugin at all
    FF_INSTANTIATE_GL           it instantiates without failing
    gl error: 0x0               it ran 120 frames without raising one

A phantom parameter that broke instantiation was found exactly here, on a build
that was otherwise clean and green. `ctest`'s `render` suite is what checks
pixels, because it can load a folder.

## Before tagging, run `tools/verify.sh`

It does the release job's macOS checks locally — registration, `lipo`, the
`CFBundleExecutable`/binary match that makes `codesign` fail with a message
naming neither the plist nor the cause, an actual ad-hoc sign of a copy, and
`oxbow` instantiation. All of it takes a second.

That matters more than it sounds: the fix for a bad tag is to re-point it, and a
re-pointed tag is skipped by the autosigner — which keys on repo+tag — leaving
the release unsigned and un-notarised for ever unless the state file is edited by
hand.

## Do not trust

- **`ATTRIBUTIONS.md`** is generated from master lists in `stoatworks-backend` by
  `scripts/sync-attributions.py`. gridiron is not registered there yet, so the
  file here is hand-written and provisional. Register it before release, and note
  that the script's `--only` flag truncates the file rather than filtering it.

## The browser demo (`demo/`)

Live at <https://gridiron-demo.stoatworks-labs.com/>, a Cloudflare Worker on a
zone **route** (`wrangler.toml`; it needs the proxied `gridiron-demo` AAAA
100:: record, and deleting that record takes the page dark under a green
deploy). `.github/workflows/deploy.yml` redeploys it on a push to main.

**`demo/` is two things.** `demo/logos` and `demo/README.md` are the logo pack
the release copies into the download as "demo logos"; everything else in
`demo/` is the page, which serves `logos/` as its default wall. The release job
copies those two by name and nothing else. Never widen that to the whole
folder, and never put anything in `demo/logos` that is not a logo:
`demo/tools/check_pack.py` fails verify if `DEMO_PACK` in `demo/plugin.js` and
the folder disagree, or if the folder holds a file the plugin would not read.
`demo/.assetsignore` keeps `tools/` and the pack's README off the site.
`demo/vendor/` is the shared kit, copied from `stoatworks-backend` -- do not edit.

What is the plugin's and what is not:

- **The shaders are the plugin's.** `demo/shaders.js` is `source/Shaders.cpp`,
  spliced by `demo/tools/check_shaders.py --write`; verify fails on a changed
  character.
- **The CPU half is a port** (`demo/port.js`): ScanFolder's rules, the trim,
  Atlas's geometry and blit, Fill, Layout, Mat4, Hash and Controls.h, in float
  arithmetic. `demo/tools/crosscheck/crosscheck.sh` (run by verify) holds Fill,
  Layout and Controls to the C++: schedules and every integer identical, floats
  within 1e-6 (V8's sin/cos/tan/pow against Apple's libm). Change one, change
  the other. `crosscheck.sh --frames DIR` also renders the real plugin class
  and the page (headless Chrome, SwiftShader) at nineteen settings and writes
  side-by-side PNGs; on 2026-10-28 every frame agreed on where the wall is to
  97% or better, with a mean difference under 0.7/255.
- **Not ported:** stb_image and nanosvg (the browser decodes; no TGA or PSD; the
  page removes live `<text>` from an SVG because nanosvg draws none, but the
  browser still draws the clip paths, masks and filters nanosvg drops), the
  Clock's seconds-or-milliseconds vote, and Diag's log. A page cannot read the
  folder around a picked file, so Logos is a folder/files picker and a drop,
  and Folder Override does nothing.

**The page reproduces the plugin, faults included.** These look wrong and are
what the plugin does -- the crosscheck matched each against the real class. Fix
them in the plugin first, then in the port, never the port alone:

- **Fit (the default), Fill and Stretch distort any logo that is not square.**
  Atlas pads each logo into a square layer, but FitUv maps the whole layer at
  the logo's own aspect, so a 4:1 wordmark is drawn at about 16:1. Trim margins
  uses the trim box and is right.
- **Scroll's wrap-around copies are drawn unfitted.** BuildLayout copies the
  cell into `ghosts` before FitUv and the edge flag are set, so the mark at
  each scrolling edge shows the whole layer stretched, with no edge treatment.
- **An animated GIF never animates.** The atlas holds frame 0 only and nothing
  reads `CellTransform::frame`. The README says animated GIFs play.
- **On the cube, a two-cell wordmark sits on a slice boundary.** Its centre is
  exactly between two slices, so which one it turns with is decided by
  rounding, and the arm64 build (fused multiply-adds) and the x86_64 build
  disagree (the crosscheck reports it). The page follows x86_64.
- **With a hero block, the schedule always says PARITY NOT EXACT (0..N).** The
  hero's logo is out of the rotation and still counted, with 0 placements, so
  `exactParity` is false whatever the wall. The plugin never shows that note;
  the page prints it under the picture.

## Ground rules carried from the fleet

- Every ranged parameter is **0..1 host-side**, mapped in `Controls.h`.
  `SetParamInfo` clamps a ranged default before `SetParamRange` can run, and
  there is no `SetParamDefault`.
- Resolume's `SetTime` is in **milliseconds**.
- Factory presets must not be copy-based — Resolume ignores value events.
- The four-character plugin id must be unique across the fleet. `GR01` is taken
  by this plugin; `GR02` is reserved for the effect variant.
- `oxbow selftest` is the only check that proves the bundle actually registers a
  plugin. A clean build and a green test run do not.
- Anything the release job does that can be done locally goes in `tools/verify.sh`.
