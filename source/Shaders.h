#pragma once

#include <string>

/**
    The passes. Everything is in centred pixels of the output raster, y up:
    P = gl_FragCoord.xy - Viewport.xy - Size / 2. The CPU converts the ops
    from frame heights.

        1. **bake**    (only when more than 48 ops wait) RGBA32F at the host's
                       raster: the oldest ops composed into the backward map.
                       A texel is live, (Q.x, Q.y, 0, 1) with Q the clip
                       position it came from, or dropped, (premultiplied
                       colour, -1 - alpha). Ping-pong between two buffers.
        2. **resize**  (only when the raster changes) the map resampled onto
                       the new raster by plane coordinates.
        3. **palette** (only on a frame a drop falls) one texel per new drop:
                       the clip's premultiplied mean over a disc of half the
                       drop's radius, 16 taps on a Vogel spiral.
        4. **display** the host's frame: each pixel traced back through the
                       waiting ops, newest first ([Lu] p. 31, after Sims);
                       inside a drop, its colour; otherwise the map, then the
                       clip. The premultiplied Mix; alpha is ink density.

    Assembled at InitGL as kVersion + kCommon + the pass by `Assemble()`, the
    one place the order is written; tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace marbling::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kBakeFragment;
extern const char* const kResizeFragment;
extern const char* const kPaletteFragment;
extern const char* const kDisplayFragment;

enum class Pass
{
	Bake,
	Resize,
	Palette,
	Display
};

std::string Assemble( Pass pass );

/// Hook bits (the Hooks uniform). A shipped instance sets 0. Each is a
/// negative control: a model that must make a check FAIL.
enum Hook : int
{
	kHookCompressible = 1 << 0,///< every tool made not area-preserving (shear, rotation and push each perturbed)
	kHookPushExponent = 1 << 1,///< a drop's push sqrt( 1 - r^2/rho^2 ) given exponent 0.6
	kHookFoldSpacing  = 1 << 2,///< the comb's fold d' taken modulo 1.25 s
	kHookNearestMap   = 1 << 3,///< the map's live texels read nearest, not bilinear
	kHookNoPremultiply = 1 << 4,///< the clip sampled straight: what is under alpha 0 leaks
	kHookBlendInk     = 1 << 5 ///< the map read bilinear everywhere: inks blended at their edges
};

} // namespace marbling::shaders
