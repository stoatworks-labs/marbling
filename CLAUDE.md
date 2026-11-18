# marbling

Paper marbling for Resolume Arena/Avenue as one FFGL effect: the clip's colours dropped as ink
on water and combed with [Lu]'s closed-form tools (drop, tine line, comb, wavy comb, circular
comb, vortex), every one exactly area-preserving on the plane. The bath is a backward map:
the newest ≤ 64 ops traced exactly per pixel per frame, older ops baked into an RGBA32F map.
Triggers from earshot's onsets, the host's beat or a button. C++/GLSL, CMake MODULE →
universal `.bundle` (macOS) + Windows `.dll` (built on GitHub's Windows runner). MIT. Public:
`github.com/stoatworks-labs/marbling`.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `MA01` | `SW Marbling` | `Marbling.bundle`, `com.stoatworks.ffgl.marbling` | effect |

`MA02` is held for a later source version; nothing of it exists.

Read `AGENTS.md` before touching a tool's formula (`kCommon` in `source/Shaders.cpp`), the
trace or the map (`traceBack`, `mapAt`, the bake), the palette, the display's alpha, the
bath's sequence or bake choice (`Bath::Trigger`, `Bath::BakeCount`), the event times
(`gatherEvents`, `handleOnsets`), the clock, or the device gate (`DevicesDisabled`).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build`. Never from a shared session on this Mac:
  nothing here has been loaded into Arena.
- Render offline: `./build/matest --out /tmp/m.png` (the defaults on the harness's colour card,
  fed the harness's kicks and hats through earshot's `feed()`, 12 s in); `--frames N`,
  `--size WxH`, `--clip F` (raw RGBA at `--size`), `--silent`, `--beat BPM` (the host's beat:
  set `Trigger On=Beat` to use it).
- List parameters: `./build/matest --list`
- Set anything by name: `./build/matest --set "Pattern=Bouquet" --set "Depth=0.5"` (a standard
  control takes the host's 0..1; an option takes its name; anything else is refused, exit 2).
  `--at N "Name=V"` sets it at frame N (an event is released the frame after).
- Film: `./build/matest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/matest --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`.
  A partial last frame is dropped (said on stderr); a reader that hangs up ends it with exit
  1, not SIGPIPE's 141.

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` under bash (GLSL reserved words and
  WebGL2 gaps, `glslc`, the submodule pin, the earshot snapshot, a fresh
  universal build with no warnings outside `external/`, the workflows' parameter count, lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest` -- oxbow is required, never skipped; every check at 1280x720 and
  320x180; ten again on Apple's software renderer at 320x180; `--offline`; Arena's alpha clips
  through `--pipe`; the pipe; the negative controls; 27 mutants; the sweep; the bench). The
  build directory (default `build-verify/`) is cleared first; `KEEP_BUILD=1` reuses it.
- One check at a time, each at both rasters (`--size WxH`: that raster alone):
  - **Geometry** (coordinate card: the output *is* the backward map): `--drop` (every pixel
    against [Lu]'s drop in double), `--rings` (concentric drops' annuli, [Lu] p. 27),
    `--jacobian` (block areas through six stacked tools), `--ink` (no blend, no stray label,
    alpha 1, πr² per drop), `--undo` (back along the same tines = identity; every stroke
    against Eq. 1/2 in double), `--period` (a comb's period is s), `--resample` (one bake's
    blur, predicted per pixel and measured).
  - **Time and host**: `--rate` (24/60/144 fps), `--clock` (499 million ms), `--resize`,
    `--paper` (and Auto Paper waiting for the last stroke), `--alpha`, `--triggers` (presses,
    beats, onsets through earshot), `--coreaudio` (an interposer counts audio calls in a child),
    `--glstate`, `--robust`.
  - **No GL**: `--model`, `--names`, `--cues` (also under `--offline`).
- The checks can fail: `./build/matest --negative` (20 wrong models), `tools/mutate.sh`
  (27 one-character mutants, arm64, 320x180, perl alarm at 300 s; its own tree, never a shared
  one; `tools/mutate.sh TEXT` runs only those whose description contains TEXT).
- No dead controls: `python3 tools/sweep.py --build build` (`--bare` drops the contexts).
- The demo clips: `python3 tools/clips.py alpha|defaults --build build` (Arena's media).
- Render cost: `./build/matest --bench` (720p, 1080p, one 4K run).
- `MATEST_RENDERER=software` runs any of it on Apple's software renderer.
- **No step opens an audio device.** matest sets `MARBLING_NO_DEVICES=1` (its `--coreaudio`
  children set it or not, case by case, always under an interposer that refuses every audio
  call);
  verify.sh exports it for oxbow and the scripts. With it set, or with Trigger On at Beat or
  Manual, the plugin neither lists devices nor looks a name up. A microphone prompt in a test
  run is a bug.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Plane units are frame heights**, origin at the frame centre, y down. Ops are stored in
  plane units and uploaded in centred pixels each frame, so a resize needs nothing for them.
- **Every op's progress is computed on the CPU in double**; the shader gets finished
  parameters and no time. A stroke in progress is the same op with α (or θ, or r²) scaled by
  its progress, which composes exactly.
- **The shader's sines are a series** (`sinSeries`/`cosSeries`, reduced to [−π/2, π/2]): Apple's
  software renderer's `sin` is off by ~1e-3. `--model` checks the series in float against libm.
- **The map's texels are live (Q, 0, 1) or ink (premultiplied rgb, −1 − a).** Bilinear only
  where all four neighbours are live; ink is read nearest and never blended.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index,
  except Audio Input, whose values are `earshot::deviceKey(name)`.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed precise`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `marbling_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `external/earshot/` is a snapshot of earshot (`github.com/stoatworks-labs/earshot`) at
  `8124191`, unpatched (`SNAPSHOT`). Never edit it here; change earshot upstream and take a new
  snapshot. Its own CLAUDE.md and AGENTS.md are earshot's as of that commit, status lines and all.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- **CI** (`.github/workflows/ci.yml`): macOS builds arm64 with the tools and runs `--list`,
  `--offline` and `glslc`; Windows builds the DLL with MSVC and loads it as a host does
  (`tools/wincheck.ps1`, whose `-Params` changes whenever a control or the About block does;
  verify.sh fails if either workflow's number is not `matest --list`'s). `release.yml` builds the
  universal bundle and the DLL with their zips, `.dmg` and installer; dispatched by hand it
  builds without publishing, and a `v*` tag publishes. Actions only because the repo is public:
  a private copy must never enable them. The software-renderer subset stays in verify.sh: a
  runner's GL is not Apple's software renderer.
- Checkout: `~/Projects/resolume/marbling`, public at `github.com/stoatworks-labs/marbling`. The
  spec is `~/Projects/resolume/specs/SPEC-marbling.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/marbling/marbling.YYYY-MM-DD.log
    %LOCALAPPDATA%\marbling\logs\marbling.YYYY-MM-DD.log

`MARBLING_LOG_DIR` overrides the folder.
