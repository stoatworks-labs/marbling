#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in plane units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index), except Audio Input, whose elements'
    values are earshot device keys (a device is remembered by name). Trigger
    and Paper are presses; Audio is Resolume's 64-bin FFT buffer, the onset
    detector's input when no device is chosen.

    Plane units: frame heights (SPEC-marbling.md, "The bath").
*/
namespace marbling
{
enum ParamId : unsigned int
{
	PT_PATTERN = 0,
	PT_TRIGGER_ON,
	PT_INPUT,///< the device menu
	PT_TRIGGER,
	PT_PAPER,
	PT_AUTO_PAPER,

	PT_DROP_SIZE,
	PT_DROP_RATE,
	PT_SPACING,
	PT_DEPTH,
	PT_SPEED,
	PT_MIX,
	PT_AUDIO,

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Bool,
	Event,
	Buffer,
	Devices
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId (one plugin, so the table's order).
const std::vector< unsigned int >& HostOrder();

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

/// The device menu: element 0 Resolume's FFT, element 1 the system default,
/// then devices by name, then spares, kInputSlots in all (spasis's shape).
/// Nothing is listed until Trigger On is Onsets (SPEC-marbling.md, Triggers).
constexpr int kInputSlots           = 16;
constexpr const char* kHostFftLabel = "Resolume FFT";
constexpr const char* kDefaultLabel = "System default";
constexpr const char* kEmptySlot    = "(none)";

enum class Pattern
{
	Stone = 0,
	Suminagashi,
	Gelgit,
	Chevron,
	Nonpareil,
	Bouquet,
	FrenchCurl,
	Count
};

enum class TriggerOn
{
	Onsets = 0,
	Beat,
	Manual,
	Count
};

double DropRadiusFromParam( float value );///< frame heights
float ParamFromDropRadius( double radius );

int DropRateFromParam( float value );///< drops per trigger, 1..8
float ParamFromDropRate( int drops );

double SpacingFromParam( float value );///< the fine comb's tine spacing, frame heights
float ParamFromSpacing( double spacing );

double DepthFromParam( float value );///< alpha, the shift at a tine, frame heights
float ParamFromDepth( double depth );

double StrokeSecondsFromParam( float value );///< Speed: one stroke's duration
float ParamFromStrokeSeconds( double seconds );

constexpr double kPi = 3.14159265358979323846;

} // namespace marbling
