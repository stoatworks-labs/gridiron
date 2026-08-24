#pragma once

#include <cmath>

/**
    Just enough linear algebra for a cube.

    Hand-rolled rather than vendored. The cube needs perspective, orthographic,
    look-at, three axis rotations, translate, scale and a multiply -- eight
    functions and about a hundred and fifty lines. glm is an excellent library
    and pulling in a few thousand headers to get eight functions would be a
    dependency the release job has to carry, the attributions file has to list,
    and Windows has to find. The fleet vendors minimally; this stays in keeping.

    **Column-major, like OpenGL.** `m[column][row]`, and `m[3]` is the
    translation column. This is the layout `glUniformMatrix4fv` expects with
    `transpose = GL_FALSE`, so a matrix goes to the GPU as-is with no fixup --
    which is the entire reason to suffer column-major in the first place.
*/
namespace gridiron
{
struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator-( const Vec3& a, const Vec3& b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 Cross( const Vec3& a, const Vec3& b )
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float Dot( const Vec3& a, const Vec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Normalise( const Vec3& v )
{
	const float len = std::sqrt( Dot( v, v ) );
	if( len <= 1e-8f )
		return { 0.0f, 0.0f, 0.0f };
	return { v.x / len, v.y / len, v.z / len };
}

/// `m[column][row]`. Element (row r, column c) is `m[c][r]`.
struct Mat4
{
	float m[ 4 ][ 4 ] = {};

	static Mat4 Identity()
	{
		Mat4 r;
		r.m[ 0 ][ 0 ] = r.m[ 1 ][ 1 ] = r.m[ 2 ][ 2 ] = r.m[ 3 ][ 3 ] = 1.0f;
		return r;
	}

	/// Pointer to 16 contiguous floats, ready for `glUniformMatrix4fv` with
	/// `transpose = GL_FALSE`.
	const float* Data() const { return &m[ 0 ][ 0 ]; }
};

/// `a * b`, applied right to left: the result transforms by `b` then by `a`.
inline Mat4 operator*( const Mat4& a, const Mat4& b )
{
	Mat4 r;
	for( int c = 0; c < 4; ++c )
		for( int row = 0; row < 4; ++row )
		{
			float sum = 0.0f;
			for( int k = 0; k < 4; ++k )
				sum += a.m[ k ][ row ] * b.m[ c ][ k ];
			r.m[ c ][ row ] = sum;
		}
	return r;
}

/// Transform a point (w = 1), discarding the w it comes back with. Used to ask
/// where a cell centre ended up, which is how slice membership is decided.
inline Vec3 TransformPoint( const Mat4& a, const Vec3& p )
{
	return { a.m[ 0 ][ 0 ] * p.x + a.m[ 1 ][ 0 ] * p.y + a.m[ 2 ][ 0 ] * p.z + a.m[ 3 ][ 0 ],
			 a.m[ 0 ][ 1 ] * p.x + a.m[ 1 ][ 1 ] * p.y + a.m[ 2 ][ 1 ] * p.z + a.m[ 3 ][ 1 ],
			 a.m[ 0 ][ 2 ] * p.x + a.m[ 1 ][ 2 ] * p.y + a.m[ 2 ][ 2 ] * p.z + a.m[ 3 ][ 2 ] };
}

inline Mat4 Translate( float x, float y, float z )
{
	Mat4 r         = Mat4::Identity();
	r.m[ 3 ][ 0 ] = x;
	r.m[ 3 ][ 1 ] = y;
	r.m[ 3 ][ 2 ] = z;
	return r;
}

inline Mat4 Scale( float x, float y, float z )
{
	Mat4 r         = Mat4::Identity();
	r.m[ 0 ][ 0 ] = x;
	r.m[ 1 ][ 1 ] = y;
	r.m[ 2 ][ 2 ] = z;
	return r;
}

inline Mat4 RotateX( float a )
{
	Mat4        r = Mat4::Identity();
	const float s = std::sin( a ), c = std::cos( a );
	r.m[ 1 ][ 1 ] = c;
	r.m[ 1 ][ 2 ] = s;
	r.m[ 2 ][ 1 ] = -s;
	r.m[ 2 ][ 2 ] = c;
	return r;
}

inline Mat4 RotateY( float a )
{
	Mat4        r = Mat4::Identity();
	const float s = std::sin( a ), c = std::cos( a );
	r.m[ 0 ][ 0 ] = c;
	r.m[ 0 ][ 2 ] = -s;
	r.m[ 2 ][ 0 ] = s;
	r.m[ 2 ][ 2 ] = c;
	return r;
}

inline Mat4 RotateZ( float a )
{
	Mat4        r = Mat4::Identity();
	const float s = std::sin( a ), c = std::cos( a );
	r.m[ 0 ][ 0 ] = c;
	r.m[ 0 ][ 1 ] = s;
	r.m[ 1 ][ 0 ] = -s;
	r.m[ 1 ][ 1 ] = c;
	return r;
}

/// Rotation about an arbitrary unit axis. The slice rotations use this so that
/// one code path covers all three cube axes.
inline Mat4 RotateAxis( const Vec3& axis, float angle )
{
	const Vec3  a = Normalise( axis );
	const float s = std::sin( angle ), c = std::cos( angle ), t = 1.0f - c;

	Mat4 r         = Mat4::Identity();
	r.m[ 0 ][ 0 ] = t * a.x * a.x + c;
	r.m[ 0 ][ 1 ] = t * a.x * a.y + s * a.z;
	r.m[ 0 ][ 2 ] = t * a.x * a.z - s * a.y;
	r.m[ 1 ][ 0 ] = t * a.x * a.y - s * a.z;
	r.m[ 1 ][ 1 ] = t * a.y * a.y + c;
	r.m[ 1 ][ 2 ] = t * a.y * a.z + s * a.x;
	r.m[ 2 ][ 0 ] = t * a.x * a.z + s * a.y;
	r.m[ 2 ][ 1 ] = t * a.y * a.z - s * a.x;
	r.m[ 2 ][ 2 ] = t * a.z * a.z + c;
	return r;
}

inline Mat4 Ortho( float l, float r, float b, float t, float n, float f )
{
	Mat4 o         = Mat4::Identity();
	o.m[ 0 ][ 0 ] = 2.0f / ( r - l );
	o.m[ 1 ][ 1 ] = 2.0f / ( t - b );
	o.m[ 2 ][ 2 ] = -2.0f / ( f - n );
	o.m[ 3 ][ 0 ] = -( r + l ) / ( r - l );
	o.m[ 3 ][ 1 ] = -( t + b ) / ( t - b );
	o.m[ 3 ][ 2 ] = -( f + n ) / ( f - n );
	return o;
}

inline Mat4 Perspective( float fovY, float aspect, float n, float f )
{
	const float tanHalf = std::tan( fovY * 0.5f );
	Mat4        p;
	p.m[ 0 ][ 0 ] = 1.0f / ( aspect * tanHalf );
	p.m[ 1 ][ 1 ] = 1.0f / tanHalf;
	p.m[ 2 ][ 2 ] = -( f + n ) / ( f - n );
	p.m[ 2 ][ 3 ] = -1.0f;
	p.m[ 3 ][ 2 ] = -( 2.0f * f * n ) / ( f - n );
	return p;
}

inline Mat4 LookAt( const Vec3& eye, const Vec3& centre, const Vec3& up )
{
	const Vec3 f = Normalise( centre - eye );
	const Vec3 s = Normalise( Cross( f, up ) );
	const Vec3 u = Cross( s, f );

	Mat4 r         = Mat4::Identity();
	r.m[ 0 ][ 0 ] = s.x;
	r.m[ 1 ][ 0 ] = s.y;
	r.m[ 2 ][ 0 ] = s.z;
	r.m[ 0 ][ 1 ] = u.x;
	r.m[ 1 ][ 1 ] = u.y;
	r.m[ 2 ][ 1 ] = u.z;
	r.m[ 0 ][ 2 ] = -f.x;
	r.m[ 1 ][ 2 ] = -f.y;
	r.m[ 2 ][ 2 ] = -f.z;
	r.m[ 3 ][ 0 ] = -Dot( s, eye );
	r.m[ 3 ][ 1 ] = -Dot( u, eye );
	r.m[ 3 ][ 2 ] = Dot( f, eye );
	return r;
}

} // namespace gridiron
