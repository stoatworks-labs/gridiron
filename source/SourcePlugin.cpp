#include "Gridiron.h"

/**
    The registration, and nothing else.

    **This file is listed directly in the source target, not in the shared
    object library.** `CFFGLPluginInfo` registers itself from a file-scope
    constructor and nothing ever references it by name, so in a static archive
    the linker is entitled to drop the whole translation unit -- giving a bundle
    that loads, exports `plugMain`, and reports that it contains no plugins.

        nm -gU Gridiron.bundle/Contents/MacOS/Gridiron | grep plugMain

    That is also why the shared code is an OBJECT library rather than a STATIC
    one, and it is why `oxbow selftest` is the only check that proves a bundle
    actually registers anything.
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< gridiron::GridironPlugin >,
	"GR01",                                                  // unique id, 4 chars, unique across the fleet
	"Gridiron",                                              // name
	2,                                                       // API major
	1,                                                       // API minor
	0,                                                       // plugin major
	1,                                                       // plugin minor
	FF_SOURCE,                                               // a generator: no input
	"Animated step-and-repeat built from a folder of logos", // description
	"Stoatworks Labs" );
