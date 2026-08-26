#include "Gridiron.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Diag.h"

#include <atomic>

namespace gridiron
{
namespace
{
/// Turn a normalised option parameter back into an index. Resolume hands option
/// parameters back as the element *value*, which for these is the index already,
/// but a host that normalises would give 0..1 -- so both are accepted and
/// clamped. Getting this wrong silently selects mode 0 for ever.
int ToOption( float v, int count )
{
	if( count <= 1 )
		return 0;
	int i = ( v <= 1.0f && count > 2 && v != std::floor( v ) ) ? static_cast< int >( v * static_cast< float >( count - 1 ) + 0.5f )
															   : static_cast< int >( v + 0.5f );
	return std::min( std::max( i, 0 ), count - 1 );
}
} // namespace

GridironPlugin::GridironPlugin()
{
	static std::atomic< int > sNextInstance{ 1 };
	mInstanceId = sNextInstance.fetch_add( 1 );
	mTag        = "[" + std::to_string( mInstanceId ) + "] ";

	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	// Declaration order is the order the host draws these, and SetParamGroup
	// collapses runs -- so an id moved out of its run splits its group in two.
	std::vector< std::string > extensions;
	for( int i = 0; i < kExtensionCount; ++i )
		extensions.emplace_back( kExtensions[ i ] );

	// SetFileParamInfo ONLY. It ends in an unconditional `params.push_back()`,
	// so pairing it with a SetParamInfo for the same index registers the
	// parameter twice: two entries both claiming ID 0, one more entry than the
	// enum has, and a phantom trailing parameter with type 0xFFFFFFFF and a NaN
	// default. Nothing warns, the plugin builds, loads and reports its name --
	// `oxbow selftest` is what shows it.
	SetFileParamInfo( PT_FOLDER_FILE, "Logos", extensions, "" );
	SetParamInfo( PT_FOLDER_PATH, "Folder Override", FF_TYPE_TEXT, "" );
	SetParamInfo( PT_RELOAD, "Reload", FF_TYPE_EVENT, false );
	SetParamGroup( PT_FOLDER_FILE, "Content" );
	SetParamGroup( PT_FOLDER_PATH, "Content" );
	SetParamGroup( PT_RELOAD, "Content" );

	// Integers, with real ranges and real defaults. Only FF_TYPE_STANDARD gets
	// its default clamped into 0..1 -- see Controls.h.
	SetParamInfo( PT_COLUMNS, "Columns", FF_TYPE_INTEGER, 6.0f );
	SetParamRange( PT_COLUMNS, 1.0f, 32.0f );
	SetParamInfo( PT_ROWS, "Rows", FF_TYPE_INTEGER, 4.0f );
	SetParamRange( PT_ROWS, 1.0f, 32.0f );

	SetOptionParamInfo( PT_FIT, "Fit", 4, 0.0f );
	SetParamElementInfo( PT_FIT, 0, "Fit", 0.0f );
	SetParamElementInfo( PT_FIT, 1, "Fill", 1.0f );
	SetParamElementInfo( PT_FIT, 2, "Trim margins", 2.0f );
	SetParamElementInfo( PT_FIT, 3, "Stretch", 3.0f );

	SetParamInfo( PT_CELL_PAD, "Cell Padding", FF_TYPE_STANDARD, 0.12f );
	SetParamInfo( PT_GRID_LINES, "Show Grid", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_GRID_WIDTH, "Grid Width", FF_TYPE_STANDARD, 0.1f );
	SetParamInfo( PT_GRID_R, "Grid Red", FF_TYPE_RED, 1.0f );
	SetParamInfo( PT_GRID_G, "Grid Green", FF_TYPE_GREEN, 1.0f );
	SetParamInfo( PT_GRID_B, "Grid Blue", FF_TYPE_BLUE, 1.0f );
	for( unsigned int i = PT_COLUMNS; i <= PT_GRID_B; ++i )
		SetParamGroup( i, "Grid" );

	SetParamInfo( PT_SEED, "Seed", FF_TYPE_INTEGER, 1.0f );
	SetParamRange( PT_SEED, 0.0f, 9999.0f );
	SetParamInfo( PT_NO_ADJACENT, "Vary Neighbours", FF_TYPE_BOOLEAN, true );
	SetParamInfo( PT_ASPECT_AWARE, "Respect Aspect", FF_TYPE_BOOLEAN, true );
	SetParamInfo( PT_EQUAL_AIRTIME, "Equal Airtime", FF_TYPE_BOOLEAN, true );
	for( unsigned int i = PT_SEED; i <= PT_EQUAL_AIRTIME; ++i )
		SetParamGroup( i, "Fill" );

	SetOptionParamInfo( PT_HERO, "Hero Block", 4, 0.0f );
	SetParamElementInfo( PT_HERO, 0, "Off", 0.0f );
	SetParamElementInfo( PT_HERO, 1, "Top", 1.0f );
	SetParamElementInfo( PT_HERO, 2, "Middle", 2.0f );
	SetParamElementInfo( PT_HERO, 3, "Bottom", 3.0f );
	SetParamInfo( PT_HERO_COLS, "Hero Columns", FF_TYPE_INTEGER, 2.0f );
	SetParamRange( PT_HERO_COLS, 1.0f, 32.0f );
	SetParamInfo( PT_HERO_ROWS, "Hero Rows", FF_TYPE_INTEGER, 1.0f );
	SetParamRange( PT_HERO_ROWS, 1.0f, 32.0f );
	SetParamInfo( PT_HERO_LOGO, "Hero Logo", FF_TYPE_INTEGER, 0.0f );
	SetParamRange( PT_HERO_LOGO, 0.0f, 255.0f );
	for( unsigned int i = PT_HERO; i <= PT_HERO_LOGO; ++i )
		SetParamGroup( i, "Hero" );

	SetOptionParamInfo( PT_MODE, "Mode", 5, 0.0f );
	SetParamElementInfo( PT_MODE, 0, "Static", 0.0f );
	SetParamElementInfo( PT_MODE, 1, "Slot Machine", 1.0f );
	SetParamElementInfo( PT_MODE, 2, "Rubik", 2.0f );
	SetParamElementInfo( PT_MODE, 3, "Twinkle", 3.0f );
	SetParamElementInfo( PT_MODE, 4, "Fade Cycle", 4.0f );

	SetParamInfo( PT_SPEED, "Speed", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( PT_SCROLL_X, "Scroll X", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( PT_SCROLL_Y, "Scroll Y", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( PT_GRID_ROT, "Grid Rotation", FF_TYPE_STANDARD, 0.5f );

	SetOptionParamInfo( PT_WALK, "Logo Walk", 5, 0.0f );
	SetParamElementInfo( PT_WALK, 0, "Off", 0.0f );
	SetParamElementInfo( PT_WALK, 1, "Up", 1.0f );
	SetParamElementInfo( PT_WALK, 2, "Down", 2.0f );
	SetParamElementInfo( PT_WALK, 3, "Left", 3.0f );
	SetParamElementInfo( PT_WALK, 4, "Right", 4.0f );
	SetParamInfo( PT_WALK_RATE, "Walk Rate", FF_TYPE_STANDARD, 0.25f );
	SetParamInfo( PT_TWINKLE, "Twinkle", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_FADE, "Fade", FF_TYPE_STANDARD, 0.0f );
	for( unsigned int i = PT_MODE; i <= PT_FADE; ++i )
		SetParamGroup( i, "Motion" );

	SetOptionParamInfo( PT_PROJECTION, "Projection", 2, 0.0f );
	SetParamElementInfo( PT_PROJECTION, 0, "Flat", 0.0f );
	SetParamElementInfo( PT_PROJECTION, 1, "Perspective", 1.0f );
	SetParamInfo( PT_FOV, "Field of View", FF_TYPE_STANDARD, 0.4f );
	SetParamInfo( PT_CAM_YAW, "Camera Yaw", FF_TYPE_STANDARD, 0.56f );
	SetParamInfo( PT_CAM_PITCH, "Camera Pitch", FF_TYPE_STANDARD, 0.56f );
	for( unsigned int i = PT_PROJECTION; i <= PT_CAM_PITCH; ++i )
		SetParamGroup( i, "Camera" );

	SetOptionParamInfo( PT_EDGE, "Edge Treatment", 4, 0.0f );
	SetParamElementInfo( PT_EDGE, 0, "Off", 0.0f );
	SetParamElementInfo( PT_EDGE, 1, "Burin (engraved)", 1.0f );
	SetParamElementInfo( PT_EDGE, 2, "Nib (inked)", 2.0f );
	SetParamElementInfo( PT_EDGE, 3, "Vectrix (traced)", 3.0f );
	SetOptionParamInfo( PT_EDGE_SCOPE, "Edge Applies To", 4, 0.0f );
	SetParamElementInfo( PT_EDGE_SCOPE, 0, "Whole wall", 0.0f );
	SetParamElementInfo( PT_EDGE_SCOPE, 1, "Every logo", 1.0f );
	SetParamElementInfo( PT_EDGE_SCOPE, 2, "Hero only", 2.0f );
	SetParamElementInfo( PT_EDGE_SCOPE, 3, "All but hero", 3.0f );
	SetParamInfo( PT_EDGE_AMOUNT, "Edge Amount", FF_TYPE_STANDARD, 1.0f );
	SetParamInfo( PT_EDGE_THRESHOLD, "Edge Threshold", FF_TYPE_STANDARD, 0.35f );
	for( unsigned int i = PT_EDGE; i <= PT_EDGE_THRESHOLD; ++i )
		SetParamGroup( i, "Edge" );

	mAboutText = "gridiron -- animated step-and-repeat";
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, mAboutText.c_str() );

	// Mirror every default into the local cache, so a host that never calls
	// SetFloatParameter before the first frame still gets what the inspector
	// shows rather than a wall of zeroes.
	mParams[ PT_COLUMNS ]        = 6.0f;
	mParams[ PT_ROWS ]           = 4.0f;
	mParams[ PT_CELL_PAD ]       = 0.12f;
	mParams[ PT_GRID_WIDTH ]     = 0.1f;
	mParams[ PT_GRID_R ]         = 1.0f;
	mParams[ PT_GRID_G ]         = 1.0f;
	mParams[ PT_GRID_B ]         = 1.0f;
	mParams[ PT_SEED ]           = 1.0f;
	mParams[ PT_NO_ADJACENT ]    = 1.0f;
	mParams[ PT_ASPECT_AWARE ]   = 1.0f;
	mParams[ PT_EQUAL_AIRTIME ]  = 1.0f;
	mParams[ PT_HERO_COLS ]      = 2.0f;
	mParams[ PT_HERO_ROWS ]      = 1.0f;
	mParams[ PT_SPEED ]          = 0.5f;
	mParams[ PT_SCROLL_X ]       = 0.5f;
	mParams[ PT_SCROLL_Y ]       = 0.5f;
	mParams[ PT_GRID_ROT ]       = 0.5f;
	mParams[ PT_WALK_RATE ]      = 0.25f;
	mParams[ PT_FOV ]            = 0.4f;
	mParams[ PT_CAM_YAW ]        = 0.56f;
	mParams[ PT_CAM_PITCH ]      = 0.56f;
	mParams[ PT_EDGE_AMOUNT ]    = 1.0f;
	mParams[ PT_EDGE_THRESHOLD ] = 0.35f;
}

FFResult GridironPlugin::InitGL( const FFGLViewportStruct* vp )
{
	// Idempotent: a host may call this again, and the offline harness does on
	// every frame. Recompiling two shaders per frame would be several
	// milliseconds and would leak the VAO and the buffer each time.
	if( mGlReady )
		return CFFGLPlugin::InitGL( vp );

	diag::init();

	// The driver, on the line above whatever fails next. A shader that compiles
	// on one machine and not on another is a driver answer, not a source answer,
	// and the reporter cannot be expected to know to send this.
	auto glString = []( GLenum name ) {
		const GLubyte* s = glGetString( name );
		return s != nullptr ? std::string( reinterpret_cast< const char* >( s ) ) : std::string( "?" );
	};
	diag::info( mTag + "GL vendor=" + glString( GL_VENDOR ) + " renderer=" + glString( GL_RENDERER ) +
				" version=" + glString( GL_VERSION ) );

	if( !mRenderer.InitGL() )
	{
		mNote = mRenderer.Note();
		diag::error( mTag + "InitGL failed: " + mNote );
		return FF_FAIL;
	}
	mGlReady = true;
	return CFFGLPlugin::InitGL( vp );
}

FFResult GridironPlugin::DeInitGL()
{
	// While the context is still current. The Atlas destructor deliberately
	// makes no GL calls, because it can run after the context is gone.
	mAtlas.Release();
	mRenderer.DeInitGL();
	mGlReady = false;
	return FF_SUCCESS;
}

FFResult GridironPlugin::SetTime( double time )
{
	mHostTimeSeen = true;
	mHostTime     = time;
	return CFFGLPlugin::SetTime( time );
}

FFResult GridironPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	if( index == PT_RELOAD && value > 0.5f )
	{
		mLoadedFolder.clear();// force the folder to be read again
	}

	mParams[ index ] = value;
	return FF_SUCCESS;
}

float GridironPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? mParams[ index ] : 0.0f;
}

FFResult GridironPlugin::SetTextParameter( unsigned int index, const char* value )
{
	const std::string v = value != nullptr ? value : "";
	if( index == PT_FOLDER_FILE )
	{
		// Verbatim, quoted, and only when it changes. The path is the single
		// most useful thing in a "points at the folder, gets nothing" report,
		// and the interesting failures are all things that survive a glance --
		// a trailing space, a UNC prefix, a separator the host rewrote.
		if( v != mFolderFile )
			diag::info( mTag + "Logos parameter set to \"" + v + "\"" );
		mFolderFile = v;
	}
	else if( index == PT_FOLDER_PATH )
	{
		if( v != mFolderPath )
			diag::info( mTag + "Folder Override set to \"" + v + "\"" );
		mFolderPath = v;
	}
	else if( index == PT_ABOUT_TEXT )
		mAboutText = v;
	else
		return FF_FAIL;
	return FF_SUCCESS;
}

char* GridironPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_FOLDER_FILE )
		return const_cast< char* >( mFolderFile.c_str() );
	if( index == PT_FOLDER_PATH )
		return const_cast< char* >( mFolderPath.c_str() );
	if( index == PT_ABOUT_TEXT )
	{
		// The note is what the operator needs to see -- most importantly that an
		// SVG's live text will not draw. Showing it here puts it in front of
		// them without a second parameter to ignore.
		mAboutText = mNote.empty() ? "gridiron -- animated step-and-repeat" : mNote;
		return const_cast< char* >( mAboutText.c_str() );
	}
	return const_cast< char* >( "" );
}

int GridironPlugin::OptionIndex( unsigned int param, int count ) const
{
	return ToOption( mParams[ param ], count );
}

void GridironPlugin::PollLoader()
{
	// The override wins when it is set: someone who typed a path meant it.
	const std::string wanted = !mFolderPath.empty() ? mFolderPath : mFolderFile;

	if( wanted != mLoadedFolder )
	{
		mLoadedFolder = wanted;
		if( !wanted.empty() )
		{
			// Rasterise vectors at a size that suits the densest grid the
			// operator is likely to be on. Re-rasterising per cell size would be
			// sharper still and would re-upload the whole folder on every drag
			// of a window edge.
			diag::info( mTag + "reading folder around \"" + wanted + "\"" );
			mLoader.Start( wanted, 512 );
			mNote = "loading...";
		}
		else
		{
			mImages.clear();
			mAtlas.Release();
			mSchedule = Schedule{};
			mNote     = "pick any image inside the folder of logos";
		}
	}

	if( mLoader.Ready() )
	{
		mImages        = mLoader.Take();
		mUploadPending = true;
		mNote          = mLoader.Note();

		// The loader's note already carries the counts and every per-file
		// complaint -- an unreadable file, a GIF that would not decode, an SVG
		// whose sponsor name is live text and will not draw. All of it is
		// invisible in the host, so all of it goes in the log.
		if( mImages.empty() )
			diag::warn( mTag + "folder loaded but nothing usable came out of it: " + mNote );
		else
			diag::info( mTag + "folder loaded: " + mNote );
	}

	if( mUploadPending && mGlReady )
	{
		mAtlas.Upload( mImages );
		mUploadPending = false;
		if( !mAtlas.Valid() )
		{
			mNote += " -- " + mAtlas.Note();
			// The gridiron#1 failure. It is terminal until the folder changes
			// or Reload is pressed, and it is invisible, so it is an error.
			diag::error( mTag + "atlas upload failed, the wall will stay black: " + mAtlas.Note() );
		}
		else
		{
			diag::info( mTag + "atlas uploaded: " + mAtlas.Note() );
		}
		// Force a schedule rebuild: the logo count has almost certainly changed.
		mScheduleKey = ScheduleKey{};
	}
}

void GridironPlugin::RebuildScheduleIfNeeded()
{
	const Mode mode = static_cast< Mode >( OptionIndex( PT_MODE, 5 ) );

	int columns = static_cast< int >( mParams[ PT_COLUMNS ] + 0.5f );
	int rows    = static_cast< int >( mParams[ PT_ROWS ] + 0.5f );
	FillGrid( mode, columns, rows, columns, rows );

	ScheduleKey key;
	key.columns      = columns;
	key.rows         = rows;
	key.seed         = static_cast< uint32_t >( std::max( 0.0f, mParams[ PT_SEED ] ) );
	key.logoCount    = mAtlas.LayerCount();
	key.hero         = OptionIndex( PT_HERO, 4 );
	key.heroCols     = static_cast< int >( mParams[ PT_HERO_COLS ] + 0.5f );
	key.heroRows     = static_cast< int >( mParams[ PT_HERO_ROWS ] + 0.5f );
	key.heroLogo     = static_cast< int >( mParams[ PT_HERO_LOGO ] + 0.5f );
	key.noAdjacent   = mParams[ PT_NO_ADJACENT ] > 0.5f;
	key.aspectAware  = mParams[ PT_ASPECT_AWARE ] > 0.5f;
	key.equalAirtime = mParams[ PT_EQUAL_AIRTIME ] > 0.5f;
	key.mode         = static_cast< int >( mode );

	if( !( key != mScheduleKey ) )
		return;

	mScheduleKey = key;

	FillOptions o;
	o.seed             = key.seed;
	o.noAdjacentRepeat = key.noAdjacent;
	o.aspectAware      = key.aspectAware;
	o.equalAirtime     = key.equalAirtime;
	// The hero block is meaningless on a cube: there is no "top of the wall".
	o.hero     = mode == Mode::Rubik ? Hero::Off : static_cast< Hero >( key.hero );
	o.heroCols = key.heroCols;
	o.heroRows = key.heroRows;
	o.heroLogo = key.heroLogo;

	mSchedule = BuildSchedule( mAtlas.Logos(), columns, rows, o );
}

FFResult GridironPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr || !mGlReady )
		return FF_FAIL;

	mClock.Tick( mHostTime, mHostTimeSeen );

	// State the unit outright rather than leaving it to be inferred. The fleet
	// has twice diagnosed a thousand-times-fast plugin by reading source when
	// one line of log would have done it.
	diag::stateChanged( mTag + "clock", mTag + "host clock is " + mClock.Unit() );

	PollLoader();
	RebuildScheduleIfNeeded();

	if( !mAtlas.Valid() || mSchedule.steps.empty() )
	{
		// Every one of these is a black clip in the host and they are not
		// distinguishable from outside. `stateChanged` means an hour of black
		// costs one line rather than 180,000.
		diag::stateChanged( mTag + "draw", mTag + "drawing nothing: " +
										( mLoadedFolder.empty()  ? "no folder picked yet"
										  : !mAtlas.Valid()      ? "the atlas is not loaded -- see the line above this one"
																 : "the schedule came out empty" ) );
		return FF_SUCCESS;// nothing to draw is not a failure
	}

	diag::stateChanged( mTag + "draw", mTag + "drawing " + std::to_string( mAtlas.LayerCount() ) + " logos on a " +
									std::to_string( static_cast< int >( mParams[ PT_COLUMNS ] + 0.5f ) ) + "x" +
									std::to_string( static_cast< int >( mParams[ PT_ROWS ] + 0.5f ) ) + " grid" );

	const int width  = static_cast< int >( currentViewport.width );
	const int height = static_cast< int >( currentViewport.height );
	if( width <= 0 || height <= 0 )
		return FF_SUCCESS;

	const Mode mode = static_cast< Mode >( OptionIndex( PT_MODE, 5 ) );

	// The operator's grid, untransformed. BuildLayout derives the virtual grid
	// itself -- handing it the transformed one draws an empty frame on a cube.
	const int columns = static_cast< int >( mParams[ PT_COLUMNS ] + 0.5f );
	const int rows    = static_cast< int >( mParams[ PT_ROWS ] + 0.5f );

	LayoutOptions lo;
	lo.mode         = mode;
	lo.projection   = static_cast< Projection >( OptionIndex( PT_PROJECTION, 2 ) );
	lo.fit          = static_cast< Fit >( OptionIndex( PT_FIT, 4 ) );
	lo.walk         = static_cast< Walk >( OptionIndex( PT_WALK, 5 ) );
	lo.edge         = static_cast< Edge >( OptionIndex( PT_EDGE, 4 ) );
	lo.edgeScope    = static_cast< EdgeScope >( OptionIndex( PT_EDGE_SCOPE, 4 ) );
	lo.cellPad      = CellPad( mParams[ PT_CELL_PAD ] );
	lo.speed        = Speed( mParams[ PT_SPEED ] );
	lo.scrollX      = Scroll( mParams[ PT_SCROLL_X ] );
	lo.scrollY      = Scroll( mParams[ PT_SCROLL_Y ] );
	lo.gridRot      = GridRot( mParams[ PT_GRID_ROT ] );
	lo.walkRate     = WalkRate( mParams[ PT_WALK_RATE ] );
	lo.twinkle      = mParams[ PT_TWINKLE ];
	lo.fade         = mParams[ PT_FADE ];
	lo.fov          = Fov( mParams[ PT_FOV ] );
	lo.camYaw       = CamAngle( mParams[ PT_CAM_YAW ] );
	lo.camPitch     = CamAngle( mParams[ PT_CAM_PITCH ] ) * 0.5f;
	lo.seed         = static_cast< uint32_t >( std::max( 0.0f, mParams[ PT_SEED ] ) );
	lo.outputAspect = static_cast< float >( width ) / static_cast< float >( height );

	// Twinkle and Fade are modes *and* continuous controls, so picking the mode
	// turns its control on rather than leaving the operator to find a slider
	// that looks like it does nothing.
	if( mode == Mode::Twinkle && lo.twinkle <= 0.0f )
		lo.twinkle = 1.0f;
	if( mode == Mode::FadeCycle && lo.fade <= 0.0f )
		lo.fade = 1.0f;

	const LayoutResult layout =
		BuildLayout( mSchedule, mAtlas.Logos(), columns, rows, static_cast< float >( mClock.Seconds() ), lo );

	DrawOptions dopts;
	dopts.edge          = lo.edge;
	dopts.edgeScope     = lo.edgeScope;
	dopts.edgeAmount    = mParams[ PT_EDGE_AMOUNT ];
	dopts.edgeThreshold = EdgeThreshold( mParams[ PT_EDGE_THRESHOLD ] );
	dopts.gridLines     = mParams[ PT_GRID_LINES ] > 0.5f;
	dopts.gridWidth     = GridWidth( mParams[ PT_GRID_WIDTH ] );
	dopts.gridR         = mParams[ PT_GRID_R ];
	dopts.gridG         = mParams[ PT_GRID_G ];
	dopts.gridB         = mParams[ PT_GRID_B ];

	mRenderer.Draw( layout, mAtlas, width, height, pGL->HostFBO, dopts );
	return FF_SUCCESS;
}

} // namespace gridiron
