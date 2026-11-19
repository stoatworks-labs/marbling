#include "Marbling.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <string>

using namespace ffglex;

namespace marbling
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// The most one frame may advance the plugin's clock (a stall, a seek).
constexpr int64_t kMaxStepUs = 100000;
/// Resolume's FFT buffer.
constexpr int kBins = 64;
/// The most beat crossings one frame places.
constexpr int kMaxBeatsPerFrame = 8;
/// The most new ops one trigger can make (French Curl's 12 vortices).
constexpr int kMaxOpsPerTrigger = 12;
/// Under Onsets, a log line of the onsets by band every this many seconds of
/// the plugin's clock (none when nothing fired).
constexpr int64_t kOnsetLogUs = 30000000;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Every pass writes every texel of its target; on Apple's GL (Metal
/// underneath) a clear makes that the pass's load action, which costs nothing.
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
MarblingPlugin::MarblingPlugin() :
	hostOrder( HostOrder() )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Bool: SetParamInfo( id, info.name, FF_TYPE_BOOLEAN, params[ id ] >= 0.5f ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Devices:
		{
			//The device menu (spasis's shape, earshot's keys), unlisted: the
			//devices are asked for on the first frame under Onsets, never here.
			SetOptionParamInfo( id, info.name, kInputSlots, 0.0f );
			const auto& slots = audio.Slots();
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			SetParamRange( id, 0.0f, 8388608.0f );
			break;
		}
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			SetBufferParamInfo( id, info.name, kBins, FF_USAGE_FFT );
			for( int i = 0; i < kBins; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint MarblingPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void MarblingPlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void MarblingPlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}

//---------------------------------------------------------------------------
FFResult MarblingPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &bakeShader, shaders::Pass::Bake, "bake" },
		{ &resizeShader, shaders::Pass::Resize, "resize" },
		{ &paletteShader, shaders::Pass::Palette, "palette" },
		{ &displayShader, shaders::Pass::Display, "display" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Marbling: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void MarblingPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of haze).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no step across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	double seconds = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( cpuHooks & kCpuFloatClock )
		seconds = static_cast< double >( static_cast< float >( seconds ) );
	nowUs = std::llround( seconds * 1e6 );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( seconds ) );
}

void MarblingPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

void MarblingPlugin::SetSampleRate( unsigned int rate )
{
	CFFGLPlugin::SetSampleRate( rate );
	hostRate.store( rate, std::memory_order_relaxed );
}

Settings MarblingPlugin::CurrentSettings( double aspect ) const
{
	Settings s;
	s.pattern       = static_cast< Pattern >( OptionIndex( params[ PT_PATTERN ], static_cast< int >( Pattern::Count ) ) );
	s.dropRadius    = DropRadiusFromParam( params[ PT_DROP_SIZE ] );
	s.dropRate      = DropRateFromParam( params[ PT_DROP_RATE ] );
	s.spacing       = SpacingFromParam( params[ PT_SPACING ] );
	s.depth         = DepthFromParam( params[ PT_DEPTH ] );
	s.strokeSeconds = StrokeSecondsFromParam( params[ PT_SPEED ] );
	s.autoPaper     = params[ PT_AUTO_PAPER ] >= 0.5f;
	s.aspect        = aspect;
	return s;
}

void MarblingPlugin::refreshMenu()
{
	const auto& slots = audio.Slots();
	for( int i = 0; i < kInputSlots; ++i )
		SetParamElementInfo( PT_INPUT, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
		                     slots[ static_cast< size_t >( i ) ].value );
	RaiseParamEvent( PT_INPUT, FF_EVENT_FLAG_ELEMENTS );
}

//---------------------------------------------------------------------------
// Onsets: earshot's band onsets, merged across bands within 50 ms on the
// audio clock, each placed inside the frame (SPEC-marbling.md, Triggers).
//---------------------------------------------------------------------------
void MarblingPlugin::handleOnsets( int64_t frameStartUs, int64_t frameUs )
{
	if( audio.EnsureListed() )
		refreshMenu();
	audio.Want( params[ PT_INPUT ] );
	audio.Apply();
	//Resolume's 64 bins are linear to half the host's audio engine rate, 375 Hz
	//each at 48 kHz, and earshot shares each between the bands it overlaps
	//(earshot 0.1.1). Until the host sends a rate, earshot's 48 kHz.
	if( !( cpuHooks & kCpuIgnoreHostRate ) )
		audio.SetHostRate( hostRate.load( std::memory_order_relaxed ) );
	audio.SetHostCentreOnlyForTest( ( cpuHooks & kCpuHostCentreOnly ) != 0 );
	float bins[ kBins ] = {};
	int binCount        = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const earshot::Frame& f = audio.Update( static_cast< double >( frameUs ) * 1e-6, bins, binCount );
	std::vector< int64_t > times;
	for( int b = 0; b < f.bandCount && b < earshot::kMaxBands; ++b )
	{
		const earshot::Band& band = f.bands[ b ];
		//The first frame primes: counts already standing are not onsets.
		if( onsetsPrimed && band.onsetCount != lastOnsetCount[ b ] )
		{
			onsetTally[ b ] += band.onsetCount - lastOnsetCount[ b ];
			int64_t at = frameUs;
			if( f.source != earshot::Source::Host && !( cpuHooks & kCpuEventsAtFrame ) && band.lastOnsetTime >= 0.0 )
				at = frameUs - std::llround( ( f.audioTime - band.lastOnsetTime ) * 1e6 );
			times.push_back( std::clamp( at, frameStartUs, frameUs ) );
		}
		lastOnsetCount[ b ] = band.onsetCount;
	}
	onsetsPrimed = true;
	std::sort( times.begin(), times.end() );
	for( int64_t t : times )
		if( t - lastOnsetUs >= std::llround( kOnsetMerge * 1e6 ) || ( cpuHooks & kCpuNoMerge ) )
		{
			deferred.push_back( t );
			lastOnsetUs = t;
			++triggerTally;
		}

	//What Onsets heard, for the log: which bands fired and how many triggers
	//they made, so a user can see the plugin hears the music.
	if( tallyFromUs == INT64_MIN )
		tallyFromUs = frameUs;
	else if( frameUs - tallyFromUs >= kOnsetLogUs )
	{
		if( triggerTally > 0 )
		{
			std::string line = "onsets in " + std::to_string( ( frameUs - tallyFromUs ) / 1000000 ) + " s ("
			                   + ( f.source == earshot::Source::Host ? std::string( "Resolume FFT" ) : std::string( "device" ) ) + "):";
			for( int b = 0; b < f.bandCount && b < earshot::kMaxBands; ++b )
				line += ( b == 0 ? " " : ", " ) + std::to_string( std::lround( f.bands[ b ].loHz ) ) + "-"
				        + std::to_string( std::lround( f.bands[ b ].hiHz ) ) + " Hz " + std::to_string( onsetTally[ b ] );
			diag::info( line + "; " + std::to_string( triggerTally ) + " triggers" );
		}
		for( uint32_t& n : onsetTally )
			n = 0;
		triggerTally = 0;
		tallyFromUs  = frameUs;
	}
}

void MarblingPlugin::gatherEvents( int64_t frameStartUs, int64_t frameUs, double aspect )
{
	const TriggerOn source = static_cast< TriggerOn >( OptionIndex( params[ PT_TRIGGER_ON ], static_cast< int >( TriggerOn::Count ) ) );

	//Presses, at the frame. Paper before a trigger pressed in the same frame.
	std::vector< std::pair< int64_t, bool > > events;//time, paper
	for( ; paperPresses > 0; --paperPresses )
		events.emplace_back( frameUs, true );
	for( ; triggerPresses > 0; --triggerPresses )
		deferred.push_back( frameUs );

	//The beat (ferrofluid's Drive): each quarter of the bar, the crossing
	//placed inside the frame by interpolating the phase. A host that never
	//moves barPhase gets a clock of the plugin's own at its BPM.
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;
	const double tempo    = bpm > 1.0f ? static_cast< double >( bpm ) : 120.0;
	const double phaseNow = hostBeatSeen ? std::clamp( static_cast< double >( barPhase ), 0.0, 1.0 )
	                                     : std::fmod( static_cast< double >( frameUs ) * 1e-6 * tempo / 240.0, 1.0 );
	const int64_t dt = frameUs - frameStartUs;
	if( source == TriggerOn::Beat && lastPhase >= 0.0 && dt > 0 )
	{
		double p0 = lastPhase, p1 = phaseNow;
		if( p1 < p0 )
			p1 += 1.0;
		if( p1 > p0 )
		{
			const double step = 0.25;
			double boundary   = ( std::floor( p0 / step + 1e-9 ) + 1.0 ) * step;
			for( int n = 0; boundary <= p1 + 1e-12 && n < kMaxBeatsPerFrame; ++n, boundary += step )
			{
				const double inside = ( cpuHooks & kCpuEventsAtFrame ) ? 1.0 : ( boundary - p0 ) / ( p1 - p0 );
				deferred.push_back( frameStartUs + std::llround( inside * static_cast< double >( dt ) ) );
			}
		}
	}
	lastPhase = phaseNow;

	if( source == TriggerOn::Onsets )
		handleOnsets( frameStartUs, frameUs );
	else if( cpuHooks & kCpuIgnoreGate )
	{
		//The negative control: what a plugin that lists and opens regardless does.
		audio.EnsureListed();
		audio.Want( params[ PT_INPUT ] );
		audio.Apply();
	}
	else
		audio.Release();

	//In time order. A frame adds at most kMaxNewPerFrame ops; a trigger that
	//would pass it waits for the next frame, keeping its time.
	std::sort( deferred.begin(), deferred.end() );
	for( int64_t t : deferred )
		events.emplace_back( t, false );
	deferred.clear();
	std::stable_sort( events.begin(), events.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );

	const Settings settings = CurrentSettings( aspect );
	int added = 0, triggered = 0;
	for( const auto& e : events )
	{
		if( e.second )
		{
			if( cpuHooks & kCpuIgnorePaper )
				continue;
			bath.Paper();
			triggerLog.push_back( { e.first, true, 0 } );
			continue;
		}
		//bakeEveryTrigger (the harness): one trigger a frame, so each is
		//baked before the next.
		if( added + kMaxOpsPerTrigger > kMaxNewPerFrame || ( bakeEveryTrigger && triggered > 0 ) )
		{
			deferred.push_back( e.first );
			continue;
		}
		const int papersBefore = bath.Papers();
		const int n            = bath.Trigger( static_cast< double >( e.first ) * 1e-6, settings );
		added += n;
		++triggered;
		triggerLog.push_back( { e.first, bath.Papers() != papersBefore, n } );
	}
	if( triggerLog.size() > 100000 )
		triggerLog.erase( triggerLog.begin(), triggerLog.begin() + 50000 );
}

//---------------------------------------------------------------------------
void MarblingPlugin::commonUniforms( const FFGLShader& shader, int x, int y, int w, int h, int outW, int outH )
{
	uniform( shader, "Hooks", hooks );
	glUniform4i( where( shader, "Viewport" ), x, y, w, h );
	glUniform2f( where( shader, "Size" ), static_cast< float >( outW ), static_cast< float >( outH ) );
	uniform( shader, "InputTexture", 0 );
	uniform( shader, "Palette", 1 );
	uniform( shader, "Map", 2 );
	glUniform2i( where( shader, "MapSize" ), std::max( mapW, 1 ), std::max( mapH, 1 ) );
	uniform( shader, "MapIdentity", bath.MapIsIdentity() ? 1 : 0 );
}

/// The ops as they stand now, in centred px (y up) of a raster `height` px
/// high; those below `forceCompleteBelow` at full strength (a bake).
void MarblingPlugin::uploadOps( const FFGLShader& shader, int count, int forceCompleteBelow, int height )
{
	const auto& ops = bath.Ops();
	count           = std::min( { count, static_cast< int >( ops.size() ), kMaxOps } );
	std::vector< float > a( static_cast< size_t >( kMaxOps ) * 4, 0.0f ), b( a.size(), 0.0f ), c( a.size(), 0.0f );
	const double H   = static_cast< double >( height );
	const double now = static_cast< double >( frame.clockUs ) * 1e-6;
	for( int k = 0; k < count; ++k )
	{
		const Op& base = ops[ static_cast< size_t >( k ) ];
		const Op o     = base.At( k < forceCompleteBelow ? 1.0 : base.Progress( now ) );
		float* A       = &a[ static_cast< size_t >( k ) * 4 ];
		float* B       = &b[ static_cast< size_t >( k ) * 4 ];
		float* C       = &c[ static_cast< size_t >( k ) * 4 ];
		A[ 0 ]         = static_cast< float >( static_cast< int >( o.type ) );
		switch( o.type )
		{
		case OpType::Drop:
			A[ 1 ] = static_cast< float >( o.cx * H );
			A[ 2 ] = static_cast< float >( o.cy * H );
			A[ 3 ] = static_cast< float >( o.r * H );
			B[ 0 ] = static_cast< float >( o.slot );
			break;
		case OpType::Comb:
			A[ 1 ] = static_cast< float >( o.mx );
			A[ 2 ] = static_cast< float >( o.my );
			A[ 3 ] = static_cast< float >( o.offset * H );
			B[ 0 ] = static_cast< float >( o.spacing * H );
			B[ 1 ] = static_cast< float >( o.alpha * H );
			B[ 2 ] = static_cast< float >( std::max( o.lambda * H, 1e-6 ) );
			C[ 0 ] = static_cast< float >( o.waveAmp * H );
			C[ 1 ] = static_cast< float >( o.waveK / H );
			C[ 2 ] = static_cast< float >( o.wavePhase );
			break;
		case OpType::Circle:
			A[ 1 ] = static_cast< float >( o.cx * H );
			A[ 2 ] = static_cast< float >( o.cy * H );
			A[ 3 ] = static_cast< float >( o.r * H );
			B[ 0 ] = static_cast< float >( o.alpha * H );
			B[ 1 ] = static_cast< float >( std::max( o.lambda * H, 1e-6 ) );
			break;
		}
	}
	uniform( shader, "OpCount", count );
	glUniform4fv( where( shader, "OpA" ), kMaxOps, a.data() );
	glUniform4fv( where( shader, "OpB" ), kMaxOps, b.data() );
	glUniform4fv( where( shader, "OpC" ), kMaxOps, c.data() );
}

/// One texel per new drop: its ink, the clip's mean where it lands.
bool MarblingPlugin::writePalette( const FFGLTextureStruct* input, int width, int height )
{
	auto& ops = bath.MutableOps();
	bool any  = false;
	for( const Op& o : ops )
		any = any || o.colourPending;
	if( !any )
		return true;
	ScopedFBOBinding target( palette.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glUseProgram( paletteShader.GetGLID() );
	//The clip on unit 0; nothing on the others (the palette is the target,
	//and is never sampled here).
	bindUnit( 0, input->Handle );
	const double H = static_cast< double >( height );
	for( Op& o : ops )
	{
		if( !o.colourPending )
			continue;
		glViewport( o.slot, 0, 1, 1 );
		commonUniforms( paletteShader, o.slot, 0, 1, 1, width, height );
		glUniform2i( where( paletteShader, "InputSize" ), static_cast< int >( input->Width ), static_cast< int >( input->Height ) );
		glUniform3f( where( paletteShader, "Drop" ), static_cast< float >( o.cx * H ), static_cast< float >( o.cy * H ), static_cast< float >( o.r * H ) );
		uniform( paletteShader, "DropClear", o.clear ? 1 : 0 );
		quad.Draw();
		o.colourPending = false;
	}
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	return true;
}

bool MarblingPlugin::bake( int count, int width, int height )
{
	const int into = 1 - current;
	if( !maps[ into ].Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
		return false;
	{
		ScopedFBOBinding target( maps[ into ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		discardTarget();
		glUseProgram( bakeShader.GetGLID() );
		bindUnit( 1, palette.TextureID() );
		bindUnit( 2, bath.MapIsIdentity() || !maps[ current ].IsValid() ? palette.TextureID() : maps[ current ].TextureID() );
		commonUniforms( bakeShader, 0, 0, width, height, width, height );
		uploadOps( bakeShader, count, count, height );
		uniform( bakeShader, "BakeLast", count - 1 );
		quad.Draw();
		unbindTextureUnits( 3 );
		glUseProgram( 0 );
	}
	current = into;
	mapW    = width;
	mapH    = height;
	bath.Baked( count );
	++frame.bakesTotal;
	return true;
}

/// The raster changed: the map resampled by plane coordinates, the old one
/// read before it is released (the addendum's photofinish trap).
bool MarblingPlugin::resizeMap( int width, int height )
{
	if( cpuHooks & kCpuResizeClears )
	{
		//The negative control: what a careless reallocation does.
		bath.Paper();
		mapW = width;
		mapH = height;
		return true;
	}
	const int into = 1 - current;
	if( !maps[ into ].Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
		return false;
	{
		ScopedFBOBinding target( maps[ into ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		discardTarget();
		glUseProgram( resizeShader.GetGLID() );
		bindUnit( 1, palette.TextureID() );
		bindUnit( 2, maps[ current ].TextureID() );
		commonUniforms( resizeShader, 0, 0, width, height, width, height );
		uniform( resizeShader, "Rescale", static_cast< float >( static_cast< double >( height ) / static_cast< double >( mapH ) ) );
		quad.Draw();
		unbindTextureUnits( 3 );
		glUseProgram( 0 );
	}
	current = into;
	mapW    = width;
	mapH    = height;
	++frame.resizes;
	return true;
}

void MarblingPlugin::display( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( displayShader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, palette.TextureID() );
	bindUnit( 2, bath.MapIsIdentity() || !maps[ current ].IsValid() ? palette.TextureID() : maps[ current ].TextureID() );
	commonUniforms( displayShader, hostViewport[ 0 ], hostViewport[ 1 ], width, height, width, height );
	glUniform2i( where( displayShader, "InputSize" ), static_cast< int >( input->Width ), static_cast< int >( input->Height ) );
	uploadOps( displayShader, kMaxOps, 0, height );
	uniform( displayShader, "MixAmount", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	quad.Draw();
	if( cpuHooks & kCpuLeakState )
		return;//the negative control: the program and three textures left bound
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
FFResult MarblingPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	const int width  = hostViewport[ 2 ];
	const int height = hostViewport[ 3 ];
	if( width <= 0 || height <= 0 || input->Width == 0 || input->Height == 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// The clock: the plugin's own, the sum of the host's steps (each at most
	// 0.1 s), in integer microseconds.
	//---------------------------------------------------------------------
	UpdateClock();
	int64_t stepUs = 0;
	if( lastNowUs != INT64_MIN && !settledJump )
		stepUs = std::clamp< int64_t >( nowUs - lastNowUs, 0, kMaxStepUs );
	settledJump = false;
	lastNowUs   = nowUs;
	const int64_t frameStartUs = clockUs;
	clockUs += stepUs;
	frame.clockUs = clockUs;

	//---------------------------------------------------------------------
	// Triggers, then the GL: palette, resize, bake, display.
	//---------------------------------------------------------------------
	const double aspect = static_cast< double >( width ) / static_cast< double >( height );
	gatherEvents( frameStartUs, clockUs, aspect );
	frame.deferred = static_cast< int >( deferred.size() );

	if( !palette.Ensure( kMaxOps, 1, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the palette" );
		return FF_FAIL;
	}
	writePalette( input, width, height );
	if( !bath.MapIsIdentity() && ( mapW != width || mapH != height ) && !resizeMap( width, height ) )
	{
		diag::error( "could not resample the map" );
		return FF_FAIL;
	}
	int toBake = ( cpuHooks & kCpuNoBake ) ? 0 : bath.BakeCount( static_cast< double >( clockUs ) * 1e-6 );
	if( bakeEveryTrigger )
	{
		//Everything finished is baked: ops are only ever left waiting while
		//they move.
		const double now = static_cast< double >( clockUs ) * 1e-6;
		toBake           = 0;
		for( const Op& o : bath.Ops() )
		{
			if( o.Progress( now ) < 1.0 )
				break;
			++toBake;
		}
	}
	if( toBake > 0 && !bake( toBake, width, height ) )
	{
		diag::error( "could not allocate the map" );
		return FF_FAIL;
	}
	frame.opCount  = static_cast< int >( bath.Ops().size() );
	frame.mapBaked = !bath.MapIsIdentity();
	display( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult MarblingPlugin::DeInitGL()
{
	uniformCache.clear();
	bakeShader.FreeGLResources();
	resizeShader.FreeGLResources();
	paletteShader.FreeGLResources();
	displayShader.FreeGLResources();
	quad.Release();
	palette.Destroy();
	maps[ 0 ].Destroy();
	maps[ 1 ].Destroy();
	mapW = mapH = 0;
	//The map is gone with the context: the bath starts again.
	bath.Paper();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult MarblingPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* MarblingPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult MarblingPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult MarblingPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Presses count on the rising edge, once.
	if( id == PT_TRIGGER || id == PT_PAPER )
	{
		const bool down = value >= 0.5f;
		bool& held      = id == PT_TRIGGER ? triggerHeld : paperHeld;
		if( down && !held )
			++( id == PT_TRIGGER ? triggerPresses : paperPresses );
		held = down;
	}
	//Audio Input is only remembered here; it opens from the frame, under
	//Onsets (Audio.h).
	params[ id ] = value;
	return FF_SUCCESS;
}

float MarblingPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace marbling
