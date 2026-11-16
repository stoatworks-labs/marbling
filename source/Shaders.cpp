#include "Shaders.h"

namespace marbling::shaders
{

const char* const kVersion = "#version 410 core\n";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// Shared by every pass: the hooks, the ops, the trace, the map, the clip.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi = 3.14159265358979;
const int kMaxOps = 64;

const int kHookCompressible  = 1;
const int kHookPushExponent  = 2;
const int kHookFoldSpacing   = 4;
const int kHookNearestMap    = 8;
const int kHookNoPremultiply = 16;
const int kHookBlendInk      = 32;

uniform int Hooks;
uniform ivec4 Viewport;//the target: x, y, width, height
uniform vec2 Size;     //the output raster, px (the palette pass's target is not it)

//The ops, oldest first ([Lu]'s tools, centred px, y up; Bath.h).
//  Drop:   A = (1, cx, cy, r)            B = (palette slot, -, -, -)
//  Comb:   A = (2, Mx, My, tine offset)  B = (spacing (0: one tine), alpha, lambda, -)
//          C = (wave amplitude (0: straight), wavenumber, phase, -)
//  Circle: A = (3, cx, cy, r (0: vortex)) B = (alpha, lambda, -, -)
uniform int OpCount;
uniform vec4 OpA[ kMaxOps ];
uniform vec4 OpB[ kMaxOps ];
uniform vec4 OpC[ kMaxOps ];
uniform sampler2D Palette;//kMaxOps x 1: each drop's premultiplied colour

//The baked backward map: live (Q.x, Q.y, 0, 1) or dropped (rgb, -1 - a).
uniform sampler2D Map;
uniform ivec2 MapSize;
uniform int MapIdentity;//1: nothing baked yet, the map is the identity

uniform sampler2D InputTexture;
uniform ivec2 InputSize;

out vec4 fragColour;

bool hook( int bit )
{
	return ( Hooks & bit ) != 0;
}

//This pixel in centred px of the output raster, y up.
vec2 centred()
{
	return gl_FragCoord.xy - vec2( Viewport.xy ) - 0.5 * Size;
}

//sin by a reduced Taylor series: Apple's software renderer's sin is off by
//up to 1.09e-3 for small arguments (the fleet's trap). The series itself is
//within 1e-7 of sin on [-pi/2, pi/2]; the range reduction adds float's
//rounding of x - k 2 pi, about |x| 6e-8. matest --model measures both, in
//float, over the arguments the ops use.
float sinSeries( float x )
{
	precise float k = floor( x * ( 0.5 / kPi ) + 0.5 );
	precise float y = x - k * ( 2.0 * kPi );
	if( y > 0.5 * kPi )
		y = kPi - y;
	else if( y < -0.5 * kPi )
		y = -kPi - y;
	float y2 = y * y;
	return y * ( 1.0 + y2 * ( -1.0 / 6.0 + y2 * ( 1.0 / 120.0 + y2 * ( -1.0 / 5040.0 + y2 * ( 1.0 / 362880.0 + y2 * ( -1.0 / 39916800.0 ) ) ) ) ) );
}
float cosSeries( float x )
{
	return sinSeries( x + 0.5 * kPi );
}

//The bath beyond the edge is the mirror image of the bath inside it.
int mirrorIndex( int i, int n )
{
	int p = 2 * n;
	int m = i - p * int( floor( float( i ) / float( p ) ) );
	return m < n ? m : p - 1 - m;
}

//The clip, premultiplied, clamped: nothing under alpha 0 can show.
vec4 clipTexel( ivec2 t )
{
	vec4 c = clamp( texelFetch( InputTexture, ivec2( mirrorIndex( t.x, InputSize.x ), mirrorIndex( t.y, InputSize.y ) ), 0 ), 0.0, 1.0 );
	return hook( kHookNoPremultiply ) ? c : vec4( c.rgb * c.a, c.a );
}

//The clip at Q (centred px of the output raster), bilinear by hand on the
//premultiplied texels. At a texel centre f is exactly 0 and the texel comes
//back bit for bit.
vec4 clipAt( vec2 Q )
{
	vec2 t  = ( Q + 0.5 * Size ) * ( vec2( InputSize ) / Size ) - 0.5;
	vec2 i0 = floor( t );
	vec2 f  = t - i0;
	ivec2 i = ivec2( i0 );
	vec4 a  = clipTexel( i );
	vec4 b  = clipTexel( i + ivec2( 1, 0 ) );
	vec4 c  = clipTexel( i + ivec2( 0, 1 ) );
	vec4 d  = clipTexel( i + ivec2( 1, 1 ) );
	return mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y );
}

vec4 mapTexel( ivec2 t )
{
	return texelFetch( Map, ivec2( mirrorIndex( t.x, MapSize.x ), mirrorIndex( t.y, MapSize.y ) ), 0 );
}

//The map at P (centred px of the map's raster): bilinear when all four
//texels are live, else the nearest texel. Ink colours are never
//interpolated, so no edge between inks is ever blurred.
vec4 mapAt( vec2 P )
{
	vec2 t  = P + 0.5 * vec2( MapSize ) - 0.5;
	vec2 i0 = floor( t );
	vec2 f  = t - i0;
	ivec2 i = ivec2( i0 );
	vec4 a  = mapTexel( i );
	vec4 b  = mapTexel( i + ivec2( 1, 0 ) );
	vec4 c  = mapTexel( i + ivec2( 0, 1 ) );
	vec4 d  = mapTexel( i + ivec2( 1, 1 ) );
	if( min( min( a.w, b.w ), min( c.w, d.w ) ) > 0.0 && !hook( kHookNearestMap ) )
		return vec4( mix( mix( a.xy, b.xy, f.x ), mix( c.xy, d.xy, f.x ), f.y ), 0.0, 1.0 );
	if( hook( kHookBlendInk ) && min( min( a.w, b.w ), min( c.w, d.w ) ) < 0.0 && max( max( a.w, b.w ), max( c.w, d.w ) ) < 0.0 )
		return mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y );
	return mapTexel( ivec2( floor( t + 0.5 ) ) );
}

//P back through ops first..last, newest first ([Lu] p. 31). Each backward
//form is the forward one with its displacement negated ([Lu] p. 27). True
//when P lands inside a drop: ink is that drop's colour.
bool traceBack( inout vec2 P, int first, int last, out vec4 ink )
{
	ink = vec4( 0.0 );
	for( int k = last; k >= first; --k )
	{
		vec4 a   = OpA[ k ];
		vec4 b   = OpB[ k ];
		int type = int( a.x + 0.5 );
		if( type == 1 )
		{
			//Ink drop ([Lu] p. 27; the raster inverse, [J-Drop]).
			vec2 d     = P - a.yz;
			float rho2 = dot( d, d );
			float r2   = a.w * a.w;
			if( rho2 < r2 )
			{
				ink = texelFetch( Palette, ivec2( int( b.x + 0.5 ), 0 ), 0 );
				return true;
			}
			if( r2 > 0.0 )
			{
				float q = ( rho2 - r2 ) / rho2;
				P       = a.yz + d * ( ( hook( kHookPushExponent ) || hook( kHookCompressible ) ) ? pow( q, 0.6 ) : sqrt( q ) );
			}
		}
		else if( type == 2 )
		{
			//Comb: [Lu] Eq. (1) with d' for many tines, between a wavy
			//path's straightening W and its undoing ([J-Wavy]).
			vec2 M = a.yz;
			vec2 N = vec2( -M.y, M.x );
			vec4 c = OpC[ k ];
			if( c.x != 0.0 )
				P -= c.x * sinSeries( c.y * dot( P, M ) + c.z ) * N;
			float d = abs( dot( P, N ) - a.w );
			if( b.x > 0.0 )
			{
				float s = hook( kHookFoldSpacing ) ? 1.25 * b.x : b.x;
				d       = 0.5 * s - abs( mod( d, s ) - 0.5 * s );
			}
			float g = b.y * b.z / ( d + b.z );
			if( hook( kHookCompressible ) )
				g *= 1.0 + 0.2 * dot( P, M ) / Size.y;
			P -= g * M;
			if( c.x != 0.0 )
				P += c.x * sinSeries( c.y * dot( P, M ) + c.z ) * N;
		}
		else if( type == 3 )
		{
			//Circular, [Lu] Eq. (2); the vortex is r = 0. Forward turns
			//clockwise on screen, so backward turns anticlockwise (y up).
			vec2 d    = P - a.yz;
			float rho = length( d );
			if( rho > 0.0 )
			{
				float th = b.x * b.y / ( abs( rho - a.w ) + b.y ) / rho;
				if( hook( kHookCompressible ) )
					th *= 1.0 + 0.2 * d.x / Size.y;
				float sn = sinSeries( th );
				float cs = cosSeries( th );
				P        = a.yz + vec2( cs * d.x - sn * d.y, sn * d.x + cs * d.y );
			}
		}
	}
	return false;
}
)";

//---------------------------------------------------------------------------
// Bake: ops 0..BakeLast composed into the map (Map is the old one).
//---------------------------------------------------------------------------
const char* const kBakeFragment = R"(
uniform int BakeLast;

void main()
{
	vec2 P = centred();
	vec4 ink;
	if( traceBack( P, 0, BakeLast, ink ) )
	{
		fragColour = vec4( ink.rgb, -1.0 - ink.a );
		return;
	}
	fragColour = MapIdentity == 1 ? vec4( P, 0.0, 1.0 ) : mapAt( P );
}
)";

//---------------------------------------------------------------------------
// Resize: the old map (Map, MapSize) onto the new raster, by plane
// coordinates; live positions rescaled with the frame height.
//---------------------------------------------------------------------------
const char* const kResizeFragment = R"(
uniform float Rescale;//new height / old height

void main()
{
	vec4 m     = mapAt( centred() / Rescale );
	fragColour = m.w > 0.0 ? vec4( m.xy * Rescale, 0.0, 1.0 ) : m;
}
)";

//---------------------------------------------------------------------------
// Palette: one new drop's ink, the clip's premultiplied mean over a disc of
// half its radius (16 taps on a Vogel spiral, equal weights).
//---------------------------------------------------------------------------
const char* const kPaletteFragment = R"(
uniform vec3 Drop;//centre (centred px of the output raster), radius px
uniform int DropClear;

void main()
{
	if( DropClear == 1 )
	{
		fragColour = vec4( 0.0 );
		return;
	}
	vec4 sum = vec4( 0.0 );
	for( int i = 0; i < 16; ++i )
	{
		float rr = 0.5 * Drop.z * sqrt( ( float( i ) + 0.5 ) / 16.0 );
		float ph = float( i ) * 2.39996323;
		sum += clipAt( Drop.xy + rr * vec2( cosSeries( ph ), sinSeries( ph ) ) ) * ( 1.0 / 16.0 );
	}
	fragColour = sum;
}
)";

//---------------------------------------------------------------------------
// Display: the host's frame.
//---------------------------------------------------------------------------
const char* const kDisplayFragment = R"(
uniform float MixAmount;

void main()
{
	ivec2 here = ivec2( gl_FragCoord.xy ) - Viewport.xy;
	ivec2 at   = ivec2( ( vec2( here ) + 0.5 ) * ( vec2( InputSize ) / Size ) );
	vec4 raw   = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		fragColour = raw;//Mix 0 hands the clip through, bit for bit
		return;
	}
	vec2 Q = centred();
	vec4 ink;
	if( !traceBack( Q, 0, OpCount - 1, ink ) )
	{
		if( MapIdentity == 1 )
			ink = clipAt( Q );
		else
		{
			vec4 m = mapAt( Q );
			ink    = m.w > 0.0 ? clipAt( m.xy ) : vec4( m.rgb, -1.0 - m.w );
		}
	}
	//The premultiplied Mix: alpha is ink density, and nothing under a
	//transparent pixel shows at any Mix.
	vec4 s     = clamp( raw, 0.0, 1.0 );
	vec4 srcP  = hook( kHookNoPremultiply ) ? s : vec4( s.rgb * s.a, s.a );
	vec4 o     = mix( srcP, ink, clamp( MixAmount, 0.0, 1.0 ) );
	fragColour = o.a > 0.0 ? vec4( o.rgb / o.a, o.a ) : vec4( 0.0 );
}
)";

std::string Assemble( Pass pass )
{
	const std::string text = std::string( kVersion ) + kCommon;
	switch( pass )
	{
	case Pass::Bake: return text + kBakeFragment;
	case Pass::Resize: return text + kResizeFragment;
	case Pass::Palette: return text + kPaletteFragment;
	case Pass::Display: return text + kDisplayFragment;
	}
	return text;
}

} // namespace marbling::shaders
