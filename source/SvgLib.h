#pragma once

/**
    nanosvg, included in exactly one place.

    Both nanosvg headers are single-header libraries: declarations come out
    plainly, implementations only under `NANOSVG_IMPLEMENTATION` and
    `NANOSVGRAST_IMPLEMENTATION`. Defining either in more than one translation
    unit gives a page of duplicate-symbol errors naming functions nobody here
    wrote. So everything that needs the types includes **this** header, which
    defines neither, and `SvgLib.cpp` is the one file that defines both.

    ## The rasteriser macro is not spelled the way you would guess

    It is `NANOSVGRAST_IMPLEMENTATION`. Not `NANOSVG_RASTERIZER_IMPLEMENTATION`,
    which is the obvious name, compiles perfectly, and fails at **link** time
    with three undefined symbols -- `nsvgCreateRasterizer`, `nsvgRasterize`,
    `nsvgDeleteRasterizer` -- and no hint that a macro is the reason. Nothing
    warns, because an unused `#define` is not an error.

    ## Why this file is not called NanoSVG.h

    Carried from burin, where it cost an hour. On a case-insensitive filesystem
    -- which is to say on the Mac this is developed on -- `#include "nanosvg.h"`
    from a file called `NanoSVG.h` resolves to **itself**, the include guard
    stops the recursion, and the vendored library is never reached. clang says
    only `-Wnonportable-include-path` in a wall of other output. On Linux and in
    CI the same source resolves differently.

    A wrapper header must not differ from the library it wraps only by case.
*/

// nanosvg.h uses `strncpy` into fixed-size id buffers, which MSVC's CRT
// deprecates in favour of its own non-portable `strncpy_s`. The library is
// vendored unmodified, so the warning is silenced at the include rather than
// fixed at the source. Same for the signed/unsigned comparison at nanosvg.h
// :3211, which clang and gcc both flag and which is upstream's to fix.
#ifdef _MSC_VER
	#pragma warning( push )
	#pragma warning( disable : 4996 )
#endif
#if defined( __clang__ ) || defined( __GNUC__ )
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#include "nanosvg.h"
#include "nanosvgrast.h"

#if defined( __clang__ ) || defined( __GNUC__ )
	#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
	#pragma warning( pop )
#endif
