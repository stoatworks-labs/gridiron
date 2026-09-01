#include "Render.h"

#include <algorithm>
#include <cstring>

#include "Shaders.h"

namespace gridiron
{
bool Renderer::InitGL()
{
	if( mReady )
		return true;

	mNote.clear();

	if( !mWall.Compile( kWallVertexShader, kWallFragmentShader ) )
	{
		mNote = "the wall shader would not compile";
		return false;
	}
	if( !mComposite.Compile( kCompositeVertexShader, kCompositeFragmentShader ) )
	{
		mWall.FreeGLResources();
		mNote = "the composite shader would not compile";
		return false;
	}

	glGenVertexArrays( 1, &mVao );
	glGenBuffers( 1, &mInstances );

	glBindVertexArray( mVao );
	glBindBuffer( GL_ARRAY_BUFFER, mInstances );

	// Six attribute slots of the sixteen guaranteed: four for the matrix, one
	// for the UV transform, one for the per-cell scalars. Every one is
	// per-instance, so the divisor is 1 on all of them -- a slot left at the
	// default divisor of 0 would be read per *vertex*, which draws four
	// different cells' worth of data across the corners of one quad and looks
	// like a shear rather than like a bug.
	const GLsizei stride = kFloatsPerInstance * static_cast< GLsizei >( sizeof( float ) );
	for( GLuint i = 0; i < 6; ++i )
	{
		glEnableVertexAttribArray( i );
		glVertexAttribPointer( i, 4, GL_FLOAT, GL_FALSE, stride,
							   reinterpret_cast< const void* >( static_cast< uintptr_t >( i * 4 * sizeof( float ) ) ) );
		glVertexAttribDivisor( i, 1 );
	}

	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );

	mReady = true;
	return true;
}

void Renderer::ReleaseTarget()
{
	if( mFbo != 0 )
	{
		glDeleteFramebuffers( 1, &mFbo );
		mFbo = 0;
	}
	if( mColour != 0 )
	{
		glDeleteTextures( 1, &mColour );
		mColour = 0;
	}
	if( mDepth != 0 )
	{
		glDeleteRenderbuffers( 1, &mDepth );
		mDepth = 0;
	}
	mTargetW = mTargetH = 0;
}

void Renderer::DeInitGL()
{
	ReleaseTarget();

	if( mInstances != 0 )
	{
		glDeleteBuffers( 1, &mInstances );
		mInstances = 0;
	}
	if( mVao != 0 )
	{
		glDeleteVertexArrays( 1, &mVao );
		mVao = 0;
	}
	mWall.FreeGLResources();
	mComposite.FreeGLResources();

	mInstanceCapacity = 0;
	mReady            = false;
}

bool Renderer::EnsureTarget( int width, int height )
{
	if( width <= 0 || height <= 0 )
		return false;
	if( mFbo != 0 && mTargetW == width && mTargetH == height )
		return true;

	ReleaseTarget();

	glGenTextures( 1, &mColour );
	glBindTexture( GL_TEXTURE_2D, mColour );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );

	// The whole reason this class exists. GL_DEPTH_COMPONENT24 is universally
	// available; a depth-stencil would work too and buys nothing here.
	glGenRenderbuffers( 1, &mDepth );
	glBindRenderbuffer( GL_RENDERBUFFER, mDepth );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );

	glGenFramebuffers( 1, &mFbo );
	glBindFramebuffer( GL_FRAMEBUFFER, mFbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColour, 0 );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepth );

	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	if( status != GL_FRAMEBUFFER_COMPLETE )
	{
		mNote = "gridiron's framebuffer is incomplete (0x" + std::to_string( static_cast< int >( status ) ) + ")";
		ReleaseTarget();
		return false;
	}

	mTargetW = width;
	mTargetH = height;
	return true;
}

void Renderer::Draw( const LayoutResult& layout,
					 const Atlas& atlas,
					 int width,
					 int height,
					 GLuint hostFBO,
					 const DrawOptions& options )
{
	if( !mReady || !atlas.Valid() )
		return;
	if( !EnsureTarget( width, height ) )
		return;

	// ---- pack the instance data ------------------------------------------
	std::vector< float > data;
	data.reserve( layout.cells.size() * kFloatsPerInstance );
	for( const CellTransform& c : layout.cells )
	{
		if( c.logo < 0 || c.logo >= atlas.LayerCount() )
			continue;

		const float* m = c.model.Data();
		data.insert( data.end(), m, m + 16 );

		data.push_back( c.uvScaleX );
		data.push_back( c.uvScaleY );
		data.push_back( c.uvOffsetX );
		data.push_back( c.uvOffsetY );

		data.push_back( static_cast< float >( c.logo ) );
		data.push_back( c.brightness );
		data.push_back( c.opacity );
		data.push_back( c.edge ? 1.0f : 0.0f );
	}

	const GLsizei instances = static_cast< GLsizei >( data.size() / kFloatsPerInstance );
	if( instances == 0 )
		return;

	glBindVertexArray( mVao );
	glBindBuffer( GL_ARRAY_BUFFER, mInstances );
	if( data.size() > mInstanceCapacity )
	{
		glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( data.size() * sizeof( float ) ), data.data(), GL_STREAM_DRAW );
		mInstanceCapacity = data.size();
	}
	else
	{
		glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast< GLsizeiptr >( data.size() * sizeof( float ) ), data.data() );
	}

	// ---- the wall --------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, mFbo );
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClearDepth( 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );

	// No back-face culling. A cube's far faces are hidden by depth, and culling
	// as well would drop the flat wall entirely whenever whole-grid rotation
	// turned it past ninety degrees.
	glDisable( GL_CULL_FACE );

	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	glUseProgram( mWall.GetGLID() );

	const Mat4 viewProjection = layout.projection * layout.view;
	glUniformMatrix4fv( glGetUniformLocation( mWall.GetGLID(), "ViewProjection" ), 1, GL_FALSE, viewProjection.Data() );

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D_ARRAY, atlas.Texture() );
	mWall.Set( "Logos", 0 );

	const bool perCell = options.edgeScope != EdgeScope::Global;
	mWall.Set( "EdgeMode", perCell ? static_cast< int >( options.edge ) : 0 );
	mWall.Set( "EdgeAmount", options.edgeAmount );
	mWall.Set( "EdgeThreshold", options.edgeThreshold );
	mWall.Set( "LayerTexel", atlas.LayerSize() > 0 ? 1.0f / static_cast< float >( atlas.LayerSize() ) : 0.0f );

	mWall.Set( "GridLines", options.gridLines ? 1 : 0 );
	mWall.Set( "GridWidth", options.gridWidth );
	mWall.Set( "GridColour", options.gridR, options.gridG, options.gridB );
	mWall.Set( "CellFill", options.cellFill );
	mWall.Set( "FillColour", options.fillR, options.fillG, options.fillB );

	glDrawArraysInstanced( GL_TRIANGLE_STRIP, 0, 4, instances );

	// ---- composite into the host -----------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( 0, 0, width, height );

	glDisable( GL_DEPTH_TEST );

	glUseProgram( mComposite.GetGLID() );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, mColour );
	mComposite.Set( "Wall", 0 );
	mComposite.Set( "Texel", 1.0f / static_cast< float >( width ), 1.0f / static_cast< float >( height ) );

	const bool global = options.edgeScope == EdgeScope::Global;
	mComposite.Set( "EdgeMode", global ? static_cast< int >( options.edge ) : 0 );
	mComposite.Set( "EdgeAmount", options.edgeAmount );
	mComposite.Set( "EdgeThreshold", options.edgeThreshold );

	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	// ---- hand the context back -------------------------------------------
	glDisable( GL_BLEND );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	glUseProgram( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );
}

} // namespace gridiron
