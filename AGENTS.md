# Working on gridiron

An FFGL **source** plugin (`GR01`) that builds an animated step-and-repeat — a
press wall / sponsor board — from a folder of logos.

Read `docs/ARCHITECTURE.md` first. It carries the design, the reasoning, and the
list of fleet traps this plugin is known to be walking into.

## Status

v0.1.0, unreleased, **never run in Resolume**. Builds on macOS; `ctest` runs four
suites and `oxbow selftest` confirms the bundle registers, instantiates and
renders. Windows and Linux have not been built.

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

## Do not trust

- **`tools/verify.sh`** is still flipbook's check list. It is wrong until
  rewritten; a pass there says nothing about gridiron.
- **`ATTRIBUTIONS.md`** is generated from master lists in `stoatworks-backend` by
  `scripts/sync-attributions.py`. gridiron is not registered there yet, so the
  file here is hand-written and provisional. Register it before release, and note
  that the script's `--only` flag truncates the file rather than filtering it.

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
