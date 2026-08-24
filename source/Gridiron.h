#pragma once

#include <string>
#include <vector>

#include "Atlas.h"
#include "Clock.h"
#include "Controls.h"
#include "Fill.h"
#include "Layout.h"
#include "Library.h"
#include "Render.h"

#include <FFGLSDK.h>

/**
    The plugin: an animated step-and-repeat built from a folder of logos.

    This class is the wiring. The parts that decide anything live elsewhere and
    are all testable without a GL context or a host:

      `Library`  the folder, decoded, off the render thread
      `Atlas`    those images as one texture array
      `Fill`     which logo goes in which cell, seeded and reproducible
      `Layout`   where every cell is in three dimensions, at this moment
      `Render`   the instanced draw, and gridiron's own depth framebuffer

    ## What happens per frame, and what does not

    The expensive things are cached on the inputs that change them. The folder
    is re-read only when the path parameter changes; the atlas is re-uploaded
    only when the folder finishes loading; the schedule is rebuilt only when the
    folder, the grid, the seed or a fill rule changes. Only the layout is solved
    every frame, and it is arithmetic over a few hundred cells.

    Getting that wrong is not a performance nicety. Re-reading a folder of sixty
    logos inside `ProcessOpenGL` would stall the composition every frame, and
    re-shuffling the schedule every frame would make the seed meaningless -- the
    wall would be different in each one.
*/
namespace gridiron
{
class GridironPlugin : public CFFGLPlugin
{
public:
	GridironPlugin();
	~GridironPlugin() override = default;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float    GetFloatParameter( unsigned int index ) override;

	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char*    GetTextParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;

private:
	void PollLoader();
	void RebuildScheduleIfNeeded();
	int  OptionIndex( unsigned int param, int count ) const;

	float mParams[ PT_COUNT ] = {};

	std::string mFolderFile;///< the file the operator picked
	std::string mFolderPath;///< the text override
	std::string mAboutText;
	std::string mNote;

	Loader               mLoader;
	std::vector< Image > mImages;
	Atlas                mAtlas;
	Renderer             mRenderer;
	Clock                mClock;

	Schedule mSchedule;

	/// What the cached schedule was built from. A rebuild happens when any of
	/// these changes and never otherwise.
	struct ScheduleKey
	{
		int      columns = 0, rows = 0;
		uint32_t seed        = 0;
		int      logoCount   = 0;
		int      hero        = 0, heroCols = 0, heroRows = 0, heroLogo = 0;
		bool     noAdjacent = false, aspectAware = false, equalAirtime = false;
		int      mode = 0;

		bool operator!=( const ScheduleKey& o ) const
		{
			return columns != o.columns || rows != o.rows || seed != o.seed || logoCount != o.logoCount ||
				   hero != o.hero || heroCols != o.heroCols || heroRows != o.heroRows || heroLogo != o.heroLogo ||
				   noAdjacent != o.noAdjacent || aspectAware != o.aspectAware || equalAirtime != o.equalAirtime ||
				   mode != o.mode;
		}
	};
	ScheduleKey mScheduleKey;

	std::string mLoadedFolder;///< what the loader was last pointed at
	bool        mUploadPending = false;

	bool   mGlReady      = false;
	bool   mHostTimeSeen = false;
	double mHostTime     = 0.0;
};

} // namespace gridiron
