#include "Bath.h"

#include <algorithm>
#include <cmath>

namespace marbling
{
uint32_t Pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

double Hash01( uint32_t a, uint32_t b )
{
	return Pcg( Pcg( a ) ^ ( b * 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

double Op::Progress( double now ) const
{
	if( !( duration > 0.0 ) )
		return 1.0;
	return std::clamp( ( now - start ) / duration, 0.0, 1.0 );
}

Op Op::At( double p ) const
{
	Op o = *this;
	if( type == OpType::Drop )
		o.r = r * std::sqrt( p );//r^2 linear in time: ink flows at a constant rate
	else
		o.alpha = alpha * p;//the tine at constant speed
	return o;
}

//---------------------------------------------------------------------------
// The recipes. s is Tine Spacing (the fine comb, [BAB]'s 1/4"); 1" = 4s,
// 1/2" = 2s. +x is lengthwise (the frame's long side). A bisecting return is
// the same comb, the other way, offset half its spacing ([BAB] "Bisect the
// lines you just made, using the same comb"; [Lu] Fig. 8c).
//---------------------------------------------------------------------------
namespace
{
Stroke comb( double dx, double dy, double spacingS, double offsetS, double waveS = 0.0 )
{
	Stroke s;
	s.kind     = Stroke::CombStroke;
	s.dirX     = dx;
	s.dirY     = dy;
	s.spacingS = spacingS;
	s.offsetS  = offsetS;
	s.waveS    = waveS;
	return s;
}
Stroke curls()
{
	Stroke s;
	s.kind = Stroke::Curls;
	return s;
}

/// [BAB]'s getgel: a 1" comb lengthwise, back bisecting; across, back bisecting.
std::vector< Stroke > getgel()
{
	return { comb( 1, 0, 4, 0 ), comb( -1, 0, 4, 2 ), comb( 0, 1, 4, 0 ), comb( 0, -1, 4, 2 ) };
}
std::vector< Stroke > nonpareil()
{
	auto r = getgel();
	r.push_back( comb( 1, 0, 1, 0 ) );//[BAB] "finish by using a 1/4" comb, going perpendicular"
	return r;
}

/// The bouquet's wavy rakes: half a frame height per wave (a choice).
constexpr double kWaveLength = 0.5;
} // namespace

const std::vector< Stroke >& Recipe( Pattern pattern )
{
	static const std::vector< Stroke > none;
	static const std::vector< Stroke > gelgit = getgel();
	//[BAB]: "Make a pair of bisecting movements with the 1" comb", then "a
	//perpendicular pair of bisecting movements with the 1/2" comb".
	static const std::vector< Stroke > chevron = { comb( 1, 0, 4, 0 ), comb( -1, 0, 4, 2 ), comb( 0, 1, 2, 0 ), comb( 0, -1, 2, 1 ) };
	static const std::vector< Stroke > nonp    = nonpareil();
	//[J-Wavy]: a dense rake, then two wide rakes each between a jiggle and its
	//undoing, the second offset with the wave the other way.
	static const std::vector< Stroke > bouquet = [] {
		auto r = nonpareil();
		r.push_back( comb( 1, 0, 8, 0, 2 ) );
		r.push_back( comb( 1, 0, 8, 4, -2 ) );
		return r;
	}();
	//[BAB]: "After making your base pattern, such as the aforementioned
	//nonpareil ... make large curls in the pattern".
	static const std::vector< Stroke > curl = [] {
		auto r = nonpareil();
		r.push_back( curls() );
		return r;
	}();
	switch( pattern )
	{
	case Pattern::Gelgit: return gelgit;
	case Pattern::Chevron: return chevron;
	case Pattern::Nonpareil: return nonp;
	case Pattern::Bouquet: return bouquet;
	case Pattern::FrenchCurl: return curl;
	default: return none;
	}
}

void Bath::DropPosition( uint32_t index, uint32_t sheet, double aspect, double& x, double& y )
{
	x = ( Hash01( index, 2u * sheet + 1u ) - 0.5 ) * aspect * 0.92;
	y = ( Hash01( index, 2u * sheet + 2u ) - 0.5 ) * 0.92;
}

void Bath::addDrop( double time, const Settings& s, double x, double y, bool clear )
{
	Op o;
	o.type          = OpType::Drop;
	o.cx            = x;
	o.cy            = y;
	o.r             = s.dropRadius;
	o.clear         = clear;
	o.slot          = nextSlot;
	o.colourPending = true;
	o.start         = time;
	o.duration      = s.strokeSeconds * kDropTimeFraction;
	nextSlot        = ( nextSlot + 1 ) % kMaxOps;
	ops.push_back( o );
	droppedArea += kPi * s.dropRadius * s.dropRadius;
}

void Bath::addStroke( double time, const Settings& s, const Stroke& stroke )
{
	if( stroke.kind == Stroke::Curls )
	{
		//Vortices on a grid, at most 12 ([BAB]'s "large curls"; the grid's
		//pitch is a choice: 8s, at least 0.3 frame heights).
		double g = std::max( 8.0 * s.spacing, 0.3 );
		int cols = 1, rows = 1;
		for( int guard = 0; guard < 100; ++guard )
		{
			cols = std::max( 1, static_cast< int >( std::floor( s.aspect / g ) ) );
			rows = std::max( 1, static_cast< int >( std::floor( 1.0 / g ) ) );
			if( cols * rows <= 12 )
				break;
			g *= 1.1;
		}
		for( int j = 0; j < rows; ++j )
			for( int i = 0; i < cols; ++i )
			{
				Op o;
				o.type     = OpType::Circle;
				o.cx       = ( i + 0.5 ) * s.aspect / cols - 0.5 * s.aspect;
				o.cy       = ( j + 0.5 ) / rows - 0.5;
				o.r        = 0.0;
				o.alpha    = 2.0 * s.depth;
				o.lambda   = g / 6.0;
				o.start    = time;
				o.duration = s.strokeSeconds;
				ops.push_back( o );
			}
		return;
	}
	Op o;
	o.type      = OpType::Comb;
	o.mx        = stroke.dirX;
	o.my        = stroke.dirY;
	o.spacing   = stroke.spacingS * s.spacing;
	o.offset    = stroke.offsetS * s.spacing;
	o.alpha     = s.depth;
	o.lambda    = o.spacing * kLambdaPerSpacing;
	o.waveAmp   = stroke.waveS * s.spacing;
	o.waveK     = stroke.waveS != 0.0 ? 2.0 * kPi / kWaveLength : 0.0;
	o.wavePhase = 0.0;
	o.start     = time;
	o.duration  = s.strokeSeconds;
	ops.push_back( o );
}

int Bath::Trigger( double time, const Settings& s )
{
	const size_t before = ops.size();
	if( stage == Stage::Done )
	{
		if( s.autoPaper )
		{
			Paper();
			return 0;
		}
		//The recipe again, on top of the old one.
		stage        = Stage::Drops;
		step         = 0;
		dropTriggers = 0;
		droppedArea  = 0.0;
	}
	++triggers;
	const double window = s.aspect;//its area, in frame heights squared
	if( stage == Stage::Drops )
	{
		if( s.pattern == Pattern::Suminagashi )
		{
			//[Wiki]: ink and a surfactant dropped alternately at the same
			//places; Yagi's stirring for spirals, every kStirEvery-th trigger.
			if( dropTriggers % kStirEvery == kStirEvery - 1 )
			{
				Op o;
				o.type     = OpType::Circle;
				o.cx       = 0.0;
				o.cy       = 0.0;
				o.r        = 0.25;
				o.alpha    = s.depth;
				o.lambda   = 0.25 / 6.0;
				o.start    = time;
				o.duration = s.strokeSeconds;
				ops.push_back( o );
			}
			else
			{
				const bool clear = ( dropTriggers % 2 ) == 1;
				for( int c = 0; c < s.dropRate; ++c )
				{
					double x = 0.0, y = 0.0;
					if( s.dropRate > 1 )
						DropPosition( static_cast< uint32_t >( c ), 1000u + static_cast< uint32_t >( papers ), s.aspect, x, y );
					addDrop( time, s, x, y, clear );
				}
				++dropIndex;
			}
			++dropTriggers;
			if( droppedArea >= kSuminagashiCover * window || dropTriggers >= kSuminagashiTriggers )
				stage = Stage::Done;
		}
		else
		{
			for( int d = 0; d < s.dropRate; ++d )
			{
				double x = 0.0, y = 0.0;
				DropPosition( dropIndex++, 0u, s.aspect, x, y );
				addDrop( time, s, x, y, false );
			}
			++dropTriggers;
			if( droppedArea >= window || dropTriggers >= kMaxDropTriggers )
				stage = Recipe( s.pattern ).empty() ? Stage::Done : Stage::Strokes;
		}
	}
	else if( stage == Stage::Strokes )
	{
		const auto& recipe = Recipe( s.pattern );
		if( step < static_cast< int >( recipe.size() ) )
			addStroke( time, s, recipe[ static_cast< size_t >( step ) ] );
		++step;
		if( step >= static_cast< int >( recipe.size() ) )
			stage = Stage::Done;
	}
	return static_cast< int >( ops.size() - before );
}

void Bath::AddForTest( Op op )
{
	if( op.type == OpType::Drop )
	{
		op.slot          = nextSlot;
		op.colourPending = true;
		nextSlot         = ( nextSlot + 1 ) % kMaxOps;
	}
	ops.push_back( op );
}

void Bath::Paper()
{
	ops.clear();
	mapIdentity  = true;
	stage        = Stage::Drops;
	step         = 0;
	dropTriggers = 0;
	droppedArea  = 0.0;
	triggers     = 0;
	++papers;
}

int Bath::BakeCount() const
{
	const int n = static_cast< int >( ops.size() );
	return n > kBakeAbove ? n - kBakeKeep : 0;
}

void Bath::Baked( int count )
{
	count = std::clamp( count, 0, static_cast< int >( ops.size() ) );
	ops.erase( ops.begin(), ops.begin() + count );
	if( count > 0 )
		mapIdentity = false;
}

} // namespace marbling
