#pragma once

#include <string>
#include <vector>

#include "Atlas.h"
#include "Layout.h"

#include <FFGLSDK.h>

/**
    The draw. One instanced pass for the wall, one full-screen pass to composite.

    ## Why gridiron owns a framebuffer

    `ProcessOpenGLStruct` hands over `HostFBO`, and the SDK's own comment says
    what it is for: so a plugin can **restore that binding after using its own
    FBOs**. It is a colour target. Nothing in FFGL promises it carries a depth
    attachment, the SDK ships no FBO helper at all, and a cube with six faces and
    rotating slices cannot be drawn without depth -- back faces would paint over
    front ones in whatever order the instances happened to be submitted.

    So the wall is drawn into a framebuffer of gridiron's own, with a depth
    buffer, and then composited into `HostFBO`. The flat wall does not need the
    depth buffer, but it costs a clear and nothing else, and one path that always
    works beats two paths where the rarely-taken one rots.

    The global edge treatment rides along in the composite pass, which is the
    natural place for it: it is a post effect on the finished wall, as against
    the per-cell treatment, which happens in the wall shader on one logo at a
    time.

    ## Restoring state

    Every `ffglex::Scoped*` binding clears to 0 on scope exit rather than
    restoring what was there, so they are no help for handing the context back to
    a host that had its own state. The state this touches is put back by hand at
    the end of `Draw`, in the same order flipbook does it, for the same reason.
*/
namespace gridiron
{
struct DrawOptions
{
	Edge      edge      = Edge::Off;
	EdgeScope edgeScope = EdgeScope::Global;
	float     edgeAmount    = 1.0f;
	float     edgeThreshold = 0.1f;

	bool  gridLines  = false;
	float gridWidth  = 0.01f;
	float gridR = 1.0f, gridG = 1.0f, gridB = 1.0f;

	/// 0 off, 1 one colour behind every cell, 2 a colour a cube face.
	int   cellFill = 0;
	float fillR = 0.10f, fillG = 0.10f, fillB = 0.10f;
};

class Renderer
{
public:
	Renderer() = default;

	bool InitGL();
	void DeInitGL();
	bool Ready() const { return mReady; }

	/// Draw the wall and composite it into `hostFBO`.
	void Draw( const LayoutResult& layout,
			   const Atlas& atlas,
			   int width,
			   int height,
			   GLuint hostFBO,
			   const DrawOptions& options );

	const std::string& Note() const { return mNote; }

private:
	bool EnsureTarget( int width, int height );
	void ReleaseTarget();

	ffglex::FFGLShader mWall;
	ffglex::FFGLShader mComposite;

	GLuint mVao      = 0;
	GLuint mInstances = 0;///< per-instance VBO
	size_t mInstanceCapacity = 0;

	GLuint mFbo     = 0;
	GLuint mColour  = 0;
	GLuint mDepth   = 0;
	int    mTargetW = 0;
	int    mTargetH = 0;

	bool        mReady = false;
	std::string mNote;
};

/// Floats per instance in the vertex buffer: mat4 + uv transform + scalars.
constexpr int kFloatsPerInstance = 16 + 4 + 4;

} // namespace gridiron
