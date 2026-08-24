#pragma once

#include <cstdint>
#include <vector>

#include "Controls.h"
#include "Fill.h"
#include "Mat4.h"

/**
    Where every cell is, in three dimensions, at a given moment.

    The flat wall and the cube are **not two code paths**. Every cell is a
    textured quad with a model matrix; the flat wall is the case where all of
    them happen to be coplanar and the camera happens to be orthographic. That
    is the decision the whole plugin is built around -- see docs/ARCHITECTURE.md
    -- and it is what makes whole-grid scroll and rotation fall out of the view
    matrix for free instead of being a second implementation of the same idea.

    ## How a press wall becomes a cube

    A cube has six square faces; a press wall is `columns` by `rows` and is
    rarely square. Rather than inventing a mapping between them, Rubik mode
    changes what the grid *is*: the cube is `N` by `N` by `N` where `N` comes
    from the grid the operator asked for, and the fill runs on a virtual grid
    `N` wide by `6N` tall -- the six faces stacked into one tall column.

    That reuses `Fill` completely, unchanged. Adjacency then works within each
    face for free, and the only artefact is that the last row of one face is
    treated as adjacent to the first row of the next, which is over-strict and
    never wrong.

    ## The moves return the cube to where it started

    A slice rotation has to accumulate -- a cube whose slices spring back is not
    a cube -- but accumulating forever means replaying an unbounded history every
    frame, and a loop point that snaps.

    So the sequence is a scramble followed by its own inverse, in reverse order.
    It accumulates honestly, it is bounded at `kMoveCount` replays per cell per
    frame, and it arrives back at a solved cube exactly at the end -- which means
    it loops seamlessly and reads as "scramble, then solve" rather than as a
    cube twitching at random.

    Slice membership is tested against where a cell **currently** is, not where
    it started. That is the difference between a Rubik's cube and six faces that
    rotate independently, and it is one line: the position is carried through
    each completed move.
*/
namespace gridiron
{
/// What the renderer needs to draw one cell.
struct CellTransform
{
	Mat4 model;

	int logo = -1;///< atlas layer; -1 draws nothing

	/// Maps cell-local 0..1 UV onto the logo's layer, implementing fit / fill /
	/// crop / stretch. Applied as `uv * scale + offset`.
	float uvScaleX = 1.0f, uvScaleY = 1.0f;
	float uvOffsetX = 0.0f, uvOffsetY = 0.0f;

	float brightness = 1.0f;///< twinkle
	float opacity    = 1.0f;///< fade

	int frame = 0;///< which frame of an animated logo

	bool hero = false;
	bool edge = false;///< does the edge treatment apply to this cell
};

struct LayoutResult
{
	std::vector< CellTransform > cells;
	Mat4                         view;
	Mat4                         projection;
};

/// Everything the layout needs that is not already in the schedule.
struct LayoutOptions
{
	Mode       mode       = Mode::Static;
	Projection projection = Projection::Flat;
	Fit        fit        = Fit::Fit;
	Walk       walk       = Walk::Off;
	Edge       edge       = Edge::Off;
	EdgeScope  edgeScope  = EdgeScope::Global;

	float cellPad  = 0.0f;
	float speed    = 1.0f;
	float scrollX  = 0.0f;
	float scrollY  = 0.0f;
	float gridRot  = 0.0f;
	float walkRate = 1.0f;
	float twinkle  = 0.0f;
	float fade     = 0.0f;

	float fov      = 0.9f;
	float camYaw   = 0.4f;
	float camPitch = 0.35f;

	uint32_t seed = 1;

	/// Output aspect, width / height. The wall is fitted into it.
	float outputAspect = 16.0f / 9.0f;
};

/// The cube's edge length in cells, derived from the operator's grid. Clamped:
/// a 1x1x1 cube is a box and an N above 8 is 384 cells of sponsor logo, which
/// is past the point where anyone can read one.
int CubeSize( int columns, int rows );

/// The virtual grid `Fill` should be given. Flat modes pass the grid straight
/// through; Rubik returns `N` by `6N`, the six faces stacked.
void FillGrid( Mode mode, int columns, int rows, int& outColumns, int& outRows );

/// How many moves are in one scramble-and-solve cycle. Even, because the second
/// half undoes the first.
constexpr int kMoveCount = 16;

/// One slice rotation.
struct CubeMove
{
	int   axis  = 0;///< 0 = X, 1 = Y, 2 = Z
	int   layer = 0;///< 0 .. n-1
	float turns = 1.0f;///< signed quarter turns
};

/// The move sequence for a seed: `kMoveCount / 2` random moves followed by their
/// inverses in reverse order, so the cube ends where it began.
std::vector< CubeMove > MoveSequence( uint32_t seed, int n );

/// Solve the whole frame.
///
/// **`columns` and `rows` are the grid the operator asked for, never the virtual
/// grid `FillGrid` returns.** This function derives the virtual grid itself, so
/// there is exactly one place that transformation happens.
///
/// The distinction is not cosmetic. Handing it the already-transformed grid is
/// silently wrong on a cube: `FillGrid` turns 6x4 into 6x36, and asking
/// `CubeSize` about *that* answers 8 rather than 6, so the layout builds an
/// 8-cube out of a schedule written for a 6-cube and draws nothing at all. It
/// costs nothing to look right -- both are just two ints -- which is why the
/// rule is stated here rather than left to the caller to infer.
LayoutResult BuildLayout( const Schedule& schedule,
						  const std::vector< Logo >& logos,
						  int columns,
						  int rows,
						  float timeSeconds,
						  const LayoutOptions& options );

} // namespace gridiron
