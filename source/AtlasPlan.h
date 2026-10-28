#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Fill.h"
#include "Library.h"

/**
    What goes in which layer of the atlas, worked out before GL is touched.

    `Atlas::Begin` allocates and `Atlas::Step` blits; everything between them is
    arithmetic, and it lives here, free of GL, so that the harness can check it
    without a context and the browser demo's port can be held to it.

    ## An animated logo is several layers

    Every frame of an animated GIF is a layer of its own, straight after the
    logo's first, and the layout picks the frame (`FrameAt`). Until v0.2.3 the
    atlas held frame 0 and nothing else, so an animated GIF sat still on the
    wall while the README said it played.

    Frames are not free: a layer is the size of the largest logo in the folder,
    so one 60-frame GIF beside a 2048px PNG would ask for a gigabyte. Frames past
    the first therefore share a budget, `kFrameBudgetBytes`, handed out in folder
    order, and a GIF whose frames do not fit is drawn as its first frame -- a
    still logo on the wall rather than a refused array and a black one -- with
    the note saying so.
*/
namespace gridiron
{
/// The layer edge to upload at, given how big a cell will actually be on screen.
/// Rounded up to a power of two and clamped to what the driver allows, because a
/// texture array that changes size every time the composition is resized would
/// re-upload the whole folder on every drag of the window.
int ChooseLayerSize( int cellPixels );

/// Where one image sits inside a square layer, keeping its aspect.
///
/// Shared by the plan, which needs it for the geometry, and the blit, which
/// needs it for the pixels. The two computing it separately is a silent
/// mis-registration waiting to happen -- the trim box would describe a
/// rectangle the pixels are not in.
struct LayerFit
{
	int dx = 0, dy = 0, dw = 0, dh = 0;
};

LayerFit PlaceIn( const Image& img, int layerSize );

/// Bytes of layer, mipmaps aside, that animation frames past the first may
/// claim between them. 64 frames at 1024px, 256 at the 512px most packs land
/// on.
constexpr size_t kFrameBudgetBytes = static_cast< size_t >( 256 ) << 20;

struct AtlasPlan
{
	int layerSize = 0;

	/// One per logo, in folder order: its geometry in layer space, its first
	/// layer and the timing of its frames.
	std::vector< Logo > logos;

	/// What each layer of the array holds, in layer order.
	struct Layer
	{
		int image = 0;///< index into the images handed to PlanAtlas
		int frame = 0;///< which of that image's frames
	};
	std::vector< Layer > layers;

	/// Animated logos that play, and ones drawn still because the budget or
	/// the GPU's layer limit ran out first.
	int animated = 0;
	int stilled  = 0;

	/// One line for the log and the plugin's note.
	std::string note;
};

/// Plan the atlas for `images` on a GPU that allows `maxSize` px a side and
/// `maxLayers` layers (either 0 or less: a conservative default).
AtlasPlan PlanAtlas( const std::vector< Image >& images, int maxSize, int maxLayers );

} // namespace gridiron
