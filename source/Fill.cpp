#include "Fill.h"

#include <algorithm>
#include <numeric>

#include "Hash.h"

namespace gridiron
{
namespace
{
/// Pick the logo for one cell.
///
/// **Constructive, not corrective.** The first version of this shuffled a bag
/// and then swapped cells around until the adjacency rule was satisfied, which
/// is the obvious approach and is wrong: local swaps cannot climb out of a local
/// minimum, and the case they fail worst is the small folder. Two sponsors on a
/// 6x4 wall have a *perfect* checkerboard available -- a grid is bipartite -- and
/// shuffle-then-repair left fifteen logos touching themselves.
///
/// Choosing constructively fixes it. At each cell take the logo with the most
/// placements still owed that does not match the neighbour to the left or above.
/// "Most owed" is what keeps the counts level; the neighbour test is what makes
/// the checkerboard fall out on its own when there are only two logos to deal.
///
/// `hero` is excluded too, for the cells that touch the hero block. The hero is
/// not an ordinary cell -- it is a merged block outside the pool -- so the
/// neighbour test cannot reach it, and the first wall rendered for publication
/// put the hero logo directly beside an ordinary copy of itself. A duplicate is
/// most visible exactly where the eye is being sent, so the block gets the same
/// protection the ordinary cells give each other. Pass -1 where there is no
/// hero or the cell does not touch it.
///
/// Ties break on a hash of (cell, logo, seed), so the choice is arbitrary but
/// fixed: same seed, same wall.
int ChooseLogo( const std::vector< int >& remaining, int left, int up, int hero, uint32_t cellKey, uint32_t seed )
{
	int   best      = -1;
	int   bestOwed  = -1;
	float bestBreak = -1.0f;

	for( size_t i = 0; i < remaining.size(); ++i )
	{
		if( remaining[ i ] <= 0 )
			continue;
		const int logo = static_cast< int >( i );
		if( logo == left || logo == up || logo == hero )
			continue;

		const float tie = Hash01( cellKey, seed ^ ( static_cast< uint32_t >( logo ) * 0x85ebca6bu ) );
		if( remaining[ i ] > bestOwed || ( remaining[ i ] == bestOwed && tie > bestBreak ) )
		{
			best      = logo;
			bestOwed  = remaining[ i ];
			bestBreak = tie;
		}
	}

	// Nothing satisfies the neighbour test -- one logo in the folder, or the last
	// few cells of an awkward grid. Take the most-owed logo anyway and let the
	// caller count the repeat: a wall with a visible repeat is still a wall, a
	// wall with a hole in it is not.
	if( best < 0 )
	{
		for( size_t i = 0; i < remaining.size(); ++i )
			if( remaining[ i ] > 0 && ( best < 0 || remaining[ i ] > remaining[ static_cast< size_t >( best ) ] ) )
				best = static_cast< int >( i );
	}
	return best;
}

/// Count cells that touch a copy of themselves. Only reported, never repaired --
/// the constructive pass has already done everything that can be done, and what
/// is left is a property of the folder (one logo has nowhere to go) rather than
/// a failure worth retrying.
int CountAdjacentRepeats( const std::vector< Placement >& cells )
{
	int n = 0;
	for( size_t i = 0; i < cells.size(); ++i )
	{
		const Placement& a = cells[ i ];
		if( a.logo < 0 || a.hero )
			continue;
		for( size_t j = 0; j < cells.size(); ++j )
		{
			// The hero IS compared against here, unlike in the chooser's own
			// neighbour test: a wall where the hero touches a copy of itself is
			// exactly as wrong as any other duplicate, and it should be counted.
			if( j == i || cells[ j ].logo != a.logo )
				continue;
			const int dc = cells[ j ].col - a.col;
			const int dr = cells[ j ].row - a.row;
			if( ( dc == 0 && ( dr == 1 || dr == -1 ) ) || ( dr == 0 && ( dc == 1 || dc == -1 ) ) )
			{
				++n;
				break;
			}
		}
	}
	return n;
}
} // namespace

HeroRect HeroBlock( int columns, int rows, const FillOptions& options )
{
	HeroRect r;
	if( options.hero == Hero::Off )
		return r;

	const int w = std::min( std::max( options.heroCols, 1 ), columns );
	const int h = std::min( std::max( options.heroRows, 1 ), rows );

	// A hero that swallows the whole wall is not a hero, it is a logo. Refuse it
	// rather than producing a schedule with no ordinary cells to be parity about.
	if( w * h >= columns * rows )
		return r;

	r.w = w;
	r.h = h;
	r.x = ( columns - w ) / 2;
	switch( options.hero )
	{
		case Hero::Top: r.y = 0; break;
		case Hero::Middle: r.y = ( rows - h ) / 2; break;
		case Hero::Bottom: r.y = rows - h; break;
		default: break;
	}
	return r;
}

Schedule BuildSchedule( const std::vector< Logo >& logos, int columns, int rows, const FillOptions& options )
{
	Schedule out;
	columns = std::max( columns, 1 );
	rows    = std::max( rows, 1 );

	const HeroRect hero = HeroBlock( columns, rows, options );

	// The ordinary cells: every grid position the hero block does not cover.
	std::vector< Placement > blank;
	for( int row = 0; row < rows; ++row )
	{
		for( int col = 0; col < columns; ++col )
		{
			if( hero.Valid() && col >= hero.x && col < hero.x + hero.w && row >= hero.y && row < hero.y + hero.h )
				continue;
			Placement p;
			p.col = col;
			p.row = row;
			blank.push_back( p );
		}
	}

	const int C = static_cast< int >( blank.size() );
	const int L = static_cast< int >( logos.size() );
	out.ordinaryCells = C;

	if( L == 0 || C == 0 )
	{
		out.note = L == 0 ? "no images in folder" : "no ordinary cells: the hero block covers the wall";
		return out;
	}

	// Cycle length. See the header: L/g steps makes C/g placements per logo,
	// both exact, and no other length can.
	const int g       = std::gcd( L, C );
	const int perLogo = options.equalAirtime ? ( C / g ) : std::max( 1, ( C + L - 1 ) / L );

	// Where each grid position lives in the cell list, so a cell can find the
	// neighbour to its left and above without searching.
	std::vector< int > gridIndex( static_cast< size_t >( columns ) * rows, -1 );
	for( size_t i = 0; i < blank.size(); ++i )
		gridIndex[ static_cast< size_t >( blank[ i ].row ) * columns + blank[ i ].col ] = static_cast< int >( i );

	// One bag is one cycle's worth of placements: exactly `perLogo` of every
	// logo. Placements are drawn from it as a **stream** across steps rather
	// than dealt a step at a time, and the cycle ends on the step that empties
	// it. That is what makes parity exact even when wide-logo spanning changes
	// how many logos a step consumes -- the bag does not care how fast it
	// drains, only that every logo is in it the same number of times.
	std::vector< int > remaining( static_cast< size_t >( L ), perLogo );
	std::vector< int > total( static_cast< size_t >( L ), 0 );

	const int kMaxSteps = 4096;
	int       bags      = 1;

	for( int step = 0; step < kMaxSteps; ++step )
	{
		Step s;
		s.cells = blank;

		// The logo *occupying* each grid position, including positions covered
		// by a neighbour's span. Distinct from Placement::logo, which is the
		// logo *drawn* there -- a covered cell draws nothing but is still next
		// to something, and adjacency has to know that.
		std::vector< int > occupying( blank.size(), -1 );

		for( size_t i = 0; i < s.cells.size(); ++i )
		{
			if( occupying[ i ] >= 0 )
				continue;

			bool anyLeft = false;
			for( int r : remaining )
				if( r > 0 )
				{
					anyLeft = true;
					break;
				}
			if( !anyLeft )
			{
				++bags;
				std::fill( remaining.begin(), remaining.end(), perLogo );
			}

			Placement& p = s.cells[ i ];

			int left = -1, up = -1;
			if( p.col > 0 )
			{
				const int li = gridIndex[ static_cast< size_t >( p.row ) * columns + ( p.col - 1 ) ];
				if( li >= 0 )
					left = occupying[ static_cast< size_t >( li ) ];
			}
			if( p.row > 0 )
			{
				const int ui = gridIndex[ static_cast< size_t >( p.row - 1 ) * columns + p.col ];
				if( ui >= 0 )
					up = occupying[ static_cast< size_t >( ui ) ];
			}

			// Does this cell touch the hero block? Only then is the hero's logo
			// off limits here.
			int heroBar = -1;
			if( options.noAdjacentRepeat && hero.Valid() )
			{
				const bool touches =
					( p.col >= hero.x - 1 && p.col <= hero.x + hero.w && p.row >= hero.y - 1 && p.row <= hero.y + hero.h );
				if( touches )
					heroBar = std::min( std::max( options.heroLogo, 0 ), L - 1 );
			}

			const uint32_t cellKey = static_cast< uint32_t >( step ) * 7919u + static_cast< uint32_t >( i );
			const int      logo    = options.noAdjacentRepeat
										 ? ChooseLogo( remaining, left, up, heroBar, cellKey, options.seed )
										 : ChooseLogo( remaining, -1, -1, -1, cellKey, options.seed );
			if( logo < 0 )
				break;

			--remaining[ static_cast< size_t >( logo ) ];
			++total[ static_cast< size_t >( logo ) ];
			p.logo         = logo;
			occupying[ i ] = logo;

			// A wordmark claims the cell to its right rather than being
			// letterboxed into one. It consumes no extra bag entry: the logo is
			// on screen once, just wider. Airtime parity is a claim about time,
			// and respecting an aspect ratio buys area, not time.
			if( options.aspectAware && logos[ static_cast< size_t >( logo ) ].SpanAspect() >= options.wideAspect &&
				p.col + 1 < columns )
			{
				const int ri = gridIndex[ static_cast< size_t >( p.row ) * columns + ( p.col + 1 ) ];
				if( ri >= 0 && occupying[ static_cast< size_t >( ri ) ] < 0 )
				{
					p.colSpan                              = 2;
					occupying[ static_cast< size_t >( ri ) ] = logo;
					s.cells[ static_cast< size_t >( ri ) ].logo    = -1;
					s.cells[ static_cast< size_t >( ri ) ].colSpan = 0;
				}
			}
		}

		out.adjacentRepeats += CountAdjacentRepeats( s.cells );

		if( hero.Valid() )
		{
			Placement h;
			h.col     = hero.x;
			h.row     = hero.y;
			h.colSpan = hero.w;
			h.rowSpan = hero.h;
			h.hero    = true;
			h.logo    = std::min( std::max( options.heroLogo, 0 ), L - 1 );
			s.cells.push_back( h );
		}

		out.steps.push_back( std::move( s ) );

		// The cycle ends on the step that empties the bag. Stopping anywhere
		// else would cut a logo's turn in half.
		bool drained = true;
		for( int r : remaining )
			if( r > 0 )
			{
				drained = false;
				break;
			}
		if( drained )
			break;
	}

	int minTotal = total.empty() ? 0 : total[ 0 ];
	int maxTotal = minTotal;
	for( int t : total )
	{
		minTotal = std::min( minTotal, t );
		maxTotal = std::max( maxTotal, t );
	}
	out.exactParity = options.equalAirtime && minTotal == maxTotal;

	const int cycleSteps = static_cast< int >( out.steps.size() );
	(void)bags;

	std::string n = std::to_string( L ) + " logos, " + std::to_string( C ) + " cells, " +
					std::to_string( cycleSteps ) + " step cycle";
	if( options.equalAirtime )
		n += ", " + std::to_string( maxTotal ) + " placements each";
	if( !out.exactParity )
		n += "; PARITY NOT EXACT (" + std::to_string( minTotal ) + ".." + std::to_string( maxTotal ) + ")";
	if( out.adjacentRepeats > 0 )
		n += "; " + std::to_string( out.adjacentRepeats ) + " adjacent repeats unavoidable";
	if( hero.Valid() )
		n += "; hero " + std::to_string( hero.w ) + "x" + std::to_string( hero.h );
	out.note = n;

	return out;
}

} // namespace gridiron
