// Decoding checks against real files on disk, and the atlas plan built from
// them. No GL, no host.
#include "../../source/AtlasPlan.h"
#include "../../source/Library.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace gridiron;

static int failures = 0;
static void Check( bool ok, const char* what )
{
	printf( "  %s %s\n", ok ? "ok  " : "FAIL", what );
	if( !ok )
		++failures;
}

static const Image* Find( const std::vector< Image >& v, const char* name )
{
	for( const auto& i : v )
		if( i.name == name )
			return &i;
	return nullptr;
}

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		printf( "usage: librarytest <folder>\n" );
		return 2;
	}
	const std::string folder = argv[ 1 ];
	const std::string anyFile = folder + "/alpha_round.png";

	printf( "scan\n" );
	auto files = ScanFolder( anyFile );
	Check( files.size() == 7, "seven supported files found, .txt and ._ resource fork ignored" );
	bool sorted = true;
	for( size_t i = 1; i < files.size(); ++i )
		if( files[ i - 1 ] > files[ i ] )
			sorted = false;
	Check( sorted, "listing is sorted by name, so indices are stable across machines" );
	Check( ScanFolder( folder ).size() == files.size(), "being handed the directory itself works too" );

	printf( "decode\n" );
	std::vector< Image > images;
	for( const auto& f : files )
	{
		Image i = Decode( f, 512 );
		if( i.Valid() )
			images.push_back( std::move( i ) );
	}
	Check( images.size() == 7, "all seven decode" );

	const Image* png = Find( images, "alpha_round.png" );
	Check( png && png->width == 256 && png->height == 256, "PNG decodes at its own size" );
	Check( png && !png->Animated(), "and is a still" );
	bool hasAlpha = false;
	if( png )
		for( size_t i = 3; i < png->frames[ 0 ].rgba.size(); i += 4 )
			if( png->frames[ 0 ].rgba[ i ] == 0 )
			{
				hasAlpha = true;
				break;
			}
	Check( hasAlpha, "alpha survives -- a logo with a transparent ground stays transparent" );

	const Image* wide = Find( images, "bravo_wide.png" );
	Check( wide && wide->aspect > 3.9f && wide->aspect < 4.1f, "a 4:1 wordmark reports aspect 4" );

	const Image* jpg = Find( images, "charlie.jpg" );
	Check( jpg && jpg->width == 300, "JPEG decodes" );

	const Image* gif = Find( images, "delta_anim.gif" );
	Check( gif && gif->Animated() && gif->frames.size() == 4, "animated GIF gives four frames" );
	bool framesDiffer = false;
	if( gif && gif->frames.size() > 1 )
		framesDiffer = gif->frames[ 0 ].rgba != gif->frames[ 3 ].rgba;
	Check( framesDiffer, "and the frames are actually different pictures" );

	printf( "vector\n" );
	const Image* svg = Find( images, "echo_outlined.svg" );
	Check( svg && svg->vector, "an SVG is marked as vector" );
	Check( svg && svg->width == 512, "and rasterises to the requested size, not its declared one" );
	Check( svg && svg->aspect > 1.9f && svg->aspect < 2.1f, "aspect comes from the document, not the raster" );
	bool inked = false;
	if( svg )
		for( size_t i = 3; i < svg->frames[ 0 ].rgba.size(); i += 4 )
			if( svg->frames[ 0 ].rgba[ i ] > 200 )
			{
				inked = true;
				break;
			}
	Check( inked, "and it actually drew something" );

	// The whole point of keeping the document: draw it again, bigger, sharp.
	if( svg )
	{
		Image again = Decode( folder + "/echo_outlined.svg", 128 );
		Check( again.width == 128, "the same file at 128px" );
		Rerasterise( again, 1024 );
		Check( again.width == 1024, "re-rasterises to 1024 from the retained document" );
		bool inkedBig = false;
		for( size_t i = 3; i < again.frames[ 0 ].rgba.size(); i += 4 )
			if( again.frames[ 0 ].rgba[ i ] > 200 )
			{
				inkedBig = true;
				break;
			}
		Check( inkedBig, "and still draws" );
	}

	printf( "trim -- inconsistent margins are the normal case in a sponsor pack\n" );
	{
		Image padded = Decode( folder + "/golf_padded.png", 512 );
		Check( padded.Valid(), "the padded fixture decodes" );
		// An 8:1 bar centred in a 400x400 canvas.
		Check( padded.aspect > 0.99f && padded.aspect < 1.01f, "raw aspect is 1: the canvas is square" );
		Check( padded.trimmedAspect > 7.5f && padded.trimmedAspect < 8.5f,
			   "trimmed aspect is 8: the artwork is a wordmark and now says so" );
		Check( padded.trimX0 > 0.05f && padded.trimX1 < 0.95f, "the trim box excludes the horizontal air" );
		Check( padded.trimY0 > 0.3f && padded.trimY1 < 0.7f, "and the vertical air" );

		const Image* tight = Find( images, "alpha_round.png" );
		Check( tight && tight->trimX0 < 0.09f && tight->trimX1 > 0.91f,
			   "a logo drawn to its edges is not trimmed away" );
	}

	printf( "an animated logo's trim holds every frame\n" );
	{
		// The trim was measured on frame 0, which was all the wall ever showed.
		// A GIF that plays needs a box holding all of it, or Trim margins crops
		// whatever the later frames draw outside the first.
		Image anim;
		anim.width  = 10;
		anim.height = 10;
		anim.aspect = 1.0f;
		for( int k = 0; k < 2; ++k )
		{
			Frame f;
			f.rgba.assign( 10 * 10 * 4, 0 );
			const int x = k == 0 ? 2 : 7;// a dot on the left, then on the right
			f.rgba[ ( static_cast< size_t >( 5 ) * 10 + x ) * 4 + 3 ] = 255;
			anim.frames.push_back( f );
		}
		ComputeTrim( anim );
		Check( anim.trimX0 < 0.25f && anim.trimX1 > 0.75f, "the box spans the dot in frame 0 and the dot in frame 1" );
	}

	printf( "the atlas plan\n" );
	{
		const AtlasPlan plan = PlanAtlas( images, 8192, 2048 );
		Check( plan.logos.size() == images.size(), "one logo per image" );
		Check( plan.layers.size() == images.size() + 3, "and one layer per frame: the four-frame GIF takes four" );

		bool contiguous = true, timed = true, boxes = true;
		for( const Logo& l : plan.logos )
		{
			const Image& img    = images[ static_cast< size_t >( l.index ) ];
			const int    frames = img.Animated() ? static_cast< int >( img.frames.size() ) : 1;
			for( int k = 0; k < frames; ++k )
			{
				const auto& layer = plan.layers[ static_cast< size_t >( l.layer + k ) ];
				if( layer.image != l.index || layer.frame != k )
					contiguous = false;
			}
			if( img.Animated() ? l.frameMs.size() != img.frames.size() : !l.frameMs.empty() )
				timed = false;

			// The artwork's box in its layer has the artwork's shape, give or
			// take the whole pixel PlaceIn rounds to, and holds the trim box.
			const float w = ( l.imageX1 - l.imageX0 ) * static_cast< float >( plan.layerSize );
			const float h = ( l.imageY1 - l.imageY0 ) * static_cast< float >( plan.layerSize );
			if( std::fabs( w / h - img.aspect ) > img.aspect * 2.0f / std::min( w, h ) )
				boxes = false;
			if( l.trimX0 < l.imageX0 - 1e-6f || l.trimX1 > l.imageX1 + 1e-6f || l.trimY0 < l.imageY0 - 1e-6f ||
				l.trimY1 > l.imageY1 + 1e-6f )
				boxes = false;
		}
		Check( contiguous, "a logo's frames are the layers straight after its first" );
		Check( timed, "the GIF carries its frame delays, and a still none" );
		Check( boxes, "every logo's image box has its own aspect and holds its trim box" );

		const Logo* wide = nullptr;
		for( const Logo& l : plan.logos )
			if( l.name == "bravo_wide.png" )
				wide = &l;
		Check( wide && std::fabs( wide->imageX1 - wide->imageX0 - 1.0f ) < 1e-6f &&
				   std::fabs( ( wide->imageY1 - wide->imageY0 ) - 0.25f ) < 0.01f,
			   "a 4:1 wordmark spans its square layer's width and a quarter of its height" );

		// No room: a GPU with two layers to spare cannot hold three more frames.
		// The GIF is drawn as its first frame, and the note says why.
		const AtlasPlan tight = PlanAtlas( images, 8192, static_cast< int >( images.size() ) + 2 );
		Check( tight.layers.size() == images.size() && tight.stilled == 1 && tight.animated == 0,
			   "with no room for its frames the GIF is one layer" );
		bool stillLogo = true;
		for( const Logo& l : tight.logos )
			if( l.frameMs.size() > 1 )
				stillLogo = false;
		Check( stillLogo, "and a still as far as the layout knows" );
		Check( tight.note.find( "drawn still" ) != std::string::npos, "and the note says so" );
		printf( "    note: %s\n", plan.note.c_str() );
		printf( "    note: %s\n", tight.note.c_str() );

		// The byte budget. A layer is the size of the folder's largest logo, so
		// at 2048px every frame is 16 MB: fifteen past the first fit in 256 MB
		// and nineteen do not. Planning reads sizes and delays, not pixels.
		auto big = []( int frames ) {
			Image img;
			img.name   = "big.gif";
			img.width  = 2048;
			img.height = 2048;
			img.frames.resize( static_cast< size_t >( frames ) );
			return img;
		};
		std::vector< Image > fits, over;
		fits.push_back( big( 16 ) );
		over.push_back( big( 20 ) );
		Check( PlanAtlas( fits, 8192, 2048 ).animated == 1, "a 16-frame GIF at 2048px fits the frame budget" );
		Check( PlanAtlas( over, 8192, 2048 ).stilled == 1, "a 20-frame one does not, and is drawn still" );
	}

	printf( "the silent failure\n" );
	const Image* live = Find( images, "foxtrot_livetext.svg" );
	Check( live != nullptr, "an SVG with live text still parses -- it does not error" );
	Check( live && live->dropped.text > 0, "but the text elements are counted" );
	Check( live && live->note.find( "outlines" ) != std::string::npos,
		   "and the note tells the operator to convert text to outlines" );

	printf( "loader\n" );
	{
		Loader l;
		l.Start( anyFile, 512 );
		for( int i = 0; i < 2000 && !l.Ready(); ++i )
			;
		while( !l.Ready() )
			;
		auto v = l.Take();
		Check( v.size() == 7, "the threaded loader returns the same seven" );
		Check( l.Note().find( "live text" ) != std::string::npos, "and surfaces the live-text warning in its note" );

		// Regression. Take() moves out of the member, so without clearing the
		// flag a caller that polls Ready() every frame -- which is exactly what
		// the plugin does -- takes the folder once and then takes nothing,
		// wiping the atlas on the frame after the one that filled it.
		Check( !l.Ready(), "Ready() is false once the folder has been taken" );
		Check( l.Take().empty(), "and a second Take is empty rather than pretending" );
		printf( "    note: %s\n", l.Note().c_str() );
	}

	printf( "\n%s\n", failures ? "FAILED" : "all checks passed" );
	return failures ? 1 : 0;
}
