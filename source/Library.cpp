#include "Library.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

// stb_image and nanosvg are compiled here and only here. STBI_NO_HDR and
// STBI_NO_PIC drop the two formats that decode to float; this path is 8-bit
// throughout and admitting a float format would only mean a silent conversion.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#include "stb_image.h"

// nanosvg's implementation lives in SvgLib.cpp, alone. This header brings in
// the declarations only.
#include "SvgLib.h"

namespace fs = std::filesystem;

namespace gridiron
{
const char* const kExtensions[]  = { "png", "jpg", "jpeg", "gif", "bmp", "tga", "psd", "svg" };
const int         kExtensionCount = static_cast< int >( sizeof( kExtensions ) / sizeof( kExtensions[ 0 ] ) );

void SvgDeleter::operator()( NSVGimage* p ) const
{
	if( p != nullptr )
		nsvgDelete( p );
}

namespace
{
std::string LowerExtension( const std::string& path )
{
	const size_t dot = path.find_last_of( '.' );
	if( dot == std::string::npos )
		return {};
	std::string ext = path.substr( dot + 1 );
	for( char& c : ext )
		c = static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
	return ext;
}

size_t FindNoCase( const std::string& hay, const char* needle, size_t from )
{
	const size_t n = std::strlen( needle );
	if( n == 0 || hay.size() < n )
		return std::string::npos;
	for( size_t i = from; i + n <= hay.size(); ++i )
	{
		size_t j = 0;
		while( j < n && std::tolower( static_cast< unsigned char >( hay[ i + j ] ) ) ==
						   std::tolower( static_cast< unsigned char >( needle[ j ] ) ) )
			++j;
		if( j == n )
			return i;
	}
	return std::string::npos;
}

/// Count element *open tags* -- `<name` followed by whitespace, `>` or `/`. The
/// trailing test is what stops `<use` also matching `<usemap`.
int CountElement( const std::string& text, const char* name )
{
	std::string open = "<";
	open += name;

	int    count = 0;
	size_t from  = 0;
	for( ;; )
	{
		const size_t at = FindNoCase( text, open.c_str(), from );
		if( at == std::string::npos )
			break;
		const size_t after = at + open.size();
		if( after >= text.size() )
			break;
		const char c = text[ after ];
		if( c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r' )
			++count;
		from = after;
	}
	return count;
}

/// Bounding box of pixels with meaningful alpha. Returns false when the image is
/// entirely transparent, in which case the caller keeps the full rect -- a wall
/// cell showing nothing is better than one showing a division by zero.
bool TrimBounds( const Frame& f, int w, int h, float& x0, float& y0, float& x1, float& y1 )
{
	// 8/255 rather than 0: exported logos routinely carry a haze of near-zero
	// alpha from antialiasing against a removed background, and trimming to
	// literally-nonzero alpha would trim to the whole canvas every time.
	const uint8_t kAlphaFloor = 8;

	int minX = w, minY = h, maxX = -1, maxY = -1;
	for( int y = 0; y < h; ++y )
	{
		for( int x = 0; x < w; ++x )
		{
			if( f.rgba[ ( static_cast< size_t >( y ) * w + x ) * 4 + 3 ] < kAlphaFloor )
				continue;
			if( x < minX ) minX = x;
			if( x > maxX ) maxX = x;
			if( y < minY ) minY = y;
			if( y > maxY ) maxY = y;
		}
	}
	if( maxX < minX || maxY < minY )
		return false;

	x0 = static_cast< float >( minX ) / static_cast< float >( w );
	y0 = static_cast< float >( minY ) / static_cast< float >( h );
	x1 = static_cast< float >( maxX + 1 ) / static_cast< float >( w );
	y1 = static_cast< float >( maxY + 1 ) / static_cast< float >( h );
	return true;
}

bool ReadWholeFile( const std::string& path, std::string& out )
{
	std::ifstream f( path, std::ios::binary );
	if( !f )
		return false;
	out.assign( std::istreambuf_iterator< char >( f ), std::istreambuf_iterator< char >() );
	return true;
}

/// Rasterise a parsed document so its longer side is `rasterPx`.
bool RasteriseSvg( NSVGimage* svg, int rasterPx, Frame& frame, int& outW, int& outH )
{
	if( svg == nullptr || svg->width <= 0.0f || svg->height <= 0.0f )
		return false;

	rasterPx = std::min( std::max( rasterPx, 1 ), 4096 );

	const float longSide = std::max( svg->width, svg->height );
	const float scale    = static_cast< float >( rasterPx ) / longSide;

	outW = std::max( 1, static_cast< int >( svg->width * scale + 0.5f ) );
	outH = std::max( 1, static_cast< int >( svg->height * scale + 0.5f ) );

	NSVGrasterizer* rast = nsvgCreateRasterizer();
	if( rast == nullptr )
		return false;

	frame.rgba.assign( static_cast< size_t >( outW ) * outH * 4, 0 );
	nsvgRasterize( rast, svg, 0.0f, 0.0f, scale, frame.rgba.data(), outW, outH, outW * 4 );
	nsvgDeleteRasterizer( rast );
	return true;
}
/// Fill in an image's trim box and trimmed aspect from its first frame.
void ComputeTrim( Image& img )
{
	if( !img.Valid() )
		return;
	if( !TrimBounds( img.frames[ 0 ], img.width, img.height, img.trimX0, img.trimY0, img.trimX1, img.trimY1 ) )
	{
		img.trimX0 = img.trimY0 = 0.0f;
		img.trimX1 = img.trimY1 = 1.0f;
	}
	const float tw = ( img.trimX1 - img.trimX0 ) * static_cast< float >( img.width );
	const float th = ( img.trimY1 - img.trimY0 ) * static_cast< float >( img.height );
	img.trimmedAspect = ( th > 0.0f ) ? ( tw / th ) : img.aspect;
}
} // namespace

Unsupported ScanUnsupported( const std::string& text )
{
	Unsupported u;
	u.text    = CountElement( text, "text" ) + CountElement( text, "tspan" );
	u.image   = CountElement( text, "image" );
	u.use     = CountElement( text, "use" );
	u.clips   = CountElement( text, "clipPath" ) + CountElement( text, "mask" );
	u.filters = CountElement( text, "filter" );
	u.pattern = CountElement( text, "pattern" );
	return u;
}

bool IsSupported( const std::string& path )
{
	const std::string ext = LowerExtension( path );
	for( int i = 0; i < kExtensionCount; ++i )
		if( ext == kExtensions[ i ] )
			return true;
	return false;
}

std::vector< std::string > ScanFolder( const std::string& anyFileInFolder )
{
	std::vector< std::string > out;
	if( anyFileInFolder.empty() )
		return out;

	std::error_code ec;
	fs::path        p( anyFileInFolder );

	// The parameter is a file, but tolerate being handed the directory itself:
	// the text override makes that easy to do by accident and there is no reason
	// to refuse it.
	fs::path dir = fs::is_directory( p, ec ) ? p : p.parent_path();
	if( dir.empty() )
		return out;

	for( fs::directory_iterator it( dir, ec ), end; it != end && !ec; it.increment( ec ) )
	{
		if( !it->is_regular_file( ec ) )
			continue;
		const std::string s = it->path().string();
		if( !IsSupported( s ) )
			continue;
		// Skip the dotfiles a Mac leaves lying around; ._Foo.png is an AppleDouble
		// resource fork, decodes as garbage, and would otherwise become a cell.
		const std::string fn = it->path().filename().string();
		if( !fn.empty() && fn[ 0 ] == '.' )
			continue;
		out.push_back( s );
	}

	std::sort( out.begin(), out.end() );
	return out;
}

Image Decode( const std::string& path, int rasterPx )
{
	Image img;
	img.path = path;
	img.name = fs::path( path ).filename().string();

	const std::string ext = LowerExtension( path );

	if( ext == "svg" )
	{
		std::string text;
		if( !ReadWholeFile( path, text ) )
		{
			img.note = img.name + ": cannot read";
			return img;
		}

		img.dropped = ScanUnsupported( text );

		// nsvgParse writes into the buffer it is given and null-terminates it,
		// so it gets a copy it is allowed to destroy.
		std::string    mutableCopy = text;
		NSVGimage*     parsed      = nsvgParse( mutableCopy.data(), "px", 96.0f );
		if( parsed == nullptr || parsed->width <= 0.0f || parsed->height <= 0.0f )
		{
			if( parsed != nullptr )
				nsvgDelete( parsed );
			img.note = img.name + ": not a usable SVG";
			return img;
		}

		Frame frame;
		int   w = 0, h = 0;
		if( !RasteriseSvg( parsed, rasterPx, frame, w, h ) )
		{
			nsvgDelete( parsed );
			img.note = img.name + ": rasteriser failed";
			return img;
		}

		img.vector = true;
		img.svg.reset( parsed );
		img.width  = w;
		img.height = h;
		img.aspect = static_cast< float >( parsed->width ) / static_cast< float >( parsed->height );
		img.frames.push_back( std::move( frame ) );
		ComputeTrim( img );

		if( img.dropped.text > 0 )
			img.note = img.name + ": " + std::to_string( img.dropped.text ) +
					   " live text element(s) WILL NOT DRAW -- convert text to outlines";
		else if( img.dropped.Any() )
			img.note = img.name + ": some elements dropped (image/use/clip/filter/pattern)";
		return img;
	}

	std::string bytes;
	if( !ReadWholeFile( path, bytes ) )
	{
		img.note = img.name + ": cannot read";
		return img;
	}

	if( ext == "gif" )
	{
		int  w = 0, h = 0, layers = 0, comp = 0;
		int* delays = nullptr;
		stbi_uc* data = stbi_load_gif_from_memory( reinterpret_cast< const stbi_uc* >( bytes.data() ),
												   static_cast< int >( bytes.size() ), &delays, &w, &h, &layers, &comp, 4 );
		if( data == nullptr || w <= 0 || h <= 0 || layers <= 0 )
		{
			if( data != nullptr )
				stbi_image_free( data );
			if( delays != nullptr )
				stbi_image_free( delays );
			img.note = img.name + ": GIF would not decode";
			return img;
		}

		const size_t frameBytes = static_cast< size_t >( w ) * h * 4;
		for( int i = 0; i < layers; ++i )
		{
			Frame f;
			f.rgba.assign( data + frameBytes * i, data + frameBytes * ( i + 1 ) );
			// A zero or absent delay means "as fast as possible" in the file and
			// "spin the CPU" here; browsers clamp it and so do we.
			f.delayMs = ( delays != nullptr && delays[ i ] > 10 ) ? delays[ i ] : 100;
			img.frames.push_back( std::move( f ) );
		}

		stbi_image_free( data );
		if( delays != nullptr )
			stbi_image_free( delays );

		img.width  = w;
		img.height = h;
		img.aspect = static_cast< float >( w ) / static_cast< float >( h );
		ComputeTrim( img );
		// Deliberately no note. `note` is what the operator is asked to act on,
		// and a frame count is not -- mixing the two teaches them to skim past
		// the line that says a sponsor's name will not draw.
		return img;
	}

	int      w = 0, h = 0, comp = 0;
	stbi_uc* data = stbi_load_from_memory( reinterpret_cast< const stbi_uc* >( bytes.data() ),
										   static_cast< int >( bytes.size() ), &w, &h, &comp, 4 );
	if( data == nullptr || w <= 0 || h <= 0 )
	{
		if( data != nullptr )
			stbi_image_free( data );
		img.note = img.name + ": " + ( stbi_failure_reason() ? stbi_failure_reason() : "would not decode" );
		return img;
	}

	Frame f;
	f.rgba.assign( data, data + static_cast< size_t >( w ) * h * 4 );
	stbi_image_free( data );

	img.width  = w;
	img.height = h;
	img.aspect = static_cast< float >( w ) / static_cast< float >( h );
	img.frames.push_back( std::move( f ) );
	ComputeTrim( img );
	return img;
}

void Rerasterise( Image& image, int rasterPx )
{
	if( !image.vector || image.svg == nullptr )
		return;
	if( std::max( image.width, image.height ) == rasterPx )
		return;

	Frame frame;
	int   w = 0, h = 0;
	if( !RasteriseSvg( image.svg.get(), rasterPx, frame, w, h ) )
		return;

	image.width  = w;
	image.height = h;
	image.frames.clear();
	image.frames.push_back( std::move( frame ) );
	ComputeTrim( image );
}

// ---------------------------------------------------------------------------

Loader::~Loader()
{
	Join();
}

void Loader::Join()
{
	mCancel.store( true );
	if( mThread.joinable() )
		mThread.join();
	mCancel.store( false );
}

void Loader::Start( const std::string& anyFileInFolder, int rasterPx )
{
	Join();

	mReady.store( false );
	mRunning.store( true );
	{
		std::lock_guard< std::mutex > lock( mMutex );
		mImages.clear();
		mNote.clear();
	}

	mThread = std::thread( [ this, anyFileInFolder, rasterPx ]() {
		const std::vector< std::string > files = ScanFolder( anyFileInFolder );

		std::vector< Image > loaded;
		std::string          problems;
		int                  textWarnings = 0;

		for( const std::string& f : files )
		{
			if( mCancel.load() )
				break;
			Image img = Decode( f, rasterPx );
			if( !img.note.empty() )
			{
				if( img.dropped.text > 0 )
					++textWarnings;
				if( problems.size() < 600 )
					problems += ( problems.empty() ? "" : "; " ) + img.note;
			}
			if( img.Valid() )
				loaded.push_back( std::move( img ) );
		}

		std::string note;
		if( files.empty() )
			note = "no supported images in that folder";
		else
			note = std::to_string( loaded.size() ) + " of " + std::to_string( files.size() ) + " loaded";
		if( textWarnings > 0 )
			note += "; " + std::to_string( textWarnings ) + " SVG(s) have live text that will not draw";
		if( !problems.empty() )
			note += " -- " + problems;

		{
			std::lock_guard< std::mutex > lock( mMutex );
			mImages = std::move( loaded );
			mNote   = std::move( note );
		}
		mRunning.store( false );
		mReady.store( true );
	} );
}

std::vector< Image > Loader::Take()
{
	std::lock_guard< std::mutex > lock( mMutex );

	// Clearing `mReady` is the whole point of this line, and leaving it out cost
	// an afternoon. `Take()` moves the images out of the member, so a second
	// call returns an empty vector -- and with `mReady` still true, a caller
	// that polls `Ready()` every frame takes the folder once and then takes
	// nothing, for ever. The wall drew correctly on the frame the load landed
	// and was wiped on the next one, which looks like a rendering bug and is
	// not.
	mReady.store( false );
	return std::move( mImages );
}

std::string Loader::Note()
{
	std::lock_guard< std::mutex > lock( mMutex );
	return mNote;
}

} // namespace gridiron
