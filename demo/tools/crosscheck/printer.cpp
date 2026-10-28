// The plugin's own arrangement and layout, printed, for the demo's crosscheck.
//
// Reads scenario.mjs's lines on stdin and answers each with what the plugin's
// C++ makes of it: Fill.cpp's schedule, Layout.cpp's every cell, and
// Controls.h's mappings on the way in, wired the way GridironPlugin wires them
// (FillGrid before the schedule, the hero off on a cube, Twinkle and Fade
// forced on by their modes, the camera pitch halved). run.mjs does the same
// with demo/port.js and prints the same lines. Floats print as their bits.
//
//   L n  aspect trimX0 trimY0 trimX1 trimY1 trimmedAspect  (x n, as bits)
//   S columns rows mode seed noAdjacent aspectAware equalAirtime hero heroCols heroRows heroLogo
//   Y mode projection fit walk edge edgeScope  cellPad speed scrollX scrollY gridRot walkRate
//     twinkle fade fov camYaw camPitch (host 0..1, as bits)  seed width height time(bits)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Controls.h"
#include "Fill.h"
#include "Layout.h"

using namespace gridiron;

namespace
{
float Bits( const std::string& hex )
{
	const uint32_t u = static_cast< uint32_t >( std::strtoul( hex.c_str(), nullptr, 16 ) );
	float          v = 0.0f;
	std::memcpy( &v, &u, 4 );
	return v;
}

void Put( float v )
{
	uint32_t u = 0;
	std::memcpy( &u, &v, 4 );
	std::printf( " %08x", u );
}

void PutMat( const Mat4& m )
{
	for( int c = 0; c < 4; ++c )
		for( int r = 0; r < 4; ++r )
			Put( m.m[ c ][ r ] );
}
} // namespace

int main()
{
	std::vector< Logo > logos;
	Schedule            schedule;
	int                 columns = 1, rows = 1;
	std::string         line;

	while( std::getline( std::cin, line ) )
	{
		std::istringstream in( line );
		std::string        tag;
		in >> tag;

		if( tag == "L" )
		{
			int n = 0;
			in >> n;
			logos.clear();
			for( int i = 0; i < n; ++i )
			{
				std::string a, x0, y0, x1, y1, t;
				in >> a >> x0 >> y0 >> x1 >> y1 >> t;
				Logo l;
				l.index         = i;
				l.aspect        = Bits( a );
				l.trimX0        = Bits( x0 );
				l.trimY0        = Bits( y0 );
				l.trimX1        = Bits( x1 );
				l.trimY1        = Bits( y1 );
				l.trimmedAspect = Bits( t );
				logos.push_back( l );
			}
			std::printf( "L %d\n", n );
		}
		else if( tag == "S" )
		{
			int      mode = 0, noAdj = 0, aspect = 0, equal = 0, hero = 0, heroCols = 0, heroRows = 0, heroLogo = 0;
			uint32_t seed = 0;
			in >> columns >> rows >> mode >> seed >> noAdj >> aspect >> equal >> hero >> heroCols >> heroRows >> heroLogo;

			int fc = 0, fr = 0;
			FillGrid( static_cast< Mode >( mode ), columns, rows, fc, fr );

			FillOptions o;
			o.seed             = seed;
			o.noAdjacentRepeat = noAdj != 0;
			o.aspectAware      = aspect != 0;
			o.equalAirtime     = equal != 0;
			o.hero             = static_cast< Mode >( mode ) == Mode::Rubik ? Hero::Off : static_cast< Hero >( hero );
			o.heroCols         = heroCols;
			o.heroRows         = heroRows;
			o.heroLogo         = heroLogo;

			schedule = BuildSchedule( logos, fc, fr, o );
			std::printf( "S %zu %d %d %d %s\n", schedule.steps.size(), schedule.ordinaryCells, schedule.exactParity ? 1 : 0,
						 schedule.adjacentRepeats, schedule.note.c_str() );
			for( const Step& s : schedule.steps )
			{
				std::printf( "T" );
				for( const Placement& p : s.cells )
					std::printf( " %d,%d,%d,%d,%d,%d", p.col, p.row, p.colSpan, p.rowSpan, p.logo, p.hero ? 1 : 0 );
				std::printf( "\n" );
			}
		}
		else if( tag == "Y" )
		{
			int         mode = 0, projection = 0, fit = 0, walk = 0, edge = 0, scope = 0, width = 0, height = 0;
			uint32_t    seed = 0;
			std::string h[ 13 ], time;
			in >> mode >> projection >> fit >> walk >> edge >> scope;
			for( std::string& s : h )
				in >> s;
			in >> seed >> width >> height >> time;

			LayoutOptions lo;
			lo.mode         = static_cast< Mode >( mode );
			lo.projection   = static_cast< Projection >( projection );
			lo.fit          = static_cast< Fit >( fit );
			lo.walk         = static_cast< Walk >( walk );
			lo.edge         = static_cast< Edge >( edge );
			lo.edgeScope    = static_cast< EdgeScope >( scope );
			lo.cellPad      = CellPad( Bits( h[ 0 ] ) );
			lo.speed        = Speed( Bits( h[ 1 ] ) );
			lo.scrollX      = Scroll( Bits( h[ 2 ] ) );
			lo.scrollY      = Scroll( Bits( h[ 3 ] ) );
			lo.gridRot      = GridRot( Bits( h[ 4 ] ) );
			lo.walkRate     = WalkRate( Bits( h[ 5 ] ) );
			lo.twinkle      = Bits( h[ 6 ] );
			lo.fade         = Bits( h[ 7 ] );
			lo.fov          = Fov( Bits( h[ 8 ] ) );
			lo.camYaw       = CamAngle( Bits( h[ 9 ] ) );
			lo.camPitch     = CamAngle( Bits( h[ 10 ] ) ) * 0.5f;
			lo.seed         = seed;
			lo.outputAspect = static_cast< float >( width ) / static_cast< float >( height );
			if( lo.mode == Mode::Twinkle && lo.twinkle <= 0.0f )
				lo.twinkle = 1.0f;
			if( lo.mode == Mode::FadeCycle && lo.fade <= 0.0f )
				lo.fade = 1.0f;

			const LayoutResult r = BuildLayout( schedule, logos, columns, rows, Bits( time ), lo );
			std::printf( "Y %zu", r.cells.size() );
			Put( EdgeThreshold( Bits( h[ 11 ] ) ) );
			Put( GridWidth( Bits( h[ 12 ] ) ) );
			PutMat( r.view );
			PutMat( r.projection );
			std::printf( "\n" );
			for( const CellTransform& c : r.cells )
			{
				std::printf( "C %d %d %d %d", c.logo, c.frame, c.hero ? 1 : 0, c.edge ? 1 : 0 );
				PutMat( c.model );
				Put( c.uvScaleX );
				Put( c.uvScaleY );
				Put( c.uvOffsetX );
				Put( c.uvOffsetY );
				Put( c.brightness );
				Put( c.opacity );
				std::printf( "\n" );
			}
		}
	}
	return 0;
}
