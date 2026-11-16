#pragma once

#include "Controls.h"

#include <cstdint>
#include <vector>

/**
    The bath on the CPU: the operations ([Lu]'s tools) and the marbler who
    performs them, trigger by trigger. No GL here, so the harness's --model
    exercises all of it without a context.

    Plane units: frame heights, origin at the frame centre, **y up** (GL's
    orientation; the shader works in centred pixels the same way up). [Lu]'s
    "positive theta is clockwise" is honoured on screen: a Circle op with
    positive alpha turns the ink clockwise as the viewer sees it.

    An op's geometry is fixed when it is made; only its strength ramps
    (SPEC-marbling.md, "Animation"): a comb's alpha and a circle's alpha
    linearly in time, a drop's r squared linearly in time. Each of those
    composes additively with itself, so a stroke in progress is exactly the
    same stroke, smaller.
*/
namespace marbling
{
/// The most operations traced exactly per pixel; the shader's arrays.
constexpr int kMaxOps = 64;
/// When more than this many are waiting, the oldest are baked...
constexpr int kBakeAbove = 48;
/// ...all but this many newest.
constexpr int kBakeKeep = 16;
/// The most new operations one frame may add (a frame's bake never touches
/// an op whose palette colour has not been written yet).
constexpr int kMaxNewPerFrame = kBakeKeep;
/// The drop stage ends at this many triggers if the area has not done it.
constexpr int kMaxDropTriggers = 12;
/// Suminagashi's drop stage: this many window areas, or this many triggers.
constexpr double kSuminagashiCover = 2.0;
constexpr int kSuminagashiTriggers = 48;
/// Suminagashi: every this-many-th trigger is a stir.
constexpr int kStirEvery = 8;
/// lambda for every comb, as a fraction of its own tine spacing (a choice).
constexpr double kLambdaPerSpacing = 1.0 / 6.0;
/// A drop grows over this fraction of a stroke's duration.
constexpr double kDropTimeFraction = 0.5;
/// Two band onsets closer than this on the audio clock are one trigger.
constexpr double kOnsetMerge = 0.05;

enum class OpType : int
{
	Drop   = 1,
	Comb   = 2,
	Circle = 3
};

struct Op
{
	OpType type = OpType::Drop;
	double cx = 0.0, cy = 0.0;      ///< Drop, Circle: the centre
	double r = 0.0;                 ///< Drop: radius. Circle: the tine's circle (0 = vortex)
	double mx = 1.0, my = 0.0;      ///< Comb: the tines' direction M (unit)
	double offset  = 0.0;           ///< Comb: a tine's distance along N = (-my, mx)
	double spacing = 0.0;           ///< Comb: tine spacing (0 = one tine line)
	double alpha = 0.0, lambda = 0.0;///< Comb, Circle: [Lu]'s maximum shift and sharpness
	double waveAmp = 0.0;           ///< Comb: the wavy path's amplitude (0 = straight)
	double waveK = 0.0, wavePhase = 0.0;///< Comb: its angular wavenumber (rad per unit along M) and phase
	bool clear = false;             ///< Drop: clear ink (suminagashi's surfactant)
	int slot   = 0;                 ///< Drop: the palette texel that holds its colour
	bool colourPending = false;     ///< Drop: the palette has not been written yet
	double start = 0.0, duration = 0.0;///< seconds on the plugin's clock

	/// 0..1: how far through its motion at `now`.
	double Progress( double now ) const;
	/// The op as it stands at progress p (Drop r*sqrt(p); Comb, Circle alpha*p).
	Op At( double p ) const;
};

/// What a trigger is made of, read from the controls when it fires.
struct Settings
{
	Pattern pattern       = Pattern::Nonpareil;
	double dropRadius     = 0.08;
	int dropRate          = 3;
	double spacing        = 0.03;
	double depth          = 0.15;
	double strokeSeconds  = 0.6;
	bool autoPaper        = true;
	double aspect         = 16.0 / 9.0;///< window width over height
};

/// One stroke of a recipe, in units of the fine spacing s.
struct Stroke
{
	enum Kind
	{
		CombStroke,
		Curls
	} kind = CombStroke;
	double dirX = 1.0, dirY = 0.0;///< M
	double spacingS = 4.0;        ///< tine spacing in s
	double offsetS  = 0.0;        ///< tine offset along N in s
	double waveS    = 0.0;        ///< wavy amplitude in s (0 straight); sign = wave's phase
};

/// The strokes after the drops, per pattern (SPEC-marbling.md, Patterns).
const std::vector< Stroke >& Recipe( Pattern pattern );

class Bath
{
public:
	enum class Stage
	{
		Drops,
		Strokes,
		Done
	};

	/// One trigger at `time`: the next step of the recipe. Returns how many
	/// ops it added (0 for a paper laid by Auto Paper, and for a trigger
	/// Auto Paper ignores because the last stroke is still moving).
	int Trigger( double time, const Settings& s );
	/// Lay the sheet: every op and the baked map go; the next trigger starts
	/// the drops again.
	void Paper();

	const std::vector< Op >& Ops() const
	{
		return ops;
	}
	std::vector< Op >& MutableOps()
	{
		return ops;
	}
	/// The harness: an op as given, a drop given the next palette slot (so
	/// it can never share one with a drop still waiting).
	void AddForTest( Op op );
	/// How many of the oldest ops to bake now (0: none): when more than
	/// kBakeAbove wait, the finished ones among the oldest, keeping the newest
	/// kBakeKeep; and never fewer than leaves kBakeAbove waiting, so the next
	/// frame's kMaxNewPerFrame always fit in kMaxOps (only then is an op still
	/// moving baked at full strength: more than 48 moving at once).
	int BakeCount( double now ) const;
	/// Those ops are in the map now: drop them from the list.
	void Baked( int count );

	bool MapIsIdentity() const
	{
		return mapIdentity;
	}
	void SetMapBaked()
	{
		mapIdentity = false;
	}
	Stage CurrentStage() const
	{
		return stage;
	}
	int StrokeStep() const
	{
		return step;
	}
	int Triggers() const
	{
		return triggers;
	}
	int Papers() const
	{
		return papers;
	}
	uint32_t DropsMade() const
	{
		return dropIndex;
	}

	/// The hashed position of drop `index` in a window of this aspect (plane
	/// units). Public so the harness can predict where the drops land.
	static void DropPosition( uint32_t index, uint32_t sheet, double aspect, double& x, double& y );

private:
	void addDrop( double time, const Settings& s, double x, double y, bool clear );
	void addStroke( double time, const Settings& s, const Stroke& stroke );

	std::vector< Op > ops;
	Stage stage        = Stage::Drops;
	int step           = 0;
	int dropTriggers   = 0;
	double droppedArea = 0.0;
	int triggers       = 0;///< since the last paper
	int papers         = 0;
	uint32_t dropIndex = 0;///< every drop since the plugin started: the hash's input
	int nextSlot       = 0;
	bool mapIdentity   = true;
};

/// PCG (O'Neill's output function on an LCG step): the fleet's hash.
uint32_t Pcg( uint32_t v );
double Hash01( uint32_t a, uint32_t b );

} // namespace marbling
