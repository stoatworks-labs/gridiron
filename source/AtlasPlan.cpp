#include "AtlasPlan.h"

#include <algorithm>

namespace gridiron
{
int ChooseLayerSize( int cellPixels )
{
	int size = 64;
	while( size < cellPixels && size < 2048 )
		size *= 2;
	return size;
}

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

AtlasPlan PlanAtlas( const std::vector< Image >& images, int maxSize, int maxLayers )
{
	AtlasPlan plan;
	if( images.empty() )
		return plan;

	if( maxSize <= 0 )
		maxSize = 2048;
	if( maxLayers <= 0 )
		maxLayers = 256;

	// The largest logo decides the layer edge, so nothing is downsampled that
	// did not have to be.
	int longest = 0;
	for( const Image& i : images )
		longest = std::max( longest, std::max( i.width, i.height ) );

	plan.layerSize = std::min( ChooseLayerSize( longest ), maxSize );

	int count = static_cast< int >( images.size() );
	if( count > maxLayers )
	{
		plan.note = "folder has " + std::to_string( count ) + " images but this GPU allows " +
					std::to_string( maxLayers ) + " layers; the rest are ignored";
		count = maxLayers;
	}

	// Which animated logos get their frames. Every logo has its first layer
	// whatever happens, so the frames past it are what is rationed: in folder
	// order, while both the GPU's layer limit and the byte budget hold.
	const size_t perLayer = static_cast< size_t >( plan.layerSize ) * static_cast< size_t >( plan.layerSize ) * 4;
	int          total    = count;
	size_t       spent    = 0;

	std::vector< bool > plays( static_cast< size_t >( count ), false );
	for( int i = 0; i < count; ++i )
	{
		const Image& img = images[ static_cast< size_t >( i ) ];
		if( !img.Animated() )
			continue;

		const int    extra = static_cast< int >( img.frames.size() ) - 1;
		const size_t bytes = static_cast< size_t >( extra ) * perLayer;
		if( total + extra <= maxLayers && spent + bytes <= kFrameBudgetBytes )
		{
			plays[ static_cast< size_t >( i ) ] = true;
			total += extra;
			spent += bytes;
			++plan.animated;
		}
		else
		{
			++plan.stilled;
		}
	}

	// Every logo's geometry, and its layers: a logo's frames are contiguous, so
	// the renderer finds frame k at `layer + k`.
	for( int i = 0; i < count; ++i )
	{
		const Image& img = images[ static_cast< size_t >( i ) ];

		Logo l;
		l.index  = i;
		l.name   = img.name;
		l.aspect = img.aspect;
		l.layer  = static_cast< int >( plan.layers.size() );

		const LayerFit p = PlaceIn( img, plan.layerSize );

		const float fx = static_cast< float >( p.dw ) / static_cast< float >( plan.layerSize );
		const float fy = static_cast< float >( p.dh ) / static_cast< float >( plan.layerSize );
		const float ox = static_cast< float >( p.dx ) / static_cast< float >( plan.layerSize );
		const float oy = static_cast< float >( p.dy ) / static_cast< float >( plan.layerSize );

		// Where the artwork is in its layer. Fit, Fill and Stretch work on this.
		l.imageX0 = ox;
		l.imageX1 = ox + fx;
		l.imageY0 = oy;
		l.imageY1 = oy + fy;

		// The trim box was measured on the file; move it into layer space.
		l.trimX0        = ox + img.trimX0 * fx;
		l.trimX1        = ox + img.trimX1 * fx;
		l.trimY0        = oy + img.trimY0 * fy;
		l.trimY1        = oy + img.trimY1 * fy;
		l.trimmedAspect = img.trimmedAspect;

		const int frames = plays[ static_cast< size_t >( i ) ] ? static_cast< int >( img.frames.size() ) : 1;
		for( int k = 0; k < frames; ++k )
		{
			plan.layers.push_back( { i, k } );
			if( frames > 1 )
				l.frameMs.push_back( img.frames[ static_cast< size_t >( k ) ].delayMs );
		}

		plan.logos.push_back( l );
	}

	if( plan.note.empty() )
	{
		plan.note = std::to_string( count ) + " logos at " + std::to_string( plan.layerSize ) + "px";
		if( plan.animated > 0 )
			plan.note += ", " + std::to_string( plan.animated ) + " animated (" + std::to_string( plan.layers.size() ) +
						 " layers)";
	}
	if( plan.stilled > 0 )
		plan.note += "; " + std::to_string( plan.stilled ) + " animated GIF" + ( plan.stilled == 1 ? " is" : "s are" ) +
					 " drawn still: the atlas has no room for " + ( plan.stilled == 1 ? "its" : "their" ) + " frames";
	return plan;
}

} // namespace gridiron
