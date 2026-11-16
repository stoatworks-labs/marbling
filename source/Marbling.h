#pragma once

#include "Audio.h"
#include "Bath.h"
#include "Controls.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <cstdint>
#include <vector>

namespace marbling
{

/// CPU-side negative controls (the harness's hooks; a shipped instance sets 0).
enum CpuHook : int
{
	kCpuEventsAtFrame = 1 << 0,///< beats and onsets placed at the frame, not inside it
	kCpuFloatClock    = 1 << 1,///< the host clock read through a float
	kCpuResizeClears  = 1 << 2,///< a resize loses the baked map instead of resampling it
	kCpuIgnorePaper   = 1 << 3,///< Paper does nothing
	kCpuIgnoreGate    = 1 << 4,///< the device menu listed and Audio Input applied whatever Trigger On says
	kCpuNoBake        = 1 << 5,///< nothing is ever baked: ops pile up past what the shader traces
	kCpuLeakState     = 1 << 6 ///< the display pass leaves its program and textures bound
};

struct TriggerRecord
{
	int64_t us;   ///< on the plugin's clock
	bool paper;   ///< a sheet laid (by Paper or by Auto Paper's trigger)
	int opsAdded; ///< ops the trigger made
};

/// What the last frame did: the harness reads it.
struct FrameData
{
	int64_t clockUs = 0;    ///< the plugin's clock (sum of clamped host steps)
	int opCount     = 0;    ///< ops traced per pixel this frame
	int bakesTotal  = 0;    ///< bake passes since the plugin was made
	int resizes     = 0;    ///< map resamples since the plugin was made
	int deferred    = 0;    ///< triggers waiting for the next frame
	bool mapBaked   = false;///< the display read a baked map
};

/**
    The plugin, `SW Marbling` (EffectPlugin.cpp registers it).

    Host indices are ParamIds (one plugin, HostOrder() is the table).
*/
class MarblingPlugin : public CFFGLPlugin
{
public:
	MarblingPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? static_cast< int >( id ) : -1;
	}
	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int shaderBits, int cpuBits )
	{
		hooks    = shaderBits;
		cpuHooks = cpuBits;
	}
	/// Bake after every trigger (the --resample check's worst case).
	void SetBakeEveryTriggerForTest( bool on )
	{
		bakeEveryTrigger = on;
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	const std::vector< TriggerRecord >& TriggerLog() const
	{
		return triggerLog;
	}
	Bath& BathForTest()
	{
		return bath;
	}
	AudioInput& AudioForTest()
	{
		return audio;
	}
	Settings CurrentSettings( double aspect ) const;

private:
	void UpdateClock();
	void gatherEvents( int64_t frameStartUs, int64_t frameUs, double aspect );
	void handleOnsets( int64_t frameStartUs, int64_t frameUs );
	void refreshMenu();
	void uploadOps( const ffglex::FFGLShader& shader, int count, int forceCompleteBelow, int height );
	void commonUniforms( const ffglex::FFGLShader& shader, int x, int y, int w, int h, int outW, int outH );
	bool writePalette( int width, int height );
	bool bake( int count, int width, int height );
	bool resizeMap( int width, int height );
	void display( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const std::vector< unsigned int >& hostOrder;
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader bakeShader, resizeShader, paletteShader, displayShader;
	ffglex::FFGLScreenQuad quad;
	PassBuffer palette;
	PassBuffer maps[ 2 ];
	int current = 0;      ///< which of maps[] holds the baked map
	int mapW = 0, mapH = 0;///< its raster

	Bath bath;
	AudioInput audio;
	FrameData frame;
	std::vector< TriggerRecord > triggerLog;
	std::vector< int64_t > deferred;///< trigger times that waited for a frame with room
	int triggerPresses = 0, paperPresses = 0;
	bool triggerHeld = false, paperHeld = false;
	bool bakeEveryTrigger = false;
	int64_t lastOnsetUs   = INT64_MIN / 2;
	uint32_t lastOnsetCount[ earshot::kMaxBands ] = {};
	bool onsetsPrimed = false;

	//The beat (ferrofluid's): the host's bar phase, or a clock of our own.
	float bpm            = 120.0f;
	float barPhase       = 0.0f;
	float lastBarPhase   = -1.0f;
	bool hostBeatSeen    = false;
	double lastPhase     = -1.0;

	//The clock. Integer microseconds: Resolume's has been seen at ~499
	//million ms, where a float resolves 0.03 s and a double's sum of frame
	//steps would still drift; differences of rounded microseconds do not.
	double hostTime     = -1.0;
	int64_t nowUs       = 0;
	int64_t lastNowUs   = INT64_MIN;
	int64_t clockUs     = 0;///< the plugin's own: sum of clamped steps
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	int hooks    = 0;
	int cpuHooks = 0;
};

} // namespace marbling
