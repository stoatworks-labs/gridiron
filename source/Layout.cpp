#include "Layout.h"

#include <algorithm>
#include <cmath>

#include "Hash.h"

namespace gridiron
{
namespace
{
constexpr float kPi     = 3.14159265358979f;
constexpr float kHalfPi = kPi * 0.5f;

float Fract( float x ) { return x - std::floor( x ); }

/// Wrap `v` into [-period/2, +period/2). What makes a step-and-repeat repeat.
float WrapCentred( float v, float period )
{
	if( period <= 0.0f )
		return v;
	return v - period * std::floor( v / period + 0.5f );
}

/// Ease in and out of a quarter turn. A slice that starts and stops abruptly
/// reads as a glitch; this is the difference between a cube being turned and a
/// cube being teleported.
float EaseInOut( float t )
{
	t = std::min( std::max( t, 0.0f ), 1.0f );
	return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 3.0f ) * 0.5f;
}

/// Decelerate to a stop. Used by the slot machine, where the whole point is
/// that the reel slows before it lands.
float EaseOut( float t )
{
	t = std::min( std::max( t, 0.0f ), 1.0f );
	const float u = 1.0f - t;
	return 1.0f - u * u * u;
}

/// Which layer of the cube a point sits in, along one axis. The cube spans
/// -0.5 .. 0.5, so a face lies exactly on a boundary and has to be clamped in
/// rather than falling off the end.
int LayerOf( const Vec3& p, int axis, int n )
{
	const float v = ( axis == 0 ) ? p.x : ( axis == 1 ) ? p.y : p.z;
	int         l = static_cast< int >( std::floor( ( v + 0.5f ) * static_cast< float >( n ) ) );
	return std::min( std::max( l, 0 ), n - 1 );
}

Vec3 AxisVector( int axis )
{
	return axis == 0 ? Vec3{ 1.0f, 0.0f, 0.0f } : axis == 1 ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 0.0f, 0.0f, 1.0f };
}

/// The six face orientations of a unit cube, as rotations applied to a quad
/// that starts life on the +Z face.
Mat4 FaceRotation( int face )
{
	switch( face )
	{
		case 0: return Mat4::Identity();       // +Z, front
		case 1: return RotateY( kHalfPi );     // +X, right
		case 2: return RotateY( kPi );         // -Z, back
		case 3: return RotateY( -kHalfPi );    // -X, left
		case 4: return RotateX( -kHalfPi );    // +Y, top
		default: return RotateX( kHalfPi );    // -Y, bottom
	}
}

/// Map cell-local 0..1 UV onto the logo's layer, honouring the fit mode.
///
/// The layer holds the artwork stretched to its full extent; `trim` is the part
/// of it that is actually opaque. Everything here is arithmetic on those two
/// rectangles, and the shader does nothing but `uv * scale + offset`.
void FitUv( Fit fit, float cellAspect, const Logo& logo, CellTransform& out )
{
	// The sub-rectangle of the layer we intend to show, and its aspect.
	float rx0 = 0.0f, ry0 = 0.0f, rx1 = 1.0f, ry1 = 1.0f;
	float contentAspect = logo.aspect;

	if( fit == Fit::Trim )
	{
		rx0           = logo.trimX0;
		ry0           = logo.trimY0;
		rx1           = logo.trimX1;
		ry1           = logo.trimY1;
		contentAspect = logo.trimmedAspect;
	}

	const float rw = rx1 - rx0;
	const float rh = ry1 - ry0;

	if( fit == Fit::Stretch )
	{
		out.uvScaleX  = rw;
		out.uvScaleY  = rh;
		out.uvOffsetX = rx0;
		out.uvOffsetY = ry0;
		return;
	}

	// `cover` is true only for Fill. Trim deliberately does not cover: it has
	// already discarded the exported margin, and covering on top of that scales
	// a wordmark until its name runs off the sides of the cell.
	const bool cover = ( fit == Fit::Fill );

	// Fraction of the cell the content occupies on each axis, before covering.
	float fx = 1.0f, fy = 1.0f;
	if( contentAspect > cellAspect )
		fy = cellAspect / contentAspect;// wider than the cell: bars top and bottom
	else
		fx = contentAspect / cellAspect;// taller: bars left and right

	if( cover )
	{
		// Scale up until neither axis has a bar left, which crops the other.
		const float k = 1.0f / std::min( fx, fy );
		fx *= k;
		fy *= k;
	}

	// uv_layer = rect_origin + ( uv_cell - centreOffset ) / f * rect_size
	out.uvScaleX  = rw / fx;
	out.uvScaleY  = rh / fy;
	out.uvOffsetX = rx0 - ( ( 1.0f - fx ) * 0.5f ) * out.uvScaleX;
	out.uvOffsetY = ry0 - ( ( 1.0f - fy ) * 0.5f ) * out.uvScaleY;
}
} // namespace

int CubeSize( int columns, int rows )
{
	return std::min( std::max( std::max( columns, rows ), 2 ), 8 );
}

void FillGrid( Mode mode, int columns, int rows, int& outColumns, int& outRows )
{
	if( mode == Mode::Rubik )
	{
		const int n = CubeSize( columns, rows );
		outColumns  = n;
		outRows     = n * 6;
		return;
	}
	outColumns = columns;
	outRows    = rows;
}

std::vector< CubeMove > MoveSequence( uint32_t seed, int n )
{
	const int half = kMoveCount / 2;

	std::vector< CubeMove > moves;
	moves.reserve( kMoveCount );

	for( int i = 0; i < half; ++i )
	{
		CubeMove m;
		m.axis  = static_cast< int >( Hash2( static_cast< uint32_t >( i ), seed ) % 3u );
		m.layer = static_cast< int >( Hash2( static_cast< uint32_t >( i ), seed ^ 0x1234567u ) % static_cast< uint32_t >( n ) );
		m.turns = ( Hash01( static_cast< uint32_t >( i ), seed ^ 0x89abcdefu ) < 0.5f ) ? 1.0f : -1.0f;
		moves.push_back( m );
	}

	// The scramble undone, last move first. This is what makes the sequence
	// return to a solved cube and therefore loop without a snap.
	for( int i = half - 1; i >= 0; --i )
	{
		CubeMove m = moves[ static_cast< size_t >( i ) ];
		m.turns    = -m.turns;
		moves.push_back( m );
	}
	return moves;
}

LayoutResult BuildLayout( const Schedule& schedule,
						  const std::vector< Logo >& logos,
						  int columns,
						  int rows,
						  float timeSeconds,
						  const LayoutOptions& options )
{
	LayoutResult out;
	if( schedule.steps.empty() || logos.empty() )
	{
		out.view       = Mat4::Identity();
		out.projection = Mat4::Identity();
		return out;
	}

	columns = std::max( columns, 1 );
	rows    = std::max( rows, 1 );

	const int   stepCount = static_cast< int >( schedule.steps.size() );
	const float t         = timeSeconds * options.speed;

	// ---- camera ----------------------------------------------------------
	if( options.projection == Projection::Perspective || options.mode == Mode::Rubik )
	{
		// Far enough back to frame the subject, and no further. The distance is
		// derived from what is actually being looked at rather than from a
		// constant: a cube seen corner-on needs its bounding sphere to fit --
		// half the space diagonal, sqrt(3)/2 -- while the flat wall only needs
		// its half-height, because the projection already carries the output
		// aspect and so the horizontal extent fits whenever the vertical does.
		//
		// A single hard-coded constant here framed the cube at three percent of
		// the frame, which is a correct render of an unreadable picture.
		const float radius = ( options.mode == Mode::Rubik ) ? 0.86602540f : 0.5f;
		const float dist   = radius / std::tan( options.fov * 0.5f ) * 1.15f;
		const Vec3  eye{ dist * std::sin( options.camYaw ) * std::cos( options.camPitch ),
						 dist * std::sin( options.camPitch ),
						 dist * std::cos( options.camYaw ) * std::cos( options.camPitch ) };
		out.view       = LookAt( eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } );
		out.projection = Perspective( options.fov, options.outputAspect, 0.05f, 100.0f );
	}
	else
	{
		// The flat wall: an orthographic box one unit tall, widened to the
		// output. Every cell is coplanar at z = 0.
		const float halfW = options.outputAspect * 0.5f;
		out.view          = Mat4::Identity();
		out.projection    = Ortho( -halfW, halfW, -0.5f, 0.5f, -10.0f, 10.0f );
	}

	// Whole-grid rotation lives in the view matrix, which is what makes it cost
	// nothing per cell and behave identically flat or on a cube.
	//
	// Scroll used to live here too, as `Translate( scrollX * timeSeconds, ... )`.
	// That was wrong twice over and it blanked the wall:
	//
	//   - **It never wrapped.** `timeSeconds` is absolute composition time, so
	//     the translation grew without bound. A step-and-repeat is by definition
	//     a repeating tile; scrolling one has to bring the far side back round.
	//     Instead the wall slid out of the orthographic box and stayed out.
	//   - **It was in the wrong unit.** `Scroll()` is documented as cells per
	//     second and returns up to +/-4, but a view-space translate is in world
	//     units, where the whole wall is one unit tall. "Four cells a second"
	//     was really four *wall heights* a second, and the wall was gone inside
	//     a quarter of a second.
	//
	// Scroll is now applied per cell, below, where the cell size is known and
	// the position can be wrapped.
	if( options.gridRot != 0.0f )
		out.view = out.view * RotateZ( options.gridRot * timeSeconds );

	const bool cube = ( options.mode == Mode::Rubik );

	// `columns` and `rows` are the operator's grid. The schedule was built on
	// the virtual grid, so derive that here -- once -- rather than expecting
	// every caller to have done it and to have done it the same way.
	const int n = CubeSize( columns, rows );
	int       gridColumns = columns, gridRows = rows;
	FillGrid( options.mode, columns, rows, gridColumns, gridRows );

	// Cell geometry for the flat wall: fit the grid into the output, keeping
	// cells square-ish rather than stretching them with the canvas.
	const float wallH     = 1.0f;
	const float wallW     = options.outputAspect;
	const float cellW     = wallW / static_cast< float >( gridColumns );
	const float cellH     = wallH / static_cast< float >( gridRows );
	const float pad       = options.cellPad;
	const float cellAspect = cube ? 1.0f : ( cellW / cellH );

	const std::vector< CubeMove > moves = cube ? MoveSequence( options.seed, n ) : std::vector< CubeMove >{};

	// Which move we are in, and how far through it.
	int   moveIndex = 0;
	float moveFrac  = 0.0f;
	if( cube )
	{
		const float mt = t * 0.5f;// a move every two seconds at speed 1
		moveIndex      = static_cast< int >( std::floor( mt ) ) % kMoveCount;
		moveFrac       = Fract( mt );
		if( moveIndex < 0 )
			moveIndex += kMoveCount;
	}

	// Which step of the schedule the wall is showing. Fade mode overrides this
	// per cell, because there the whole point is that cells are out of step.
	const int globalStep = stepCount == 1 ? 0 : static_cast< int >( std::floor( t * 0.25f ) ) % stepCount;

	// Whole-cell offsets for a logo walking through the grid.
	int walkC = 0, walkR = 0;
	if( options.walk != Walk::Off )
	{
		const int steps = static_cast< int >( std::floor( timeSeconds * options.walkRate ) );
		switch( options.walk )
		{
			case Walk::Left: walkC = steps; break;
			case Walk::Right: walkC = -steps; break;
			case Walk::Up: walkR = steps; break;
			case Walk::Down: walkR = -steps; break;
			default: break;
		}
	}

	const Step& step = schedule.steps[ static_cast< size_t >( globalStep ) ];

	// Wrap-around copies of the cells at the wall's edges, appended after the
	// wall itself so the draw order is still front-to-back within each.
	std::vector< CellTransform > ghosts;

	for( const Placement& p : step.cells )
	{
		if( p.colSpan == 0 )
			continue;// covered by the span of the cell to its left

		CellTransform c;
		c.hero = p.hero;

		// ---- which logo ---------------------------------------------------
		int logo = p.logo;
		int cellStep = globalStep;

		if( options.walk != Walk::Off && !p.hero )
		{
			// Content moves between cells; the cells themselves do not move.
			const int sc = ( ( p.col + walkC ) % gridColumns + gridColumns ) % gridColumns;
			const int sr = ( ( p.row + walkR ) % gridRows + gridRows ) % gridRows;
			for( const Placement& q : step.cells )
				if( q.col == sc && q.row == sr && q.colSpan != 0 )
				{
					logo = q.logo;
					break;
				}
		}

		// Fade mode gives every cell its own phase and its own place in the
		// schedule, which is what "changing the logo each time they loop" means.
		float fadeOpacity = 1.0f;
		if( options.fade > 0.0f && !p.hero )
		{
			const float phase = Hash01( static_cast< uint32_t >( p.row * gridColumns + p.col ), options.seed ^ 0x51ed270bu );
			const float u     = t * 0.3f + phase;
			cellStep          = static_cast< int >( std::floor( u ) ) % stepCount;
			if( cellStep < 0 )
				cellStep += stepCount;

			// A triangle held near the top: cells spend most of the cycle up
			// and swap content while they are dark.
			const float f = Fract( u );
			const float v = f < 0.15f ? ( f / 0.15f ) : ( f > 0.85f ? ( 1.0f - f ) / 0.15f : 1.0f );
			fadeOpacity   = 1.0f - options.fade * ( 1.0f - v );

			const Step& s2 = schedule.steps[ static_cast< size_t >( cellStep ) ];
			for( const Placement& q : s2.cells )
				if( q.col == p.col && q.row == p.row && q.colSpan != 0 )
				{
					logo = q.logo;
					break;
				}
		}

		if( logo < 0 || logo >= static_cast< int >( logos.size() ) )
			continue;
		c.logo = logo;

		// ---- brightness and opacity ---------------------------------------
		c.opacity = fadeOpacity;
		if( options.twinkle > 0.0f && !p.hero )
		{
			// Every cell at a different point in its own cycle. The hash is the
			// whole trick: without it the wall pulses in lockstep and reads as
			// one flashing rectangle rather than as twinkling.
			const float phase = Hash01( static_cast< uint32_t >( p.row * gridColumns + p.col ), options.seed ^ 0x2545f491u );
			const float rate  = 0.6f + Hash01( static_cast< uint32_t >( p.row * gridColumns + p.col ), options.seed ^ 0x7feb352du ) * 0.8f;
			const float s     = 0.5f + 0.5f * std::sin( ( t * rate + phase ) * 2.0f * kPi );
			c.brightness      = 1.0f - options.twinkle * ( 1.0f - s );
		}

		// ---- where it is ---------------------------------------------------
		const float spanW = static_cast< float >( p.colSpan );
		const float spanH = static_cast< float >( p.rowSpan );

		if( cube )
		{
			const int face    = p.row / n;
			const int faceRow = p.row % n;
			const int faceCol = p.col;

			const float cs = 1.0f / static_cast< float >( n );
			const float x  = ( static_cast< float >( faceCol ) + 0.5f * spanW ) * cs - 0.5f;
			const float y  = 0.5f - ( static_cast< float >( faceRow ) + 0.5f * spanH ) * cs;

			const Mat4 faceRot = FaceRotation( face );
			Mat4       base    = faceRot * Translate( x, y, 0.5f ) *
						  Scale( cs * spanW * ( 1.0f - pad ), cs * spanH * ( 1.0f - pad ), 1.0f );

			// Replay the moves. Slice membership is tested against where the
			// cell has got to, not where it started -- that is what makes this a
			// Rubik's cube rather than six independently spinning faces.
			Vec3 pos   = TransformPoint( faceRot, { x, y, 0.5f } );
			Mat4 accum = Mat4::Identity();

			for( int i = 0; i < moveIndex; ++i )
			{
				const CubeMove& m = moves[ static_cast< size_t >( i ) ];
				if( LayerOf( pos, m.axis, n ) != m.layer )
					continue;
				const Mat4 r = RotateAxis( AxisVector( m.axis ), m.turns * kHalfPi );
				accum        = r * accum;
				pos          = TransformPoint( r, pos );
			}

			const CubeMove& active = moves[ static_cast< size_t >( moveIndex ) ];
			if( LayerOf( pos, active.axis, n ) == active.layer )
				accum = RotateAxis( AxisVector( active.axis ), active.turns * kHalfPi * EaseInOut( moveFrac ) ) * accum;

			c.model = accum * base;
		}
		else
		{
			float cx = ( static_cast< float >( p.col ) + 0.5f * spanW ) * cellW - wallW * 0.5f;
			float cy = wallH * 0.5f - ( static_cast< float >( p.row ) + 0.5f * spanH ) * cellH;

			// The slot machine: each column its own reel, decelerating to a stop
			// on a whole cell. The travel is an integer number of cells, so
			// easing all the way to it *cannot* stop out of alignment -- the
			// landing is exact by construction rather than by a snap at the end.
			if( options.mode == Mode::SlotMachine && !p.hero )
			{
				const uint32_t key   = static_cast< uint32_t >( p.col );
				const float    cycle = 6.0f;
				const float    u     = Fract( t / cycle );
				const float    stop  = 0.45f + Hash01( key, options.seed ^ 0x68e31da4u ) * 0.45f;
				const int      trips = gridRows * ( 2 + static_cast< int >( Hash01( key, options.seed ^ 0xb5297a4du ) * 4.0f ) );

				const float travel = static_cast< float >( trips ) * EaseOut( std::min( u / stop, 1.0f ) );
				const float shift  = Fract( travel );
				const int   whole  = static_cast< int >( std::floor( travel ) );

				cy -= shift * cellH;
				// Wrap the cell back to the top of the column as it leaves the
				// bottom, so the reel is endless rather than a strip sliding off.
				if( cy < -wallH * 0.5f - cellH * 0.5f )
					cy += wallH + cellH;

				// The content scrolls with the reel.
				const int sr = ( ( p.row + whole ) % gridRows + gridRows ) % gridRows;
				for( const Placement& q : step.cells )
					if( q.col == p.col && q.row == sr && q.colSpan != 0 )
					{
						c.logo = q.logo;
						break;
					}
			}

			// Whole-grid scroll, in cells per second, wrapped into the wall.
			//
			// The wall is `gridColumns` x `gridRows` cells and tiles the output
			// exactly, so its period is the wall itself. Wrapping a cell centre
			// modulo that period sends the cell that leaves one side back in at
			// the other -- the same idea as the slot machine's column wrap,
			// applied to both axes and to the whole grid at once.
			if( options.scrollX != 0.0f )
				cx = WrapCentred( cx + options.scrollX * cellW * timeSeconds, wallW );
			if( options.scrollY != 0.0f )
				cy = WrapCentred( cy + options.scrollY * cellH * timeSeconds, wallH );

			const float scaleX = cellW * spanW * ( 1.0f - pad );
			const float scaleY = cellH * spanH * ( 1.0f - pad );

			c.model = Translate( cx, cy, 0.0f ) * Scale( scaleX, scaleY, 1.0f );

			// Wrapping alone leaves a hole. The cells cover exactly one wall and
			// no more, so the moment they shift by any fraction of a cell there
			// is that much bare canvas at the trailing edge -- which on a
			// sponsor board is a strip of nothing marching across the screen.
			//
			// So a cell near an edge is drawn a second time one whole wall away,
			// where it fills that hole. The copy is clipped by the projection
			// when it is not needed, which is cheaper than working out whether
			// it is.
			if( options.scrollX != 0.0f || options.scrollY != 0.0f )
			{
				for( int gy = -1; gy <= 1; ++gy )
				{
					for( int gx = -1; gx <= 1; ++gx )
					{
						if( gx == 0 && gy == 0 )
							continue;// the cell itself, already pushed below
						if( gx != 0 && options.scrollX == 0.0f )
							continue;
						if( gy != 0 && options.scrollY == 0.0f )
							continue;

						const float gcx = cx + static_cast< float >( gx ) * wallW;
						const float gcy = cy + static_cast< float >( gy ) * wallH;

						// Only if some part of it lands on the canvas.
						if( std::abs( gcx ) - scaleX * 0.5f > wallW * 0.5f )
							continue;
						if( std::abs( gcy ) - scaleY * 0.5f > wallH * 0.5f )
							continue;

						CellTransform g = c;
						g.model         = Translate( gcx, gcy, 0.0f ) * Scale( scaleX, scaleY, 1.0f );
						ghosts.push_back( g );
					}
				}
			}
		}

		// ---- which frame, for an animated logo -----------------------------
		c.frame = static_cast< int >( std::floor( timeSeconds * 10.0f ) );

		// ---- fit ------------------------------------------------------------
		const float thisCellAspect = cellAspect * ( spanW / spanH );
		FitUv( options.fit, thisCellAspect, logos[ static_cast< size_t >( c.logo ) ], c );

		// ---- does the edge treatment reach this cell ------------------------
		switch( options.edgeScope )
		{
			case EdgeScope::EveryCell: c.edge = options.edge != Edge::Off; break;
			case EdgeScope::HeroOnly: c.edge = options.edge != Edge::Off && p.hero; break;
			case EdgeScope::ExceptHero: c.edge = options.edge != Edge::Off && !p.hero; break;
			default: c.edge = false; break;// Global is a post pass, not a cell property
		}

		out.cells.push_back( c );
	}

	out.cells.insert( out.cells.end(), ghosts.begin(), ghosts.end() );

	return out;
}

} // namespace gridiron
