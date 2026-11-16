#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace marbling
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}

//The ranges (SPEC-marbling.md, Controls). No source gives a drop's size or a
//comb's depth as a fraction of the tray, so the ranges are wide; [BAB]'s comb
//widths (1/4" to 1") on an 18" tray are 0.014 to 0.056 tray heights, inside
//Tine Spacing's range.
constexpr double kDropLow = 0.01, kDropHigh = 0.25;
constexpr int kDropRateMax = 8;
constexpr double kSpacingLow = 0.0125, kSpacingHigh = 0.125;
constexpr double kDepthLow = 0.02, kDepthHigh = 0.6;
constexpr double kStrokeSlow = 2.0, kStrokeFast = 0.1;

const char* const kPatternNames[] = { "Stone", "Suminagashi", "Gelgit", "Chevron", "Nonpareil", "Bouquet", "French Curl" };
const char* const kTriggerNames[] = { "Onsets", "Beat", "Manual" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: set on Resolume's demo clips through --pipe (AGENTS.md,
// "Defaults").
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_PATTERN, "Pattern", "Marbling", static_cast< float >( Pattern::Nonpareil ), kPatternNames, static_cast< int >( Pattern::Count ) ),
	option( PT_TRIGGER_ON, "Trigger On", "Trigger", static_cast< float >( TriggerOn::Onsets ), kTriggerNames, static_cast< int >( TriggerOn::Count ) ),
	{ PT_INPUT, "Audio Input", "Trigger", Kind::Devices, 0.0f, nullptr, 0 },
	event( PT_TRIGGER, "Trigger", "Trigger" ),
	event( PT_PAPER, "Paper", "Marbling" ),
	{ PT_AUTO_PAPER, "Auto Paper", "Marbling", Kind::Bool, 1.0f, nullptr, 0 },

	standard( PT_DROP_SIZE, "Drop Size", "Drops", ParamFromDropRadius( 0.08 ) ),
	standard( PT_DROP_RATE, "Drop Rate", "Drops", ParamFromDropRate( 3 ) ),
	standard( PT_SPACING, "Tine Spacing", "Comb", ParamFromSpacing( 0.03 ) ),
	standard( PT_DEPTH, "Depth", "Comb", ParamFromDepth( 0.15 ) ),
	standard( PT_SPEED, "Speed", "Comb", ParamFromStrokeSeconds( 0.6 ) ),
	standard( PT_MIX, "Mix", "Marbling", 1.0f ),
	{ PT_AUDIO, "Audio", "Trigger", Kind::Buffer, 0.0f, nullptr, 0 },
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder()
{
	static const std::vector< unsigned int > order = [] {
		std::vector< unsigned int > o;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
			o.push_back( id );
		return o;
	}();
	return order;
}

int OptionIndex( float value, int count )
{
	if( !( value == value ) )
		return 0;
	const long i = std::lround( value );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double DropRadiusFromParam( float value )
{
	return geometric( value, kDropLow, kDropHigh );
}
float ParamFromDropRadius( double radius )
{
	return inverseGeometric( radius, kDropLow, kDropHigh );
}

int DropRateFromParam( float value )
{
	return 1 + static_cast< int >( std::lround( clamp01( value ) * ( kDropRateMax - 1 ) ) );
}
float ParamFromDropRate( int drops )
{
	return static_cast< float >( std::clamp( drops - 1, 0, kDropRateMax - 1 ) ) / static_cast< float >( kDropRateMax - 1 );
}

double SpacingFromParam( float value )
{
	return geometric( value, kSpacingLow, kSpacingHigh );
}
float ParamFromSpacing( double spacing )
{
	return inverseGeometric( spacing, kSpacingLow, kSpacingHigh );
}

double DepthFromParam( float value )
{
	return geometric( value, kDepthLow, kDepthHigh );
}
float ParamFromDepth( double depth )
{
	return inverseGeometric( depth, kDepthLow, kDepthHigh );
}

double StrokeSecondsFromParam( float value )
{
	return geometric( value, kStrokeSlow, kStrokeFast );
}
float ParamFromStrokeSeconds( double seconds )
{
	return inverseGeometric( seconds, kStrokeSlow, kStrokeFast );
}

} // namespace marbling
