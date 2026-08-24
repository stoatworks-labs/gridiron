# Attributions

Gridiron is built on other people's work. This file lists what that work is, who
did it, and what it is doing here.

> **Provisional.** Across the fleet this file is generated from master lists in
> `stoatworks-backend` by `scripts/sync-attributions.py`. Gridiron is not
> registered there yet, so this copy is hand-written. Register it before release
> — and note that the script's `--only` flag truncates the file rather than
> filtering it.

## Third-party code this project uses

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>
Licence: BSD-3-Clause
Copyright: FreeFrame

Vendored as a git submodule at `external/ffgl`, pinned to `b1afaf9`.

The plugin ABI itself. An FFGL source is defined by this SDK's headers — there is
no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives through `vcpkg.json` on Windows and Linux. Resolves OpenGL entry points
on Windows, where the system headers stop at OpenGL 1.1. macOS uses the system
OpenGL framework and does not need it.

### stb_image

<https://github.com/nothings/stb>
Licence: MIT / public domain (dual)
Copyright: Sean Barrett

One single header vendored, unmodified, at `external/stb`.

Decodes every raster format the plugin accepts, and is also what decodes animated
GIFs frame by frame — which is the whole of gridiron's animated-content support
and cost no additional dependency.

### nanosvg

<https://github.com/memononen/nanosvg>
Licence: zlib
Copyright: Mikko Mononen

Two single headers vendored, unmodified, at `external/nanosvg`.

Parses and rasterises SVG. Chosen because it rasterises **at a size given at call
time**, which is what lets a vector logo be redrawn crisply at whatever size its
cell turns out to be rather than resampled from a fixed bitmap. Sponsor packs are
vector, and a press wall draws the same mark at wildly different sizes depending
on grid density.

Its limitation is documented loudly in the README: it has no font engine, so
live `<text>` is dropped entirely.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or
you would rather not be listed — open an issue and it will be fixed.
