# Gridiron

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The arrangement is not
> asserted but **measured**: an offline harness builds real schedules and checks
> that every logo gets the same number of placements to the unit, that no logo
> touches a copy of itself, and that the same seed gives the same wall. The
> rendering is checked the same way — a headless GL context drives the real
> plugin class against a real folder and counts pixels. **It has not yet been
> run in Resolume.**

An animated step-and-repeat: the sponsor wall you stand in front of to be
photographed, built from a folder of logos and given something to do.

Point it at a folder. It fills a grid intelligently, and then animates —
scrolling reels, twinkling cells, cells that fade and change logo, or the whole
wall wrapped onto a Rubik's cube that turns a slice at a time.

FFGL source plugin for Resolume Arena and Avenue.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/gridiron/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`gridiron-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/gridiron/releases/download/v0.1.0/gridiron-0.1.0-macos-universal.dmg) | 2.5 MB |
| Universal (Apple Silicon + Intel) · .zip archive | [`gridiron-macos-universal.zip`](https://github.com/stoatworks-labs/gridiron/releases/latest/download/gridiron-macos-universal.zip) | 2.1 MB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`gridiron-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/gridiron/releases/download/v0.1.0/gridiron-0.1.0-windows-x86_64-setup.exe) | 2.0 MB |
| x64 · .zip archive | [`gridiron-windows-x86_64.zip`](https://github.com/stoatworks-labs/gridiron/releases/latest/download/gridiron-windows-x86_64.zip) | 1.9 MB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/gridiron/releases](https://github.com/stoatworks-labs/gridiron/releases).

The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Pointing it at a folder

**Pick any single image inside the folder.** Gridiron reads the directory around
it.

This is not a preference. FFGL's parameter types stop at `FF_TYPE_FILE` — a
*file* dialog — and there is no directory type in the specification, so a folder
picker cannot be built. Picking any file inside the folder is one click in the
real native dialog and needs no path typing. `Folder Override` takes a literal
path for anyone driving the plugin from a script.

Accepted: `png` `jpg` `jpeg` `gif` `bmp` `tga` `psd` `svg`. Animated GIFs play.

## ⚠️ Convert text to outlines before exporting an SVG

**Live `<text>` in an SVG will not draw. At all.**

The vector parser has no font engine — no font loading, no glyph outlines, not
even a stub. An SVG whose wordmark is live text parses without error, reports no
failure, and draws nothing where the sponsor's name should be.

On a sponsor wall this is the worst thing that can happen, so gridiron counts the
dropped elements and says so in its About field. But the fix is at export:

> Illustrator: select the text, **Type → Create Outlines** (⇧⌘O), then export.
> Inkscape: **Path → Object to Path** (⇧⌘C).

## How the wall is filled

Four rules, in this priority order when they conflict:

1. **Reproducible.** A seed fixes the arrangement. The same folder and seed give
   the same wall — across a reload, across a machine, and across the gap between
   the wall the client approved and the wall that goes up on the night. Shuffle
   the seed until you like one, then leave it.
2. **No adjacent repeat.** A logo never touches a copy of itself. This is what
   makes a small pack over a big wall read as designed rather than looped, and
   it is the first thing an eye picks out of a photograph.
3. **Equal airtime.** Every logo is on screen for the same total *time* per
   cycle — not the same number of placements. Sponsors pay for parity.
4. **Aspect-aware.** A long wordmark is given two cells rather than being
   letterboxed into one, and is never cropped in half.

Rule 2 beats rule 4 because a duplicated neighbour is visible from across a room
and a letterboxed logo is not.

### Fit modes

| mode | what it does |
|---|---|
| **Fit** | whole mark visible, letterboxed. The safe default for a client's logo. |
| **Fill** | fills the cell, overflow cropped |
| **Trim margins** | discards the exported margin, then letterboxes |
| **Stretch** | distorted to the cell |

**Trim margins** is the one worth knowing about. Sponsor packs never have
consistent margins — one logo is exported tight to its bounding box, the next
sits in a square canvas with a third of it air, and dropped into identical cells
they read as wildly different sizes, which looks like a mistake by whoever built
the wall. Trim measures where the opaque pixels actually are and normalises
every mark to the same *optical* size.

It letterboxes rather than filling, deliberately. Filling after a trim is what
**Fill** already does, and on a wordmark it scales until the name runs off both
sides of the cell — "COLOUR BARS & GRILL" comes out as "FE ARJR BARE & CO".

## Modes

- **Static** — a wall.
- **Slot Machine** — each column is a reel at its own speed, decelerating to a
  stop. The travel is a whole number of cells, so a reel cannot land out of
  alignment: the landing is exact by construction, not snapped at the end.
- **Rubik** — the wall wrapped onto a cube, one slice turning at a time. The
  sequence is a scramble followed by its own inverse, so it returns to a solved
  cube and loops without a jump.
- **Twinkle** — every cell at a different point between full brightness and
  black. The phases are hashed per cell, so nothing pulses in lockstep.
- **Fade Cycle** — cells fade out and in, changing logo while they are dark.

On top of any of them: whole-grid scroll and rotation, logo walk (content moving
*through* the grid while the cells stay put), a visible grid, and a hero block at
top, middle or bottom for the headline sponsor or the event name.

## Edge treatments

The three line-art looks from the fleet — **Burin** (engraved), **Nib** (inked
contour), **Vectrix** (traced) — applied to the whole wall, to every logo
individually, to the hero only, or to everything except the hero.

Per-logo is the reason these are built in rather than left to the Resolume
chain: hero solid, everything else line art, is not something you can get by
stacking a plugin on top.

## Building

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

`ctest` runs four suites: the arrangement, the geometry, the decoding, and — on
macOS — a real render in a headless GL context.

## Status

**v0.1.0, unreleased. Never run in Resolume.** Verified as far as an offline
harness and `oxbow selftest` can reach: the bundle registers, instantiates,
loads a folder and draws a wall, a hero block, per-cell line art and a turning
cube, with no GL errors. What no harness can tell you is how it behaves in a
real composition on real hardware.

## Licence

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
