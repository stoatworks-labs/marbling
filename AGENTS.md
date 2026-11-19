# AGENTS.md: Marbling

Read this before changing a tool's formula, the trace, the map or the bake, the palette, the
display's alpha, the bath's sequence, the event times, the clock or the device gate.
`CLAUDE.md` has the commands; the spec is `~/Projects/resolume/specs/SPEC-marbling.md` (its As
built section records where this repo departs from it).

## Status

**v0.1.1, 2026-11-19: earshot `02306da` and the host's sample rate, so the lowest onset band
(40-160 Hz) fires on Resolume's FFT** (next section). Nothing else changed: no control, default
or id, and the audio-input path renders bit for bit as v0.1.0's. verify.sh passes with
`--hostfft` added and 23 of 23 negative controls. The rest of this section is v0.1.0's.

**v0.1.0, 2026-11-17; public at `github.com/stoatworks-labs/marbling` from 2026-11-18.**
`OXBOW=… tools/verify.sh` passes on this Mac (Apple
Silicon, macOS 26) against a fresh universal Release build: every check at 1280x720 and
320x180, ten on Apple's software renderer at 320x180, `--offline` with no GL context, Arena's
two DXV alpha clips through `--pipe`, the pipe, 20 of 20 negative controls, 27 of 27 mutants,
11 of 11 controls alive in the sweep, oxbow reading and rendering the bundle with devices
disabled. **Loaded into Resolume Arena 7.29.0 on a Mac (REST and the composition monitor, a
test card) and 7.27.1 on Windows (plugin-bench's gate, 9 passed, 0 failed, 2 skipped) on
2026-11-18; README's Status has the readings. No live audio device ever opened.** Onsets on
Resolume's FFT fire in Arena on the Mac with `Beat 001.wav` playing below (re-tested 2026-11-19;
the first run's `Beat 001.mov` has no audio stream: open question 8, settled). The DLL is built by CI (MSVC, clean on the first compile of marbling and
earshot). earshot is a snapshot of `github.com/stoatworks-labs/earshot` at `02306da`
(v0.1.0: `8124191`), unpatched.

## Resolume's FFT (v0.1.1)

Arena 7.29.0 was measured on this Mac on 2026-11-19 (snowglobe's agent, SW Hostprobe): its 64
bins are linear, 375 Hz each over 0-24 kHz (the 48 kHz engine rate, also for a 44.1 kHz clip),
magnitude, mono (L+R)/2, a new spectrum about every 43 ms. earshot `8124191` read them as linear
to 22.05 kHz and gave each bin whole to the band holding its centre: bin 0's centre, 172 Hz, is
above 160 Hz, so Marbling's lowest band (40-160 Hz) got no bin and never fired there, and every
kick triggered through 160-640 Hz. `02306da` shares each bin among the bands it overlaps (energy
f·m², level f·m, flux f·rise) and defaults to the measured law; the lowest band gets 0.32 of
bin 0.

- **The rate.** `Marbling::SetSampleRate` (FFGL; the SDK's own `sampleRate` member is never
  initialised, so the plugin keeps an atomic) stores it; the render thread hands it to
  `AudioInput::SetHostRate`, which reconfigures earshot's `hostBins.sampleRate` and logs "host
  audio at N Hz: Resolume's FFT bins are N/128 Hz each" once per change. 0 or a rate outside
  8 kHz-768 kHz keeps the default (48 kHz). It is the plugin's only `configure()` (Audio
  Input is `selectDevice`, the bands are fixed), which matters: earshot applies a
  `configure()` at the next `update()`, and a second one before then replaces the first
  (eidophone's trap, where a control's configure undid the rate's). `--hostfft` sets the rate
  and every control in one frame, then a new rate with every control and Trigger On away and
  back, and reads the rate back each time. Configure earshot anywhere else and build that
  Config from one place that carries the rate.
- **What changed on screen.** On a kick the lowest band fires in the same frame as 160-640 Hz
  and the 50 ms merge makes them one trigger, so kicks trigger as before. The new band adds a
  trigger of its own only where the low end moves without the rest (a bass note, a low tom):
  offline, `Beat 001.wav` made into Arena's bins gives 7.50 triggers a second against 6.75
  (+11%), `Bass 001.wav` 5.92 against 5.68 (+4%) (`matest --hostwav`).
- **In Arena 7.29.0 on this Mac** (2026-11-19: the pre-tag bundle, then the release's own
  notarised bundle, against a v0.1.0 build with only the 30-s log line added, the "tally"
  instrument, same probe): per whole 30-s window, `Beat 001.wav` gave 252/256 and 234/225
  triggers (lowest band 161/176 and 143/128) against v0.1.0's 239/250 (lowest band 0);
  `Bass 001.wav` 208-212 and 200-213 (lowest 91-99) against 214-215 (0). The versions differ
  by less than two v0.1.1 runs differ from each other (Arena's frame timing), so triggers did
  not measurably change; the model's +11%/+4% did not show. Arena's counts run about 1 a
  second above the model's on both loops. Snapshot pairs saturate (40 of 40 for both versions on both loops), so they can't
  tell the versions apart; the log line can.
- **The log.** Every 30 s in which anything triggered, the plugin logs onsets per band and the
  triggers they made ("onsets in 30 s (Resolume FFT): 40-160 Hz N, ..."), so a host run reads
  which bands fire without a harness.
- **`--hostfft`** builds the bins as Arena does (2048-point Hann, 16 fine bins to a bin, held
  2048 samples) from 128 BPM loops at 48 and 44.1 kHz and fails against `8124191`'s centre rule
  (`kCpuHostCentreOnly`), a plugin that ignores the host's rate (`kCpuIgnoreHostRate`) and one
  with no merge (`kCpuNoMerge`).

## The idea

**Every marbler's stroke is a map of the water onto itself that moves ink and never thins
it.** [Lu] wrote each tool in closed form; each is a shear along a direction (tine, comb, wavy
comb) or about a centre (circular comb, vortex), or a radial push ρ′² = ρ² + r² (a drop), and
every one has Jacobian determinant exactly 1 on the plane (the spec derives it). So the
picture is a backward map from the screen to where the ink came from: the clip, or a drop. No
ink is blended, so an edge between two inks stays sharp however many strokes cross it.

## What the literature settled (the spec has the claim-by-claim table)

- The tools and their formulas are [Lu]'s (pp. 27–30), read from the paper with the
  equations checked against the typeset pages. The IDEAS bullet's "Jaffer and Lu" is Lu et al.
- **Area preservation is exact, not a limit,** for every tool and every composition, in exact
  arithmetic on the plane. What departs from it here is float32, the bake's resampling, and
  the window's edge (ink leaves; after a bake the bath beyond the window is the window's mirror
  image). Each is measured.
- "A stroke back the other way only partly undoes": **a stroke drawn back along the same tines
  undoes it exactly** ([J-Math], "combing and its reverse cancel"; measured to 9.8e-4 px).
  Marblers draw the return offset half a spacing ([BAB] "bisect"), which does not.
- The pattern recipes are the marblers' ([BAB], the Scott excerpt, [J-HowTo], [J-Wavy]); the
  menu uses their names. No source gives a comb's depth or sharpness as a fraction of the tray.
- Not read: the Met Museum's essay (HTTP 429), the books [BAB] cites, Sims's back-tracing paper.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Marbling`, `MA01`, effect, `com.stoatworks.ffgl.marbling`, harness
  `matest`. `MA02` held for a source version, not built.
- **State: a baked backward map plus the newest ops traced exactly** (spec, State). At most 64
  ops traced per pixel per frame, newest first; when more than 48 wait, the oldest finished
  ones beyond the newest 16 are composed into an RGBA32F map at the output raster (and never
  fewer than leaves 48 waiting, so the next frame's ≤ 16 new ops always fit; only then is a
  moving op baked, at full strength). A texel is live (Q.x, Q.y, 0, 1) or ink (premultiplied
  rgb, −1 − a). Reading the map: bilinear when all four texels are live, else nearest. **Ink
  colours are never interpolated**; positions are resampled once per bake. At the defaults a
  single recipe stays under 48 ops and never bakes (measured for Bouquet: 43 ops, 0 bakes, at
  its finish); by count, French Curl (up to 53), more drops a trigger, Suminagashi and Auto
  Paper off go past it.
- **The first ink is the clip, live.** The clip keeps playing through the marbling. Beyond the
  window it is mirror-extended (`mirrorIndex`), so the bath has no seam at the frame edge.
- **A drop's ink is the clip's colour where it lands**: the premultiplied mean over a disc of
  half its radius (16 taps on a Vogel spiral), in the frame it falls, then fixed. The mean is
  taken in the clip's encoded values, not linear light (a choice: an eyedropper of the
  picture). Written to a 64-texel palette; a frame's bake never touches a drop whose colour is
  not written yet (≤ 16 new ops a frame).
- **Alpha is ink density.** The clip is premultiplied before anything samples it; the Mix is
  premultiplied; the output is unpremultiplied for the host. Mix 0 returns the clip by
  texelFetch, bit for bit.
- **Animation.** A comb or circular stroke in progress is the same op with α (θ) scaled by its
  progress, a drop with r² scaled: these compose exactly, so a stroke cut into frames is the
  whole stroke. A stroke takes Speed's 0.1–2 s; a drop grows over half that.
- **The patterns' numbers.** λ = s/6 for every comb (`kLambdaPerSpacing`), α = Depth. The
  bouquet's two wavy rakes: 8s apart, amplitude 2s, wavelength half a frame height, waves
  opposed, the second offset 4s. French Curl's curls: up to 12 vortices on a grid of pitch
  max(8s, 0.3), α = 2 × Depth, λ = pitch/6. Suminagashi: ink and clear drops alternate at
  fixed centres; every 8th trigger a circular stir of radius 0.25, α = Depth.
- **The sequence.** The drop stage ends when the dropped area reaches the window's (in frame
  heights squared) or after 12 triggers (Suminagashi: two windows or 48). Drops land at PCG
  hashes of their index, inside 92% of the window.
- **Auto Paper waits for the last stroke.** The spec said "the next trigger lays the paper";
  on the harness's music (a trigger every 0.25 s) that wiped the 0.6 s last stroke while it
  was still moving, so the finished marbling was never seen. A trigger now does nothing until
  every op has finished. The spec's sequence paragraph is updated.
- **Triggers.** Onsets: earshot's band onsets (bands 40/160/640/2560/8000 Hz) merged within
  50 ms, each placed at hostNow − (audioTime − lastOnsetTime), clamped inside the frame.
  Beat: each quarter of the bar, the crossing interpolated inside the frame (ferrofluid's);
  a phase that never moves gets the plugin's own clock at the BPM. At most 8 beats a frame.
- **The device gate.** CoreAudio is touched only when Trigger On is Onsets and
  `MARBLING_NO_DEVICES` is unset; the menu is listed on the first frame that needs it, never in
  the constructor. earshot's CMake links CoreAudio, AudioToolbox and CoreFoundation at load
  time, so the claim is "nothing calls into them or loads them by name", not "not linked".
- **Workflows only once public.** The machine's rule is GitHub Actions on public repos only.
  Until it was public marbling shipped none, so nothing could run by accident; `ci.yml` and
  `release.yml` came with the public repo (woofer's, with its `wincheck.ps1`). The
  software-renderer subset stays in verify.sh: a runner's GL is not Apple's software renderer.

## Defaults

Pattern Nonpareil, Trigger On Onsets, Audio Input Resolume FFT, Auto Paper on, Mix 1: choices.

**Drop Size 0.08 frame heights, from Arena's demo clips** (`tools/clips.py defaults`): each of
the 33 `.mov`s under `/Applications/Resolume Arena/media`, one frame held still at 320x180,
Stone, 3 drops a trigger, twelve presses, through `--pipe`; colour kept = the output's RGB
standard deviation over the input's. Median over the clips: 0.972 (r 0.03), 0.962 (0.05),
**0.905 (0.08)**, 0.764 (0.12), 0.516 (0.18). The default is the largest size keeping 80%:
0.08, which the scaffold already had. The metric reads contrast, not hue: clips with smooth
gradients score above 1 (Trinity_09 1.38 at 0.08), because flat discs on a gradient add edges.

**Chosen, not measured:** Drop Rate 3 (twelve triggers drop 36 drops of r 0.08, about 40% of a
16:9 window before overlaps: room for the comb to show the clip between them); Tine Spacing 0.03 frame heights (the fine comb;
[BAB]'s ¼" to 1" combs on an 18" tray are 0.014 to 0.056 tray heights, and 4s = 0.12 is the
1" comb); Depth 0.15 (a little more than the 1" comb's spacing, 0.12, so a tine drags ink past its
neighbour's line);
Speed 0.6 s (a stroke lasts about one beat at 100 BPM). No source gives a drop's size or a
comb's depth relative to a tray, so none of these is "right".

## Cost

`matest --bench` in the final verify.sh run of 2026-11-17 (this Mac's GPU, Apple Silicon,
shared with other builds; load average about 5). GL_TIME_ELAPSED, median and worst of 120
frames after 240 of warm-up:

| case | raster | GPU median | GPU worst | wall median | ops traced |
| --- | --- | --- | --- | --- | --- |
| defaults, host beat 120 BPM | 720p | 0.24 ms (1.4% of a 60 fps frame) | 0.70 ms | 0.43 ms | 33 |
| defaults, host beat 120 BPM | 1080p | 0.53 ms (3.2%) | 0.61 ms | 0.75 ms | 33 |
| 64 ops traced, the most a frame can | 720p | 0.57 ms (3.4%) | 1.35 ms | 0.78 ms | 64 |
| 64 ops traced, the most a frame can | 1080p | 1.21 ms (7.3%) | 1.51 ms | 1.43 ms | 64 |

The 4K line (0.53 ms median, 1.32 worst) is not comparable: 30 frames of warm-up leave only 3
ops traced. The cost grows with the ops traced, not with the pattern's age, because a bake
folds the old ones into one map read. **A bake frame's own cost was not measured** (no case
above baked).

## The traps actually hit

- **The palette pass never bound the clip.** Every drop sampled the zero texture (the
  driver said "unit 0 ... unloadable"). The plugin's bug, found by the checks' first runs;
  `writePalette` now binds the input, sets InputSize, and unbinds.
- **Ink thinned evenly reads back right.** The output is unpremultiplied, so a drop's ink
  scaled by 16/17 in all four channels has the right colour and only its alpha short: `--ink`
  passed against that mutant. The census now requires alpha 1 on its opaque cards.
- **A reflection reads as a valid position.** The coordinate card is mirror-extended, so a
  position pushed beyond the window reads back as its reflection, inside. Excluded where the
  map's local determinant (central differences) is not positive; every tool keeps orientation.
- **Auto Paper wiped strokes in motion** (above): found by the sweep, where at 5 s on the
  harness's music three controls read as dead because the default sheet had just been laid.
  The sweep now reads at 4 s, mid-strokes.
- **An axis-aligned comb is a degenerate resample test**: bilinear is exact along an axis-
  aligned shear's lines, so the residue was zero. The test comb is diagonal (0.6, 0.8).
- **120 BPM beats land exactly on 24 and 60 fps frames**, so "beats placed at the frame" passed
  `--triggers`. The checks use 100 BPM (0.6 s, between frames at both rates).
- **A wavy comb's reverse needs the opposite phase**, not the same one: the same phase is a
  different path. The identity cases also require the mean move within tolerance.
- **Blocks of 16 px did not resolve at 320x180** (the comb's folds cut every block), and the
  sag bound had been counting the boundary loop's own 90° corners. Blocks are now
  max(36 px, 0.1 H), second differences are taken along grid lines, and a third of the clean
  blocks must resolve.
- **`--robust` baked nothing** because Auto Paper reset the sheet first: it runs with Auto
  Paper off.
- **miniaudio runtime-links CoreAudio** (a dlopen at miniaudio.h:36584), so the interposer
  counts dlopens of the audio frameworks and `Audio*` dlsyms as well as the calls; its own
  control (gate off, Onsets) sees 2 dlopens, refused, so no device opens.
- **A hit on Resolume's FFT can trigger twice.** The first `--hostfft` asked for one trigger
  per hit and failed on the shipped code: a hit's attack reaches 640-2560 Hz one host update
  (42.7 ms) before its low end, and with a frame's quantising the two onsets land 50 ms apart,
  just outside the merge. v0.1.0 does the same (3 of 10 kicks); the check now counts doubles
  and refuses only two triggers in one frame (what no merge gives). Open question 9.
- **The lowest band's floor in silence is about 1e-4**, so leakage from hats alone fired it:
  the claps-and-hats case asks only that it never fires on a frame no other band fired.
- The machine's: heredocs hang without TMPDIR set; no `timeout` (perl alarm); in a while-read
  loop ffmpeg needs `-nostdin`; CMake needs `LANGUAGES C CXX` for the interposer's C; Apple's
  software renderer takes ~9.5 min for every check at 320x180, so verify.sh runs ten there.

## Verified, and assumed

Verified, by `tools/verify.sh` on 17 November 2026 (numbers in README's Status and below):
every claim there, at 1280x720 and 320x180 on this Mac's GPU, ten again on Apple's software
renderer; the CPU checks with no GL context; the alpha check on Arena's DXV clips; the
negative controls and mutants; the sweep, and `--bare`; the bundle universal, signed ad hoc,
read and rendered by oxbow.

Assumed: [Lu]'s incompressible, immiscible inks; the hyperbolic decay αλ/(d + λ); λ = s/6;
drops round and one colour; a moving stroke does not drag a drop that falls during it (the
later op is composed after); the clip mirror-extended beyond the window; Resolume's FFT
onsets usable (earshot's host path is frame-quantised; its bins follow Arena 7.29.0's law as
measured on one Mac on 2026-11-19).

Not done: loading it into Resolume; a live device; a Windows build; a user guide, presets, an
OpenFX port, a browser demo; antialiasing (edges are pixel-sharp; [Lu] uses 2×2 RGSS).

## Would this hold on another rasteriser, at another raster?

Nothing in the picture path uses texture filtering, derivatives or coverage: every read is
`texelFetch` with the bilinear done by hand, one full-raster triangle, and the shader's sines
are a series (Apple's software renderer's `sin` is off by ~1e-3). Tolerances are float32 at
the raster's width plus the map's own stretch, so they scale with the raster. One line per
check (software = Apple's renderer at 320x180, in verify.sh unless said):

- `--drop`: 1e-3 px + 16·2⁻²⁴·W·stretch. Software: passes.
- `--rings`: edges against the pixel grid's √2/2 px. Software: passes.
- `--jacobian`: chord sag from the picture's own second differences plus float. Software: passes.
- `--undo`: 1e-3 px + 8·2⁻²⁴·W·(1 + α/λ). Software: passes.
- `--period`: 0.5 px (a cusp between samples). Software: passes.
- `--resample`: the bilinear prediction in double, with float times the map's gradient.
  Software: passes.
- `--paper`, `--alpha`: bit-exact through texelFetch. Software: pass.
- `--clock`: bit-exact, same context. Software: passes.
- `--glstate`: GL queries. Software: passes.
- `--ink`, `--rate`, `--resize`, `--robust`: hundreds of frames each, left off the software
  run for time. An earlier `--all` on the software renderer at 320x180 (commit 33e5243, 2026-11-16,
  9 min 32 s) passed all 55 checks, before the census's alpha rule, the six-case `--undo` and
  Auto Paper's wait: not re-run since.
- `--triggers`: event times on the CPU in double, read from the plugin's log: no rasteriser.
- `--hostfft`: earshot's host path on bins made in double; event times only, no rasteriser:
  GPU only.
- `--coreaudio`: a process-level interposer (DYLD_INSERT_LIBRARIES): macOS only.
- `--model`, `--names`, `--cues`: no GL; run by `--offline`.

## The negative controls and mutants

`matest --negative`, 23 wrong models, each must fail its check; a check with none fails:

| check | wrong model |
| --- | --- |
| `--drop` | a drop's push given exponent 0.6, not ½ |
| `--rings` | a drop's push given exponent 0.6, not ½ |
| `--jacobian` | every tool perturbed off area-preserving (shear, rotation, push) |
| `--ink` | the map read bilinear across ink too: inks blended at their edges |
| `--undo` | every tool perturbed off area-preserving |
| `--undo` | the comb's fold taken modulo 1.25 s |
| `--period` | the comb's fold taken modulo 1.25 s |
| `--resample` | the baked map read nearest, not bilinear |
| `--rate` | beats placed at the frame, not inside it |
| `--clock` | the host's clock read through a float |
| `--resize` | a resize loses the map instead of resampling it |
| `--paper` | Paper does nothing |
| `--alpha` | the clip sampled straight, not premultiplied |
| `--triggers` | beats and onsets placed at the frame, not inside it |
| `--hostfft` | Resolume's bins filed whole under the band holding each one's centre (earshot 8124191's rule): the lowest band gets none |
| `--hostfft` | the host's sample rate never reaches earshot |
| `--hostfft` | every band's onset a trigger of its own, none merged |
| `--coreaudio` | the device menu listed and Audio Input applied whatever Trigger On says |
| `--glstate` | the display pass leaves its program and textures bound |
| `--robust` | nothing ever baked: ops pile up past what the shader traces |
| `--model` | a drop in progress composed in r, not r² |
| `--names` | a seventeen-character name added |
| `--cues` | every control ramped between keys |

`tools/mutate.sh`, 27 one-character mutants of the shipped code, each caught. One arm64 tree;
before each mutant the object and the matest binary are deleted, and the mutant is proved in
the build (the object differs from the unmutated one; a GLSL mutant's text is in the binary);
every named check must pass unmutated first; 320x180; perl alarm 300 s (a timeout counts as
caught, and is said):

| file | mutant | caught by |
| --- | --- | --- |
| Shaders.cpp (GLSL) | a drop's backward map with 1 + r²/ρ² under the root, − → + | `--drop` |
| Shaders.cpp (GLSL) | inside the drop taken for outside, < → > | `--drop` |
| Shaders.cpp (GLSL) | the comb's fold to the nearest tine turned outward, − → + | `--undo` |
| Shaders.cpp (GLSL) | the tine's falloff αλ/(d − λ), + → − | `--undo` |
| Shaders.cpp (GLSL) | the comb pulling against its own direction, − → + | `--undo` |
| Shaders.cpp (GLSL) | the wavy comb's conjugation undone the wrong way, − → + | `--undo` |
| Shaders.cpp (GLSL) | the circular stroke's distance from its circle as ρ + R, − → + | `--undo` |
| Shaders.cpp (GLSL) | the circular stroke's rotation not a rotation, − → + | `--undo` |
| Shaders.cpp (GLSL) | the sine series' cubic term 1/7, 6 → 7 | `--undo` |
| Shaders.cpp (GLSL) | the baked map's vertical weight read from x, y → x | `--resample` |
| Shaders.cpp (GLSL) | a resize scaling the map's positions the wrong way, * → / | `--resize` |
| Shaders.cpp (GLSL) | a drop's ink the mean of 16 samples over 17, 6 → 7 | `--ink` |
| Shaders.cpp (GLSL) | the bake's ink marker −1 + a, − → + | `--ink` |
| Shaders.cpp (GLSL) | the clip premultiplied by adding its alpha, * → + | `--alpha` |
| Shaders.cpp (GLSL) | Mix 1 returning the clip untouched, 0 → 1 | `--alpha` |
| Bath.cpp | Nonpareil's finishing comb the ½" one, not the ¼", 1 → 2 | `--model` |
| Bath.cpp | a stroke in progress travelling α / p, * → / | `--model` |
| Bath.cpp | Auto Paper laying the sheet over a stroke still moving, < → > | `--paper` |
| Bath.cpp | the forced bake n + 48, − → + | `--model` |
| Marbling.cpp | a comb's offset uploaded divided by the height, * → / | `--undo` |
| Marbling.cpp | the clock in tenths of a millisecond, 6 → 5 | `--triggers` |
| Marbling.cpp | a beat placed in the frame by boundary + p0, − → + | `--triggers` |
| Marbling.cpp | onsets kept only inside the merge window, > → < | `--triggers` |
| Marbling.cpp | a button that can never be pressed, 0.5 → 1.5 | `--triggers` |
| Marbling.cpp | the resize's scale height times the old height, / → * | `--resize` |
| Marbling.cpp | the audio device opened when Trigger On is not Onsets, == → != | `--coreaudio` |
| Audio.cpp | `MARBLING_NO_DEVICES=1` read as off, "0" → "1" | `--coreaudio` |

## The sweep's honest list

`tools/sweep.py` renders every control at both ends, 240 frames at 320x180 on the defaults
with the harness's music: 11 swept, 0 dead, 0 barely alive. With `--bare` (no per-control
context), **Auto Paper** goes dead: it acts only once a recipe has finished, and at 4 s the
first one is still in its strokes (its context reads at 5.5 s). Audio Input is not swept (it
acts on devices; `--coreaudio` covers its gate), nor Audio (the host's FFT buffer) or the About
block.

## Open questions

1. **How long should a finished marbling show?** Auto Paper now waits for the last stroke, then
   lays the sheet on the next trigger: on fast onsets (every 0.25 s) the finished pattern
   shows for about one trigger. A hold (a number of beats or seconds) would be a new control or
   a fixed choice.
2. **λ = s/6 is the main source of the bake's blur.** At the default comb one bake moves 12% of
   pixels more than half a pixel at 1280x720 (46% at 320x180); λ = 0.02 gives 1%. A softer
   default λ would trade the tines' sharp wake for less blur after a bake. At the defaults a
   single recipe never bakes, so this shows only with French Curl, more drops, Suminagashi or
   Auto Paper off.
3. **The demo-clip metric rewards contrast**: Drop Size's default rests on it; Drop Rate,
   Tine Spacing, Depth and Speed are chosen by eye on the harness's card, not on clips.
4. **Drop colours in encoded values, not linear light**: an eyedropper's choice; a linear mean
   would darken less at big radii.
5. **A moving stroke does not drag a drop that falls during it.** Physical, and possible
   (compose a new drop before the moving strokes), but it breaks "ops in trigger order".
6. **Antialiasing**: edges are pixel-sharp. [Lu]'s 2×2 RGSS would cost four traces a pixel.
7. **MA02**, the source version (a bath of the plugin's own colours): held, not built.
8. **Settled 2026-11-19: Resolume's FFT does reach the plugin.** The 2026-11-18 run played
   `Beat 001.mov`, which has no audio stream (Arena's demo clips keep their sound in the
   `.wav`s), so the buffer was silent. With `Beat 001.wav` on the layer below and the Audio
   buffer on Composition, Onsets fired (39 of 39 snapshot pairs over 8.5 s changed; none at
   rest, on Local over the card or on External). Arena's audio settings were left as they were.
   `matest --silent` still covers the empty-buffer case (no triggers, no crash). That run's
   triggers all came through 160-640 Hz and up: the lowest band got no bin until v0.1.1.
9. **A hit can trigger twice on Resolume's FFT**, 50 ms apart (above, the traps): 4 of 10
   kicks at 48 kHz offline, 0 at 44.1 kHz, and v0.1.0 the same. A wider merge (two host
   updates, ~90 ms) would absorb it but would also merge real sixteenths at fast tempos; not
   changed in a band-fix release.

## Browser demo

https://marbling-demo.stoatworks-labs.com/ (2026-11-18), from `demo/`, deployed by
`wrangler.toml` (a Worker route: the zone is at Cloudflare's 100 custom domains, so
`marbling-demo` is a proxied AAAA `100::` record made through the API) and
`.github/workflows/deploy.yml` on every push to main. carousel's, mirrorball's, fountain's and
woofer's pattern.

- **It runs the plugin, it doesn't port it.** `demo/marbling-core.wasm` is `Marbling.cpp` and
  everything it calls (Bath, Controls, Audio, PassBuffer, Shaders, Diag), earshot's snapshot
  (every `src/*.cpp` but `Miniaudio.cpp`/`MiniaudioImpl.cpp`, with `NoCapture.cpp`: its own
  `EARSHOT_CAPTURE=OFF` build, so the module has no device backend at all) and the FFGL SDK's
  plugin classes, FFGLLog, FFGLShader, FFGLFBO, FFGLScreenQuad and the scoped bindings,
  compiled unmodified by emscripten (`demo/tools/build-wasm.sh`, em++ 6.0.5, `-O3`, about 15 s).
  The op list, the recipes, Auto Paper, the triggers and the passes are that C++. Left out:
  `EffectPlugin.cpp`. The script fails if a `source/*.cpp` or an earshot `src/*.cpp` it doesn't
  list appears.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the plugin, reads its
  declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter, SetTime, SetBeatInfo and ProcessOpenGL as matest's Rig does, and hands the
  page the harness's accessors: the plugin's clock, ops, bakes, the trigger log, the bath's
  stage, earshot's state); `demo/wasm/gl_shim.cpp` takes glShaderSource (refusing any text that
  isn't byte for byte the page's assembly of `demo/shaders.js`, then the kit's `port()`, then
  checking line by line that the port changed only the version line, the precisions and the
  word `precise` on sinSeries' `k` and `y`) and glEnable/glDisable/glIsEnabled
  (GL_PROGRAM_POINT_SIZE, which WebGL2 lacks). The plugin draws into a host framebuffer of the
  page's (RGBA8, registered in emscripten's tables so its saved-state glGet names it); the page
  premultiplies that onto the canvas with its own small GLSL pass, because the plugin's output
  is straight colour (Suminagashi's clear drops are alpha 0) and the kit's canvas is
  premultiplied. Also the page's: the host row (audio, tempo, a file), the colour card (matest's
  `lookCard` arithmetic as a kit clip, in float, 8-bit), the read-outs' units (H = frame
  heights), tooltips, the thirteen presets (in frame heights, drops and seconds, through the
  plugin's inverses via `ma_param_from`), the line under the picture and Diag's log from
  emscripten's memory file system (`MARBLING_LOG_DIR=/marbling/logs`). Trigger and Paper are
  buttons that send 1 then 0 (the kit has no event control). Off the panel: Audio Input, the
  Audio buffer and the About block (the panel's note says so).
- **The audio, never a device.** The page never calls a device API (`check_shaders.py` fails if
  a served file names getUserMedia, enumerateDevices or mediaDevices), the module cannot open
  one, and `ma_prepare` sets `MARBLING_NO_DEVICES=1`. The host row's choices: matest's music
  (`musicAt` copied from `tools/matest/main.cpp` into the glue, with `kRate` and Rig::Render's
  feed lines, held character for character by `check_shaders.py`), nothing fed (`--silent`:
  earshot falls back to the Audio buffer, which nothing writes, so Onsets never fires), and a
  file of the visitor's, decoded by `decodeAudioData` in an `OfflineAudioContext` at 48 kHz (no
  output device), mono to both channels, fed through `ma_feed` and looped. Each frame feeds the
  samples up to the page's clock before ProcessOpenGL, as matest does (frame 0 gets none).
  Nothing is played aloud. Every audio change is a new instance from time 0 (a fresh sheet).
- **The beat.** A tempo select (60–174 BPM, 120 by default) sent every frame as SetBeatInfo with
  the bar's phase from the page's clock, `(phase0 + (t − t0) × BPM / 240) mod 1`, anchored at the
  instance's start and moved on a tempo change so the phase carries on: at a constant tempo
  exactly matest `--beat`'s `fmod(t × BPM / 240, 1)`. Sent under Onsets and Manual too, where it
  is unused.
- **The clock.** The page sends seconds from frame deltas (capped at 0.1 s). Each new instance
  (load, a change of audio, Restart) is primed with five frames about a millisecond apart so
  the plugin's seconds-or-milliseconds vote settles before the first frame shown (mirrorball's
  fix); nothing is fed during them, so a paused page stands still. The plugin's clock then runs
  about 5 ms ahead of the page's (its wall-clock frames before the vote settles); every op and
  trigger is timed from it, so nothing moves by it (the crosscheck agrees). A Trigger pressed
  while paused lays its drop at radius 0, and it grows when the clock moves; Paper shows at
  once.
- **Photosensitivity.** The note at the top names the fine, high-contrast regular stripes the
  combed patterns (Gelgit, Chevron, Nonpareil, Bouquet, French Curl) fill the frame with, which
  ITU-R BT.1702 / Ofcom's pattern guidance treats as a risk in themselves, and the large-area
  changes at the trigger rate (a new drop's push, Auto Paper's fresh sheet, Suminagashi's
  alternating ink and clear drops). No default was changed for it, and there is no gate: the
  page opens playing on the plugin's defaults (Nonpareil on onsets). Embed mode (`?embed=1`)
  hides the note. Its flash figures are the release's survey (README, "Patterns and flashes":
  xenon's flashcheck rule over all 33 of Resolume's demo clips through `matest --pipe --wav`;
  none over three flashes a second at any setting tried, the plugin's own worst two a second).
- **Checked by `tools/verify.sh`** (`demo/tools/check_shaders.py`, its new "Browser demo's
  copies" step): the seven GLSL pieces character for character and the order the five stages
  join them (read from Marbling.cpp's `Stage` table, `Assemble()` and InitGL's one `Compile`);
  the kit's `port()` text (hashed) and a mirror of it per stage; all 18 uniforms set (19 calls)
  by the setter their type takes, read from a hand table of the `uniform( shader, "Name", arg )`
  calls checked against the source; matest's music and feed lines in the glue; no served file
  naming a device API; the hashes of every input the .wasm was built from
  (`demo/wasm/inputs.sha256`, 64 files) and the SDK pin; no output executable. Any change under
  `source/` or `external/earshot/` needs `demo/tools/build-wasm.sh` rerun and its outputs
  committed (mode 644).
- **Compared with matest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built matest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused, the settings applied, the clock restarted,
  then N presses of Step (the clock at N/60), with the page's own Trigger button clicked where
  the case presses; the plugin's own output read back from the host framebuffer (before the
  page premultiplies it) and the clip it was handed; `matest --pipe --fps 60 --beat BPM` gets
  that clip N + 1 times with the same host values and the presses as a `--script`, and its last
  frame (fed the same audio, its clock at N/60) is compared. Apple M4 Max, 640×360 (691,200
  colour values a case), the colour card:

  | case | audio | N | values off by 1 / 2–8 / more | pixels > 8 codes off (on an ink edge) | page: triggers, bakes |
  | --- | --- | --- | --- | --- | --- |
  | defaults: Nonpareil finished and still | music | 280 | 2,527 / 7 / 3 | 1 (1) | 17, 0 |
  | Manual, Trigger at 20, 50, 80, 110 | music | 118 | 57 / 0 / 0 | 0 | 4, 0 |
  | Beat at 128 BPM, Gelgit | nothing fed | 360 | 2,705 / 0 / 0 | 0 | 12, 0 |
  | Suminagashi, 1 drop, Mix 50% | music | 240 | 8,746 / 0 / 0 | 0 | 16, 0 |
  | Stone, 8 drops, Auto Paper off | music | 330 | 2,194 / 4 / 16 | 6 (6) | 22, 4 |
  | French Curl, Speed 0.2 s, Auto Paper off | music | 420 | 4,821 / 826 / 158 | 99 (99) | 28, 1 |

  Alpha identical in every case. The negative controls: matest with Depth one slider step
  (0.001) off on the defaults case puts 135,411 values off (mean 2.92 codes against the case's
  0.004); with every press one frame late on the Manual case, 101,233 (mean 0.83 against
  0.00008). So the triggers land on the same frames and in the same places (onsets found by
  earshot in wasm included), and the picture agrees to the last code except where an ink edge
  passes within a last bit of a pixel centre: those pixels take the other ink, which is what a
  hard-edged map does under any float difference (the bake and the finest combs make most of
  them). Every pixel off by more than one code (642 at most, 0.28% of the frame, French Curl)
  has a 4-neighbour more than 8 codes from it, but that test is weak on a combed picture, where
  most of the frame is edge by it (93.7% of the finished Nonpareil, 66.5% of the French Curl):
  the count is the figure, not the test. The mark the script applies: ≤ 0.5% of pixels off by
  more than one code, every pixel off by more than 8 on an ink edge in one picture or the
  other. The first run used
  woofer's mark (max 8 codes, 1% of values) and failed four cases on exactly those edge pixels
  and on Mix 50%'s one-code rounding; the defaults case then sat at 5.0 s, one frame after Auto
  Paper's sheet (0 ops: an empty comparison its negative control could not see), and is now the
  finished pattern, which the script requires.
- **Also checked on the page** (CDP, the real GPU, 960×540): no console messages; Manual, one
  Trigger: 1 trigger, 3 drops, 54% of pixels moved more than 8 codes 0.3 s later; Nonpareil on
  the music reaches its finished pattern (stage done, 5 of 5 strokes, still) at 4.67 s, 17
  triggers, and Auto Paper's sheet comes on the trigger at 4.77 s; Depth 0.4 against 0.15 at
  the same frame differs in 93% of pixels, Bouquet against Nonpareil in 89%; Pattern changed
  while playing takes effect on the next trigger; a 4 s mono WAV of 100 BPM kicks chosen
  through the file input (CDP `DOM.setFileInputFiles`) decodes, starts a new instance and fires
  Onsets about once a kick (10 triggers in 6.3 s), looped.
- **Every gap, as the page discloses it** (banner, the panel's note, the "does not reproduce"
  list): it runs the plugin, not a port, and the units, tooltips and presets are the page's;
  no audio device or microphone, earshot built without capture, Audio Input and the Audio
  buffer off the panel, the live-device path and Onsets on Resolume's own FFT absent; the audio
  is the harness's track or a file decoded, resampled to 48 kHz and looped in the page, never
  played aloud; the beat is a tempo the page sends, not Resolume's bar, tap or sync; every audio
  change is a new instance from time 0; GL is emscripten's WebGL2 with four shimmed entry
  points and the kit's GLSL port, which drops `precise`; the float targets need
  EXT_color_buffer_float and the page stops without it; the page's premultiplying copy to the
  canvas; the clock is the page's and primed, a paused control move moves nothing, a paused
  Trigger lays a drop of radius 0; a frame is a display refresh; WebAssembly has no fused
  multiply-add and musl's libm; the clip is 8-bit and premultiplied by the kit and again by the
  plugin, so the kit's alpha clip is left out; the warning is the page's; the About block is
  off the panel; nothing on the page is measured against Resolume (the crosscheck is against
  matest; the release's Arena checks, Mac and win-lab, drove the plugin, not this page); Onsets
  on Resolume's FFT fire in Arena on the Mac (from v0.1.1 the lowest band too), and the plugin
  has never opened an audio device.
- **Traps hit**: the shim needs `(?:\.|->)Compile\(` in check_shaders (InitGL calls
  `stage.shader->Compile`); kit textures and framebuffers must be in emscripten's
  `GL.textures`/`GL.framebuffers` with `.name` set, or the plugin's saved-state glGet reads 0
  and restores the wrong binding; the kit's canvas is premultiplied and has no
  `preserveDrawingBuffer`, so the crosscheck reads the page's host framebuffer through
  `renderer.readback()` instead of the canvas; a control that shapes the ink (Pattern, the drops'
  and comb's) acts on the next trigger, so moving it while paused changes nothing on screen
  (the panel's note says so); the kit's inspector note says every declared parameter is on the
  panel, so the page appends which three are not. bubble's `R32F` readback trap does not arise:
  the plugin reads nothing back, and the crosscheck reads RGBA8 only.
- **Not done**: the crosscheck has run once, on one GPU (Apple M4 Max); no other browser or GPU
  has drawn the page.
