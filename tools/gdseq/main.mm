// Render a cue sheet through the real plugin class, for the project video.
//
// The footage is a RENDER, not a screen recording, and the end card says so.
// Same reason as flipbook, orrery and old-cathode: an FFGL plugin has no window
// and no UI of its own, its control surface is Resolume's inspector, and driving
// Arena means clicking a clip grid and an effects browser that are both
// custom-drawn with nothing in the accessibility tree to address. So the footage
// comes from the plugin's own harness instead -- every frame is the real
// GridironPlugin, the same code Resolume loads, being operated.
//
// Frames go out as raw RGBA on one stream, because ffmpeg reads that directly
// and PNG-per-frame would spend more time compressing than rendering.
//
//     ./build/gdseq --out /tmp/g.rgba --script tools/video.cues \
//                   --logos demo/logos --size 1920x1080 --seconds 30 --fps 30
//
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "Gridiron.h"

using namespace gridiron;

namespace
{
/// Cue-sheet names. Deliberately single tokens: the parser splits the line on
/// whitespace, so "Cell Padding=0.2" would be read as a time and a fragment.
struct Named
{
	const char*  name;
	unsigned int id;
};
const Named kNames[] = {
	{ "Columns", PT_COLUMNS }, { "Rows", PT_ROWS }, { "Fit", PT_FIT },
	{ "CellPad", PT_CELL_PAD }, { "ShowGrid", PT_GRID_LINES }, { "GridWidth", PT_GRID_WIDTH },
	{ "GridR", PT_GRID_R }, { "GridG", PT_GRID_G }, { "GridB", PT_GRID_B },
	{ "Seed", PT_SEED }, { "NoAdjacent", PT_NO_ADJACENT }, { "Aspect", PT_ASPECT_AWARE },
	{ "EqualAirtime", PT_EQUAL_AIRTIME },
	{ "Hero", PT_HERO }, { "HeroCols", PT_HERO_COLS }, { "HeroRows", PT_HERO_ROWS },
	{ "HeroLogo", PT_HERO_LOGO },
	{ "Mode", PT_MODE }, { "Speed", PT_SPEED }, { "ScrollX", PT_SCROLL_X },
	{ "ScrollY", PT_SCROLL_Y }, { "GridRot", PT_GRID_ROT },
	{ "Walk", PT_WALK }, { "WalkRate", PT_WALK_RATE },
	{ "Twinkle", PT_TWINKLE }, { "Fade", PT_FADE },
	{ "Projection", PT_PROJECTION }, { "Fov", PT_FOV },
	{ "CamYaw", PT_CAM_YAW }, { "CamPitch", PT_CAM_PITCH },
	{ "Edge", PT_EDGE }, { "EdgeScope", PT_EDGE_SCOPE },
	{ "EdgeAmount", PT_EDGE_AMOUNT }, { "EdgeThreshold", PT_EDGE_THRESHOLD },
};

int Lookup( const std::string& name )
{
	for( const Named& n : kNames )
		if( name == n.name )
			return static_cast< int >( n.id );
	return -1;
}

struct Cue
{
	double      from = 0.0, to = 0.0;
	std::string name;
	float       first = 0.0f, second = 0.0f;
	bool        ramp = false;
};

/// `T Name=V` sets at a time; `T1..T2 Name=V1..V2` ramps between two. Adapted
/// from flipbook, whose cue sheets read the same way on purpose -- the series
/// is cut from these and one syntax across the fleet is one thing to remember.
bool ParseCues( const std::string& path, std::vector< Cue >& cues )
{
	std::FILE* file = std::fopen( path.c_str(), "rb" );
	if( file == nullptr )
	{
		std::fprintf( stderr, "cannot open cue sheet %s\n", path.c_str() );
		return false;
	}
	char line[ 1024 ];
	int  number = 0;
	while( std::fgets( line, sizeof( line ), file ) != nullptr )
	{
		++number;
		std::string text = line;
		const size_t hash = text.find( '#' );
		if( hash != std::string::npos )
			text = text.substr( 0, hash );
		const size_t firstReal = text.find_first_not_of( " \t\r\n" );
		if( firstReal == std::string::npos )
			continue;
		text = text.substr( firstReal );
		const size_t split = text.find_first_of( " \t" );
		if( split == std::string::npos )
			continue;

		const std::string when       = text.substr( 0, split );
		std::string       assignment = text.substr( split );
		const size_t      at         = assignment.find_first_not_of( " \t" );
		if( at == std::string::npos )
			continue;
		assignment = assignment.substr( at );
		while( !assignment.empty() && std::strchr( " \t\r\n", assignment.back() ) != nullptr )
			assignment.pop_back();

		Cue cue;
		const size_t timeRange = when.find( ".." );
		if( timeRange != std::string::npos )
		{
			cue.from = std::strtod( when.substr( 0, timeRange ).c_str(), nullptr );
			cue.to   = std::strtod( when.substr( timeRange + 2 ).c_str(), nullptr );
			cue.ramp = true;
		}
		else
			cue.from = cue.to = std::strtod( when.c_str(), nullptr );

		const size_t equals = assignment.find( '=' );
		if( equals == std::string::npos )
		{
			std::fprintf( stderr, "%s:%d: expected Name=value\n", path.c_str(), number );
			std::fclose( file );
			return false;
		}
		cue.name                = assignment.substr( 0, equals );
		const std::string value = assignment.substr( equals + 1 );

		if( Lookup( cue.name ) < 0 )
		{
			std::fprintf( stderr, "%s:%d: no such parameter '%s'\n", path.c_str(), number, cue.name.c_str() );
			std::fclose( file );
			return false;
		}

		const size_t valueRange = value.find( ".." );
		if( cue.ramp && valueRange != std::string::npos )
		{
			cue.first  = std::strtof( value.substr( 0, valueRange ).c_str(), nullptr );
			cue.second = std::strtof( value.substr( valueRange + 2 ).c_str(), nullptr );
		}
		else
		{
			cue.first = cue.second = std::strtof( value.c_str(), nullptr );
			cue.ramp  = false;
		}
		cues.push_back( cue );
	}
	std::fclose( file );
	return true;
}

/// Eased, because a parameter that starts and stops abruptly reads as a cut.
float Ease( float t )
{
	if( t <= 0.0f ) return 0.0f;
	if( t >= 1.0f ) return 1.0f;
	return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 3.0f ) * 0.5f;
}
} // namespace

int main( int argc, char** argv )
{
	std::string out, script, logos;
	int         W = 1920, H = 1080;
	double      seconds = 30.0, fps = 30.0;

	for( int i = 1; i < argc; ++i )
	{
		std::string a = argv[ i ];
		auto next = [ & ]() { return ( i + 1 < argc ) ? argv[ ++i ] : ""; };
		if( a == "--out" ) out = next();
		else if( a == "--script" ) script = next();
		else if( a == "--logos" ) logos = next();
		else if( a == "--seconds" ) seconds = std::atof( next() );
		else if( a == "--fps" ) fps = std::atof( next() );
		else if( a == "--size" ) { std::string s = next(); const size_t x = s.find('x');
			if( x != std::string::npos ) { W = std::atoi( s.substr(0,x).c_str() ); H = std::atoi( s.substr(x+1).c_str() ); } }
	}
	if( out.empty() || script.empty() || logos.empty() )
	{
		std::fprintf( stderr, "usage: gdseq --out FILE --script CUES --logos DIR [--size WxH] [--seconds S] [--fps F]\n" );
		return 2;
	}

	std::vector< Cue > cues;
	if( !ParseCues( script, cues ) )
		return 1;

	CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile,
		(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0 };
	CGLPixelFormatObj pix; GLint npix = 0;
	if( CGLChoosePixelFormat( attrs, &pix, &npix ) != kCGLNoError ) { std::fprintf(stderr,"no pixel format\n"); return 1; }
	CGLContextObj ctx = nullptr;
	if( CGLCreateContext( pix, nullptr, &ctx ) != kCGLNoError ) { std::fprintf(stderr,"no context\n"); return 1; }
	CGLSetCurrentContext( ctx );

	GLuint tex = 0, fbo = 0;
	glGenTextures( 1, &tex ); glBindTexture( GL_TEXTURE_2D, tex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glGenFramebuffers( 1, &fbo ); glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	GridironPlugin plugin;
	FFGLViewportStruct vp = { 0, 0, (FFUInt32)W, (FFUInt32)H };
	plugin.InitGL( &vp );

	// Any file inside the folder: that is the whole interaction, so the video's
	// own footage is produced the way an operator produces a wall.
	const std::string anyFile = logos + "/resolume-arena.svg";
	plugin.SetTextParameter( PT_FOLDER_FILE, anyFile.c_str() );

	std::FILE* sink = std::fopen( out.c_str(), "wb" );
	if( sink == nullptr ) { std::fprintf( stderr, "cannot write %s\n", out.c_str() ); return 1; }

	std::vector< uint8_t > px( (size_t)W * H * 4 );
	std::vector< uint8_t > flip( (size_t)W * H * 4 );
	const int total = (int)( seconds * fps + 0.5 );

	// The folder loads on a worker thread. Spin the plugin until it has, before
	// the clock starts, or the opening seconds are black.
	for( int i = 0; i < 600; ++i )
	{
		ProcessOpenGLStruct g = {}; g.HostFBO = fbo;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo ); glViewport( 0, 0, W, H );
		glClearColor(0,0,0,0); glClear( GL_COLOR_BUFFER_BIT ); glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		plugin.SetTime( 0.0 ); plugin.ProcessOpenGL( &g );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		if( i > 8 ) break;
	}

	for( int f = 0; f < total; ++f )
	{
		const double t = f / fps;

		// Apply every cue whose time has arrived. Later cues win, so the sheet
		// reads top to bottom like a score.
		for( const Cue& c : cues )
		{
			const int id = Lookup( c.name );
			if( id < 0 ) continue;
			if( c.ramp )
			{
				if( t < c.from ) continue;
				const double span = ( c.to > c.from ) ? ( c.to - c.from ) : 1e-9;
				const float  k    = Ease( (float)std::min( 1.0, ( t - c.from ) / span ) );
				plugin.SetFloatParameter( (unsigned)id, c.first + ( c.second - c.first ) * k );
			}
			else if( t >= c.from )
				plugin.SetFloatParameter( (unsigned)id, c.first );
		}

		ProcessOpenGLStruct g = {}; g.HostFBO = fbo;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo ); glViewport( 0, 0, W, H );
		glClearColor( 0.043f, 0.047f, 0.063f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT ); glBindFramebuffer( GL_FRAMEBUFFER, 0 );

		// Milliseconds: what Resolume sends, and what Clock will settle on.
		plugin.SetTime( t * 1000.0 );
		plugin.ProcessOpenGL( &g );

		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );

		// GL hands back bottom-up; ffmpeg wants top-down.
		const size_t stride = (size_t)W * 4;
		for( int y = 0; y < H; ++y )
			std::memcpy( &flip[ (size_t)y * stride ], &px[ (size_t)( H - 1 - y ) * stride ], stride );
		std::fwrite( flip.data(), 1, flip.size(), sink );

		if( f % 30 == 0 )
			std::fprintf( stderr, "\r  %4d / %d frames", f, total );
	}
	std::fprintf( stderr, "\r  %4d / %d frames\n", total, total );

	std::fclose( sink );
	plugin.DeInitGL();
	CGLSetCurrentContext( nullptr ); CGLDestroyContext( ctx );
	return 0;
}
