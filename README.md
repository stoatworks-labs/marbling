# marbling

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into **Resolume
> Arena 7.29.0 on a Mac and 7.27.1 on Windows**, over a test card only, where it registers,
> draws, and on the Mac answers its buttons, the Beat trigger and every control as the harness
> says; **Onsets on Resolume's own FFT never fired there**, and it has never opened a live audio
> device (see [Status](#status)). The rest is established
> by an offline harness, `matest`, that drives the real plugin class through the same
> `ProcessOpenGL` a host calls, in a headless GL context on a synthetic clock, on this Mac's GPU
> at 1280x720 and 320x180 and on Apple's software renderer, and feeds it audio through the same
> earshot `feed()` path a device would use. Every tool is [Lu]'s closed form, exactly
> area-preserving on the plane; read back from the rendered picture, a block of the frame keeps
> its area through six stacked tools to **2.1e-4** (worst, against bounds of 6e-5 to 1.9e-3 from
> the picture's own chord sag), a drop's displacement matches the closed form to **2.8e-4 px**,
> and a comb drawn back along its own tines restores every measured pixel to **9.8e-4 px**. Through a
> whole Nonpareil, **no pixel** is a blend of inks or a colour that was never dropped. One bake's
> resampling is the one place the picture blurs, and it is measured, not hidden: at the default
> comb, **12%** of pixels move more than half a pixel at 1280x720. 24, 60 and 144 fps give **the
> same picture** (compared at 320x180). **20** deliberately wrong models and **27** one-character mutants of the
> shipped code are all caught. The patterns' depths and the drops' sizes are **chosen** (no
> source gives them as fractions of a tray): see [Status](#status).

Paper marbling for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL effect,
**SW Marbling**: your clip's colours dropped as ink on water and combed into the patterns
marblers make: stone, suminagashi, gelgit, chevron, nonpareil, bouquet, French curl.

![A frame filled edge to edge with a marbled bouquet pattern: rows of feather-shaped plumes,
each a fan of thin parallel bands, leaning alternately left and right in waves across the
frame. The bands run through the whole spectrum, green and yellow on the left, blue and violet
in the middle, magenta, red and orange on the right, with white and dark navy veins between
them](docs/hero.png)

<sub>One frame rendered by `matest`, not captured from Resolume: SW Marbling's Bouquet on the
harness's colour card, 4.9 s into the harness's music at the defaults (`--frames 295 --set
"Pattern=Bouquet"`), the last stroke just finished.</sub>

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/marbling/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`marbling-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/marbling/releases/download/v0.1.0/marbling-0.1.0-macos-universal.dmg) | 583 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`marbling-macos-universal.zip`](https://github.com/stoatworks-labs/marbling/releases/latest/download/marbling-macos-universal.zip) | 520 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`marbling-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/marbling/releases/download/v0.1.0/marbling-0.1.0-windows-x86_64-setup.exe) | 356 KB |
| x64 · .zip archive | [`marbling-windows-x86_64.zip`](https://github.com/stoatworks-labs/marbling/releases/latest/download/marbling-windows-x86_64.zip) | 245 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/marbling/releases](https://github.com/stoatworks-labs/marbling/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## The one idea

**Every stroke a marbler makes is a map of the water's surface onto itself that moves ink and
never thins it.** Lu, Jaffer, Jin, Zhao and Mao ("Mathematical Marbling", 2012) wrote each tool
as a closed form: an ink drop pushes everything around it out by √(ρ² + r²), a tine drags ink
along by αλ/(d + λ), a comb repeats the tine, a circular comb turns it about a centre. Each
is a shear or a radial push whose Jacobian determinant is **exactly 1** (the spec derives it),
so no ink is created, lost, mixed or blurred, and the edge between two inks stays sharp
however many strokes cross it.

Here the bath is a window onto an infinite plane, the first ink on it is your clip (live: the
clip keeps playing through the marbling), and a drop's ink is the clip's colour where it lands.
Each pixel is traced back through the newest strokes (up to 64), exactly, every frame; older
strokes are composed into a backward map. Nothing diffuses.

## Patterns

Each trigger is the next step: first the drops, then the strokes, in the order marblers give
them. s is Tine Spacing (the ¼" comb; ½" = 2s, 1" = 4s).

| Pattern | Who calls it that | After the drops |
| --- | --- | --- |
| Stone | battal (ebru); Stone (Scott) | nothing: drops only |
| Suminagashi | Japanese floating ink | ink and clear drops alternately at the same places; every 8th trigger a slow stir |
| Gelgit | getgel, gelgit | a 1" comb lengthwise, back bisecting; across, back bisecting |
| Chevron | chevron | a 1" comb lengthwise and back; a ½" comb across and back |
| Nonpareil | nonpareil | the gelgit, then a ¼" comb lengthwise |
| Bouquet | bouquet; fleur-de-lis | the nonpareil, then two wide wavy rakes, the waves opposed |
| French Curl | French curl, snail | the nonpareil, then curls on a grid |

## Triggers

- **Onsets** (the default): drum hits, from [earshot](https://github.com/stoatworks-labs/earshot),
  the fleet's live-audio front end ([a snapshot](external/earshot/SNAPSHOT) of commit `8124191`,
  unmodified). Resolume's FFT by default, or a capture
  device. Bands that fire within 50 ms are one trigger.
- **Beat**: each quarter of Resolume's bar, placed inside the frame.
- **Manual**: the Trigger button only. The button works under every source.

When the recipe is done, Auto Paper lays a fresh sheet on the first trigger after the last
stroke has finished, so the finished marbling is always seen whole. Paper lays one now.

## Controls

| Control | What it does |
| --- | --- |
| Pattern | Stone, Suminagashi, Gelgit, Chevron, Nonpareil (default), Bouquet, French Curl |
| Trigger On | Onsets (default), Beat, Manual |
| Audio Input | Resolume FFT (default), System default, a device by name |
| Trigger | one trigger |
| Paper | a clean sheet, now |
| Auto Paper | a clean sheet after each finished recipe (on) |
| Drop Size | radius 0.01–0.25 frame heights (0.08) |
| Drop Rate | 1–8 drops per trigger (3) |
| Tine Spacing | the fine comb, 0.0125–0.125 frame heights (0.03) |
| Depth | how far a tine drags the ink, 0.02–0.6 frame heights (0.15) |
| Speed | one stroke takes 2 s – 0.1 s (0.6 s); a drop grows over half that |
| Mix | the marbling over the clip, premultiplied (1) |

**Alpha is ink density.** The clip is premultiplied before anything samples it, so nothing
under a transparent pixel ever shows, a clear region of the clip stays clear wherever the comb
takes it, and an opaque clip stays opaque at every Mix. Mix 0 hands the clip through bit for
bit.

## Status

**v0.1.0, 18 November 2026, and early days.** It has been loaded into Resolume Arena 7.29.0
on a Mac and 7.27.1 on Windows, over a test card only (below). It hasn't been through a show,
nothing has been timed inside Resolume, and it has never opened a live audio device.
`oxbow probe` reads the bundle as a host does (`SW Marbling` / `MA01` / effect, inputs 1..1).
There's a [user guide](docs/USER-GUIDE.md) and a [browser demo](#browser-demo), but no presets,
no OpenFX port and no source version (`MA02` is held).

### In Resolume Arena

**On a Mac** (Arena 7.29.0, Apple M4 Max, 18 November 2026), over Arena's REST API and the
composition monitor's snapshot, with the plugin's own log: one layer added on top of a
composition holding an opaque colour card (the harness's, rendered at 1920x1080), SW Marbling
added to its clip, the layer removed afterwards and nothing saved.

- Arena registers **SW Marbling** and shows all 18 of its parameters after its own Opacity,
  with the constructor's defaults to the float. The About line reads "Marbling v0.1.0 - MIT -
  Stoatworks Labs, stoatworks-labs.com". Arena's clock reaches the plugin in milliseconds (the
  plugin's vote picked a scale of 0.001).
- **At the defaults, with nothing playing, it is the identity**: 0.0 levels from the bare card
  and 0.0 between snapshots two seconds apart. Nothing triggers without sound.
- **Trigger pressed over REST** (held 0.3 s) under Manual dropped ink over 47% of the middle of
  the frame; a second later the picture was still to the level. Seventeen presses ran a whole
  Nonpareil, still to the level two seconds after its last stroke; the eighteenth laid Auto
  Paper's fresh sheet, 0.0 from the bare card. **Paper** after two presses: 0.0 from bare.
- **Every control did what the harness says**, each read in a still context: one drop at Drop
  Size 0.3 moved 1.8% of the frame, at 0.8 62%; Drop Rate 1 and 8 drops moved 2.2% and 5.1%;
  the first stroke at Depth 0.2 and 0.9 changed the picture by 10.0 and 47.7 levels; **Tine
  Spacing's** first comb left tines 83.1 px and 216.0 px apart at 0.2 and 0.6, against 85.6 and
  215.0 px from the formula (4s of the frame's height); a stroke at Speed 0, the default and 1
  stopped moving 2.1 s, 0.8 s and under 0.3 s after the press (2, 0.6 and 0.1 s set, plus the
  press and a snapshot's latency). Stone with Auto Paper on laid a sheet on the trigger after its
  drops; with it off, the drops started again on top; Nonpareil combed. Mix 0 and Arena's
  Opacity 0 were 0.0 from the bare card.
- **Beat** on Arena's clock: 25 of 29 snapshots in six seconds changed.
- **Onsets on Resolume's FFT never fired.** With Arena's own demo clip Beat 001 playing on the
  layer below (its transport running), the default trigger gave nothing in twelve seconds. SW
  Needle v0.1.2, the fleet's VU meter, read the same "Composition" FFT on the same setup and
  didn't move either, and woofer's agent saw the same on this Mac. So Resolume's FFT reached no
  plugin here; why wasn't found, and Arena's audio settings were left as they were.
- On launch the plugin listed 11 audio inputs (the menu is filled on the first frame under
  Onsets) and opened none. No microphone prompt appeared. Arena's Quit dialog was answered
  Quit, and every composition file's modified time was unchanged.

**On Windows** (Arena 7.27.1 on win-lab, Mesa's llvmpipe, no GPU, no sound device, 18 November
2026), through plugin-bench's Arena gate, twice: with the DLL from a build dispatched before the
tag, and with the DLL from v0.1.0's own release zip (the same source; the two DLLs differ in
hash): **9 passed, 0 failed, 2 skipped** both times. It loads and registers as `SW Marbling` / `MA01`, all
19 parameters (Arena's Opacity first) match name, order, type, range and default, the two
16-character names arrive whole, and it renders. At the defaults it is the identity there too
(0 pixels from its bare input). The gate can't press a button, so Trigger and Paper were
skipped, and with no sound device Audio Input and the Audio buffer were skipped. Under Trigger
On Beat the other 10 controls all moved the picture; but under Beat the pattern grows and is
cleared every few seconds whatever the controls do, so that says less about each control than
the Mac's still-context reading above. Before that, CI compiles the DLL with MSVC (no warnings
in this repo's code or earshot's) and `tools/wincheck.ps1` loads it as a host does.

### Measured offline, on macOS

Every number is read off the rendered picture, with a coordinate card as the clip (each texel
holds its own position, so the output *is* the backward map) and labelled flat cards on the
frames a drop falls. Each check names its tolerance and where the tolerance comes from;
AGENTS.md has them all.

- **The drop.** Every pixel's backward map against [Lu]'s closed form composed in double: worst
  2.8e-4 px at 1280x720 (tolerance 2.2e-3: float32 at that width times the map's stretch). Five
  concentric drops: each ink's annulus edges on the closed form's radii within the pixel grid's
  √2/2 px, areas 16282–16290 px against 16286.
- **Area.** Each tool stacked on the last (tine, comb, wavy comb, circle, vortex, drop): every
  clean block's area, by the shoelace over its mapped boundary, within its own bound; worst
  |area/area₀ − 1| 5.1e-7 to 2.1e-4.
- **Ink.** Through Nonpareil's 17 real triggers, traced exactly or baked after every trigger, at
  both rasters: 0 pixels a blend of inks or a colour never dropped, 0 labels never dropped,
  every ink pixel opaque. Six overlapping drops and five strokes kept inside the window: each
  drop's pixel count against πr² within the pixels its edge can cross (worst 0.05 of that).
- **Strokes.** Comb, wavy comb, circle and vortex against [Lu]'s Eq. 1 and 2 composed in
  double: worst 7.2e-4 px. A comb drawn back along the same tines, straight or wavy: the
  identity to 9.8e-4 px. A bisecting return (the marbler's way): 31 px from it on average. The
  comb's period off the picture: exactly s.
- **The one blur: a bake.** A comb baked, then reversed exactly: every pixel matches a
  per-pixel bilinear prediction, and the residue is the resampling. At 1280x720 with α 0.15, λ
  0.02: worst 2.3 px, mean 0.031, 1.05% of pixels over 0.5 px. At the default comb (λ = s/6 =
  0.005, a steeper cusp): worst 3.1 px, mean 0.24, 12.2% over 0.5 px; at 320x180, 46%. Ink
  colours are never interpolated, only positions, and a bake happens once per 32 or more strokes.
- **Time.** A press and the host's beat at 24, 60 and 144 fps: the same trigger times (0 µs
  apart) and 0 pixels different at 2.75 s. Resolume's clock at 499 million ms: bit-identical.
  Beats land inside the frame to 0 µs; onsets fed through earshot land 0–26.7 ms after the hit
  (one analysis window), latest 6.0 ms, none missed or doubled.
- **Raster, paper, alpha.** A resize mid-run: every judged pixel on the per-pixel prediction. Paper:
  the clip back bit for bit. Mix 0: 0 of 3.7 million floats differ; white under transparent
  pixels: 0 floats differ, on the coordinate card and on Arena's two DXV alpha clips.
- **No audio device.** With `MARBLING_NO_DEVICES=1`, or with Trigger On at Beat or Manual, an
  interposer sees 0 audio-framework loads and 0 CoreAudio calls (and does see them with the gate
  off and Onsets on, so it can see).
- **Defaults.** Drop Size from Arena's 33 demo clips: at 0.08 the median clip keeps 0.905 of
  its colour spread (0.12: 0.76). The rest are choices: AGENTS.md says why.
- **Cost** (this Mac's GPU, an M4 Max shared with other builds; GL_TIME_ELAPSED medians over
  the three verify runs of 17 and 18 November): the defaults 0.13–0.24 ms at 720p and
  **0.33–0.53 ms at 1080p (2–3.2% of a 60 fps frame)**; the most a frame can trace (64 ops)
  0.52–0.57 and 1.21–1.22 ms. A bake frame's own cost was not measured.

### Not established

- Whether it looks like a real marbled sheet to a marbler. The recipes follow the marblers'
  accounts; every comb's depth and sharpness (α = Depth, λ = spacing/6) is a choice, since no
  source gives them.
- Real inks are not incompressible: here drops push old ink out of the window instead of
  compressing a surfactant film. No feathering, no ink sinking, no colour mixing at an edge.
- A stroke still moving does not drag a drop that falls during it (the later op is composed
  after it).
- **Onsets on Resolume's FFT in a host.** It never fired in Arena on a Mac (above), and nothing
  else on that input moved either. Offline, earshot's host fallback is frame-quantised and
  uncalibrated.
- **A live audio device.** Never opened: the device path is earshot's, tested there on a
  scripted backend.
- **How long a finished pattern should show.** Auto Paper waits for the last stroke, then lays
  the sheet on the next trigger, so on fast onsets a finished marbling shows for about one
  trigger (a quarter of a second at four a second). A hold would be a new control.
- **λ = s/6** is the main source of a bake's blur (12% of pixels over half a pixel at the
  default comb); a softer default would blur less but give a duller wake. The drop colours are
  averaged in the clip's encoded values, not linear light. Edges are not antialiased.
- **Drop Rate, Tine Spacing, Depth and Speed** are chosen on the harness's card, not measured
  on clips; Drop Size's measure on the demo clips rewards contrast.
- **Other GPUs.** One Apple GPU, Apple's software renderer and Mesa's llvmpipe only.

### Patterns and flashes

The combed patterns (Gelgit, Chevron, Nonpareil, Bouquet, French Curl) fill the frame with
fine, high-contrast regular stripes, which the ITU-R BT.1702 / Ofcom pattern guidance treats
as a photosensitivity risk on their own; the guide, the demo page and the release video say so
at the top. Flashes were measured on all 33 of Resolume's demo clips through `matest --pipe` at
60 fps (xenon's flashcheck rule: the whole frame and 25 third-of-frame windows in linear light),
with the clips' own stems (`--wav`), the stems layered and limited hot, or a 128 or 200 BPM
beat, at the defaults, 8 drops a trigger, 8 of the largest drops with the fastest strokes (also
as Stone and Suminagashi), and Suminagashi at its defaults: **no clip went over three flashes a
second at any of them**. The plugin's own worst was two a second over the whole frame
(Suminagashi at its fastest, on a clip with none bare); the one clip at three a second, in a
third of the frame, was at three bare.

## Browser demo

[marbling-demo.stoatworks-labs.com](https://marbling-demo.stoatworks-labs.com/) runs the plugin
in your browser: its own C++, the plugin class with its op list, the patterns' recipes, its
audio input and earshot under it, compiled unmodified to WebAssembly and drawing with its own
GLSL in WebGL2. The page is the host, and it never opens or lists an audio device or asks for a
microphone. It feeds the plugin through earshot's `feed()` as `matest` does: `matest`'s own
kicks and hats, nothing at all (`matest --silent`), or an audio file of yours decoded in the
page and never uploaded; nothing is played aloud. Beat takes a tempo the page sends as
`SetBeatInfo`, as `matest --beat` does. Audio Input and the live-device path are not in the
browser. The clip is the harness's colour card, drawn in the page. A frame of the page matched
`matest`'s on the same settings, audio and presses in six cases, with the triggers on the same
frames: every value within one 8-bit code but for at most 0.28% of a frame's pixels, all at ink
edges, which the two GPUs put on different sides of a pixel centre
([AGENTS.md](AGENTS.md#browser-demo) has the figures). The combed patterns fill the
frame with fine high-contrast stripes, and the page says so at the top. It is a demo, not the
plugin, and the page lists what it leaves out.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Marbling.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena: **SW Marbling** is under Effects.

Windows needs Visual Studio 2022 and vcpkg (for GLEW, `x64-windows-static-md`); `.github/workflows/ci.yml` is the recipe CI runs.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot, under bash: shader hygiene and `glslc`, the submodule pin and the
earshot snapshot, a fresh universal build with no warnings, the bundle's architecture, plist
and ad-hoc signature, a host's view of it through `oxbow` (required), every check at two
rasters and ten on the software renderer, the checks that need no GL context, Arena's alpha
clips, the pipe, the negative controls, the mutants, the sweep for dead controls and the render
cost. No step opens an audio device. `CLAUDE.md` lists the checks one by one.

A clip through it, triggered on a 120 BPM beat (without `--set` and `--beat` it triggers on the
harness's own kicks and hats, fed through earshot):

```bash
ffmpeg -i clip.mov -f rawvideo -pix_fmt rgba - \
  | ./build/matest --pipe --fps 30 --size 1280x720 --set "Trigger On=Beat" --beat 120 \
  | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mp4
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
