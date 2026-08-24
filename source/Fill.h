#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Controls.h"

/**
    Which logo goes in which cell, and when it changes.

    Four properties are asked of the arrangement, and they are not independent.
    In priority order when they conflict:

      1. **Reproducible.** Same folder, same seed, same wall -- across a reload,
         across a machine, across the gap between the wall the client approved
         and the wall that goes up on the night. Everything below is driven from
         `Hash.h` and the seed; nothing consults a clock or an allocator.

      2. **No adjacent repeat.** A logo never touches a copy of itself
         horizontally or vertically. This is the rule that makes a small pack
         repeated over a big wall read as designed rather than looped, and it is
         the first thing an eye picks out of a photograph of a press board.

      3. **Equal airtime.** Every logo is on screen for the same total time
         across a cycle. Not the same number of *placements* -- the same
         *duration*. Sponsors pay for parity and someone will eventually ask us
         to prove it.

      4. **Aspect-aware.** A long wordmark is not cropped in half, and may be
         given two cells side by side rather than letterboxed into one.

    ## Why that is the priority order

    2 beats 4 because a duplicated neighbour is visible from across a room and a
    letterboxed logo is not. 3 beats 4 for the same reason it exists: parity is
    contractual and fit is aesthetic. 1 beats everything because an arrangement
    you cannot reproduce is one you cannot approve.

    ## Equal airtime, exactly

    With `L` logos and `C` ordinary cells, let `g = gcd(L, C)`. A cycle is
    `L / g` steps long, every step fills all `C` cells, and over the cycle each
    logo appears exactly `C / g` times. Those divide evenly by construction,
    which is the entire reason the cycle length is defined this way rather than
    being a parameter -- any other length makes exact parity impossible and
    turns "equal airtime" into "roughly equal airtime", which is not a claim
    worth making to a sponsor.

    Wide-logo spanning (rule 4) consumes a neighbouring cell, and the logo that
    would have occupied it is **deferred to the head of the next step rather
    than dropped**. So parity still holds -- it holds across a small number of
    cycles instead of exactly one. `Schedule::exactParity` says which of the two
    you got, so the harness and the operator can both tell.
*/
namespace gridiron
{
/// A logo, as far as the arrangement and the layout are concerned. The pixels
/// live in the atlas; this is everything about a logo that is geometry.
struct Logo
{
	int         index = 0;///< layer in the atlas, and position in the sorted listing
	std::string name;     ///< filename, for diagnostics and the hero picker

	/// Aspect of the whole layer, artwork plus whatever margin was exported
	/// with it.
	float aspect = 1.0f;

	/// The opaque part of the layer, and its aspect.
	float trimX0 = 0.0f, trimY0 = 0.0f, trimX1 = 1.0f, trimY1 = 1.0f;
	float trimmedAspect = 1.0f;

	/// Which aspect the "is this a wordmark?" test should use.
	///
	/// **The trimmed one, always.** A 4:1 wordmark exported into a square canvas
	/// -- which is how half of every sponsor pack arrives -- has a raw `aspect`
	/// of exactly 1 and is still a 4:1 wordmark. Testing the raw aspect means
	/// the logos that most need two cells are precisely the ones that never get
	/// them.
	float SpanAspect() const { return trimmedAspect; }
};

/// One cell's worth of the answer.
struct Placement
{
	int  col     = 0;
	int  row     = 0;
	int  colSpan = 1;///< 2 when a wide logo took the cell to its right
	int  rowSpan = 1;
	int  logo    = -1;///< index into the library; -1 means "draw nothing"
	bool hero    = false;
};

/// One step of the cycle: a complete wall.
struct Step
{
	std::vector< Placement > cells;
};

struct Schedule
{
	std::vector< Step > steps;

	/// Cells that carry ordinary content, excluding the hero block. The
	/// denominator every parity claim is made against.
	int ordinaryCells = 0;

	/// True when every logo got exactly the same number of placements in this
	/// cycle. False once wide-logo spanning has deferred anything, which does
	/// not lose a placement but does move it into the next cycle.
	bool exactParity = true;

	/// Adjacency violations the repair pass could not remove. Non-zero is not a
	/// bug -- one logo on a wall of forty cells has nowhere to go, and neither
	/// does two logos on a grid where every cell has an even number of
	/// neighbours. It is reported rather than hidden so the note can say so.
	int adjacentRepeats = 0;

	/// One line for the log and for `gdtest --fill`.
	std::string note;
};

struct FillOptions
{
	uint32_t seed             = 1;
	bool     noAdjacentRepeat = true;
	bool     aspectAware      = true;
	bool     equalAirtime     = true;

	Hero hero      = Hero::Off;
	int  heroCols  = 2;
	int  heroRows  = 1;
	int  heroLogo  = 0;

	/// Above this width-to-height ratio a logo is "wide" and may claim the cell
	/// to its right. 2.2 is a wordmark; below it is a roundel or a lockup that
	/// fits a cell honestly.
	float wideAspect = 2.2f;
};

/// Solve the whole cycle. Deterministic in (logos, columns, rows, options).
Schedule BuildSchedule( const std::vector< Logo >& logos, int columns, int rows, const FillOptions& options );

/// The hero block's cells, in grid coordinates. Exposed for the harness and for
/// the layout, which needs to know the hole before it can place anything.
/// Returns an empty rect (w or h zero) when the hero is off or will not fit.
struct HeroRect
{
	int x = 0, y = 0, w = 0, h = 0;
	bool Valid() const { return w > 0 && h > 0; }
};
HeroRect HeroBlock( int columns, int rows, const FillOptions& options );

} // namespace gridiron
