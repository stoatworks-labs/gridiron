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
	mLogos.clear();
}

int ChooseLayerSize( int cellPixels )
{
	int size = 64;
	while( size < cellPixels && size < 2048 )
		size *= 2;
	return size;
}

bool Atlas::Upload( const std::vector< Image >& images )
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

	glGenTextures( 1, &mTexture );
	if( mTexture == 0 )
	{
		mNote = "could not create the texture array";
		return false;
	}

	glBindTexture( GL_TEXTURE_2D_ARRAY, mTexture );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, mLayerSize, mLayerSize, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );

	if( glGetError() != GL_NO_ERROR )
	{
		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
		Release();
		mNote = "the GPU refused a " + std::to_string( mLayerSize ) + "px array of " + std::to_string( layers ) + " layers";
		return false;
	}

	// glTexSubImage3D reads tightly packed rows; the default alignment of 4
	// happens to be right for RGBA8 but saying so costs nothing and stops this
	// breaking if the format ever changes.
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

	std::vector< uint8_t > layer( static_cast< size_t >( mLayerSize ) * mLayerSize * 4, 0 );

	for( int i = 0; i < layers; ++i )
	{
		const Image& img = images[ static_cast< size_t >( i ) ];

		std::fill( layer.begin(), layer.end(), static_cast< uint8_t >( 0 ) );

		// Fit the artwork into the layer, keeping its aspect. The padding stays
		// transparent, which is what makes bleeding harmless.
		int dw = mLayerSize, dh = mLayerSize;
		if( img.aspect >= 1.0f )
			dh = std::max( 1, static_cast< int >( static_cast< float >( mLayerSize ) / img.aspect + 0.5f ) );
		else
			dw = std::max( 1, static_cast< int >( static_cast< float >( mLayerSize ) * img.aspect + 0.5f ) );
		dw = std::min( dw, mLayerSize );
		dh = std::min( dh, mLayerSize );

		const int dx = ( mLayerSize - dw ) / 2;
		const int dy = ( mLayerSize - dh ) / 2;

		if( !img.frames.empty() )
			Blit( img.frames[ 0 ].rgba.data(), img.width, img.height, layer, mLayerSize, dx, dy, dw, dh );

		glTexSubImage3D( GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, mLayerSize, mLayerSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, layer.data() );

		// The logo's geometry, expressed against the *layer* rather than against
		// the original file -- which is what the shader will be sampling.
		Logo l;
		l.index  = i;
		l.name   = img.name;
		l.aspect = img.aspect;

		const float fx = static_cast< float >( dw ) / static_cast< float >( mLayerSize );
		const float fy = static_cast< float >( dh ) / static_cast< float >( mLayerSize );
		const float ox = static_cast< float >( dx ) / static_cast< float >( mLayerSize );
		const float oy = static_cast< float >( dy ) / static_cast< float >( mLayerSize );

		// The trim box was measured on the file; move it into layer space.
		l.trimX0        = ox + img.trimX0 * fx;
		l.trimX1        = ox + img.trimX1 * fx;
		l.trimY0        = oy + img.trimY0 * fy;
		l.trimY1        = oy + img.trimY1 * fy;
		l.trimmedAspect = img.trimmedAspect;

		mLogos.push_back( l );
	}

	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	// Clamp, not repeat. A logo that repeats at its layer edge draws a sliver of
	// its own opposite side along the cell border.
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glGenerateMipmap( GL_TEXTURE_2D_ARRAY );

	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );

	mLayers = layers;
	if( mNote.empty() )
		mNote = std::to_string( layers ) + " logos at " + std::to_string( mLayerSize ) + "px";
	return true;
}

} // namespace gridiron
