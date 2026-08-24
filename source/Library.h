#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct NSVGimage;

/**
    The folder, decoded.

    ## Why a file picker points at a folder

    FFGL's parameter types stop at `FF_TYPE_FILE` (14): a *file* dialog with an
    extension whitelist, handed to `SetFileParamInfo`. There is no directory
    type, no way to add one, and no host-side affordance to borrow. The whole
    plugin is "point me at a folder of logos", so the interaction has to be built
    out of the one picker that exists.

    So the operator picks **any single image inside the folder** and gridiron
    reads the directory around it. One click, the real native dialog, no typing a
    path into a text field. `PT_FOLDER_PATH` overrides it for anyone driving the
    plugin from a script, where a real path is easier than a fake file.

    ## Vector logos rasterise at the size the cell actually is

    A sponsor pack is vector. A press wall is the one place the same mark gets
    drawn at wildly different sizes depending on how dense the grid is, and
    resampling one fixed-size PNG across all of them is exactly what makes a
    sponsor board look cheap.

    So an SVG keeps its parsed `NSVGimage` and is re-rasterised whenever the cell
    footprint changes -- burin's trick, and it matters more here than it did
    there. `Image::vector` says whether an entry can do this; a PNG cannot and is
    resampled like anything else.

    ## The failure that matters most

    **nanosvg does not render `<text>`. At all.** No font loading, no glyph
    outlines, not even a stub. An SVG whose wordmark is live text parses without
    error, reports no failure, and draws nothing where the sponsor's name should
    be. On a sponsor wall that is the worst thing this plugin could possibly do,
    and it does it silently.

    Every SVG is therefore scanned for the elements nanosvg will drop, before it
    is handed over, and the count travels with the image so the plugin can say so
    out loud. The operator-facing fix is to convert text to outlines on export.
*/
namespace gridiron
{
/// Elements nanosvg silently drops, counted by scanning the source. Adapted
/// from burin, where the same scan exists for the same reason.
struct Unsupported
{
	int text    = 0;///< the big one: no text support whatever
	int image   = 0;
	int use     = 0;
	int clips   = 0;///< clipPath + mask
	int filters = 0;
	int pattern = 0;

	bool Any() const { return text || image || use || clips || filters || pattern; }
};

/// Count the elements nanosvg will drop. Scans the source text rather than the
/// parsed tree, because by the time nanosvg has parsed, the dropped elements are
/// gone and there is nothing left to count.
Unsupported ScanUnsupported( const std::string& text );

/// One decoded frame. A still has exactly one; an animated GIF has several.
struct Frame
{
	std::vector< uint8_t > rgba;   ///< straight-alpha RGBA8, row 0 at the top
	int                    delayMs = 100;
};

/// Owns the parsed vector document, so an SVG can be redrawn at a new size.
struct SvgDeleter
{
	void operator()( NSVGimage* p ) const;
};

struct Image
{
	std::string path;
	std::string name;///< filename only; what the hero picker and the log show

	int   width  = 0;
	int   height = 0;
	float aspect = 1.0f;///< of the artwork, never of the cell

	std::vector< Frame > frames;

	/// Bounding box of the pixels that are actually opaque, as fractions of the
	/// image, from the first frame. `0,0,1,1` when the artwork reaches every
	/// edge.
	///
	/// This exists because a sponsor pack never has consistent margins. One logo
	/// is exported tight to its bounding box, the next sits in a square canvas
	/// with 30% air around it, and dropped into identical cells they read as
	/// wildly different sizes -- which looks like a mistake by whoever built the
	/// wall. `Fit::Trim` letterboxes to this box instead of to the whole layer,
	/// so every mark reads at the same optical size whatever margin it shipped
	/// with.
	float trimX0 = 0.0f, trimY0 = 0.0f, trimX1 = 1.0f, trimY1 = 1.0f;

	/// Aspect of the trimmed artwork. This, not `aspect`, is what the wide-logo
	/// span test should use -- a 4:1 wordmark exported into a square canvas has
	/// an `aspect` of 1 and is still a 4:1 wordmark.
	float trimmedAspect = 1.0f;

	bool                                    vector = false;
	std::unique_ptr< NSVGimage, SvgDeleter > svg;///< retained only when `vector`
	Unsupported                             dropped;

	std::string note;

	bool Valid() const { return width > 0 && height > 0 && !frames.empty(); }
	bool Animated() const { return frames.size() > 1; }
};

/// Extensions offered by the file parameter: stb_image's useful set plus svg.
/// HDR and PIC are left out deliberately -- they decode to float and this path
/// is 8-bit.
extern const char* const kExtensions[];
extern const int         kExtensionCount;

/// Is this a file we would try to decode? Case-insensitive on the extension.
bool IsSupported( const std::string& path );

/// Every supported file in the directory containing `anyFileInFolder`, sorted by
/// filename.
///
/// **Sorted, and sorted by name.** The order is the identity of a logo: the hero
/// picker is an index into this listing and the atlas layer is the same index,
/// so a listing that came back in directory order -- which is arbitrary, and
/// differs between machines and between filesystems -- would make the seed
/// meaningless and move the hero logo when the wall moved to another machine.
std::vector< std::string > ScanFolder( const std::string& anyFileInFolder );

/// Decode one file. `rasterPx` is the size a vector logo should be drawn at, in
/// pixels on its longer side; it is ignored for raster formats.
///
/// A failure comes back with `Valid()` false and a `note` saying why. There is no
/// error code because every caller does the same thing with one -- log it and
/// draw nothing -- and a code would only be a second way to spell the message.
Image Decode( const std::string& path, int rasterPx );

/// Re-rasterise a vector image at a new size. No-op for raster images and for
/// a size it is already at.
void Rerasterise( Image& image, int rasterPx );

/// The whole folder, loaded off the render thread.
///
/// Decoding sixty logos cannot happen inside a `ProcessOpenGL` callback, and the
/// first frame after a folder change must not stall the composition. `Ready()`
/// goes true once, and `Take()` moves the result out.
class Loader
{
public:
	Loader() = default;
	~Loader();

	Loader( const Loader& )            = delete;
	Loader& operator=( const Loader& ) = delete;

	/// Begin loading the folder containing `anyFileInFolder`. Cancels and
	/// replaces any load already running.
	void Start( const std::string& anyFileInFolder, int rasterPx );

	bool Running() const { return mRunning.load(); }
	bool Ready() const { return mReady.load(); }

	/// Move the loaded images out, and clear `Ready()`.
	///
	/// **Consuming.** The images are moved, not copied, so this hands back the
	/// folder exactly once; `Ready()` goes false so a caller polling every frame
	/// cannot take an empty vector on the next one and wipe what it just got.
	std::vector< Image > Take();

	/// What happened, for the log and the plugin's note field.
	std::string Note();

private:
	void Join();

	std::thread          mThread;
	std::mutex           mMutex;
	std::vector< Image > mImages;
	std::string          mNote;
	std::atomic< bool >  mRunning{ false };
	std::atomic< bool >  mReady{ false };
	std::atomic< bool >  mCancel{ false };
};

} // namespace gridiron
