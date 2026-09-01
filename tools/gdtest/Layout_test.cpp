// Geometry checks. No GL: the matrices are arithmetic and this drives the real
// BuildLayout, so what is checked here is what the shader will be handed.
#include "../../source/Layout.h"

#include <cmath>
#include <cstdio>
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
		l.index         = i;
		l.aspect        = aspect;
		l.trimmedAspect = aspect;
		v.push_back( l );
	}
	return v;
}

/// The centre of a cell in world space: the model matrix applied to the origin.
static Vec3 Centre( const CellTransform& c )
{
	return TransformPoint( c.model, { 0.0f, 0.0f, 0.0f } );
}

int main()
{
	printf( "matrix basics\n" );
	{
		const Mat4 i = Mat4::Identity();
		const Vec3 p = TransformPoint( i, { 1.0f, 2.0f, 3.0f } );
		Check( p.x == 1.0f && p.y == 2.0f && p.z == 3.0f, "identity leaves a point alone" );

		const Mat4 t = Translate( 1.0f, 2.0f, 3.0f );
		const Vec3 q = TransformPoint( t, { 0.0f, 0.0f, 0.0f } );
		Check( q.x == 1.0f && q.y == 2.0f && q.z == 3.0f, "translate translates" );

		// Right to left: scale first, then translate.
		const Vec3 r = TransformPoint( Translate( 10.0f, 0.0f, 0.0f ) * Scale( 2.0f, 2.0f, 2.0f ), { 1.0f, 0.0f, 0.0f } );
		Check( std::fabs( r.x - 12.0f ) < 1e-5f, "multiply applies right to left" );

		// Four quarter turns is identity.
		Mat4 acc = Mat4::Identity();
		for( int k = 0; k < 4; ++k )
			acc = RotateY( 3.14159265f * 0.5f ) * acc;
		const Vec3 s = TransformPoint( acc, { 1.0f, 0.0f, 0.0f } );
		Check( std::fabs( s.x - 1.0f ) < 1e-4f && std::fabs( s.z ) < 1e-4f, "four quarter turns come back" );

		// The translation column must be m[3], or glUniformMatrix4fv sends
		// nonsense with transpose = GL_FALSE.
		const float* d = t.Data();
		Check( d[ 12 ] == 1.0f && d[ 13 ] == 2.0f && d[ 14 ] == 3.0f,
			   "translation sits in elements 12..14, as OpenGL column-major requires" );
	}

	printf( "flat wall\n" );
	{
		FillOptions fo;
		auto        logos = MakeLogos( 11 );
		auto        sched = BuildSchedule( logos, 6, 4, fo );

		LayoutOptions lo;
		lo.outputAspect = 16.0f / 9.0f;
		auto r          = BuildLayout( sched, logos, 6, 4, 0.0f, lo );

		Check( r.cells.size() == 24, "a 6x4 wall lays out 24 cells" );

		bool coplanar = true, inside = true;
		for( const auto& c : r.cells )
		{
			const Vec3 p = Centre( c );
			if( std::fabs( p.z ) > 1e-5f )
				coplanar = false;
			if( std::fabs( p.x ) > lo.outputAspect * 0.5f || std::fabs( p.y ) > 0.5f )
				inside = false;
		}
		Check( coplanar, "every cell is coplanar at z = 0" );
		Check( inside, "and inside the wall" );

		std::set< std::pair< int, int > > seen;
		bool                              distinct = true;
		for( const auto& c : r.cells )
		{
			const Vec3 p   = Centre( c );
			auto       key = std::make_pair( static_cast< int >( p.x * 1000.0f ), static_cast< int >( p.y * 1000.0f ) );
			if( !seen.insert( key ).second )
				distinct = false;
		}
		Check( distinct, "no two cells sit on top of each other" );
	}

	printf( "cube\n" );
	{
		const int n = CubeSize( 4, 4 );
		Check( n == 4, "a 4x4 grid gives a 4-cube" );

		int fc = 0, fr = 0;
		FillGrid( Mode::Rubik, 4, 4, fc, fr );
		Check( fc == 4 && fr == 24, "the fill grid is N wide by 6N tall: six faces stacked" );

		FillOptions fo;
		auto        logos = MakeLogos( 7 );
		auto        sched = BuildSchedule( logos, fc, fr, fo );

		LayoutOptions lo;
		lo.mode = Mode::Rubik;
		lo.seed = 3;

		// t = 0: nothing has turned yet, so every cell must be exactly on the
		// surface of the unit cube.
		auto r = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
		Check( r.cells.size() == 96, "6 faces x 16 cells = 96" );

		bool onSurface = true;
		for( const auto& c : r.cells )
		{
			const Vec3  p   = Centre( c );
			const float m   = std::max( std::max( std::fabs( p.x ), std::fabs( p.y ) ), std::fabs( p.z ) );
			if( std::fabs( m - 0.5f ) > 1e-4f )
				onSurface = false;
		}
		Check( onSurface, "at rest every cell lies on the cube's surface" );

		int faces[ 6 ] = { 0, 0, 0, 0, 0, 0 };
		for( const auto& c : r.cells )
		{
			const Vec3 p = Centre( c );
			if( std::fabs( p.z - 0.5f ) < 1e-4f ) faces[ 0 ]++;
			else if( std::fabs( p.x - 0.5f ) < 1e-4f ) faces[ 1 ]++;
			else if( std::fabs( p.z + 0.5f ) < 1e-4f ) faces[ 2 ]++;
			else if( std::fabs( p.x + 0.5f ) < 1e-4f ) faces[ 3 ]++;
			else if( std::fabs( p.y - 0.5f ) < 1e-4f ) faces[ 4 ]++;
			else if( std::fabs( p.y + 0.5f ) < 1e-4f ) faces[ 5 ]++;
		}
		bool sixteenEach = true;
		for( int i = 0; i < 6; ++i )
			if( faces[ i ] != 16 )
				sixteenEach = false;
		Check( sixteenEach, "all six faces carry exactly 16 cells" );

		// Mid-turn, cells legitimately leave the cube's bounding box -- a
		// corner cubie of a turning slice traces an arc of radius sqrt(0.5),
		// which is why a physical cube needs clearance. So the invariant is not
		// "inside the box", it is **rigidity**: every rotation here is about an
		// axis through the origin, so a cell's distance from the centre can
		// never change. A cell that drifts is a matrix bug.
		bool rigid = true;
		{
			auto                 rest = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
			std::vector< float > radius;
			for( const auto& c : rest.cells )
			{
				const Vec3 p = Centre( c );
				radius.push_back( std::sqrt( p.x * p.x + p.y * p.y + p.z * p.z ) );
			}
			for( int k = 1; k <= 64; ++k )
			{
				const float t  = static_cast< float >( k ) * 0.37f;
				auto        rr = BuildLayout( sched, logos, 4, 4, t, lo );
				if( rr.cells.size() != radius.size() )
				{
					rigid = false;
					break;
				}
				for( size_t i = 0; i < rr.cells.size(); ++i )
				{
					const Vec3  p = Centre( rr.cells[ i ] );
					const float d = std::sqrt( p.x * p.x + p.y * p.y + p.z * p.z );
					if( std::fabs( d - radius[ i ] ) > 1e-3f )
						rigid = false;
				}
			}
		}
		Check( rigid, "every cell keeps its distance from the centre: the motion is rigid" );

		// Between moves the cube must be solid again -- every completed move is
		// a whole quarter turn, so at a move boundary nothing is mid-swing.
		bool solidBetweenMoves = true;
		for( int k = 0; k < 16; ++k )
		{
			const float t  = static_cast< float >( k ) * 2.0f;// a move every 2s at speed 1
			auto        rr = BuildLayout( sched, logos, 4, 4, t, lo );
			for( const auto& c : rr.cells )
			{
				const Vec3  p = Centre( c );
				const float m = std::max( std::max( std::fabs( p.x ), std::fabs( p.y ) ), std::fabs( p.z ) );
				if( std::fabs( m - 0.5f ) > 1e-3f )
					solidBetweenMoves = false;
			}
		}
		Check( solidBetweenMoves, "at every move boundary the cube is solid again" );

		// The loop point. The sequence scrambles then unscrambles, so one full
		// cycle must land exactly back on the opening frame -- otherwise the
		// clip visibly jumps every time round.
		{
			auto a = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
			auto b = BuildLayout( sched, logos, 4, 4, 32.0f, lo );// 16 moves x 2s
			bool identical = a.cells.size() == b.cells.size();
			for( size_t i = 0; identical && i < a.cells.size(); ++i )
			{
				const Vec3 p = Centre( a.cells[ i ] ), q = Centre( b.cells[ i ] );
				if( std::fabs( p.x - q.x ) > 1e-3f || std::fabs( p.y - q.y ) > 1e-3f || std::fabs( p.z - q.z ) > 1e-3f )
					identical = false;
			}
			Check( identical, "one full cycle returns to a solved cube: the loop does not snap" );
		}

		// The scramble is followed by its own inverse, so the cycle returns to a
		// solved cube. Without this the loop point snaps.
		auto moves = MoveSequence( 3, n );
		Check( static_cast< int >( moves.size() ) == kMoveCount, "the sequence is kMoveCount long" );
		bool mirrored = true;
		for( int i = 0; i < kMoveCount / 2; ++i )
		{
			const auto& a = moves[ static_cast< size_t >( i ) ];
			const auto& b = moves[ static_cast< size_t >( kMoveCount - 1 - i ) ];
			if( a.axis != b.axis || a.layer != b.layer || a.turns != -b.turns )
				mirrored = false;
		}
		Check( mirrored, "second half is the first half inverted, in reverse order" );
	}

	printf( "fit\n" );
	{
		FillOptions fo;
		fo.aspectAware = false;
		auto logos     = MakeLogos( 4, 4.0f );// 4:1 marks
		logos[ 0 ].trimX0 = 0.25f;
		logos[ 0 ].trimX1 = 0.75f;
		auto sched        = BuildSchedule( logos, 4, 4, fo );

		LayoutOptions lo;
		lo.outputAspect = 1.0f;// square output, so cells are square

		lo.fit  = Fit::Stretch;
		auto st = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
		Check( std::fabs( st.cells[ 0 ].uvScaleX - 1.0f ) < 1e-5f && std::fabs( st.cells[ 0 ].uvScaleY - 1.0f ) < 1e-5f,
			   "stretch maps the cell onto the whole layer" );

		lo.fit  = Fit::Fit;
		auto ft = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
		// A 4:1 mark in a 1:1 cell is letterboxed: it uses the full width and a
		// quarter of the height, so v must run four times faster than u.
		Check( std::fabs( ft.cells[ 0 ].uvScaleX - 1.0f ) < 1e-4f, "fit uses the full width" );
		Check( std::fabs( ft.cells[ 0 ].uvScaleY - 4.0f ) < 1e-4f, "and compresses v by the aspect, letterboxing" );

		lo.fit  = Fit::Fill;
		auto fl = BuildLayout( sched, logos, 4, 4, 0.0f, lo );
		Check( fl.cells[ 0 ].uvScaleX < 1.0f && std::fabs( fl.cells[ 0 ].uvScaleY - 1.0f ) < 1e-4f,
			   "fill covers the cell and crops the width instead" );
	}

	printf( "twinkle: the point is that no two cells agree\n" );
	{
		FillOptions fo;
		auto        logos = MakeLogos( 9 );
		auto        sched = BuildSchedule( logos, 6, 4, fo );

		LayoutOptions lo;
		lo.twinkle = 1.0f;
		auto r     = BuildLayout( sched, logos, 6, 4, 1.7f, lo );

		std::set< int > levels;
		for( const auto& c : r.cells )
			levels.insert( static_cast< int >( c.brightness * 200.0f ) );
		Check( levels.size() > 15, "cells are spread across many brightness levels, not pulsing in lockstep" );

		LayoutOptions off;
		auto          r2 = BuildLayout( sched, logos, 6, 4, 1.7f, off );
		bool          allFull = true;
		for( const auto& c : r2.cells )
			if( std::fabs( c.brightness - 1.0f ) > 1e-6f )
				allFull = false;
		Check( allFull, "and are all at full brightness when twinkle is off" );
	}

	printf( "a cube keeps its stickers\n" );
	{
		// gridiron#5: "after every couple of rotations it is as though the cube
		// resets". The geometry was never the problem -- it is smooth to within
		// float noise throughout. The schedule was: it turned over on its own
		// four-second clock, which is every TWO moves, and swapped 52 of 54
		// cells at once. A Rubik's cube's stickers do not change.
		std::vector< Logo > logos;
		for( int i = 0; i < 24; ++i )
		{
			Logo l;
			l.index         = i;
			l.aspect        = 1.0f;
			l.trimX1        = 1.0f;
			l.trimY1        = 1.0f;
			l.trimmedAspect = 1.0f;
			logos.push_back( l );
		}

		FillOptions fo;
		int fc = 0, fr = 0;
		FillGrid( Mode::Rubik, 3, 3, fc, fr );
		const Schedule sched = BuildSchedule( logos, fc, fr, fo );

		LayoutOptions lo;
		lo.mode         = Mode::Rubik;
		lo.outputAspect = 16.0f / 9.0f;

		// One full scramble-and-solve cycle is kMoveCount moves at two seconds
		// each. Walk just under it, finely.
		const float cycle = static_cast< float >( kMoveCount ) * 2.0f;

		std::vector< int > previousLogos;
		std::vector< Mat4 > previousModels;
		int  swaps = 0;
		float worstJump = 0.0f;

		for( float t = 0.05f; t < cycle - 0.05f; t += 0.01f )
		{
			const auto r = BuildLayout( sched, logos, 3, 3, t, lo );

			std::vector< int >  nowLogos;
			std::vector< Mat4 > nowModels;
			for( const auto& c : r.cells )
			{
				nowLogos.push_back( c.logo );
				nowModels.push_back( c.model );
			}

			if( !previousLogos.empty() && nowLogos != previousLogos )
				++swaps;

			if( previousModels.size() == nowModels.size() )
			{
				for( size_t i = 0; i < nowModels.size(); ++i )
				{
					const Vec3 a = TransformPoint( previousModels[ i ], { 0.0f, 0.0f, 0.0f } );
					const Vec3 b = TransformPoint( nowModels[ i ], { 0.0f, 0.0f, 0.0f } );
					const float d = std::sqrt( ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ) +
					                           ( a.z - b.z ) * ( a.z - b.z ) );
					if( d > worstJump )
						worstJump = d;
				}
			}

			previousLogos  = nowLogos;
			previousModels = nowModels;
		}

		Check( swaps == 0, "no cell changes logo inside one scramble-and-solve cycle" );
		printf( "    %d logo swaps in %.0f s of cube\n", swaps, cycle );

		// A quarter turn eased over two seconds moves a cell about 0.008 per
		// 10 ms sample at its fastest. Anything past 0.02 is a cut, not motion.
		Check( worstJump < 0.02f, "and the geometry never jumps" );
		printf( "    largest cell movement between 10 ms samples %.4f\n", worstJump );

		// It must still deal a new set eventually, or a cube would show the same
		// logos for ever and the whole schedule would be pointless.
		const auto first = BuildLayout( sched, logos, 3, 3, 1.0f, lo );
		const auto later = BuildLayout( sched, logos, 3, 3, cycle + 1.0f, lo );
		bool dealt = false;
		for( size_t i = 0; i < first.cells.size() && i < later.cells.size(); ++i )
			if( first.cells[ i ].logo != later.cells[ i ].logo )
				dealt = true;
		Check( dealt, "but the next cycle deals a new set" );
	}

	printf( "\n%s\n", failures ? "FAILED" : "all checks passed" );
	return failures ? 1 : 0;
}
