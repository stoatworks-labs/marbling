# marbling

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume, and it has never opened a live audio device.** What's established is established
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

- **Onsets** (the default): drum hits, from earshot. Resolume's FFT by default, or a capture
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

**2026-11-16, v0.1.0, local only.** Built and verified offline on one Mac (Apple Silicon);
never loaded into Resolume, never run against a real audio device, never built on Windows.
No OpenFX port and no browser demo.

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
- **Cost** (this Mac's GPU, shared with other builds; GL_TIME_ELAPSED medians): the defaults
  0.24 ms at 720p and **0.53 ms at 1080p (3.2% of a 60 fps frame)**; the most a frame can trace
  (64 ops) 0.57 and 1.21 ms. A bake frame's own cost was not measured.

### Not established

- Whether it looks like a real marbled sheet to a marbler. The recipes follow the marblers'
  accounts; every comb's depth and sharpness (α = Depth, λ = spacing/6) is a choice, since no
  source gives them.
- Real inks are not incompressible: here drops push old ink out of the window instead of
  compressing a surfactant film. No feathering, no ink sinking, no colour mixing at an edge.
- A stroke still moving does not drag a drop that falls during it (the later op is composed
  after it).
- Resolume's FFT onsets: earshot's host fallback is frame-quantised and uncalibrated.
- Windows: never built.

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

Windows needs Visual Studio 2022 and vcpkg (for GLEW). It has never been built.

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
