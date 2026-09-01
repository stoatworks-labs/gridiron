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
	mScratch.clear();
	mScratch.shrink_to_fit();
}

int ChooseLayerSize( int cellPixels )
{
	int size = 64;
	while( size < cellPixels && size < 2048 )
		size *= 2;
	return size;
}

namespace
{
/// Where one image sits inside a square layer, keeping its aspect.
///
/// Pulled out of the upload loop because Begin() needs it for the geometry and
/// Step() needs it for the blit, and the two computing it separately is a
/// silent mis-registration waiting to happen -- the trim box would describe a
/// rectangle the pixels are not in.
struct LayerFit
{
	int dx = 0, dy = 0, dw = 0, dh = 0;
};

LayerFit PlaceIn( const Image& img, int layerSize )
{
	LayerFit p;
	p.dw = layerSize;
	p.dh = layerSize;

	if( img.aspect >= 1.0f )
		p.dh = std::max( 1, static_cast< int >( static_cast< float >( layerSize ) / img.aspect + 0.5f ) );
	else
		p.dw = std::max( 1, static_cast< int >( static_cast< float >( layerSize ) * img.aspect + 0.5f ) );

	p.dw = std::min( p.dw, layerSize );
	p.dh = std::min( p.dh, layerSize );

	// The padding stays transparent, which is what makes bleeding harmless.
	p.dx = ( layerSize - p.dw ) / 2;
	p.dy = ( layerSize - p.dh ) / 2;
	return p;
}
} // namespace

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
	if( maxSize <= 0 )
		maxSize = 2048;
	if( maxLayers <= 0 )
		maxLayers = 256;

	// The largest logo decides the layer edge, so nothing is downsampled that
	// did not have to be.
	int longest = 0;
	for( const Image& i : images )
		longest = std::max( longest, std::max( i.width, i.height ) );

	mLayerSize = std::min( ChooseLayerSize( longest ), static_cast< int >( maxSize ) );

	int layers = static_cast< int >( images.size() );
	if( layers > maxLayers )
	{
		mNote  = "folder has " + std::to_string( layers ) + " images but this GPU allows " +
				std::to_string( maxLayers ) + " layers; the rest are ignored";
		layers = static_cast< int >( maxLayers );
	}

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

	// Every logo's geometry, up front. This is arithmetic, not pixels, so it
	// costs nothing worth budgeting -- and doing it here means LayerCount() and
	// Logos() are final the moment Begin() returns, so the layout is built once
	// and the wall can draw while the pixels are still arriving.
	for( int i = 0; i < layers; ++i )
	{
		const Image& img = images[ static_cast< size_t >( i ) ];

		Logo l;
		l.index  = i;
		l.name   = img.name;
		l.aspect = img.aspect;

		const LayerFit p = PlaceIn( img, mLayerSize );

		const float fx = static_cast< float >( p.dw ) / static_cast< float >( mLayerSize );
		const float fy = static_cast< float >( p.dh ) / static_cast< float >( mLayerSize );
		const float ox = static_cast< float >( p.dx ) / static_cast< float >( mLayerSize );
		const float oy = static_cast< float >( p.dy ) / static_cast< float >( mLayerSize );

		// The trim box was measured on the file; move it into layer space.
		l.trimX0        = ox + img.trimX0 * fx;
		l.trimX1        = ox + img.trimX1 * fx;
		l.trimY0        = oy + img.trimY0 * fy;
		l.trimY1        = oy + img.trimY1 * fy;
		l.trimmedAspect = img.trimmedAspect;

		mLogos.push_back( l );
	}

	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );

	mLayers   = layers;
	mUploaded = 0;
	mScratch.assign( static_cast< size_t >( mLayerSize ) * mLayerSize * 4, 0 );

	if( mNote.empty() )
		mNote = std::to_string( layers ) + " logos at " + std::to_string( mLayerSize ) + "px";
	return true;
}

bool Atlas::Step( const std::vector< Image >& images, size_t pixelBudget )
{
	if( mTexture == 0 || mUploaded >= mLayers )
		return true;

	// The caller is supposed to pass the same vector it gave Begin(). If it did
	// not, indexing it would read off the end -- say so and stop rather than
	// corrupt the wall or the process.
	if( images.size() < static_cast< size_t >( mLayers ) )
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
		const int i      = mUploaded;
		const Image& img = images[ static_cast< size_t >( i ) ];

		std::fill( mScratch.begin(), mScratch.end(), static_cast< uint8_t >( 0 ) );

		const LayerFit p = PlaceIn( img, mLayerSize );
		if( !img.frames.empty() )
			Blit( img.frames[ 0 ].rgba.data(), img.width, img.height, mScratch, mLayerSize, p.dx, p.dy, p.dw, p.dh );

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
