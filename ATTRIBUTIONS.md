# Attributions

Marbling is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### earshot — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot>  
Licence: MIT  
Copyright: Stoatworks Labs

external/earshot/ is earshot, the fleet's shared audio front end, as a snapshot: one commit taken with git archive and left unmodified, named in external/earshot/SNAPSHOT. It is built into the plugin by add_subdirectory and supplies the capture device, chosen by name with a Silent state, and the audio analysis the plugin reads every frame. It brings miniaudio with it, credited below.

### The device menu — Stoatworks spasis, by way of rubens

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index, with spare slots (source/Audio.cpp, source/Controls.cpp). Adapted from rubens (https://github.com/stoatworks-labs/rubens), which adapted it from spasis.

### The plugin shell, the harness and the tooling — Stoatworks rubens and haze

<https://github.com/stoatworks-labs/rubens>  
Licence: MIT  
Copyright: Stoatworks Labs

The OBJECT core with the plugin registered from the MODULE target alone, the table of controls, the host-clock vote (rosette's, by way of haze, https://github.com/stoatworks-labs/haze); the harness's rig, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout exits 1), --negative with its perturbation hooks, --names, --cues, --offline and --bench; tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py are rubens's, adapted (mutate.sh rebuilds one tree and proves each mutant is in the build). scripts/release-lib.sh is the fleet's, unchanged.

### Defaults from Resolume's demo clips — Stoatworks nipkow, by way of intensifier

<https://github.com/stoatworks-labs/nipkow>  
Licence: MIT  
Copyright: Stoatworks Labs

tools/clips.py is nipkow's, by way of intensifier (https://github.com/stoatworks-labs/intensifier): Arena's bundled media through the harness's --pipe, the alpha check on the two DXV clips with real transparency, and a default read off the median clip. Adapted.

### The beat — Stoatworks ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

SetBeatInfo(bpm, barPhase) read as ferrofluid's Drive does: a trigger at each quarter of the bar, the crossing placed inside the frame by interpolating the phase, and a clock of the plugin's own when the host never moves the phase (source/Marbling.cpp). Adapted.

### The pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's. Adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is radar's, which is vectrix's, which lifted it from resolume-scopes' Scopes.cpp: the host's GL state captured and put back.

### The release workflow and the DLL check — Stoatworks woofer, and mirrorball before it

<https://github.com/stoatworks-labs/woofer>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are woofer's, which are mirrorball's (haze's and xenon's before them): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is woofer's (mirrorball's, pinhole's before them). Adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### miniaudio, inside earshot

<https://github.com/mackron/miniaudio>  
Licence: Public Domain (Unlicense) or MIT-0, at your choice  
Copyright: David Reid

Arrives inside the vendored earshot snapshot, at external/earshot/third_party/miniaudio/miniaudio.h, unmodified. Not vendored separately.

earshot's capture backend: it lists the capture devices and runs the audio callback, on CoreAudio and WASAPI. The plugin never calls miniaudio itself; it arrives only with earshot.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Mathematical Marbling (2012): the tools — Shufang Lu, Aubrey Jaffer, Xiaogang Jin, Hanli Zhao and Xiaoyang Mao

<https://doi.org/10.1109/MCG.2011.51>

"Mathematical Marbling", IEEE Computer Graphics and Applications 32(6) (2012) 26–35. The ink drop (p. 27), the straight tine line, Eq. (1), and the comb (p. 28), the wavy and circular tools, Eq. (2), and the vortex (p. 30), and tracing each pixel back through every operation (p. 31). Every tool formula in source/Shaders.cpp and the harness's own double-precision copy. No code was taken. Read on 2026-11-16.

### The maths and the raster inverses — Aubrey Jaffer

<https://people.csail.mit.edu/jaffer/Marbling/>

"Mathematics of Marbling", "Dropping Paint", "Wavy Lines" and "How-To": the drop's raster inverse, the wavy comb as jiggle, rake, unjiggle, the bouquet as a dense rake and two jiggled wide rakes, the nonpareil's recipe, and "combing and its reverse cancel". Read on 2026-11-16.

### The patterns, and their names — bookartbook (unsigned); Schiffer Publishing; Wikipedia; the Turkish Ministry of Culture and Tourism

<https://bookartbook.pages.dev/>

"A brief theory of combed patterns" (bookartbook, unsigned): the getgel, chevron, nonpareil and French curl with their comb widths. Marbling: Practical Modern Techniques (Schiffer 2020), the publisher's excerpt, pp. 116–125: the Stone, Gelgit, Chevron and Fleur-de-lis. "Paper marbling", Wikipedia: suminagashi's alternating ink and surfactant, and Tokutaro Yagi's stirring. "Ebru: Turkish Art of Marbling" (Turkish Ministry of Culture and Tourism): battal, gelgit and the other named ebru kinds. The pattern names in the Pattern menu are theirs. Read on 2026-11-16.

## Standards and published specifications

What the implementation is measured against.

- **OpenGL 4.1 core profile** — texelFetch at integer texels with no host filtering: every read of the clip and of the baked map, the map's bilinear done by hand where all four texels are live.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
