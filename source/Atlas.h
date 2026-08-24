#pragma once

#include <string>
#include <vector>

#include "Fill.h"
#include "Library.h"

// The FFGL SDK pulls in the right GL headers for each platform; going direct
// would mean three #ifdef branches and getting GLEW's ordering wrong on Windows.
#include <FFGLSDK.h>

/**
    Every logo, on the GPU, as one texture array.

    ## An array and not a packed atlas

    A packed atlas is smaller and needs one binding, which is why it is the
    usual answer. It is the wrong answer here.

    An atlas bleeds at the boundaries between packed images under linear
    filtering and under mipmapping, and on a step-and-repeat what bleeds into a
    logo is **the sponsor logo packed next to it**. A faint halo of somebody
    else's mark around the edge of yours is the single most visible artefact
    this plugin could produce, and it is exactly the kind of thing that is
    invisible on the development machine at 1:1 and obvious on a 4K wall.

    `GL_TEXTURE_2D_ARRAY` gives every logo its own layer with its own edges, its
    own clamping and its own mips. It costs padding -- every layer is the same
    size, so a wide logo wastes the rows above and below it -- and that padding
    is transparent, so anything that does bleed is bleeding transparency into
    transparency, which is nothing.

    ## Layers are square, and the aspect is carried separately

    Each logo is drawn into its layer **preserving its aspect**, centred, with
    transparent padding, and the fraction of the layer it occupies goes to the
    shader as part of the UV transform. Stretching the artwork to fill a square
    layer and unstretching in the shader would be smaller and simpler and would
    quietly halve the horizontal resolution of every wordmark.

    ## The size guard

    `GL_MAX_TEXTURE_SIZE` and `GL_MAX_ARRAY_TEXTURE_LAYERS` are real ceilings and
    neither reports a useful error: `glTexImage3D` fails, the sampler returns
    black, and a wall of black cells looks exactly like a folder that did not
    load. Both are queried and both are enforced here, with what happened
    recorded in `note` rather than left for someone to guess at.
*/
namespace gridiron
{
class Atlas
{
public:
	Atlas() = default;
	~Atlas();

	Atlas( const Atlas& )            = delete;
	Atlas& operator=( const Atlas& ) = delete;

	/// Upload `images` as a texture array. Replaces whatever was there.
	/// Must be called with a current GL context.
	bool Upload( const std::vector< Image >& images );

	void Release();

	GLuint Texture() const { return mTexture; }
	int    LayerCount() const { return mLayers; }
	int    LayerSize() const { return mLayerSize; }
	bool   Valid() const { return mTexture != 0 && mLayers > 0; }

	/// The geometry of each uploaded logo, in the order it was uploaded, ready
	/// to hand to `BuildSchedule` and `BuildLayout`.
	const std::vector< Logo >& Logos() const { return mLogos; }

	/// One line saying what was uploaded and what was refused.
	const std::string& Note() const { return mNote; }

private:
	GLuint              mTexture   = 0;
	int                 mLayers    = 0;
	int                 mLayerSize = 0;
	std::vector< Logo > mLogos;
	std::string         mNote;
};

/// The layer edge to upload at, given how big a cell will actually be on screen.
/// Rounded up to a power of two and clamped to what the driver allows, because a
/// texture array that changes size every time the composition is resized would
/// re-upload the whole folder on every drag of the window.
int ChooseLayerSize( int cellPixels );

} // namespace gridiron
