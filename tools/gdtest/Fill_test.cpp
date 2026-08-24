// Offline checks on the arrangement. No GL, no host, no plugin -- Fill.cpp is
// pure arithmetic and this drives the real one.
#include "../../source/Fill.h"

#include <cstdio>
#include <map>
#include <set>

using namespace gridiron;

static int failures = 0;

static void Check( bool ok, const char* what )
{
	printf( "  %s %s\n", ok ? "ok  " : "FAIL", what );
	if( !ok )
		++failures;
}

static std::vector< Logo > MakeLogos( int n, float aspect = 1.0f )
{
	std::vector< Logo > v;
	for( int i = 0; i < n; ++i )
	{
		Logo l;
		l.index  = i;
		l.aspect        = aspect;
		l.trimmedAspect = aspect;
		l.name   = "logo" + std::to_string( i ) + ".png";
		v.push_back( l );
	}
	return v;
}

/// Every ordinary placement in the cycle, counted per logo.
static std::map< int, int > Counts( const Schedule& s )
{
	std::map< int, int > c;
	for( const auto& step : s.steps )
		for( const auto& p : step.cells )
			if( p.logo >= 0 && !p.hero )
				c[ p.logo ]++;
	return c;
}

static int AdjacentRepeats( const Schedule& s )
{
	int n = 0;
	for( const auto& step : s.steps )
	{
		for( size_t i = 0; i < step.cells.size(); ++i )
		{
			const auto& a = step.cells[ i ];
			if( a.logo < 0 || a.hero )
				continue;
			for( size_t j = i + 1; j < step.cells.size(); ++j )
			{
				const auto& b = step.cells[ j ];
				if( b.logo != a.logo || b.hero )
					continue;
				const int dc = b.col - a.col, dr = b.row - a.row;
				if( ( dc == 0 && ( dr == 1 || dr == -1 ) ) || ( dr == 0 && ( dc == 1 || dc == -1 ) ) )
					++n;
			}
		}
	}
	return n;
}

int main()
{
	printf( "reproducible\n" );
	{
		FillOptions o;
		o.seed  = 7;
		auto a  = BuildSchedule( MakeLogos( 11 ), 6, 4, o );
		auto b  = BuildSchedule( MakeLogos( 11 ), 6, 4, o );
		o.seed  = 8;
		auto c  = BuildSchedule( MakeLogos( 11 ), 6, 4, o );

		bool same = a.steps.size() == b.steps.size();
		for( size_t i = 0; same && i < a.steps.size(); ++i )
			for( size_t j = 0; j < a.steps[ i ].cells.size(); ++j )
				if( a.steps[ i ].cells[ j ].logo != b.steps[ i ].cells[ j ].logo )
					same = false;
		Check( same, "same seed gives the identical wall" );

		bool differs = false;
		for( size_t i = 0; i < a.steps.size() && !differs; ++i )
			for( size_t j = 0; j < a.steps[ i ].cells.size(); ++j )
				if( a.steps[ i ].cells[ j ].logo != c.steps[ i ].cells[ j ].logo )
					differs = true;
		Check( differs, "a different seed gives a different wall" );
	}

	printf( "equal airtime\n" );
	{
		// 11 logos over 24 cells: gcd 1, so 11 steps and 24 placements each.
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 11 ), 6, 4, o );
		Check( s.steps.size() == 11, "11 logos / 24 cells gives an 11 step cycle" );
		auto c = Counts( s );
		Check( c.size() == 11, "every logo appears" );
		bool equal = true;
		for( auto& kv : c )
			if( kv.second != 24 )
				equal = false;
		Check( equal, "every logo gets exactly 24 placements" );
		Check( s.exactParity, "parity reported exact" );
	}
	{
		// 8 logos over 24 cells: gcd 8, so 1 step and 3 placements each.
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 8 ), 6, 4, o );
		Check( s.steps.size() == 1, "8 logos / 24 cells gives a 1 step cycle" );
		auto c = Counts( s );
		bool equal = c.size() == 8;
		for( auto& kv : c )
			if( kv.second != 3 )
				equal = false;
		Check( equal, "every logo gets exactly 3 placements" );
	}
	{
		// More logos than cells: everyone still gets the same total.
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 50 ), 4, 3, o );
		auto c = Counts( s );
		bool equal = c.size() == 50;
		int  first = c.empty() ? -1 : c.begin()->second;
		for( auto& kv : c )
			if( kv.second != first )
				equal = false;
		Check( equal, "50 logos on 12 cells still share airtime equally" );
	}

	printf( "no adjacent repeat\n" );
	{
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 11 ), 6, 4, o );
		Check( AdjacentRepeats( s ) == 0, "11 logos on a 6x4 wall: none touch themselves" );
		Check( s.adjacentRepeats == 0, "and the schedule agrees" );
	}
	{
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 5 ), 8, 6, o );
		Check( AdjacentRepeats( s ) == 0, "5 logos on a 48 cell wall: still none" );
	}
	{
		// One logo cannot avoid itself. The pass must say so, not spin.
		FillOptions o;
		auto s = BuildSchedule( MakeLogos( 1 ), 4, 4, o );
		Check( s.steps.size() == 1, "one logo terminates" );
		Check( s.adjacentRepeats > 0, "and reports the repeats it could not remove" );
	}

	// Regression. Shuffle-then-repair scored 4, 8, 7, 28, 15 and 19 repeats on
	// these six grids. A grid is bipartite, so two logos have a perfect
	// checkerboard available on every one of them and the only right answer is
	// zero. This is the check that says the constructive chooser is still
	// constructive.
	printf( "no adjacent repeat: the bipartite case\n" );
	{
		bool allClean = true;
		for( int c : { 4, 5, 6 } )
		{
			for( int r : { 4, 5 } )
			{
				FillOptions o;
				auto        s = BuildSchedule( MakeLogos( 2 ), c, r, o );
				if( AdjacentRepeats( s ) != 0 )
					allClean = false;
			}
		}
		Check( allClean, "two logos checkerboard perfectly on every grid" );

		FillOptions o;
		auto        s = BuildSchedule( MakeLogos( 3 ), 5, 5, o );
		Check( AdjacentRepeats( s ) == 0, "three logos on 5x5 too" );
	}

	printf( "hero\n" );
	{
		FillOptions o;
		o.hero     = Hero::Top;
		o.heroCols = 2;
		o.heroRows = 1;
		o.heroLogo = 3;
		auto s     = BuildSchedule( MakeLogos( 11 ), 6, 4, o );
		Check( s.ordinaryCells == 22, "a 2x1 hero removes 2 of 24 cells from the pool" );

		bool heroFound = false, heroRight = true, overlap = false;
		for( const auto& step : s.steps )
		{
			for( const auto& p : step.cells )
			{
				if( p.hero )
				{
					heroFound = true;
					if( p.logo != 3 || p.colSpan != 2 || p.rowSpan != 1 || p.row != 0 )
						heroRight = false;
				}
				else if( p.row == 0 && p.col >= 2 && p.col < 4 )
					overlap = true;
			}
		}
		Check( heroFound, "the hero block is placed" );
		Check( heroRight, "it shows the requested logo at the requested span" );
		Check( !overlap, "no ordinary cell is drawn underneath it" );
	}
	{
		// Regression. The hero block sits outside the ordinary pool, so the
		// chooser's left/up neighbour test could not see it, and the very first
		// wall rendered for publication put the hero logo directly beside an
		// ordinary copy of itself -- in the top row, where the eye is being sent.
		FillOptions o;
		o.hero     = Hero::Top;
		o.heroCols = 2;
		o.heroRows = 1;
		o.heroLogo = 5;
		auto s     = BuildSchedule( MakeLogos( 11 ), 6, 4, o );

		bool clash = false;
		for( const auto& step : s.steps )
		{
			HeroRect h = HeroBlock( 6, 4, o );
			for( const auto& p : step.cells )
			{
				if( p.hero || p.logo != 5 )
					continue;
				const bool touches =
					( p.col >= h.x - 1 && p.col <= h.x + h.w && p.row >= h.y - 1 && p.row <= h.y + h.h );
				if( touches )
					clash = true;
			}
		}
		Check( !clash, "no cell touching the hero block repeats the hero's logo" );
	}
	{
		FillOptions o;
		o.hero     = Hero::Middle;
		o.heroCols = 99;
		o.heroRows = 99;
		auto s     = BuildSchedule( MakeLogos( 4 ), 4, 4, o );
		Check( s.ordinaryCells == 16, "a hero that would swallow the wall is refused" );
	}

	printf( "aspect\n" );
	{
		// A folder of wordmarks. Spanning should happen, and nothing should be
		// lost -- deferred placements come back.
		FillOptions o;
		auto wide = MakeLogos( 6, 3.0f );
		auto s    = BuildSchedule( wide, 6, 4, o );
		int spans = 0;
		for( const auto& step : s.steps )
			for( const auto& p : step.cells )
				if( p.colSpan == 2 )
					++spans;
		Check( spans > 0, "wide logos claim a second cell" );

		// Regression. Deferring displaced logos into a queue lost them when the
		// cycle was one step long: six logos that should each have been placed
		// four times came out 2, 1, 3, 2, 2, 2. Spanning consumes a cell, not a
		// turn.
		auto c2 = Counts( s );
		bool spanParity = c2.size() == 6;
		int  firstCount = c2.empty() ? -1 : c2.begin()->second;
		for( auto& kv : c2 )
			if( kv.second != firstCount )
				spanParity = false;
		Check( spanParity, "spanning consumes a cell, not a turn: parity holds" );
		Check( s.exactParity, "and the schedule says parity is exact" );

		// The case that actually turns up. Half of every sponsor pack is exported
		// into a square canvas, so the raw aspect is 1 and the trimmed aspect is
		// the truth. Spanning must follow the truth.
		{
			FillOptions po;
			auto        padded = MakeLogos( 6 );
			for( auto& l : padded )
			{
				l.aspect        = 1.0f;// square canvas
				l.trimmedAspect = 4.0f;// 4:1 wordmark inside it
			}
			auto ps    = BuildSchedule( padded, 6, 4, po );
			int  pspan = 0;
			for( const auto& st : ps.steps )
				for( const auto& p : st.cells )
					if( p.colSpan == 2 )
						++pspan;
			Check( pspan > 0, "a wordmark padded into a square canvas still spans" );
		}

		o.aspectAware = false;
		auto flat     = BuildSchedule( wide, 6, 4, o );
		int  spans2   = 0;
		for( const auto& step : flat.steps )
			for( const auto& p : step.cells )
				if( p.colSpan == 2 )
					++spans2;
		Check( spans2 == 0, "and do not when aspect awareness is off" );
	}

	printf( "degenerate\n" );
	{
		FillOptions o;
		auto s = BuildSchedule( {}, 6, 4, o );
		Check( s.steps.empty() && !s.note.empty(), "an empty folder yields nothing and says why" );
	}

	printf( "\n%s\n", failures ? "FAILED" : "all checks passed" );
	return failures ? 1 : 0;
}
