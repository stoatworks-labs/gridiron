#pragma once

#include <cstdint>
#include <cmath>

/**
    Host parameters, and what they mean.

    ## Integers are integers here

    `CFFGLPluginManager::SetParamInfo` clamps a default into 0..1 before
    returning, and `SetParamRange` can only be called afterwards -- there is no
    `SetParamDefault`. So a default of 6 declared the naive way becomes 1,
    silently, and most of the fleet answers that by keeping every numeric
    parameter at 0..1 and converting on this side.

    **The clamp is guarded on `FF_TYPE_STANDARD` only.** Read the SDK: the
    `if( pType == FF_TYPE_STANDARD )` test is the whole of it, and an
    `FF_TYPE_INTEGER` default passes through untouched. The counts here --
    Columns, Rows, the hero span, the seed -- are therefore declared as real
    integers with real ranges and real defaults.

    They have to be. This plugin is laying out somebody's sponsor board. A
    0..1 slider that lands on 5 or 7 either side of the 6 the designer asked for
    is not a rounding difference, it is the wrong wall.

    Everything continuous stays 0..1 and is mapped below, with curves rather
    than straight lines wherever the useful part of the range is bunched at one
    end -- see `Speed`, where the difference between a slow drift and a slightly
    less slow drift is worth more slider than the top of the range is.

    ## Declaration order is load-bearing

    `SetParamGroup` collapses *runs* of same-group parameters. The order of
    `ParamId` is the order the host draws them, and moving one id out of its run
    silently splits that group in the inspector into two groups with the same
    name.
*/
namespace gridiron
{
enum ParamId : unsigned int
{
	// --- Content -----------------------------------------------------------
	PT_FOLDER_FILE = 0,///< FF_TYPE_FILE -- any image *inside* the folder
	PT_FOLDER_PATH,    ///< FF_TYPE_TEXT -- override, for scripted setups
	PT_RELOAD,         ///< FF_TYPE_EVENT

	// --- Grid --------------------------------------------------------------
	PT_COLUMNS,   ///< FF_TYPE_INTEGER
	PT_ROWS,      ///< FF_TYPE_INTEGER
	PT_FIT,       ///< FF_TYPE_OPTION -- fit / fill / crop / stretch
	PT_CELL_PAD,  ///< FF_TYPE_STANDARD
	PT_GRID_LINES,///< FF_TYPE_BOOLEAN -- the visible/invisible grid
	PT_GRID_WIDTH,///< FF_TYPE_STANDARD
	PT_GRID_R,
	PT_GRID_G,
	PT_GRID_B,

	// --- Fill --------------------------------------------------------------
	PT_SEED,          ///< FF_TYPE_INTEGER -- fixes the arrangement
	PT_NO_ADJACENT,   ///< FF_TYPE_BOOLEAN
	PT_ASPECT_AWARE,  ///< FF_TYPE_BOOLEAN
	PT_EQUAL_AIRTIME, ///< FF_TYPE_BOOLEAN

	// --- Hero --------------------------------------------------------------
	PT_HERO,     ///< FF_TYPE_OPTION -- off / top / middle / bottom
	PT_HERO_COLS,///< FF_TYPE_INTEGER
	PT_HERO_ROWS,///< FF_TYPE_INTEGER
	PT_HERO_LOGO,///< FF_TYPE_INTEGER -- which logo, by sorted position in the folder

	// --- Motion ------------------------------------------------------------
	PT_MODE,      ///< FF_TYPE_OPTION -- the preset behaviours
	PT_SPEED,     ///< FF_TYPE_STANDARD
	PT_SCROLL_X,  ///< FF_TYPE_STANDARD -- signed, whole-grid
	PT_SCROLL_Y,  ///< FF_TYPE_STANDARD -- signed, whole-grid
	PT_GRID_ROT,  ///< FF_TYPE_STANDARD -- signed, whole-grid
	PT_WALK,      ///< FF_TYPE_OPTION -- logo movement *through* the grid
	PT_WALK_RATE, ///< FF_TYPE_STANDARD
	PT_TWINKLE,   ///< FF_TYPE_STANDARD -- depth, 0 = off
	PT_FADE,      ///< FF_TYPE_STANDARD -- depth, 0 = off

	// --- Camera ------------------------------------------------------------
	PT_PROJECTION,///< FF_TYPE_OPTION -- flat / perspective
	PT_FOV,       ///< FF_TYPE_STANDARD
	PT_CAM_YAW,   ///< FF_TYPE_STANDARD -- signed
	PT_CAM_PITCH, ///< FF_TYPE_STANDARD -- signed

	// --- Edge --------------------------------------------------------------
	PT_EDGE,      ///< FF_TYPE_OPTION -- off / burin / nib / vectrix
	PT_EDGE_SCOPE,///< FF_TYPE_OPTION -- where it lands
	PT_EDGE_AMOUNT,
	PT_EDGE_THRESHOLD,

	// -- Cell fill -----------------------------------------------------------
	//
	// Appended, so nothing above it renumbers. gridiron#6: a logo with alpha
	// lets you see through a Rubik cube to the faces behind it.
	PT_CELL_FILL,
	PT_FILL_R,
	PT_FILL_G,
	PT_FILL_B,

	PT_ABOUT_TEXT,

	PT_COUNT
};

/// Fit modes, in declaration order. What a logo does inside its cell.
/// What a logo does inside its cell.
///
/// `Trim` is the one worth understanding. It removes the transparent margin the
/// artwork was exported with and *then letterboxes* -- it does not fill the
/// cell. Filling after a trim is what `Fill` already does, and on a wordmark it
/// scales until the name runs off both sides of the cell: "COLOUR BARS & GRILL"
/// renders as "FE ARJR BARE & CO", which is the precise failure this plugin
/// exists to avoid.
///
/// What `Trim` is for is that a sponsor pack never has consistent margins -- one
/// mark exported tight to its bounding box, the next sitting in a square canvas
/// with a third of it air. Dropped into identical cells they read as wildly
/// different sizes, which looks like a mistake by whoever built the wall.
/// Trimming first makes every mark the same *optical* size without cutting any
/// of them.
enum class Fit : int
{
	Fit = 0,///< whole logo visible, letterboxed. The safe default for a client's mark.
	Fill,   ///< cell filled, overflow cropped
	Trim,   ///< margin removed, then letterboxed. Normalises a mixed pack.
	Stretch ///< distorted to the cell. Present because someone will ask for it.
};

/// Where the merged hero block sits. Its cells leave the ordinary pool, so the
/// fill works around the hole rather than drawing under it.
///
/// The block shows `PT_HERO_LOGO`, an index into the folder listing sorted by
/// filename -- 0 is the first file. It is an index and not a second file picker
/// because the key sponsor is already in the folder, and a picker would let the
/// two disagree about what is on the wall.
enum class Hero : int
{
	Off = 0,
	Top,
	Middle,
	Bottom
};

/// The named behaviours. Each is a preset in the host as well as a mode here,
/// because "slot machine" is a thing an operator wants to pick, not a set of
/// eight sliders they want to discover.
enum class Mode : int
{
	Static = 0,
	SlotMachine,///< columns scroll at different speeds, optionally spinning down
	Rubik,      ///< the wall on a cube, one slice rotating at a time
	Twinkle,    ///< every cell at a different point between full and black
	FadeCycle   ///< cells fade out and in, changing logo at the trough
};

/// Which way a logo walks through the grid. Distinct from whole-grid scroll:
/// this reassigns *content between cells*, it does not move the cells.
enum class Walk : int
{
	Off = 0,
	Up,
	Down,
	Left,
	Right
};

enum class Projection : int
{
	Flat = 0,///< orthographic, every cell coplanar
	Perspective
};

enum class Edge : int
{
	Off = 0,
	Burin,  ///< engraved line
	Nib,    ///< inked contour
	Vectrix ///< vector trace
};

/// Where an edge treatment lands. Per-cell is the whole reason this is built in
/// rather than stacked in the Resolume chain -- everything else here you could
/// get by dropping burin on top of gridiron.
enum class EdgeScope : int
{
	Global = 0, ///< the finished wall, as a post pass
	EveryCell,  ///< each logo separately, in the cell shader
	HeroOnly,
	ExceptHero
};

// ---------------------------------------------------------------------------
// The mappings. Continuous parameters arrive 0..1 and become real units here.
// ---------------------------------------------------------------------------

/// Cell padding, as a fraction of the smaller cell dimension. Caps below a half
/// because a padding of 0.5 leaves a cell with no interior at all.
inline float CellPad( float v )
{
	return v * 0.45f;
}

/// Grid line width, in fractions of a cell. Same reasoning as the padding cap.
inline float GridWidth( float v )
{
	return 0.002f + v * 0.08f;
}

/// Master speed. Cubed, because the useful range is bunched at the bottom: a
/// press wall that reads as *designed* moves slowly, and the top of this slider
/// exists for people who want a strobe.
inline float Speed( float v )
{
	return v * v * v * 8.0f;
}

/// Signed scroll, in cells per second.
inline float Scroll( float v )
{
	const float s = v * 2.0f - 1.0f;
	return s * s * s * 4.0f;
}

/// Signed whole-grid rotation, radians per second.
inline float GridRot( float v )
{
	const float s = v * 2.0f - 1.0f;
	return s * s * s * 3.14159265f;
}

/// Logo walk rate, in cell steps per second.
inline float WalkRate( float v )
{
	return 0.05f + v * v * 6.0f;
}

/// Vertical field of view in radians. Only consulted in Perspective.
inline float Fov( float v )
{
	return ( 15.0f + v * 75.0f ) * 3.14159265f / 180.0f;
}

/// Signed camera yaw/pitch, radians. Enough to see the cube is a cube.
inline float CamAngle( float v )
{
	return ( v * 2.0f - 1.0f ) * 3.14159265f;
}

/// Edge threshold, on the gradient magnitude. Never exactly zero: a threshold of
/// zero makes every fragment an edge and the wall goes solid.
inline float EdgeThreshold( float v )
{
	return 0.01f + v * v * 0.6f;
}

} // namespace gridiron
