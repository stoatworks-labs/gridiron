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

	printf( "fit keeps the artwork's own shape\n" );
	{
		// The atlas pads every logo into a SQUARE layer, centred and keeping its
		// aspect, so a 4:1 mark spans its layer's width and the middle quarter
		// of its height. These logos are built the way PlanAtlas builds them;
		// what is checked is where the artwork's box lands in the cell.
		//
		// Up to v0.2.3, Fit, Fill and Stretch mapped the whole square layer at
		// the artwork's aspect and ignored where the artwork was in it: a 4:1
		// wordmark came out at 16:1, by default, on every wall. Only Trim, which
		// starts from the trim box, was right.
		auto padded = []( float aspect ) {
			Logo        l;
			const float fx  = aspect >= 1.0f ? 1.0f : aspect;
			const float fy  = aspect >= 1.0f ? 1.0f / aspect : 1.0f;
			l.aspect        = aspect;
			l.trimmedAspect = aspect;
			l.imageX0 = l.trimX0 = ( 1.0f - fx ) * 0.5f;
			l.imageX1 = l.trimX1 = l.imageX0 + fx;
			l.imageY0 = l.trimY0 = ( 1.0f - fy ) * 0.5f;
			l.imageY1 = l.trimY1 = l.imageY0 + fy;
			return l;
		};

		bool fitShape = true, fitInside = true, fillShape = true, fillCovers = true, stretchFills = true, trimShape = true;
		for( float aspect : { 4.0f, 2.5f, 1.0f, 0.6f } )
		{
			std::vector< Logo > logos = { padded( aspect ) };
			FillOptions         fo;
			const Schedule      sched = BuildSchedule( logos, 1, 1, fo );

			for( float output : { 1.0f, 16.0f / 9.0f, 0.6f, 5.0f } )// a 1x1 wall: the cell is the output's shape
			{
				for( Fit fit : { Fit::Fit, Fit::Fill, Fit::Stretch, Fit::Trim } )
				{
					LayoutOptions lo;
					lo.outputAspect = output;
					lo.fit          = fit;
					const auto r    = BuildLayout( sched, logos, 1, 1, 0.0f, lo );
					if( r.cells.size() != 1 )
					{
						fitShape = false;
						continue;
					}
					const CellTransform& c = r.cells[ 0 ];

					// Where the artwork box (or the trim box, for Trim) lands in
					// cell-local 0..1, and the shape it is drawn at on screen.
					const Logo& l  = logos[ 0 ];
					const float x0 = fit == Fit::Trim ? l.trimX0 : l.imageX0, x1 = fit == Fit::Trim ? l.trimX1 : l.imageX1;
					const float y0 = fit == Fit::Trim ? l.trimY0 : l.imageY0, y1 = fit == Fit::Trim ? l.trimY1 : l.imageY1;
					const float u0 = ( x0 - c.uvOffsetX ) / c.uvScaleX, u1 = ( x1 - c.uvOffsetX ) / c.uvScaleX;
					const float v0 = ( y0 - c.uvOffsetY ) / c.uvScaleY, v1 = ( y1 - c.uvOffsetY ) / c.uvScaleY;
					const float drawn = ( u1 - u0 ) * output / ( v1 - v0 );
					const float eps   = 1e-4f;

					switch( fit )
					{
						case Fit::Fit:
							if( std::fabs( drawn / aspect - 1.0f ) > eps )
								fitShape = false;
							// Inside the cell, and touching it on one axis.
							if( u0 < -eps || u1 > 1.0f + eps || v0 < -eps || v1 > 1.0f + eps ||
								( std::fabs( u1 - u0 - 1.0f ) > eps && std::fabs( v1 - v0 - 1.0f ) > eps ) )
								fitInside = false;
							break;
						case Fit::Fill:
							if( std::fabs( drawn / aspect - 1.0f ) > eps )
								fillShape = false;
							if( u0 > eps || u1 < 1.0f - eps || v0 > eps || v1 < 1.0f - eps )
								fillCovers = false;
							break;
						case Fit::Stretch:
							if( std::fabs( u0 ) > eps || std::fabs( u1 - 1.0f ) > eps || std::fabs( v0 ) > eps ||
								std::fabs( v1 - 1.0f ) > eps )
								stretchFills = false;
							break;
						default:
							if( std::fabs( drawn / l.trimmedAspect - 1.0f ) > eps )
								trimShape = false;
							break;
					}
				}
			}
		}
		Check( fitShape, "Fit draws a padded logo at its own aspect (a 4:1 wordmark is 4:1, not 16:1)" );
		Check( fitInside, "and inside its cell, touching it on one axis" );
		Check( fillShape, "Fill draws it at its own aspect too" );
		Check( fillCovers, "and covers the cell" );
		Check( stretchFills, "Stretch puts the artwork, not the padded layer, edge to edge in the cell" );
		Check( trimShape, "Trim, which was always right, still is" );
	}

	printf( "scroll's wrap-around copies are finished cells\n" );
	{
		// A cell near the edge of a scrolling wall is drawn again one whole wall
		// away, to fill the strip the scroll uncovers. Those copies were taken
		// before the fit, the frame and the edge flag were set, so the mark
		// entering at each edge was the whole layer stretched over the cell
		// with no edge treatment, and snapped to its real shape once it had
		// wrapped. A copy must be its cell in everything but where it is.
		std::vector< Logo > logos;
		for( float aspect : { 4.0f, 1.0f, 0.6f, 2.5f, 1.4f } )
		{
			Logo        l;
			const float fx  = aspect >= 1.0f ? 1.0f : aspect;
			const float fy  = aspect >= 1.0f ? 1.0f / aspect : 1.0f;
			l.index         = static_cast< int >( logos.size() );
			l.aspect        = aspect;
			l.trimmedAspect = aspect;
			l.imageX0 = l.trimX0 = ( 1.0f - fx ) * 0.5f;
			l.imageX1 = l.trimX1 = l.imageX0 + fx;
			l.imageY0 = l.trimY0 = ( 1.0f - fy ) * 0.5f;
			l.imageY1 = l.trimY1 = l.imageY0 + fy;
			if( aspect == 1.0f )
				l.frameMs = { 100, 150, 250 };// and one of them animated
			logos.push_back( l );
		}
		FillOptions    fo;
		const Schedule sched = BuildSchedule( logos, 6, 4, fo );

		LayoutOptions lo;
		lo.outputAspect = 16.0f / 9.0f;
		lo.scrollX      = 1.3f;
		lo.scrollY      = -0.7f;
		lo.edge         = Edge::Nib;
		lo.edgeScope    = EdgeScope::EveryCell;

		int  ghosts = 0, unmatched = 0;
		bool same   = true;
		for( Fit fit : { Fit::Fit, Fit::Fill, Fit::Trim, Fit::Stretch } )
		{
			lo.fit = fit;
			for( float t : { 0.37f, 2.9f, 61.25f } )
			{
				const auto  r     = BuildLayout( sched, logos, 6, 4, t, lo );
				const float wallW = lo.outputAspect;

				for( const auto& g : r.cells )
				{
					// A cell's own centre is wrapped into the wall; a copy's is a
					// whole wall outside it.
					const Vec3 gp = Centre( g );
					if( std::fabs( gp.x ) <= wallW * 0.5f + 1e-4f && std::fabs( gp.y ) <= 0.5f + 1e-4f )
						continue;
					++ghosts;

					const CellTransform* source = nullptr;
					for( const auto& c : r.cells )
					{
						const Vec3  cp = Centre( c );
						const float kx = ( gp.x - cp.x ) / wallW, ky = gp.y - cp.y;
						if( &c != &g && std::fabs( kx - std::round( kx ) ) < 1e-3f && std::fabs( ky - std::round( ky ) ) < 1e-3f &&
							std::fabs( c.model.m[ 0 ][ 0 ] - g.model.m[ 0 ][ 0 ] ) < 1e-6f &&
							std::fabs( c.model.m[ 1 ][ 1 ] - g.model.m[ 1 ][ 1 ] ) < 1e-6f &&
							std::fabs( cp.x ) <= wallW * 0.5f + 1e-4f && std::fabs( cp.y ) <= 0.5f + 1e-4f )
						{
							source = &c;
							break;
						}
					}
					if( source == nullptr )
					{
						++unmatched;
						continue;
					}
					if( g.logo != source->logo || g.frame != source->frame || g.edge != source->edge ||
						g.uvScaleX != source->uvScaleX || g.uvScaleY != source->uvScaleY ||
						g.uvOffsetX != source->uvOffsetX || g.uvOffsetY != source->uvOffsetY ||
						g.brightness != source->brightness || g.opacity != source->opacity )
						same = false;
				}
			}
		}
		Check( ghosts > 0, "a scrolling wall draws wrap-around copies" );
		Check( unmatched == 0, "every copy is one whole wall from a cell of the wall" );
		Check( same, "and carries that cell's fit, frame and edge treatment" );
		printf( "    %d copies checked\n", ghosts );
	}

	printf( "an animated logo plays its own frames\n" );
	{
		// The atlas held frame 0 and nothing read the frame index, so an
		// animated GIF sat still while the README said it played. The frame is
		// now picked from the GIF's own delays, looped, from composition time.
		Logo gif;
		gif.frameMs = { 100, 200, 300 };
		struct Case
		{
			float t;
			int   frame;
		};
		const Case cases[] = { { 0.0f, 0 },   { 0.099f, 0 }, { 0.1f, 1 },  { 0.299f, 1 }, { 0.3f, 2 },
							   { 0.599f, 2 }, { 0.6f, 0 },   { 0.75f, 1 }, { 600.25f, 1 } };
		bool         right   = true;
		for( const Case& c : cases )
			if( FrameAt( gif, c.t ) != c.frame )
			{
				right = false;
				printf( "    at %.3f s: frame %d, wanted %d\n", c.t, FrameAt( gif, c.t ), c.frame );
			}
		Check( right, "each frame shows for its own delay, and the animation loops" );

		Logo still;
		bool stillStill = true;
		for( float t : { 0.0f, 0.15f, 1.0f, 33.3f, 600.0f } )
			if( FrameAt( still, t ) != 0 )
				stillStill = false;
		Check( stillStill, "a still is always frame 0" );

		// Through BuildLayout: on a 2x1 wall of one still and one GIF, only the
		// GIF's cell changes frame.
		std::vector< Logo > logos( 2 );
		logos[ 1 ].index   = 1;
		logos[ 1 ].frameMs = gif.frameMs;
		FillOptions    fo;
		const Schedule sched = BuildSchedule( logos, 2, 1, fo );
		LayoutOptions  lo;
		std::set< int > gifFrames, stillFrames;
		for( float t : { 0.05f, 0.15f, 0.35f, 0.65f } )
			for( const auto& c : BuildLayout( sched, logos, 2, 1, t, lo ).cells )
				( c.logo == 1 ? gifFrames : stillFrames ).insert( c.frame );
		Check( gifFrames.size() == 3, "the layout walks the GIF's cell through all three frames" );
		Check( stillFrames.size() == 1 && *stillFrames.begin() == 0, "and leaves the still on frame 0" );
	}

	printf( "a wordmark on the cube turns with the slice of its first cell\n" );
	{
		// A wordmark across two cells has its centre exactly on the boundary
		// between two slices, so which slice it turned with was decided by the
		// last bit of a rounding, and the arm64 build (fused multiply-adds) and
		// the x86_64 build rounded it to different sides. The slice is now
		// chosen by the centre of its first cell, half a cell from any
		// boundary.
		//
		// Checked against a reference cube where every wordmark is two single
		// cells: the left half of each wordmark must go wherever the reference's
		// left cell goes. Both boundaries of a 3-cube are covered -- even rows
		// span columns 0-1, odd rows 1-2.
		const int n = 3;
		Schedule  wm, ref;
		wm.steps.resize( 1 );
		ref.steps.resize( 1 );
		auto put = []( Step& s, int col, int r, int span, int logo ) {
			Placement p;
			p.col     = col;
			p.row     = r;
			p.colSpan = span;
			p.logo    = logo;
			s.cells.push_back( p );
		};
		for( int row = 0; row < 6 * n; ++row )
		{
			const bool lead = row % 2 == 0;// the wordmark leads the row
			if( lead )
			{
				put( wm.steps[ 0 ], 0, row, 2, 0 );
				put( wm.steps[ 0 ], 1, row, 0, 0 );
				put( wm.steps[ 0 ], 2, row, 1, 1 );
			}
			else
			{
				put( wm.steps[ 0 ], 0, row, 1, 1 );
				put( wm.steps[ 0 ], 1, row, 2, 0 );
				put( wm.steps[ 0 ], 2, row, 0, 0 );
			}
			put( ref.steps[ 0 ], 0, row, 1, lead ? 0 : 1 );
			put( ref.steps[ 0 ], 1, row, 1, 0 );
			put( ref.steps[ 0 ], 2, row, 1, lead ? 1 : 0 );
		}
		std::vector< Logo > logos( 2 );
		logos[ 0 ].aspect = logos[ 0 ].trimmedAspect = 4.0f;
		logos[ 1 ].index                            = 1;

		LayoutOptions lo;
		lo.mode    = Mode::Rubik;
		lo.cellPad = 0.0f;// so the wordmark's left half is exactly its left cell

		bool  follows = true;
		int   torn    = 0, samples = 0;
		float worst   = 0.0f;
		for( uint32_t seed = 1; seed <= 12; ++seed )
		{
			lo.seed = seed;
			for( int k = 0; k < 4 * kMoveCount; ++k )
			{
				const float t = static_cast< float >( k ) * 0.5f;// on, a quarter and halfway through each move
				const auto  a = BuildLayout( wm, logos, n, n, t, lo );
				const auto  b = BuildLayout( ref, logos, n, n, t, lo );
				if( a.cells.size() != static_cast< size_t >( 12 * n ) || b.cells.size() != static_cast< size_t >( 18 * n ) )
				{
					follows = false;
					break;
				}
				for( int row = 0; row < 6 * n; ++row )
				{
					const bool lead = row % 2 == 0;
					const auto& w   = a.cells[ static_cast< size_t >( row * 2 + ( lead ? 0 : 1 ) ) ];
					const auto& l   = b.cells[ static_cast< size_t >( row * 3 + ( lead ? 0 : 1 ) ) ];
					const auto& r   = b.cells[ static_cast< size_t >( row * 3 + ( lead ? 1 : 2 ) ) ];

					const Vec3  half = TransformPoint( w.model, { -0.25f, 0.0f, 0.0f } );
					const Vec3  left = Centre( l );
					const float d    = std::sqrt( ( half.x - left.x ) * ( half.x - left.x ) + ( half.y - left.y ) * ( half.y - left.y ) +
											  ( half.z - left.z ) * ( half.z - left.z ) );
					worst = std::max( worst, d );
					if( d > 1e-4f )
						follows = false;

					// Did this sample actually pull the two cells apart? If no
					// move ever did, the check above proves nothing.
					const Vec3  right = Centre( r );
					const float apart = std::sqrt( ( right.x - left.x ) * ( right.x - left.x ) +
												   ( right.y - left.y ) * ( right.y - left.y ) +
												   ( right.z - left.z ) * ( right.z - left.z ) );
					if( std::fabs( apart - 1.0f / static_cast< float >( n ) ) > 1e-3f )
						++torn;
					++samples;
				}
			}
		}
		Check( torn > 0, "the moves do pull a wordmark's two cells into different slices" );
		Check( follows, "and every wordmark goes with its left-hand cell's slice, every time" );
		printf( "    %d of %d samples had the two cells in different places; worst miss %.2g\n", torn, samples, worst );
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
