// The real plugin's frames, for comparing with the page's.
//
//     frames <logo folder> <width> <height> < frames.txt > frames.rgba
//     frames <logo folder> geometry > plugin-geometry.txt
//
// Drives GridironPlugin -- the class Resolume loads, with the real stb_image
// and nanosvg decoders, the real atlas and the real shaders -- in a headless
// GL 4.1 context, and writes each frame's pixels (premultiplied RGBA8, bottom
// row first, as glReadPixels gives them) to stdout, one after another.
//
// Each line of frames.txt is `time name=value ...`, with the page's parameter
// ids (frames.mjs writes it and drives the page through the same lines). A
// value is what the host sends: 0..1 for a slider, the index for an option,
// the integer for an integer.
//
// `geometry` prints, instead, what the plugin's own Library and Atlas make of
// the folder -- stb_image and nanosvg decoding it, the trim measured on the
// result, each logo placed in its layer -- one line per logo: name, aspect,
// the trim box in layer space and the trimmed aspect. frames.mjs prints the
// page's the same way, so the two decoders can be compared logo by logo.
//
// Before the first frame it spins the plugin, feeding it the wall clock as
// seconds, until the folder has loaded, the atlas has filled and the plugin's
// Clock has voted for seconds -- after which SetTime(t) means t seconds
// exactly, and the frame is the one the page draws at the same t.
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Atlas.h"
#include "Gridiron.h"
#include "Library.h"

using namespace gridiron;

namespace
{
struct Named
{
	const char*  id;
	unsigned int param;
};

// The page's ids (demo/plugin.js), to ParamId.
const Named kNames[] = {
	{ "folderOverride", PT_FOLDER_PATH }, { "reload", PT_RELOAD },
	{ "columns", PT_COLUMNS }, { "rows", PT_ROWS }, { "fit", PT_FIT }, { "cellPad", PT_CELL_PAD },
	{ "showGrid", PT_GRID_LINES }, { "gridWidth", PT_GRID_WIDTH },
	{ "gridR", PT_GRID_R }, { "gridG", PT_GRID_G }, { "gridB", PT_GRID_B },
	{ "seed", PT_SEED }, { "varyNeighbours", PT_NO_ADJACENT }, { "respectAspect", PT_ASPECT_AWARE },
	{ "equalAirtime", PT_EQUAL_AIRTIME },
	{ "hero", PT_HERO }, { "heroColumns", PT_HERO_COLS }, { "heroRows", PT_HERO_ROWS }, { "heroLogo", PT_HERO_LOGO },
	{ "mode", PT_MODE }, { "speed", PT_SPEED }, { "scrollX", PT_SCROLL_X }, { "scrollY", PT_SCROLL_Y },
	{ "gridRotation", PT_GRID_ROT }, { "walk", PT_WALK }, { "walkRate", PT_WALK_RATE },
	{ "twinkle", PT_TWINKLE }, { "fade", PT_FADE },
	{ "projection", PT_PROJECTION }, { "fov", PT_FOV }, { "camYaw", PT_CAM_YAW }, { "camPitch", PT_CAM_PITCH },
	{ "edge", PT_EDGE }, { "edgeScope", PT_EDGE_SCOPE }, { "edgeAmount", PT_EDGE_AMOUNT },
	{ "edgeThreshold", PT_EDGE_THRESHOLD },
	{ "cellFill", PT_CELL_FILL }, { "fillR", PT_FILL_R }, { "fillG", PT_FILL_G }, { "fillB", PT_FILL_B },
};

int Lookup( const std::string& id )
{
	for( const Named& n : kNames )
		if( id == n.id )
			return static_cast< int >( n.param );
	return -1;
}
} // namespace

int main( int argc, char** argv )
{
	const bool geometry = argc == 3 && std::string( argv[ 2 ] ) == "geometry";
	if( argc < 4 && !geometry )
	{
		std::fprintf( stderr, "usage: frames <logo folder> <width> <height> < frames.txt > frames.rgba\n"
							  "       frames <logo folder> geometry\n" );
		return 2;
	}
	const std::string folder = argv[ 1 ];
	const int         W      = geometry ? 64 : std::atoi( argv[ 2 ] );
	const int         H      = geometry ? 64 : std::atoi( argv[ 3 ] );

	CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
										kCGLPFAAccelerated, (CGLPixelFormatAttribute)0 };
	CGLPixelFormatObj       pix;
	GLint                   npix = 0;
	if( CGLChoosePixelFormat( attrs, &pix, &npix ) != kCGLNoError )
		return 1;
	CGLContextObj ctx = nullptr;
	if( CGLCreateContext( pix, nullptr, &ctx ) != kCGLNoError )
		return 1;
	CGLSetCurrentContext( ctx );

	GLuint tex = 0, fbo = 0;
	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	if( geometry )
	{
		// What PollLoader asks the Loader for: every file ScanFolder keeps,
		// decoded at 512 px, then Atlas::Begin's geometry.
		std::vector< Image > images;
		for( const std::string& path : ScanFolder( folder + "/any" ) )
		{
			Image img = Decode( path, 512 );
			if( img.Valid() )
				images.push_back( std::move( img ) );
			else
				std::printf( "# %s did not load: %s\n", path.c_str(), img.note.c_str() );
		}
		Atlas atlas;
		atlas.Begin( images );
		for( const Logo& l : atlas.Logos() )
			std::printf( "%s %.6f %.6f %.6f %.6f %.6f %.6f\n", l.name.c_str(), l.aspect, l.trimX0, l.trimY0, l.trimX1, l.trimY1,
						 l.trimmedAspect );
		atlas.Release();
		return 0;
	}

	GridironPlugin     plugin;
	FFGLViewportStruct vp = { 0, 0, (FFUInt32)W, (FFUInt32)H };
	if( plugin.InitGL( &vp ) != FF_SUCCESS )
		return 1;

	// The interaction the plugin is built round: any file inside the folder.
	const std::string anyFile = folder + "/resolume-arena.svg";
	plugin.SetTextParameter( PT_FOLDER_FILE, anyFile.c_str() );

	std::vector< uint8_t > px( static_cast< size_t >( W ) * H * 4 );
	auto frame = [ & ]( double t ) {
		ProcessOpenGLStruct g = {};
		g.HostFBO             = fbo;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, W, H );
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		plugin.SetTime( t );
		plugin.ProcessOpenGL( &g );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	};

	// Spin on the wall clock, in seconds, so the folder lands, the atlas fills
	// and the Clock locks to seconds (it needs four agreeing frames).
	const auto start = std::chrono::steady_clock::now();
	for( int i = 0; i < 150; ++i )
	{
		const double now = std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count();
		frame( now );
		std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
	}

	std::string line;
	int         n = 0;
	while( std::getline( std::cin, line ) )
	{
		std::istringstream in( line );
		double             t = 0.0;
		if( !( in >> t ) )
			continue;
		std::string kv;
		while( in >> kv )
		{
			const size_t eq = kv.find( '=' );
			const int    id = Lookup( kv.substr( 0, eq ) );
			if( id < 0 )
			{
				std::fprintf( stderr, "frames: no parameter %s\n", kv.c_str() );
				return 1;
			}
			plugin.SetFloatParameter( static_cast< unsigned >( id ), std::strtof( kv.substr( eq + 1 ).c_str(), nullptr ) );
		}
		// Twice: the first frame after a change rebuilds the schedule and is
		// otherwise identical; the second is the one compared.
		frame( t );
		frame( t );
		std::fwrite( px.data(), 1, px.size(), stdout );
		++n;
	}
	std::fprintf( stderr, "frames: %d frame(s) of %dx%d, about: %s\n", n, W, H, plugin.GetTextParameter( PT_ABOUT_TEXT ) );

	plugin.DeInitGL();
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( ctx );
	return 0;
}
