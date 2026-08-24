#include "Shaders.h"

namespace gridiron
{
const char* const kWallVertexShader = R"(#version 410 core

layout( location = 0 ) in vec4 iModel0;
layout( location = 1 ) in vec4 iModel1;
layout( location = 2 ) in vec4 iModel2;
layout( location = 3 ) in vec4 iModel3;
layout( location = 4 ) in vec4 iUv;   // scaleX, scaleY, offsetX, offsetY
layout( location = 5 ) in vec4 iMisc; // layer, brightness, opacity, edge

uniform mat4 ViewProjection;

out vec2  vCellUv;
out vec3  vLayerUv;
out float vBrightness;
out float vOpacity;
out float vEdge;

void main()
{
	// The unit quad, straight out of the vertex id. For a four-vertex triangle
	// strip the ids run 0,1,2,3 and this gives (0,0) (1,0) (0,1) (1,1), which is
	// the strip order -- not the winding order, which would be wrong.
	vec2 corner = vec2( float( gl_VertexID & 1 ), float( ( gl_VertexID >> 1 ) & 1 ) );

	vCellUv = corner;

	mat4 model  = mat4( iModel0, iModel1, iModel2, iModel3 );
	gl_Position = ViewProjection * model * vec4( corner - 0.5, 0.0, 1.0 );

	// v is flipped here and only here. The layer was uploaded with row 0 at the
	// top, and the quad's v runs upward, so without this every logo on the wall
	// is upside down -- which is obvious with a wordmark and easy to miss with
	// a roundel.
	vec2 uv  = vec2( corner.x, 1.0 - corner.y ) * iUv.xy + iUv.zw;
	vLayerUv = vec3( uv, iMisc.x );

	vBrightness = iMisc.y;
	vOpacity    = iMisc.z;
	vEdge       = iMisc.w;
}
)";

const char* const kWallFragmentShader = R"(#version 410 core

in vec2  vCellUv;
in vec3  vLayerUv;
in float vBrightness;
in float vOpacity;
in float vEdge;

uniform sampler2DArray Logos;

uniform int   EdgeMode;      // 0 off, 1 burin, 2 nib, 3 vectrix
uniform float EdgeAmount;
uniform float EdgeThreshold;
uniform float LayerTexel;    // 1.0 / layerSize

uniform int   GridLines;
uniform float GridWidth;
uniform vec3  GridColour;

out vec4 fragColour;

// Outside the layer rectangle there is nothing, not the clamped edge pixel.
// GL_CLAMP_TO_EDGE would smear the logo's border row across the whole letterbox
// bar, which on a fitted wide mark paints two coloured stripes down the cell.
vec4 Tap( vec2 uv )
{
	if( uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 )
		return vec4( 0.0 );
	return texture( Logos, vec3( uv, vLayerUv.z ) );
}

// Gradient magnitude by Sobel, measured on ink coverage rather than on colour.
// A sponsor logo is usually flat colour on transparency, so the interesting
// boundary is the alpha silhouette; using luminance alone finds nothing at all
// on a single-colour mark.
float EdgeStrength( vec2 uv )
{
	float t = LayerTexel;
	float s[ 9 ];
	int k = 0;
	for( int y = -1; y <= 1; ++y )
	{
		for( int x = -1; x <= 1; ++x )
		{
			vec4 c   = Tap( uv + vec2( float( x ), float( y ) ) * t );
			s[ k++ ] = c.a * ( 0.299 * c.r + 0.587 * c.g + 0.114 * c.b + 0.35 );
		}
	}
	float gx = ( s[ 2 ] + 2.0 * s[ 5 ] + s[ 8 ] ) - ( s[ 0 ] + 2.0 * s[ 3 ] + s[ 6 ] );
	float gy = ( s[ 6 ] + 2.0 * s[ 7 ] + s[ 8 ] ) - ( s[ 0 ] + 2.0 * s[ 1 ] + s[ 2 ] );
	return sqrt( gx * gx + gy * gy );
}

void main()
{
	vec4 c = Tap( vLayerUv.xy );

	if( EdgeMode > 0 && vEdge > 0.5 )
	{
		float e = smoothstep( EdgeThreshold, EdgeThreshold * 2.5, EdgeStrength( vLayerUv.xy ) );

		if( EdgeMode == 1 )
		{
			// Burin: engraved. The mark keeps its colour and the boundary is cut
			// into it as a dark line.
			c.rgb = mix( c.rgb, c.rgb * 0.15, e * EdgeAmount );
		}
		else if( EdgeMode == 2 )
		{
			// Nib: inked contour. Interior dropped, outline kept, in the mark's
			// own colour so a two-colour logo still reads as two colours.
			vec3 ink = c.rgb;
			c        = vec4( ink, e );
			c.a     *= EdgeAmount;
		}
		else
		{
			// Vectrix: a traced line. Bright, thin, and independent of the
			// mark's own colour -- this is the one that reads as a plot rather
			// than as a logo.
			c = vec4( vec3( 1.0 ) * e, e * EdgeAmount );
		}
	}

	c.rgb *= vBrightness;
	c.a   *= vOpacity;

	if( GridLines != 0 )
	{
		// Distance to the nearest cell border, in cell-local units. Drawn over
		// the logo rather than under it so a cell whose mark reaches the edge
		// still shows its border.
		vec2  d    = min( vCellUv, 1.0 - vCellUv );
		float near = min( d.x, d.y );
		float line = 1.0 - smoothstep( GridWidth * 0.5, GridWidth, near );
		c.rgb      = mix( c.rgb, GridColour, line );
		c.a        = max( c.a, line * vOpacity );
	}

	// Premultiplied out, to match the host's buffer and the blend function the
	// draw sets up.
	fragColour = vec4( c.rgb * c.a, c.a );
}
)";

const char* const kCompositeVertexShader = R"(#version 410 core

out vec2 vUv;

void main()
{
	vec2 corner = vec2( float( gl_VertexID & 1 ), float( ( gl_VertexID >> 1 ) & 1 ) );
	vUv         = corner;
	gl_Position = vec4( corner * 2.0 - 1.0, 0.0, 1.0 );
}
)";

const char* const kCompositeFragmentShader = R"(#version 410 core

in vec2 vUv;

uniform sampler2D Wall;
uniform vec2      Texel;

uniform int   EdgeMode;
uniform float EdgeAmount;
uniform float EdgeThreshold;

out vec4 fragColour;

float EdgeStrength( vec2 uv )
{
	float s[ 9 ];
	int k = 0;
	for( int y = -1; y <= 1; ++y )
	{
		for( int x = -1; x <= 1; ++x )
		{
			vec4 c   = texture( Wall, uv + vec2( float( x ), float( y ) ) * Texel );
			s[ k++ ] = c.a * ( 0.299 * c.r + 0.587 * c.g + 0.114 * c.b + 0.35 );
		}
	}
	float gx = ( s[ 2 ] + 2.0 * s[ 5 ] + s[ 8 ] ) - ( s[ 0 ] + 2.0 * s[ 3 ] + s[ 6 ] );
	float gy = ( s[ 6 ] + 2.0 * s[ 7 ] + s[ 8 ] ) - ( s[ 0 ] + 2.0 * s[ 1 ] + s[ 2 ] );
	return sqrt( gx * gx + gy * gy );
}

void main()
{
	vec4 c = texture( Wall, vUv );

	if( EdgeMode > 0 )
	{
		float e = smoothstep( EdgeThreshold, EdgeThreshold * 2.5, EdgeStrength( vUv ) );
		if( EdgeMode == 1 )
			c.rgb = mix( c.rgb, c.rgb * 0.15, e * EdgeAmount );
		else if( EdgeMode == 2 )
			c = vec4( c.rgb, mix( c.a, e, EdgeAmount ) );
		else
			c = mix( c, vec4( vec3( e ), e ), EdgeAmount );
	}

	fragColour = c;
}
)";

} // namespace gridiron
