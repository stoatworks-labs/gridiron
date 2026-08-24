// Decoding checks against real files on disk. No GL, no host.
#include "../../source/Library.h"

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
