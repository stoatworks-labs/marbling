"""The browser demo against the plugin's harness: the same clip, the same raster,
the same settings, the same audio, the same presses, two renderers.

    python3 demo/tools/crosscheck.py --matest build/matest [--keep DIR] [--only TEXT]

Needs a GPU, Google Chrome, numpy, a built matest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.
(woofer's crosscheck, which is mirrorball's, with marbling's clock, beat and
presses.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal) at ?size=WxH&clip=card. The page is
    paused, the case's settings applied, its audio chosen and the clock
    restarted: a new instance at time 0, primed. Then Step is pressed N times,
    one frame of 1/60 s each, so the page has fed the audio up to N/60 s at
    48 kHz and its clock reads N/60. Where the case presses Trigger, the page's
    own Trigger button is clicked after that many steps (the press and the
    release, then the frame drawn again at the same time, as a visitor's click
    does). The last frame is read back through renderer.readback(): the
    plugin's own output in the page's host framebuffer (straight colour, before
    the page premultiplies it for the canvas) and the clip texture the plugin
    was handed.
  - by the PLUGIN: `matest --pipe --fps 60 --size WxH --beat BPM --set ...`
    (the real MarblingPlugin in a GL 4.1 context, its clock preset to seconds),
    fed that clip for every frame, with the same host values: every control as
    the page's own hostValue() sends it, the float32 printed to 9 digits, and
    the presses as a --script ('k Trigger 1', 'k+1 Trigger 0'). matest feeds
    frame k the music up to k/60 s before drawing it (frame 0 gets none), as
    the page does for its frame at k/60, so the page's frame at N/60 is
    matest's frame N: N + 1 frames are piped and the last is compared. The
    page sends SetBeatInfo every frame whatever Trigger On is, so matest is
    given --beat on every case too (it is read only under Beat).

What can differ, and why: the page's instance votes on its clock (seconds or
milliseconds) against the wall clock, as in a host, and runs a few
milliseconds ahead of the page's clock from its priming; matest presets the
clock to seconds and starts at 0. Every op is timed from the plugin's own
clock, so the offset should cancel. WebAssembly has no fused multiply-add and
its libm is musl's, so the op list's doubles and earshot's analysis can differ
from arm64's in the last digits, and the GPU is this browser's ANGLE on Metal,
not the GL 4.1 driver.

The two outputs are compared byte for byte (RGB, and alpha separately). There
is no pass mark the plugin's own checks give for this, and a byte count alone
is the wrong one for a picture of hard-edged inks: where the traced position
of a pixel centre lands a last-bit away from an ink edge, the two GPUs can put
that pixel on different sides of it, and it is off by the whole difference
between two inks. So a case fails when more than 0.5% of its pixels are off by
more than one code, or when any pixel off by more than 8 codes is not on an
ink edge (a 4-neighbour more than 8 codes from it, in either picture), or when
alpha is off by more than one code. The script prints how many values are off
by 1, by 2-8 and by more, and how many pixels are edge flips. The edge test is
weak on a combed picture, where most of the frame is edge by it (93.7% of the
finished Nonpareil): there the count of pixels off is the figure. The defaults
case is the finished Nonpareil, still, the frame before Auto Paper's sheet
(the page's own stats say so, and the script requires it). Two negative
controls must differ from the page by more than their case does, so the
comparison is shown to see a slip that small: matest with Depth one slider
step (0.001) from the page's on the defaults case, and matest with every press
one frame late on the Manual case, whose last drop is still growing.

Exit 1 when a case is past that mark, a negative control passes, or the page
logs an error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"

# name, audio (the host row's id), BPM, frames N, presses (Trigger clicked
# after that many steps), settings by the plugin's own names (the page's
# kitValues(): {'units': x} is in the plugin's units and goes through its
# compiled ParamFrom*, an option is its element's name, a bare number the
# host's 0..1).
CASES = [
    ("defaults: Nonpareil finished, 4.67 s", "track", 120, 280, [], {}),
    ("Manual: Trigger at 20, 50, 80, 110", "track", 120, 118, [20, 50, 80, 110], {"Trigger On": "Manual"}),
    ("Beat at 128 BPM, Gelgit, nothing fed", "silence", 128, 360, [], {"Trigger On": "Beat", "Pattern": "Gelgit"}),
    ("Suminagashi, one drop, Mix 50%", "track", 120, 240, [], {"Pattern": "Suminagashi", "Drop Rate": {"units": 1}, "Mix": 0.5}),
    ("Stone, 8 drops, Auto Paper off (bakes)", "track", 120, 330, [], {"Pattern": "Stone", "Drop Rate": {"units": 8}, "Auto Paper": 0}),
    ("French Curl, fast strokes, Auto Paper off", "track", 120, 420, [], {"Pattern": "French Curl", "Speed": {"units": 0.2}, "Auto Paper": 0}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control on the panel, as the page sends it to the instance.
HOST = """import('./plugin.js').then(m => {
  const out = [];
  for (const p of m.PARAMS) {
    const h = m.hostValue(p.ff, m.mounted.params.get(p.id));
    if (h !== null) out.push([p.ff.name, h]);
  }
  return out;
})"""

# Restart with the audio and tempo chosen: the host row's own path (a new instance at time 0).
CHOOSE = """import('./plugin.js').then(m => { m.audio.id = %s; m.host.bpm = %d; m.restartClock(); })"""

STEP = """(async () => {
  const step = [...document.querySelectorAll('.transport .btn')].find((b) => b.textContent === 'Step');
  const frame = () => new Promise((resolve) => requestAnimationFrame(() => resolve()));
  const presses = new Set(%s);
  for (let i = 0; i < %d; i += 1) {
    if (presses.has(i)) {
      document.getElementById('ma-trigger').click();
      await frame();
    }
    step.click();
    await frame();
  }
  const m = await import('./plugin.js');
  const r = m.renderer.readback();
  const b64 = (a) => { let s = ''; for (let i = 0; i < a.length; i += 0x8000) s += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000)); return btoa(s); };
  return { time: m.mounted.state.time, stats: m.renderer.stats(), w: r.width, h: r.height, iw: r.inputWidth, ih: r.inputHeight, out: b64(r.output), input: b64(r.input) };
})()"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def counter(self):
        return self.run("(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")

    def instances(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.instances || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)")) > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.counter()
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.counter() != n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def top_first(raw, w, h):
    """GL's rows (bottom first) as the pipe's (top first)."""
    return np.frombuffer(raw, np.uint8).reshape(h, w, 4)[::-1].copy()


def matest(binary, host, frame, count, audio, bpm, presses, scratch, override=None, late=0):
    """The last of `count` frames, each fed the same clip."""
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    extra = ["--silent"] if audio == "silence" else []
    script = []
    if presses:
        path = os.path.join(scratch, "cues")
        with open(path, "w") as f:
            for k in presses:
                f.write(f"{k + late} Trigger 1\n{k + late + 1} Trigger 0\n")
        script = ["--script", path]
    proc = subprocess.Popen([binary, "--pipe", "--fps", "60", "--size", SIZE, "--beat", str(bpm), *extra, *script, *sets],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def feed():
        try:
            for _ in range(count):
                proc.stdin.write(frame)
            proc.stdin.close()
        except BrokenPipeError:
            pass

    writer = threading.Thread(target=feed, daemon=True)
    writer.start()
    size = len(frame)
    last, got = None, 0
    while True:
        chunk = proc.stdout.read(size)
        if not chunk:
            break
        if len(chunk) < size:
            chunk += proc.stdout.read(size - len(chunk))
        if len(chunk) == size:
            last, got = chunk, got + 1
    writer.join()
    err = proc.stderr.read().decode()
    if proc.wait() != 0 or got != count:
        raise RuntimeError(f"matest {' '.join(extra + script + sets)}: {got} of {count} frames; {err[:400]}")
    return last


def compare(a, b):
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    rgb = diff[..., :3]
    return diff, rgb, int((rgb > 0).sum())


def edges(picture):
    """Pixels with a 4-neighbour more than 8 codes away in RGB."""
    p = picture[..., :3].astype(np.int16)
    pad = np.pad(p, ((1, 1), (1, 1), (0, 0)), mode="edge")
    out = np.zeros(p.shape[:2], bool)
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        nb = pad[1 + dy:1 + dy + p.shape[0], 1 + dx:1 + dx + p.shape[1]]
        out |= np.abs(nb - p).max(axis=2) > 8
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--matest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    ap.add_argument("--only", help="run only the cases whose name contains this")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    server, base = serve()
    page = Page()
    scratch = tempfile.mkdtemp(prefix="marbling-crosscheck-")
    failures = 0
    try:
        for name, audio, bpm, count, presses, settings in CASES:
            if args.only and args.only not in name:
                continue
            page.load(f"{base}?size={SIZE}&clip=card")
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            kit = page.run(f"import('./plugin.js').then(m => m.kitValues({json.dumps(settings)}))")
            if kit:
                page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            before = page.instances()
            page.after(CHOOSE % (json.dumps(audio), bpm))
            if page.instances() != before + 1:
                raise RuntimeError("choosing the audio did not start a new instance")
            r = page.run(STEP % (json.dumps(presses), count))
            if abs(r["time"] - count / 60) > 1e-9:
                raise RuntimeError(f"the page's clock reads {r['time']} after {count} steps")
            if (r["w"], r["h"], r["iw"], r["ih"]) != (w, h, w, h):
                raise RuntimeError(f"the page drew {r['w']}x{r['h']} from a {r['iw']}x{r['ih']} clip, not {SIZE}")
            ours = top_first(base64.b64decode(r["out"]), w, h)
            clip = top_first(base64.b64decode(r["input"]), w, h)
            host = page.run(HOST)
            theirs_raw = matest(args.matest, host, clip.tobytes(), count + 1, audio, bpm, presses, scratch)
            theirs = np.frombuffer(theirs_raw, np.uint8).reshape(h, w, 4)

            diff, rgb, off = compare(ours, theirs)
            pixel = rgb.max(axis=2)
            over1 = int((pixel > 1).sum())
            big = pixel > 8
            flips = int((big & (edges(ours) | edges(theirs))).sum())
            ok = over1 <= 0.005 * pixel.size and flips == int(big.sum()) and diff[..., 3].max() <= 1
            s = r["stats"]
            if name.startswith("defaults") and not (s["stage"] == "done" and s["still"] and s["ops"] > 0):
                ok = False
                print(f"FAIL  {name}: the page is not on the finished pattern ({s['stage']}, still {s['still']}, {s['ops']} ops)")
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'}  {name:<44} {off} of {rgb.size} values off (1 code: {int((rgb == 1).sum())},"
                  f" 2-8: {int(((rgb >= 2) & (rgb <= 8)).sum())}, more: {int((rgb > 8).sum())}; max {rgb.max()});"
                  f" {over1} pixels off by more than 1 code, {int(big.sum())} by more than 8 ({flips} of them edge flips);"
                  f" alpha max {diff[..., 3].max()}"
                  f" | page: {s['triggers']} triggers, {s['sheets']} sheets, {s['ops']} ops, {s['bakes']} bakes, stage {s['stage']},"
                  f" clock {s['clock']:.4f} s, earshot {s['state']}")

            negative = None
            if name.startswith("defaults"):
                negative = ("negative: Depth one step (0.001) off", {"Depth": dict(host)["Depth"] + 0.001}, 0)
            elif presses:
                negative = ("negative: every press one frame late", None, 1)
            if negative:
                label, override, late = negative
                slip_raw = matest(args.matest, host, clip.tobytes(), count + 1, audio, bpm, presses, scratch, override, late)
                _, slip, slip_off = compare(ours, np.frombuffer(slip_raw, np.uint8).reshape(h, w, 4))
                caught = slip.mean() > rgb.mean()
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {label:<44} max {slip.max()} codes, mean {slip.mean():.2e}"
                      f" against the case's {rgb.mean():.2e}, {slip_off} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, "".join(c if c.isalnum() else "-" for c in name))
                Image.fromarray(ours[..., :3]).save(f"{stem}-page.png")
                Image.fromarray(np.ascontiguousarray(theirs[..., :3])).save(f"{stem}-matest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases within the mark' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
