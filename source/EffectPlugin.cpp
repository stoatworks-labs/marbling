#include "Marbling.h"

/**
    The registration, in its own MODULE target (CMakeLists.txt): the
    CFFGLPluginInfo below registers itself from a file-scope constructor that
    nothing references, so it must not live in the OBJECT library the harness
    also links. `SW Marbling` is eleven characters, inside FFGL's 16.
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< marbling::MarblingPlugin >,// Create method
	"MA01",                                   // Plugin unique ID of maximum length 4
	"SW Marbling",                            // Plugin name
	2,                                        // API major version number
	1,                                        // API minor version number
	0,                                        // Plugin major version number
	1,                                        // Plugin minor version number
	FF_EFFECT,                                // Plugin type
	"Paper marbling (suminagashi, ebru): the clip's colours dropped as ink on water and combed with the closed-form, "
	"area-preserving marbling tools of Lu, Jaffer et al. (2012) -- drops that push older ink into rings, getgel, chevron, "
	"nonpareil, bouquet and French curl combs -- fired by onsets, the beat or a button.",
	"Marbling FFGL effect"// About
);

extern "C" const char* MarblingBuildStamp()
{
	return "marbling " MARBLING_VERSION ", built " __DATE__ " " __TIME__;
}
