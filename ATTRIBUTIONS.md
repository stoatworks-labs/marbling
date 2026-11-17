# Attributions

Marbling is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-16).** Marbling is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without
their work.

### Live audio — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot> (intended home; on 2026-11-16 a local repository only)  
Licence: MIT  
Copyright: Stoatworks

Vendored at external/earshot as a snapshot of commit 8124191, unmodified (external/earshot/SNAPSHOT). The capture device worker, the device list and keys, the band onsets (`Band::onsetCount`, `lastOnsetTime`) that Trigger On = Onsets merges into triggers, `feed()` (which the harness drives), and the fallback to Resolume's FFT bins. earshot's own attributions (spasis, pattern, miniaudio) are in external/earshot/ATTRIBUTIONS.md.

### The device menu — Stoatworks spasis, by way of rubens

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index, with spare slots (source/Audio.cpp, source/Controls.cpp). Adapted from rubens (local only), which adapted it from spasis.

### The plugin shell, the harness and the tooling — Stoatworks rubens and haze

rubens and haze: local repositories on 2026-11-16  
Licence: MIT  
Copyright: Stoatworks Labs

The OBJECT core with the plugin registered from the MODULE target alone, the table of controls, the host-clock vote (rosette's, by way of haze), the About block (source/StoatworksAbout*.h, a provisional hand copy of rubens's); the harness's rig, PNG writer, cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--names`, `--cues`, `--offline` and `--bench`; tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py are rubens's, adapted (mutate.sh now rebuilds one tree and proves each mutant is in the build). tools/clips.py is nipkow's (local only), by way of intensifier.

### The beat — Stoatworks ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

`SetBeatInfo(bpm, barPhase)` read as ferrofluid's Drive does: a trigger at each quarter of the bar, the crossing placed inside the frame by interpolating the phase, and a clock of the plugin's own when the host never moves the phase (source/Marbling.cpp). Adapted.

### The pass buffers, the log and the GL state guard — Stoatworks millpond and vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/millpond>, <https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's; source/GLState.h is radar's, which is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### miniaudio — David Reid

<https://github.com/mackron/miniaudio>  
Licence: public domain (the Unlicense) or MIT No Attribution, at your choice  
Copyright: David Reid

Arrives inside the earshot snapshot at external/earshot/third_party/miniaudio, unmodified. Capture device enumeration and the audio callback.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

## Research this is built on

No code was taken from these. The tools are written from the paper; the patterns from the
marblers' own accounts. Read on 2026-11-16.

### The tools — Lu, Jaffer, Jin, Zhao & Mao 2012

S. Lu, A. Jaffer, X. Jin, H. Zhao, X. Mao, "Mathematical Marbling", IEEE Computer Graphics and Applications 32(6) (2012) 26–35, doi:10.1109/MCG.2011.51. The ink drop (p. 27), the straight tine line, Eq. (1), and the comb (p. 28), the wavy and circular tools, Eq. (2), and the vortex (p. 30), and tracing each pixel back through every operation (p. 31). Every tool formula in source/Shaders.cpp and the harness's own double-precision copy in tools/matest/checks_geometry.inc.

### The maths and the raster inverses — Aubrey Jaffer

Aubrey Jaffer, "Mathematics of Marbling", "Dropping Paint", "Wavy Lines" and "How-To", people.csail.mit.edu/jaffer/Marbling/. The drop's raster inverse, the wavy comb as jiggle, rake, unjiggle, the bouquet as a dense rake and two jiggled wide rakes, the nonpareil's recipe, and "combing and its reverse cancel".

### The patterns — the marblers

"A brief theory of combed patterns", bookartbook.pages.dev (unsigned): the getgel, chevron, nonpareil and French curl with their comb widths. *Marbling: Practical Modern Techniques* (Schiffer 2020), the publisher's excerpt, pp. 116–125: the Stone, Gelgit, Chevron and Fleur-de-lis. "Paper marbling", Wikipedia: suminagashi's alternating ink and surfactant, and Tokutaro Yagi's stirring. Turkish Ministry of Culture and Tourism, "Ebru: Turkish Art of Marbling": battal, gelgit and the other named ebru kinds. The pattern names in the Pattern menu are theirs.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
