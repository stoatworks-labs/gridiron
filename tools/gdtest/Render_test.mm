// Pixels, from the real plugin class, in a real GL context, with no host.
//
// oxbow proves the bundle registers and instantiates, which is the check that
// catches a plugin containing no plugin. What it cannot do is load a folder:
// `--set` drives float parameters and a FF_TYPE_FILE path arrives through
// SetTextParameter, so under oxbow gridiron correctly draws nothing. This is
// where the wall is actually checked.
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "Atlas.h"
#include "Gridiron.h"
#include "Library.h"

using namespace gridiron;

static int failures = 0;
static void Check( bool ok, const char* what )
{
	printf( "  %s %s\n", ok ? "ok  " : "FAIL", what );
	if( !ok )
		++failures;
}

namespace
{
constexpr int kW = 640;
constexpr int kH = 360;

struct Target
{
	GLuint fbo = 0, colour = 0;

	void Create()
	{
		glGenTextures( 1, &colour );
		glBindTexture( GL_TEXTURE_2D, colour );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glBindTexture( GL_TEXTURE_2D, 0 );

		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
};

std::vector< uint8_t > ReadBack( const Target& t )
{
	std::vector< uint8_t > px( static_cast< size_t >( kW ) * kH * 4, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, t.fbo );
	glReadPixels( 0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	return px;
}

int LitPixels( const std::vector< uint8_t >& px )
{
	int n = 0;
	for( size_t i = 3; i < px.size(); i += 4 )
		if( px[ i ] > 8 )
			++n;
	return n;
}

/// Drive the plugin until its folder has loaded and it has drawn a frame.
std::vector< uint8_t > RenderAt( GridironPlugin& p, const Target& t, double seconds )
{
	ProcessOpenGLStruct gl = {};
	gl.HostFBO             = t.fbo;

	glBindFramebuffer( GL_FRAMEBUFFER, t.fbo );
	glViewport( 0, 0, kW, kH );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	p.SetTime( seconds );
	p.ProcessOpenGL( &gl );
	return ReadBack( t );
}
} // namespace

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		printf( "usage: gdtest_render <logo folder>\n" );
		return 2;
	}
	const std::string anyFile = std::string( argv[ 1 ] ) + "/alpha_round.png";

	CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile,
										(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
										kCGLPFAAccelerated,
										kCGLPFAColorSize,
										(CGLPixelFormatAttribute)24,
										(CGLPixelFormatAttribute)0 };
	CGLPixelFormatObj       pix;
	GLint                   npix = 0;
	if( CGLChoosePixelFormat( attrs, &pix, &npix ) != kCGLNoError )
	{
		printf( "no pixel format\n" );
		return 1;
	}
	CGLContextObj ctx = nullptr;
	if( CGLCreateContext( pix, nullptr, &ctx ) != kCGLNoError )
	{
		printf( "no context\n" );
		return 1;
	}
	CGLSetCurrentContext( ctx );
	printf( "GL %s\n\n", glGetString( GL_VERSION ) );

	Target target;
	target.Create();

	printf( "the wall draws\n" );
	GridironPlugin plugin;

	FFGLViewportStruct vp = { 0, 0, kW, kH };
	Check( plugin.InitGL( &vp ) == FF_SUCCESS, "InitGL succeeds" );

	plugin.SetTextParameter( PT_FOLDER_FILE, anyFile.c_str() );

	// The folder loads on a worker thread, so the first frames legitimately draw
	// nothing. Spin until it lands.
	std::vector< uint8_t > px;
	int                    spins = 0;
	for( ; spins < 400; ++spins )
	{
		px = RenderAt( plugin, target, 0.0 );
		if( LitPixels( px ) > 0 )
			break;
	}
	const int lit = LitPixels( px );
	const int lit0 = lit;// baseline the scroll checks are measured against
	Check( lit > 0, "something is on screen once the folder has loaded" );
	printf( "    %d lit pixels of %d after %d frames\n", lit, kW * kH, spins + 1 );

	Check( glGetError() == GL_NO_ERROR, "and no GL error was raised" );

	// A 6x4 wall of padded cells covers a good part of the frame but nothing
	// like all of it -- a full frame would mean one logo drawn over everything.
	const float coverage = static_cast< float >( lit ) / static_cast< float >( kW * kH );
	Check( coverage > 0.02f && coverage < 0.85f, "coverage looks like a grid, not one stretched logo" );

	printf( "the grid is a grid\n" );
	{
		// Scan through the *middle of a row of cells*, not the middle of the
		// frame. On a 4-row wall the frame's centre line lands exactly in the
		// gutter between rows two and three, where finding no ink at all is the
		// correct answer and a useless test.
		int best = 0;
		for( int row = 0; row < 4; ++row )
		{
			const int y = kH * ( 2 * row + 1 ) / 8;// centre of each of four rows
			int       transitions = 0;
			bool      inInk       = false;
			for( int x = 0; x < kW; ++x )
			{
				const bool ink = px[ ( static_cast< size_t >( y ) * kW + x ) * 4 + 3 ] > 8;
				if( ink != inInk )
				{
					++transitions;
					inInk = ink;
				}
			}
			if( transitions > best )
				best = transitions;
		}
		Check( best >= 4, "a scan through a row of cells crosses several cell gaps" );
		printf( "    %d ink/gap transitions on the busiest row\n", best );
	}

	printf( "motion actually moves\n" );
	{
		plugin.SetFloatParameter( PT_MODE, 3.0f );// Twinkle
		plugin.SetFloatParameter( PT_TWINKLE, 1.0f );
		plugin.SetFloatParameter( PT_SPEED, 0.9f );

		// Clock decides whether the host is talking milliseconds or seconds by
		// measuring host time against the real clock over several frames, so
		// the frames have to be *paced* -- rendered back to back they are the
		// same instant, nothing votes, and it falls back to the wall clock
		// where two immediate renders are identical.
		//
		// 20ms of host time per 20ms of real time is what Resolume does at
		// 50fps, so this exercises the real detection rather than working
		// around it.
		auto paced = [ & ]( double startMs, int frames ) {
			std::vector< uint8_t > out;
			for( int i = 0; i < frames; ++i )
			{
				out = RenderAt( plugin, target, startMs + i * 20.0 );
				std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
			}
			return out;
		};
		auto a = paced( 1000.0, 10 );
		auto b = paced( 3000.0, 10 );

		long diff = 0;
		for( size_t i = 0; i < a.size(); i += 4 )
			diff += std::abs( static_cast< int >( a[ i ] ) - static_cast< int >( b[ i ] ) );
		Check( diff > 10000, "twinkle changes the picture between two moments" );
		printf( "    total red-channel delta %ld\n", diff );
	}

	printf( "the cube draws\n" );
	{
		plugin.SetFloatParameter( PT_MODE, 2.0f );// Rubik
		plugin.SetFloatParameter( PT_TWINKLE, 0.0f );

		std::vector< uint8_t > c;
		for( int i = 0; i < 4; ++i )
			c = RenderAt( plugin, target, 5000.0 + i * 20.0 );

		const int cubeLit = LitPixels( c );
		Check( cubeLit > 0, "Rubik mode puts something on screen" );
		printf( "    %d lit pixels\n", cubeLit );
		Check( glGetError() == GL_NO_ERROR, "and raises no GL error" );

		// A cube is seen in perspective from a corner, so it cannot fill the
		// frame the way a flat wall does.
		const float cubeCoverage = static_cast< float >( cubeLit ) / static_cast< float >( kW * kH );
		Check( cubeCoverage < 0.75f, "and does not fill the frame: it is a cube in perspective" );

		std::vector< uint8_t > d;
		for( int i = 0; i < 4; ++i )
			d = RenderAt( plugin, target, 6500.0 + i * 20.0 );
		long moved = 0;
		for( size_t i = 0; i < c.size(); i += 4 )
			moved += std::abs( static_cast< int >( c[ i ] ) - static_cast< int >( d[ i ] ) );
		Check( moved > 10000, "and the slices are turning" );
	}

	printf( "whole-grid scroll wraps instead of leaving\n" );
	{
		// The wall used to be translated by `scroll * timeSeconds` with no wrap
		// and in world units rather than cells, so any scroll off centre slid
		// the whole step-and-repeat out of the orthographic box within a second
		// or two and it never came back. Every value below returned an empty
		// frame before the wrap landed.
		plugin.SetFloatParameter( PT_MODE, 0.0f );// Static
		plugin.SetFloatParameter( PT_TWINKLE, 0.0f );
		plugin.SetFloatParameter( PT_FADE, 0.0f );

		auto litAt = [ & ]( double startMs ) {
			std::vector< uint8_t > out;
			for( int i = 0; i < 6; ++i )
				out = RenderAt( plugin, target, startMs + i * 20.0 );
			return out;
		};

		const float ends[] = { 0.0f, 0.25f, 0.75f, 1.0f };
		for( float v : ends )
		{
			plugin.SetFloatParameter( PT_SCROLL_Y, 0.5f );
			plugin.SetFloatParameter( PT_SCROLL_X, v );
			// Ten minutes in. Composition time is absolute, so "it survived the
			// first two seconds" proves nothing about a real show.
			const int lit = LitPixels( litAt( 600000.0 ) );
			Check( lit > lit0 / 4, "ScrollX holds the wall on screen ten minutes in" );
			printf( "    ScrollX %.2f -> %d lit\n", v, lit );
		}
		plugin.SetFloatParameter( PT_SCROLL_X, 0.5f );
		for( float v : ends )
		{
			plugin.SetFloatParameter( PT_SCROLL_Y, v );
			const int lit = LitPixels( litAt( 600000.0 ) );
			Check( lit > lit0 / 4, "ScrollY holds the wall on screen ten minutes in" );
			printf( "    ScrollY %.2f -> %d lit\n", v, lit );
		}

		// ...and it is genuinely scrolling, not merely pinned in place.
		plugin.SetFloatParameter( PT_SCROLL_Y, 0.5f );
		plugin.SetFloatParameter( PT_SCROLL_X, 0.8f );
		auto s1 = litAt( 300000.0 );
		auto s2 = litAt( 300500.0 );
		long moved = 0;
		for( size_t i = 0; i < s1.size(); i += 4 )
			moved += std::abs( static_cast< int >( s1[ i ] ) - static_cast< int >( s2[ i ] ) );
		Check( moved > 10000, "and the wall is actually moving" );
		printf( "    red-channel delta between two moments %ld\n", moved );

		plugin.SetFloatParameter( PT_SCROLL_X, 0.5f );
	}

	printf( "the budgeted upload builds the same atlas as the one-shot one\n" );
	{
		// gridiron#4: 190 logos uploaded in one call locked Resolume solid for
		// about a minute. The upload is now spread over frames, and the thing
		// worth proving is that spreading it changes NOTHING about the result --
		// a budget that split a logo across two calls, or a scratch buffer left
		// dirty between them, would show up as a wall that is subtly wrong
		// rather than as a failure.
		std::vector< gridiron::Image > images;
		for( const std::string& f : gridiron::ScanFolder( anyFile ) )
		{
			gridiron::Image img = gridiron::Decode( f, 512 );
			if( img.Valid() )
				images.push_back( std::move( img ) );
		}
		Check( !images.empty(), "the fixture folder decoded" );

		auto readBack = []( gridiron::Atlas& a ) {
			std::vector< uint8_t > bytes( static_cast< size_t >( a.LayerSize() ) * a.LayerSize() * 4 * a.LayerCount() );
			glBindTexture( GL_TEXTURE_2D_ARRAY, a.Texture() );
			glGetTexImage( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data() );
			glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
			return bytes;
		};

		gridiron::Atlas oneShot;
		Check( oneShot.Upload( images ), "Upload() succeeds" );
		const std::vector< uint8_t > wanted = readBack( oneShot );

		gridiron::Atlas budgeted;
		Check( budgeted.Begin( images ), "Begin() succeeds" );

		// The layout is built off these, and it is built before the pixels
		// arrive -- so they have to be final the moment Begin() returns.
		Check( budgeted.LayerCount() == oneShot.LayerCount(), "LayerCount is final after Begin" );
		Check( budgeted.Logos().size() == oneShot.Logos().size(), "Logos are final after Begin" );
		Check( budgeted.Uploading(), "and it knows it has not finished" );

		// A budget of one pixel: the smallest possible, so this takes one step
		// per layer and never more than one -- the "at least one a call" floor.
		int steps = 0;
		const int ceiling = budgeted.LayerCount() + 2;
		while( steps < ceiling && !budgeted.Step( images, 1 ) )
			++steps;
		++steps;
		Check( steps < ceiling, "Step() terminates" );
		Check( steps == budgeted.LayerCount(), "one layer a step at the smallest budget" );
		Check( !budgeted.Uploading(), "and it knows it has finished" );
		printf( "    %d layers in %d steps\n", budgeted.LayerCount(), steps );

		Check( readBack( budgeted ) == wanted, "byte for byte the same atlas" );

		oneShot.Release();
		budgeted.Release();
	}

	printf( "a host that leaves a GL error pending\n" );
	{
		// gridiron#1: black in Resolume, first outside user, first hour.
		//
		// Atlas::Upload asked glGetError() whether its glTexImage3D had worked.
		// glGetError reports the oldest unreported error in the queue, not the
		// one the last call raised -- so an error the *host* left behind reads
		// as "the GPU refused the array". The atlas is dropped, ProcessOpenGL
		// returns early for ever after, and the operator gets a black clip with
		// no way to tell it from an empty folder.
		//
		// Nothing in this file could catch that before, because a test owns its
		// context and never litters in it. Only a host does. So this case
		// litters deliberately.
		GridironPlugin dirty;
		FFGLViewportStruct dvp = { 0, 0, kW, kH };
		Check( dirty.InitGL( &dvp ) == FF_SUCCESS, "InitGL succeeds on a dirty context" );
		dirty.SetTextParameter( PT_FOLDER_FILE, anyFile.c_str() );

		std::vector< uint8_t > dpx;
		for( int i = 0; i < 400; ++i )
		{
			glEnable( (GLenum)0x1234 );// GL_INVALID_ENUM, left pending, as a host would
			dpx = RenderAt( dirty, target, 0.0 );
			if( LitPixels( dpx ) > 0 )
				break;
		}
		const int dirtyLit = LitPixels( dpx );
		Check( dirtyLit > 0, "the wall still draws" );
		printf( "    %d lit pixels\n", dirtyLit );

		// The About field is the only diagnostic gridiron has. If the upload was
		// wrongly abandoned it says so there, so assert on the words too -- a
		// future refactor could make this draw for some other reason.
		const std::string about = dirty.GetTextParameter( PT_ABOUT_TEXT );
		Check( about.find( "refused" ) == std::string::npos, "and does not claim the GPU refused the array" );

		dirty.DeInitGL();
		while( glGetError() != GL_NO_ERROR )
		{
		}
	}

	printf( "teardown\n" );
	Check( plugin.DeInitGL() == FF_SUCCESS, "DeInitGL succeeds" );
	Check( glGetError() == GL_NO_ERROR, "with no GL error" );

	printf( "\n%s\n", failures ? "FAILED" : "all checks passed" );
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( ctx );
	return failures ? 1 : 0;
}
