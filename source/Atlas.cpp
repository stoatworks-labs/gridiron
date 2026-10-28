#include "Atlas.h"

#include <algorithm>
#include <cmath>

namespace gridiron
{
namespace
{
/// Nearest-neighbour resample of one RGBA8 image into a destination rectangle
/// inside a layer.
///
/// Nearest and not bilinear on purpose. This runs once per logo at upload, the
/// destination is chosen to be at least as big as the source in the common case,
/// and a box filter here would fight the mipmaps generated immediately
/// afterwards. Where quality matters -- a vector logo -- the answer is not a
/// better resample, it is `Rerasterise`, which redraws the artwork at the size
/// it will be seen at and never resamples at all.
void Blit( const uint8_t* src, int sw, int sh, std::vector< uint8_t >& dst, int layerSize, int dx, int dy, int dw, int dh )
{
	for( int y = 0; y < dh; ++y )
	{
		const int sy = std::min( sh - 1, ( y * sh ) / std::max( dh, 1 ) );
		for( int x = 0; x < dw; ++x )
		{
			const int sx = std::min( sw - 1, ( x * sw ) / std::max( dw, 1 ) );

			const size_t s = ( static_cast< size_t >( sy ) * sw + sx ) * 4;
			const size_t d = ( static_cast< size_t >( dy + y ) * layerSize + ( dx + x ) ) * 4;
			dst[ d + 0 ]   = src[ s + 0 ];
			dst[ d + 1 ]   = src[ s + 1 ];
			dst[ d + 2 ]   = src[ s + 2 ];
			dst[ d + 3 ]   = src[ s + 3 ];
		}
	}
}
} // namespace

Atlas::~Atlas()
{
	// No GL call here. A destructor can run after the context is gone -- on
	// plugin unload it usually does -- and glDeleteTextures on a dead context is
	// undefined rather than merely useless. DeInitGL calls Release() while the
	// context is still current, which is the only safe place for it.
}

void Atlas::Release()
{
	if( mTexture != 0 )
	{
		glDeleteTextures( 1, &mTexture );
		mTexture = 0;
	}
	mLayers    = 0;
	mLayerSize = 0;
	mUploaded  = 0;
	mLogos.clear();
	mSources.clear();
	mScratch.clear();
	mScratch.shrink_to_fit();
}

bool Atlas::Upload( const std::vector< Image >& images )
{
	if( !Begin( images ) )
		return false;

	// No budget: one call, everything. The plugin never takes this path.
	while( !Step( images, static_cast< size_t >( -1 ) ) )
	{
	}
	return true;
}

bool Atlas::Begin( const std::vector< Image >& images )
{
	Release();
	mNote.clear();

	if( images.empty() )
	{
		mNote = "nothing to upload";
		return false;
	}

	GLint maxSize = 0, maxLayers = 0;
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &maxSize );
	glGetIntegerv( GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers );

	// Which layer holds which image and frame, and every logo's geometry. This
	// is arithmetic, not pixels, so it costs nothing worth budgeting -- and
	// doing it here means LayerCount() and Logos() are final the moment Begin()
	// returns, so the layout is built once and the wall can draw while the
	// pixels are still arriving.
	// A driver that answers 0 for either limit gets PlanAtlas's conservative
	// defaults rather than an empty array.
	AtlasPlan plan   = PlanAtlas( images, static_cast< int >( maxSize ), static_cast< int >( maxLayers ) );
	mLayerSize       = plan.layerSize;
	const int layers = static_cast< int >( plan.layers.size() );

	// Drain whatever the host left in the queue before asking the question.
	// glGetError reports the *oldest* unreported error, not the one the previous
	// call raised, and a host is under no obligation to hand the plugin a clean
	// context -- Resolume routinely does not. Without this, one stale
	// GL_INVALID_ENUM from somewhere else in the composition is read as "the GPU
	// refused the array", the atlas is thrown away, and the wall is black for
	// the rest of the session with nothing in front of the operator to say why.
	// That is a black screen caused entirely by another plugin's litter.
	while( glGetError() != GL_NO_ERROR )
	{
	}

	glGenTextures( 1, &mTexture );
	if( mTexture == 0 )
	{
		mNote = "could not create the texture array";
		return false;
	}

	glBindTexture( GL_TEXTURE_2D_ARRAY, mTexture );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, mLayerSize, mLayerSize, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );

	const GLenum uploadError = glGetError();
	if( uploadError != GL_NO_ERROR )
	{
		// Read the size *before* Release(), which zeroes it. Reporting it
		// afterwards said "the GPU refused a 0px array", which is not a size any
		// GPU was ever asked for and sends the reader looking for the wrong bug.
		const int askedSize = mLayerSize;
		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
		Release();
		mNote = "the GPU refused a " + std::to_string( askedSize ) + "px array of " + std::to_string( layers ) +
				" layers (" + std::to_string( ( static_cast< size_t >( askedSize ) * askedSize * 4 * layers ) >> 20 ) +
				" MB, GL error 0x" + std::to_string( static_cast< unsigned >( uploadError ) ) + ")";
		return false;
	}

	// glTexSubImage3D reads tightly packed rows; the default alignment of 4
	// happens to be right for RGBA8 but saying so costs nothing and stops this
	// breaking if the format ever changes.
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );

	mLogos    = std::move( plan.logos );
	mSources  = std::move( plan.layers );
	mLayers   = layers;
	mUploaded = 0;
	mScratch.assign( static_cast< size_t >( mLayerSize ) * mLayerSize * 4, 0 );
	mNote = plan.note;
	return true;
}

bool Atlas::Step( const std::vector< Image >& images, size_t pixelBudget )
{
	if( mTexture == 0 || mUploaded >= mLayers )
		return true;

	// The caller is supposed to pass the same vector it gave Begin(). If it did
	// not, indexing it would read off the end -- say so and stop rather than
	// corrupt the wall or the process.
	if( images.size() < mLogos.size() )
	{
		mNote = "the image list changed under the upload";
		Release();
		return true;
	}

	const size_t perLayer = static_cast< size_t >( mLayerSize ) * mLayerSize;

	glBindTexture( GL_TEXTURE_2D_ARRAY, mTexture );
	// glTexSubImage3D reads tightly packed rows; the default alignment of 4
	// happens to be right for RGBA8 but saying so costs nothing and stops this
	// breaking if the format ever changes. Set per Step: a host is under no
	// obligation to leave the pixel store where the last frame left it.
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	size_t spent = 0;
	// At least one layer per call whatever the budget, or a layer bigger than
	// the budget would never upload and the wall would stay blank for ever.
	while( mUploaded < mLayers && ( spent == 0 || spent + perLayer <= pixelBudget ) )
	{
		const int                i      = mUploaded;
		const AtlasPlan::Layer& source = mSources[ static_cast< size_t >( i ) ];
		const Image&            img    = images[ static_cast< size_t >( source.image ) ];

		std::fill( mScratch.begin(), mScratch.end(), static_cast< uint8_t >( 0 ) );

		// Every frame of an animated logo sits where its first does: one
		// LayerFit, so the frames register exactly on one another.
		const LayerFit p = PlaceIn( img, mLayerSize );
		if( static_cast< size_t >( source.frame ) < img.frames.size() )
			Blit( img.frames[ static_cast< size_t >( source.frame ) ].rgba.data(), img.width, img.height, mScratch,
			      mLayerSize, p.dx, p.dy, p.dw, p.dh );

		glTexSubImage3D( GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, mLayerSize, mLayerSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
		                 mScratch.data() );

		++mUploaded;
		spent += perLayer;
	}

	const bool done = mUploaded >= mLayers;
	if( done )
	{
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		// Clamp, not repeat. A logo that repeats at its layer edge draws a sliver
		// of its own opposite side along the cell border.
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

		// Once, at the end. Regenerating per Step would cost more than the
		// uploads it is budgeting, and every mip built before the last layer
		// landed would be thrown away anyway.
		glGenerateMipmap( GL_TEXTURE_2D_ARRAY );

		// The scratch layer is the size of one texture layer -- up to 16 MB at
		// 2048px -- and nothing needs it again until the next folder.
		mScratch.clear();
		mScratch.shrink_to_fit();
	}
	else
	{
		// A partially filled array still has to be samplable, and
		// GL_LINEAR_MIPMAP_LINEAR without mipmaps reads as incomplete on strict
		// drivers -- which draws nothing at all rather than the logos so far.
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	return done;
}

} // namespace gridiron
