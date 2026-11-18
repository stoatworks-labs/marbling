/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (woofer's glue, which is
    mirrorball's and fountain's, with marbling's names.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Marbling.cpp` and everything it calls -- the op list and
    the patterns' recipes (Bath.cpp), the controls and their conversions, the
    audio input and earshot under it, the pass buffers and the GLSL --
    compiled UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `MarblingPlugin`, as EffectPlugin.cpp's registration
        would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, SetBeatInfo
        (the host's bar, as `matest --beat` sends it) and ProcessOpenGL with
        one input texture and the page's framebuffer as the host's, as
        `tools/matest`'s Rig does.

    One thing here is the plugin's HARNESS rather than a host: the audio.
    matest never opens a device; it hands the plugin its music through
    earshot's feed() (`AudioInput::Feed`, which is `Engine::feedBegin` and
    `Engine::feed`), the path a device's samples take inside earshot. So does
    this page. `musicAt` and the lines that feed it are matest's own, copied
    from tools/matest/main.cpp character for character (and held to it by
    demo/tools/check_shaders.py). A file the visitor chooses is decoded by the
    page and reaches feed() through `ma_feed`.

    No audio device exists in this module: earshot is built with
    EARSHOT_CAPTURE=OFF (NoCapture.cpp; miniaudio is not compiled), and
    `ma_prepare` sets MARBLING_NO_DEVICES=1, verify.sh's switch, so the
    plugin's AudioInput never asks for a list and never selects a device.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Bath.h"
#include "Controls.h"
#include "Diag.h"
#include "Marbling.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace marbling;

namespace
{
//---------------------------------------------------------------------------
// tools/matest/main.cpp's music, verbatim (check_shaders.py compares).
//---------------------------------------------------------------------------
constexpr int kRate = 48000;

double musicAt( double t )
{
	if( t < 0.0 )
		return 0.0;
	const double beat = std::fmod( t, 0.5 );
	double s          = 0.0;
	s += 0.6 * std::exp( -beat / 0.06 ) * std::sin( 2.0 * kPi * ( 55.0 + 60.0 * std::exp( -beat / 0.02 ) ) * beat );
	const double off = std::fmod( t + 0.25, 0.5 );
	//A hat: a burst of deterministic noise, 30 ms.
	const uint32_t n = static_cast< uint32_t >( t * kRate );
	s += 0.25 * std::exp( -off / 0.01 ) * ( Hash01( n, 7u ) * 2.0 - 1.0 );
	return s;
}

//---------------------------------------------------------------------------
// One plugin instance and the host-side structs it is handed.
//---------------------------------------------------------------------------
struct Instance
{
	MarblingPlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	/// matest's Rig::fedUntil: the music has gone to feed() up to here.
	double fedUntil = 0.0;

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------

/// Before the first instance: give the plugin's log (Diag.cpp) somewhere to
/// go, a directory in the page's in-memory file system named through the
/// override Diag.cpp reads (its own `mkdir -p` goes through system(), which a
/// browser does not have); and MARBLING_NO_DEVICES=1, verify.sh's switch, so
/// AudioInput never lists, looks up or opens a device (a build with no
/// capture backend would find none anyway).
EMSCRIPTEN_KEEPALIVE void ma_prepare()
{
	setenv( "MARBLING_LOG_DIR", "/marbling/logs", 1 );
	setenv( "MARBLING_NO_DEVICES", "1", 1 );
	mkdir( "/marbling", 0755 );
	mkdir( "/marbling/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE Instance* ma_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void ma_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int ma_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ma_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ma_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ma_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ma_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float ma_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ma_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ma_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ma_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int ma_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int ma_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ma_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ma_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// The host's beat: SetBeatInfo( BPM, the bar's phase 0..1 ), as matest
/// --beat sends it before every frame. The page works the phase out.
EMSCRIPTEN_KEEPALIVE void ma_set_beat( Instance* instance, double bpm, double barPhase )
{
	instance->plugin.SetBeatInfo( static_cast< float >( bpm ), static_cast< float >( barPhase ) );
}

/// One frame: SetTime, then ProcessOpenGL with one input of clipWidth x
/// clipHeight whose GL name is `clip` (a texture the page registered with
/// emscripten's GL tables), into the host framebuffer `hostFbo` (likewise
/// registered; the viewport is the page's, set before the call, as a host
/// sets it). 1 on success.
EMSCRIPTEN_KEEPALIVE int ma_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight, int hostFbo )
{
	instance->plugin.SetTime( seconds );
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = static_cast< GLuint >( hostFbo );
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The audio, as matest hands it over.
//---------------------------------------------------------------------------

/// matest's music up to the page's time `t`, both channels, into feed():
/// Rig::Render's own lines, with the instance's plugin and its fedUntil.
/// Returns the samples fed.
EMSCRIPTEN_KEEPALIVE int ma_feed_music( Instance* instance, double t )
{
	MarblingPlugin& plugin = instance->plugin;
	double& fedUntil       = instance->fedUntil;
	const auto audio       = musicAt;
	int fed                = 0;
	//Every sample from the last frame's time to this one, once.
	const int from = static_cast< int >( std::llround( fedUntil * kRate ) );
	const int to   = static_cast< int >( std::llround( t * kRate ) );
	if( to > from )
	{
		std::vector< float > pcm( static_cast< size_t >( to - from ) * 2 );
		for( int k = from; k < to; ++k )
			pcm[ static_cast< size_t >( k - from ) * 2 ] = pcm[ static_cast< size_t >( k - from ) * 2 + 1 ] =
				static_cast< float >( audio( static_cast< double >( k ) / kRate ) );
		plugin.AudioForTest().Feed( pcm.data(), to - from, kRate, 2 );
		fed = to - from;
	}
	fedUntil = t;
	return fed;
}

/// A file the visitor chose, decoded by the page: `frames` interleaved frames
/// of `channels` at `rate`, into feed(), the path matest's music takes.
EMSCRIPTEN_KEEPALIVE void ma_feed( Instance* instance, const float* interleaved, int frames, int rate, int channels )
{
	if( frames > 0 )
		instance->plugin.AudioForTest().Feed( interleaved, frames, rate, channels );
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Marbling.cpp's CurrentSettings applies to it. NaN where the plugin uses
// the host's value as it is (Mix, Auto Paper) or an index.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ma_convert( Instance* instance, int index, float v )
{
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_DROP_SIZE: return DropRadiusFromParam( v );
	case PT_DROP_RATE: return DropRateFromParam( v );
	case PT_SPACING: return SpacingFromParam( v );
	case PT_DEPTH: return DepthFromParam( v );
	case PT_SPEED: return StrokeSecondsFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion. NaN for a parameter with
/// no conversion.
EMSCRIPTEN_KEEPALIVE double ma_param_from( Instance* instance, int index, double value )
{
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_DROP_SIZE: return ParamFromDropRadius( value );
	case PT_DROP_RATE: return ParamFromDropRate( static_cast< int >( std::lround( value ) ) );
	case PT_SPACING: return ParamFromSpacing( value );
	case PT_DEPTH: return ParamFromDepth( value );
	case PT_SPEED: return ParamFromStrokeSeconds( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (LastFrame(), TriggerLog(), BathForTest(),
// AudioForTest()).
//---------------------------------------------------------------------------

/// The plugin's clock, seconds: the sum of the host's steps, each at most 0.1 s.
EMSCRIPTEN_KEEPALIVE double ma_clock( Instance* instance )
{
	return static_cast< double >( instance->plugin.LastFrame().clockUs ) * 1e-6;
}
/// Ops traced per pixel on the last frame (at most 64).
EMSCRIPTEN_KEEPALIVE int ma_op_count( Instance* instance )
{
	return instance->plugin.LastFrame().opCount;
}
EMSCRIPTEN_KEEPALIVE int ma_bakes( Instance* instance )
{
	return instance->plugin.LastFrame().bakesTotal;
}
EMSCRIPTEN_KEEPALIVE int ma_resizes( Instance* instance )
{
	return instance->plugin.LastFrame().resizes;
}
EMSCRIPTEN_KEEPALIVE int ma_deferred( Instance* instance )
{
	return instance->plugin.LastFrame().deferred;
}
EMSCRIPTEN_KEEPALIVE int ma_map_baked( Instance* instance )
{
	return instance->plugin.LastFrame().mapBaked ? 1 : 0;
}
/// Triggers that did something since the plugin was made (drops, strokes),
/// sheets laid (by Paper or by Auto Paper's trigger), and the time of the
/// latest of either on the plugin's clock (-1: none yet).
EMSCRIPTEN_KEEPALIVE int ma_triggers( Instance* instance )
{
	int n = 0;
	for( const TriggerRecord& t : instance->plugin.TriggerLog() )
		n += !t.paper && t.opsAdded > 0 ? 1 : 0;
	return n;
}
EMSCRIPTEN_KEEPALIVE int ma_sheets( Instance* instance )
{
	int n = 0;
	for( const TriggerRecord& t : instance->plugin.TriggerLog() )
		n += t.paper ? 1 : 0;
	return n;
}
EMSCRIPTEN_KEEPALIVE double ma_last_trigger( Instance* instance )
{
	const auto& log = instance->plugin.TriggerLog();
	return log.empty() ? -1.0 : static_cast< double >( log.back().us ) * 1e-6;
}
/// The bath: its stage (0 drops, 1 strokes, 2 done), the next stroke of the
/// recipe, triggers since the last sheet and every drop since the plugin
/// started (the hash's input).
EMSCRIPTEN_KEEPALIVE int ma_stage( Instance* instance )
{
	return static_cast< int >( instance->plugin.BathForTest().CurrentStage() );
}
EMSCRIPTEN_KEEPALIVE int ma_stroke_step( Instance* instance )
{
	return instance->plugin.BathForTest().StrokeStep();
}
EMSCRIPTEN_KEEPALIVE int ma_recipe_length( Instance* instance, int pattern )
{
	return static_cast< int >( Recipe( static_cast< Pattern >( pattern ) ).size() );
}
EMSCRIPTEN_KEEPALIVE int ma_bath_triggers( Instance* instance )
{
	return instance->plugin.BathForTest().Triggers();
}
EMSCRIPTEN_KEEPALIVE double ma_drops_made( Instance* instance )
{
	return static_cast< double >( instance->plugin.BathForTest().DropsMade() );
}
/// 1 when every op in the list has finished moving at the plugin's clock.
EMSCRIPTEN_KEEPALIVE int ma_still( Instance* instance )
{
	const double now = static_cast< double >( instance->plugin.LastFrame().clockUs ) * 1e-6;
	for( const Op& o : instance->plugin.BathForTest().Ops() )
		if( o.Progress( now ) < 1.0 )
			return 0;
	return 1;
}
/// earshot's capture state and the source of its last frame (None, Device --
/// which feed() is -- or Host, Resolume's FFT), and the audio it has analysed.
EMSCRIPTEN_KEEPALIVE const char* ma_capture_state_name( Instance* instance )
{
	return earshot::toString( instance->plugin.AudioForTest().Frame().capture );
}
EMSCRIPTEN_KEEPALIVE int ma_audio_source( Instance* instance )
{
	return static_cast< int >( instance->plugin.AudioForTest().Frame().source );
}
EMSCRIPTEN_KEEPALIVE double ma_audio_time( Instance* instance )
{
	return instance->plugin.AudioForTest().Frame().audioTime;
}
/// Samples of matest's music fed so far, at 48 kHz.
EMSCRIPTEN_KEEPALIVE double ma_fed( Instance* instance )
{
	return std::llround( instance->fedUntil * kRate );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ma_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
