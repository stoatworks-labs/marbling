/**
 * Marbling — browser demo.
 *
 * Paper marbling as an FFGL effect (SW Marbling, MA01): the clip's colours
 * dropped as ink on water and combed into the patterns marblers make. Every
 * tool is a closed form from Lu, Jaffer, Jin, Zhao and Mao ("Mathematical
 * Marbling", 2012), a map of the water onto itself that moves ink and never
 * thins it; each pixel is traced back through the newest strokes, exactly,
 * every frame, and older strokes are composed into a backward map.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `marbling-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Marbling.cpp` — the plugin class, with
 * its constructor, its clock and seconds-or-milliseconds vote, its triggers
 * (earshot's onsets, the beat's quarters, the presses), the palette, bake,
 * resize and display passes and every uniform it sets — and everything it
 * calls: the op list and the patterns' recipes (Bath.cpp), the controls and
 * their conversions (Controls.cpp), the pass buffers, the audio input
 * (Audio.cpp) and, under it, earshot's snapshot, built with no device backend
 * at all (EARSHOT_CAPTURE=OFF), with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader and FFGLFBO. Its GL calls go to this page's WebGL2
 * context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     EffectPlugin.cpp's registration does in a bundle), reads its parameter
 *     declarations back through the SDK's host getters — the panel below is
 *     built from them, not from a list typed here — and forwards
 *     SetFloatParameter, SetTime, SetBeatInfo and ProcessOpenGL, as
 *     tools/matest's Rig does. The plugin draws into a framebuffer of the
 *     page's (the host's, as Resolume's is), and the page copies that to the
 *     canvas, premultiplying it on the way (the plugin's output is straight
 *     colour with alpha; the canvas is premultiplied).
 *   - **The audio.** No device: the page never asks for a microphone and never
 *     lists an input. It feeds the plugin as its harness does, through
 *     earshot's feed() (AudioInput::Feed, the path a device's samples take
 *     inside earshot): matest's own music, generated in the wasm by matest's
 *     own function (copied into glue.cpp, held to tools/matest/main.cpp by
 *     demo/tools/check_shaders.py), or a file the visitor chooses, decoded
 *     here. Or nothing fed, as matest --silent. Nothing is played aloud.
 *   - **The beat.** A tempo in the host row, sent every frame as SetBeatInfo
 *     (BPM, the bar's phase from the page's clock), as matest --beat sends it.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()` and checks it changed nothing but the
 *     version line, the precisions and the word `precise`. glEnable /
 *     glDisable / glIsEnabled: GL_PROGRAM_POINT_SIZE, which the plugin saves
 *     and restores every frame, does not exist in WebGL2.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ma_convert); the units, the tooltips
 *     and the presets are this page's words. The presets are written in frame
 *     heights, drops and seconds and go to the host's 0..1 through the
 *     plugin's own inverse functions (ma_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Every audio choice is a new instance, from time 0.** earshot's feed, once
 * begun, is what its engine reads; so the page never switches a running
 * instance between sources. Changing the audio restarts the clock, as Restart
 * does, and the new instance lays a fresh sheet and starts the track at its
 * beginning. A change of tempo does not: the bar's phase carries on from where
 * it was.
 *
 * **Each new instance is primed** (mirrorball's rule): five frames about a
 * millisecond apart, so the plugin's seconds-or-milliseconds vote settles
 * before the first frame shown and a paused page stands still. Nothing is fed
 * during them.
 *
 * **Photosensitivity.** The note at the top of index.html says what the
 * patterns are and what was measured. No default is changed for it.
 *
 * **Left off the panel**: Audio Input (the device menu: no device exists
 * here), the Audio buffer (Resolume's FFT, which a host writes) and the About
 * block, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import { SOURCES } from './vendor/sources.js';
import * as S from './shaders.js';
import createMarbling from './marbling-core.js';

const REPO = 'https://github.com/stoatworks-labs/marbling';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of Assemble() and InitGL's stages by
// check_shaders.py), one text per stage.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and what became of each. */
const shaderCheck = { matched: new Set(), accepted: 0, refused: 0, portRefused: [] };

/**
 * port()'s output for one of the page's stages, checked line by line against
 * the plugin's text: after `#version 300 es` and the precisions (exactly what
 * port() puts in front of an empty text), every line must be the plugin's,
 * but for the blank line(s) port()'s version pattern takes with the version
 * line, and the word `precise ` off exactly the declarations S.PRECISE lists
 * for that stage (k and y in sinSeries, in every fragment stage). Null if
 * anything else differs. check_shaders.py's check_port() is the same check,
 * at verify time. (carousel's.)
 */
function portChecked(ours) {
  const ported = port(ours.text, { stage: ours.stage });
  const head = port('', { stage: ours.stage });
  if (!ours.text.startsWith(S.VERSION) || !ported.startsWith(head)) return null;
  const before = ours.text.slice(S.VERSION.length).split('\n');
  while (before.length > 1 && before[0].trim() === '') before.shift();
  const after = ported.slice(head.length).split('\n');
  if (before.length !== after.length) return null;
  const stripped = [];
  for (let i = 0; i < before.length; i += 1) {
    if (before[i] === after[i]) continue;
    const m = /^(\s*)precise (float (\w+) = .*)$/.exec(before[i]);
    if (m && after[i] === m[1] + m[2]) {
      stripped.push(m[3]);
      continue;
    }
    return null;
  }
  const want = S.PRECISE[ours.name] ?? [];
  if (stripped.join(',') !== want.join(',')) return null;
  return ported;
}

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies, or a port
 * that changed more than it may, is refused (an empty source, so the
 * plugin's InitGL fails and logs which pass) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.find((s) => s.stage === stage && s.text === text);
  if (!ours) {
    shaderCheck.refused += 1;
    return '';
  }
  const ported = portChecked(ours);
  if (!ported) {
    shaderCheck.portRefused.push(ours.name);
    return '';
  }
  shaderCheck.matched.add(ours.name);
  shaderCheck.accepted += 1;
  return ported;
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createMarbling({
    marblingShaderSource: shaderSource,
    print: (line) => { printed.push(line); console.log(line); },
    printErr: (line) => { printed.push(line); console.warn(line); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at ${REPO}.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Before any instance: the plugin's log in the page's memory, and verify.sh's
// switch that keeps AudioInput from ever asking for a device list.
core._ma_prepare();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._ma_new();
  const list = [];
  const n = core._ma_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ma_param_type(h, i);
    const d = { index: i, name: text(core._ma_param_name(h, i)), type, group: text(core._ma_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ma_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ma_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ma_param_element_name(h, i, e)));
        d.values.push(core._ma_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._ma_min_inputs(h), core._ma_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions.
  return { list, inputs, converter: h };
}

const DECLARED = declarations();
if (DECLARED.inputs.join() !== '1,1') {
  throw new Error(`the plugin declares ${DECLARED.inputs.join('–')} inputs; this page hands it exactly one`);
}

/** Controls.cpp's conversion of a host value (NaN where the plugin takes it as it is). */
const convert = (d, v) => core._ma_convert(DECLARED.converter, d.index, Math.fround(v));
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (d, units) => core._ma_param_from(DECLARED.converter, d.index, units);

//===========================================================================
// What the panel leaves out, by the plugin's own names: the device menu (no
// device exists here), Resolume's FFT buffer and the About block.
//===========================================================================

const LEFT_OUT = ['Audio Input', 'Audio'];
const onPanel = (d) => d.group !== 'About' && !LEFT_OUT.includes(d.name);
for (const name of LEFT_OUT) {
  if (!DECLARED.list.some((d) => d.name === name)) throw new Error(`the plugin no longer declares ${name}; this page's notes say it is left out`);
}

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit. H is the frame's
// height, the unit every length in the plugin is in.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const UNITS = {
  'Drop Size': (x) => `${fixed(x, 3)} H`,
  'Drop Rate': (x) => `${fixed(x, 0)} drop${x === 1 ? '' : 's'}`,
  'Tine Spacing': (x) => `${fixed(x, 4)} H`,
  Depth: (x) => `${fixed(x, 3)} H`,
  Speed: (x) => `${fixed(x, 2)} s`,
};

function readout(d) {
  return (v) => {
    const x = convert(d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Pattern: 'What each trigger builds, in the order marblers give the steps: first the drops, then the strokes. Stone: drops only. Suminagashi: ink and clear drops alternately at the same places, every 8th trigger a slow stir. Gelgit: a 1″ comb lengthwise and back, bisecting, then across. Chevron: a 1″ comb lengthwise and back, a ½″ comb across and back. Nonpareil (the default): the gelgit, then a ¼″ comb lengthwise. Bouquet: the nonpareil, then two wide wavy rakes. French Curl: the nonpareil, then curls on a grid.',
  'Trigger On': 'What makes the next step happen. Onsets (the default): drum hits, from earshot’s analysis of the audio the host row feeds; bands that fire within 50 ms are one trigger. Beat: each quarter of the bar, from the host row’s tempo. Manual: the Trigger button only. The Trigger button works under all three.',
  Trigger: 'One trigger: the next step of the pattern, now. FF_TYPE_EVENT: the button sends the press and the release. Pressed while paused, a drop is laid at its centre with no size yet; it grows when the clock moves (Play or Step).',
  Paper: 'A clean sheet, now: every drop and stroke goes and the clip shows through again. FF_TYPE_EVENT: the button sends the press and the release.',
  'Auto Paper': 'On (the default): when the pattern is finished, the first trigger after its last stroke has stopped lays a fresh sheet, so the finished marbling is always seen whole. Off: the recipe runs again on top of the old one.',
  'Drop Size': 'A drop’s radius, 0.01–0.25 frame heights (H), 0.08 by default. The slider is geometric.',
  'Drop Rate': 'Drops per trigger, 1–8 (3 by default), at places hashed from the drop’s number, not from the time.',
  'Tine Spacing': 'The fine comb’s tine spacing s, 0.0125–0.125 frame heights (H), 0.03 by default: the ¼″ comb. The ½″ comb is 2s and the 1″ comb 4s. The slider is geometric.',
  Depth: 'How far a tine drags the ink, 0.02–0.6 frame heights (H), 0.15 by default. The slider is geometric.',
  Speed: 'How long one stroke takes, 2 s at the left to 0.1 s at the right (0.6 s by default); a drop grows over half that.',
  Mix: 'The marbling over the clip, premultiplied. 0 hands the clip straight back.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed). Trigger and Paper are
// FF_TYPE_EVENT: the kit has no event control, so they are given its toggle
// and the toggle is replaced with a button (below).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
};

/** The kit reads these from the query string itself, and this page reads `audio` and `bpm`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'audio', 'bpm']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN: return d.default > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return 0;
    default: return d.default;
  }
}

const PARAMS = [];
for (const d of DECLARED.list.filter(onPanel)) {
  if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  PARAMS.push(p);
}
const DEFAULTS = Object.fromEntries(PARAMS.map((p) => [p.id, p.default]));
const EVENTS = PARAMS.filter((p) => p.ff.type === FF_TYPE_EVENT);

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

//===========================================================================
// The clip: matest's colour card, drawn in the page. Marbling shows only
// what it moves, and a drop's ink is the clip's colour where it lands, so the
// clip has to be colour everywhere. The arithmetic is lookCard()'s in
// tools/matest/main.cpp, per texel centre, in float rather than double, into
// the kit's 8-bit buffer.
//===========================================================================

const CARD = `
uniform float Time;
uniform vec2 Resolution;
in vec2 uv;
out vec4 fragColor;

void main()
{
	vec2 px  = floor( gl_FragCoord.xy );
	float u  = ( px.x + 0.5 ) / Resolution.x;
	float v  = ( px.y + 0.5 ) / Resolution.y;
	float h  = mod( u * 1.5 + v * 0.35, 1.0 ) * 6.0;
	int sector = int( h ) % 6;
	float f  = h - floor( h );
	vec3 c;
	if( sector == 0 ) c = vec3( 1.0, f, 0.0 );
	else if( sector == 1 ) c = vec3( 1.0 - f, 1.0, 0.0 );
	else if( sector == 2 ) c = vec3( 0.0, 1.0, f );
	else if( sector == 3 ) c = vec3( 0.0, 1.0 - f, 1.0 );
	else if( sector == 4 ) c = vec3( f, 0.0, 1.0 );
	else c = vec3( 1.0, 0.0, 1.0 - f );
	float band   = 0.55 + 0.45 * ( ( int( v * 9.0 ) % 2 ) == 1 ? 1.0 : 0.6 );
	bool chequer = ( ( int( u * 12.0 ) + int( v * 7.0 ) ) % 5 ) == 0;
	bool bar     = v > 0.46 && v < 0.54;
	c = bar ? vec3( 1.0 ) : chequer ? vec3( 0.05, 0.05, 0.20 ) : c * band;
	fragColor = vec4( c, 1.0 );
}
`;

if (!SOURCES.some((s) => s.id === 'card')) {
  SOURCES.unshift({
    id: 'card',
    name: 'Colour card (the harness’s clip)',
    hint: 'The plugin harness’s own clip (lookCard in matest), drawn in the page: hue sweeping across, value bands down, a chequer of dark squares and a white bar, so every drop takes a colour and every stroke has edges to bend.',
    fragment: CARD,
  });
}

//===========================================================================
// The audio: the stand-in for what reaches the plugin in Resolume.
//===========================================================================

const FEED_RATE = 48000;
const AUDIO = [
  { id: 'track', name: 'matest’s music (kicks and hats)',
    hint: 'The plugin’s harness’s own track, generated in the WebAssembly by matest’s own function (musicAt): a kick every half second (120 BPM) and a hat on the off-beat. Fed through earshot’s feed() as a device’s samples would arrive, on both channels. Not played aloud.' },
  { id: 'silence', name: 'Nothing fed (matest --silent)',
    hint: 'matest --silent: no audio at all. earshot falls back to Resolume’s FFT buffer, which nothing writes here, so Onsets never fires: use Beat or Manual, or the Trigger button.' },
  { id: 'file', name: 'Your audio file',
    hint: 'A file you choose, decoded in this page by the browser and fed through feed(), looped. It is not uploaded and not played aloud.' },
];
const audio = {
  id: AUDIO.some((a) => a.id === query.get('audio') && a.id !== 'file') ? query.get('audio') : 'track',
  file: null, // { name, left: Float32Array, right: Float32Array, seconds, channels }
  epoch: 0, // bumped on every change of audio: a new instance
};

/** The host's tempo for Beat, sent every frame as SetBeatInfo. */
const TEMPOS = [60, 90, 120, 128, 140, 174];
const host = {
  bpm: TEMPOS.includes(Number(query.get('bpm'))) ? Number(query.get('bpm')) : 120,
};

/**
 * Decode a file the visitor chose. An OfflineAudioContext renders to memory,
 * not to an output device; decodeAudioData resamples to its 48 kHz. Nothing
 * here opens a device or asks for one.
 */
async function decodeFile(file) {
  const bytes = await file.arrayBuffer();
  const context = new OfflineAudioContext(2, 1, FEED_RATE);
  const buffer = await context.decodeAudioData(bytes);
  const left = buffer.getChannelData(0);
  const right = buffer.numberOfChannels > 1 ? buffer.getChannelData(1) : left;
  if (!left.length) throw new Error('it holds no samples');
  return { name: file.name, left: new Float32Array(left), right: new Float32Array(right), seconds: left.length / FEED_RATE, channels: buffer.numberOfChannels };
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (frame
// heights, drops, seconds) and is converted by the plugin's own inverse
// (ma_param_from); an option is its element's name; a bare number is the
// host's 0..1. They are the page's choices.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: Nonpareil on the music’s onsets', {}],
  ['Stone: big drops, nothing combed', { Pattern: 'Stone', 'Drop Size': u(0.16) }],
  ['Suminagashi: one drop a trigger, ink and clear in turn', { Pattern: 'Suminagashi', 'Drop Rate': u(1) }],
  ['Gelgit: the 1″ comb both ways', { Pattern: 'Gelgit' }],
  ['Chevron', { Pattern: 'Chevron' }],
  ['Bouquet: the nonpareil and two wavy rakes', { Pattern: 'Bouquet' }],
  ['French Curl: the nonpareil and curls', { Pattern: 'French Curl' }],
  ['A coarse, deep comb', { 'Tine Spacing': u(0.08), Depth: u(0.4) }],
  ['Slow strokes: 2 s each', { Speed: u(2) }],
  ['Eight small drops a trigger', { 'Drop Rate': u(8), 'Drop Size': u(0.05) }],
  ['On the beat: each quarter of the bar', { 'Trigger On': 'Beat' }],
  ['Manual: the Trigger button only', { 'Trigger On': 'Manual' }],
  ['Auto Paper off: the recipe again on top', { 'Auto Paper': 0 }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, a bare
 * number the host's 0..1. (demo/tools/crosscheck.py uses it too.)
 */
function kitValues(spec, what = 'settings') {
  const values = {};
  for (const [name, value] of Object.entries(spec)) {
    const p = PARAMS.find((x) => x.name === name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    if (p.ff.type === FF_TYPE_EVENT) throw new Error(`${what}: ${name} is an event, pressed, not set`);
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else {
      const hostValueOf = typeof value === 'object' ? paramFrom(p.ff, value.units) : value;
      if (!Number.isFinite(hostValueOf) || hostValueOf < -1e-6 || hostValueOf > 1 + 1e-6) throw new Error(`${what}: ${name} ${JSON.stringify(value)} is outside the control`);
      values[p.id] = Math.min(1, Math.max(0, hostValueOf));
    }
  }
  return values;
}

function kitPresets() {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) out[title] = kitValues(spec, `preset “${title}”`);
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT:
    case FF_TYPE_TEXT:
    case FF_TYPE_BUFFER: return null;
    default: return Math.fround(v);
  }
}

// The page's copy of the plugin's framebuffer to the canvas: the plugin's
// output is straight colour with alpha (a clear drop is alpha 0), the canvas
// is premultiplied, so it is premultiplied here, texel for texel. This is the
// page's GLSL, not the plugin's.
const PRESENT_VERTEX = `#version 300 es
void main()
{
	vec2 p = vec2( gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0 );
	gl_Position = vec4( p, 0.0, 1.0 );
}
`;
const PRESENT_FRAGMENT = `#version 300 es
precision highp float;
uniform sampler2D Host;
out vec4 fragColor;
void main()
{
	vec4 c = texelFetch( Host, ivec2( gl_FragCoord.xy ), 0 );
	fragColor = vec4( c.rgb * c.a, c.a );
}
`;

function compile(gl, vertex, fragment) {
  const program = gl.createProgram();
  for (const [type, source] of [[gl.VERTEX_SHADER, vertex], [gl.FRAGMENT_SHADER, fragment]]) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new GLError(`the page’s present pass would not compile: ${gl.getShaderInfoLog(shader)}`);
    gl.attachShader(program, shader);
  }
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new GLError(`the page’s present pass would not link: ${gl.getProgramInfoLog(program)}`);
  return program;
}

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: the one extension the plugin needs,
  // EXT_color_buffer_float (it renders into RGBA32F: the palette and the two
  // maps), the kit asked for itself (needFloat) and stops without. Every float
  // texture is read with texelFetch, so no float filtering is needed.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  const present = compile(gl, PRESENT_VERTEX, PRESENT_FRAGMENT);
  const presentHost = gl.getUniformLocation(present, 'Host');
  const presentVao = gl.createVertexArray();

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let presses = 0;
  let lastTime = null;
  let epoch = -1;
  let source = null;
  let lastInput = null;
  // The host's bar: its phase is the page's clock times BPM / 240 from an
  // anchor, the anchor moved on a change of tempo so the phase carries on.
  let beat = { bpm: host.bpm, time: 0, phase: 0 };
  // The visitor's file, fed as a device's samples would arrive: the absolute
  // sample clock at 48 kHz, samples [0, fileSent) gone to feed(), looped.
  let fileSent = 0;
  let scratch = 0;
  let scratchFrames = 0;

  // The host's framebuffer, as Resolume hands one to ProcessOpenGL: RGBA8,
  // the raster's size, in emscripten's tables so the plugin can bind it and
  // its saved-state readback (glGet of the binding) names it.
  const target = { texture: null, fbo: null, width: 0, height: 0, textureName: 0, fboName: 0 };
  function ensureTarget(width, height) {
    if (target.fbo && target.width === width && target.height === height) return;
    if (target.fbo) {
      core.GL.framebuffers[target.fboName] = null;
      core.GL.textures[target.textureName] = null;
      gl.deleteFramebuffer(target.fbo);
      gl.deleteTexture(target.texture);
    }
    target.texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, target.texture);
    gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, width, height);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.bindTexture(gl.TEXTURE_2D, null);
    target.fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target.texture, 0);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) throw new GLError('the host framebuffer is incomplete');
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    target.textureName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[target.textureName] = target.texture;
    target.texture.name = target.textureName;
    target.fboName = core.GL.getNewId(core.GL.framebuffers);
    core.GL.framebuffers[target.fboName] = target.fbo;
    target.fbo.name = target.fboName;
    target.width = width;
    target.height = height;
  }

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height, time) {
    if (instance) core._ma_delete(instance);
    instance = core._ma_new();
    pushed = new Map();
    frames = 0;
    instances += 1;
    fileSent = 0;
    beat = { bpm: host.bpm, time, phase: 0 };
    source = audio.file || audio.id !== 'file' ? audio.id : 'track';
    if (!core._ma_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : shaderCheck.portRefused.length
          ? `the port of ${shaderCheck.portRefused.join(', ')} changed more than the version line, the precisions and \`precise\`, so it was not compiled`
          : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Marbling could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p.ff, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ma_set_float(instance, p.ff.index, value);
    }
  }

  function phaseAt(time) {
    if (host.bpm !== beat.bpm) {
      beat = { bpm: host.bpm, time, phase: (beat.phase + (time - beat.time) * beat.bpm / 240) % 1 };
    }
    return (beat.phase + (time - beat.time) * beat.bpm / 240) % 1;
  }

  function feedFile(end) {
    const f = audio.file;
    const n = Math.min(Math.max(0, Math.round(end) - fileSent), 4 * FEED_RATE);
    if (n <= 0) return;
    if (n > scratchFrames) {
      if (scratch) core._free(scratch);
      scratchFrames = Math.max(n, FEED_RATE);
      scratch = core._malloc(scratchFrames * 2 * 4);
    }
    const out = core.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + 2 * n);
    const length = f.left.length;
    for (let k = 0; k < n; k += 1) {
      const at = (fileSent + k) % length;
      out[2 * k] = f.left[at];
      out[2 * k + 1] = f.right[at];
    }
    core._ma_feed(instance, scratch, n, FEED_RATE, 2);
    fileSent += n;
  }

  /** The audio up to page time `time`, before the plugin renders it (matest's order). */
  function feed(time) {
    if (source === 'track') core._ma_feed_music(instance, time);
    else if (source === 'file') feedFile(time * FEED_RATE);
  }

  /** SetBeatInfo, SetTime and ProcessOpenGL into the host's framebuffer. */
  function process(time, input, width, height) {
    core._ma_set_beat(instance, host.bpm, phaseAt(time));
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, width, height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (!core._ma_process(instance, time, glName(input.texture), input.width, input.height, target.fboName)) {
      throw new GLError(`The plugin’s ProcessOpenGL failed${printed.length ? `: ${printed.slice(-2).join(' ')}` : ''}.`);
    }
  }

  /**
   * Five host frames about a millisecond apart, before the first one shown:
   * enough for the plugin's seconds-or-milliseconds vote (four frames that
   * agree against the wall clock) to settle, so a paused page stands still.
   * Nothing is fed during them. The plugin's clock runs on the wall clock
   * until the vote settles, a few ms; the clock then steps back to the
   * page's time, which the plugin takes as no time passing.
   */
  function prime(time, input, width, height) {
    const t0 = performance.now();
    for (let k = 0; k < 5; k += 1) {
      if (k > 0) {
        // The plugin needs 0.5 ms of wall clock between votes. Date.now() caps
        // the wait where performance.now() stands still (a test harness's
        // stepped clock).
        const until = performance.now() + 1.05;
        const cap = Date.now() + 20;
        while (performance.now() < until && Date.now() < cap) { /* wait */ }
      }
      process(time + (performance.now() - t0) / 1000, input, width, height);
    }
  }

  function draw(width, height) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, width, height);
    gl.disable(gl.BLEND);
    gl.useProgram(present);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, target.texture);
    gl.uniform1i(presentHost, 0);
    gl.bindVertexArray(presentVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.useProgram(null);
  }

  /** Bytes of a texture, rows bottom first (GL's order). */
  function readTexture(texture, width, height) {
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
    const px = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, px);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.deleteFramebuffer(fbo);
    return px;
  }

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      ensureTarget(width, height);
      // A new instance for a change of audio, and for Restart (the page's
      // clock going back).
      const fresh = !instance || epoch !== audio.epoch || (lastTime !== null && time < lastTime);
      lastTime = time;
      epoch = audio.epoch;
      if (fresh) start(width, height, time);
      push(params);
      if (fresh) prime(time, input, width, height);

      feed(time);
      process(time, input, width, height);
      draw(width, height);
      lastInput = input;

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (frames <= 3) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of SW Marbling`);
        }
      }
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.presses = String(presses);
      gl.canvas.dataset.audio = source;
      gl.canvas.dataset.ops = String(core._ma_op_count(instance));
      gl.canvas.dataset.triggers = String(core._ma_triggers(instance));
    },
    /** FF_TYPE_EVENT: the press and the release, as the harness's Press() sends them. */
    press(name) {
      const p = EVENTS.find((x) => x.name === name);
      if (!instance || !p) return;
      core._ma_set_float(instance, p.ff.index, 1);
      core._ma_set_float(instance, p.ff.index, 0);
      presses += 1;
    },
    /** The last frame, as the plugin drew it (straight colour) and as it was handed the clip: rows bottom first. */
    readback() {
      if (!instance || !lastInput) return null;
      return {
        width: target.width,
        height: target.height,
        output: readTexture(target.texture, target.width, target.height),
        input: readTexture(lastInput.texture, lastInput.width, lastInput.height),
        inputWidth: lastInput.width,
        inputHeight: lastInput.height,
      };
    },
    stats() {
      if (!instance) return null;
      const pattern = Math.round(core._ma_get_float(instance, PARAMS.find((p) => p.name === 'Pattern').ff.index));
      return {
        source,
        clock: core._ma_clock(instance),
        ops: core._ma_op_count(instance),
        bakes: core._ma_bakes(instance),
        resizes: core._ma_resizes(instance),
        deferred: core._ma_deferred(instance),
        mapBaked: core._ma_map_baked(instance) === 1,
        triggers: core._ma_triggers(instance),
        sheets: core._ma_sheets(instance),
        lastTrigger: core._ma_last_trigger(instance),
        stage: ['drops', 'strokes', 'done'][core._ma_stage(instance)] ?? '?',
        strokeStep: core._ma_stroke_step(instance),
        recipe: core._ma_recipe_length(instance, pattern),
        bathTriggers: core._ma_bath_triggers(instance),
        dropsMade: core._ma_drops_made(instance),
        still: core._ma_still(instance) === 1,
        state: text(core._ma_capture_state_name(instance)),
        audioSource: core._ma_audio_source(instance),
        fed: source === 'file' ? fileSent : core._ma_fed(instance),
        bpm: host.bpm,
        frames,
        presses,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._ma_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. marbling-core.wasm is source/Marbling.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, its triggers, the palette, bake, resize and display passes, every uniform and the GL state it saves and puts back — and everything it calls (Bath, Controls, Audio, PassBuffer, Shaders, Diag), with earshot’s snapshot (8124191, unpatched) and the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The op list — every drop, comb and circle, the patterns’ recipes, Auto Paper, the drops’ hashed places — is that C++. Only the registration (EffectPlugin.cpp) is left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'No audio device, and no microphone: the page never asks for one, never lists one and makes no sound. earshot is compiled here with no capture backend at all (EARSHOT_CAPTURE=OFF: its NoCapture.cpp, and miniaudio is not compiled), and the plugin is told not to ask for a device list (MARBLING_NO_DEVICES=1, its verification switch). So two of the plugin’s controls are not on this panel: Audio Input (the device menu: Resolume FFT, System default, or a device by name) and the Audio buffer (Resolume’s FFT, which a host writes and nobody sets). The live-device path — a device opened by earshot’s worker thread, the menu’s listing, a device going missing or failing — and Onsets on Resolume’s own FFT are not in the browser.',
  'The audio is the plugin’s harness’s, handed over as its harness hands it: through earshot’s feed() (AudioInput::Feed), the path a device’s samples take inside earshot, so Onsets fires on the drum hits earshot finds in it. The track is matest’s own (a kick every half second, a hat on the off-beat), generated in the WebAssembly by matest’s musicAt, copied character for character with the lines that feed it and held to tools/matest by the repository’s verify script; it is fed up to the page’s clock before each frame, at 48 kHz, on both channels, as matest feeds it. “Nothing fed” is matest --silent. Your own audio file is decoded by this browser (Web Audio’s decodeAudioData, in an OfflineAudioContext, resampled to 48 kHz; a mono file goes to both channels), never uploaded, and fed the same way, looped. Nothing is played aloud.',
  'Beat takes the host’s bar from the host row’s tempo: SetBeatInfo every frame with the BPM and the bar’s phase from the page’s clock, as matest --beat sends it; a change of tempo carries the phase on. Resolume’s own bar follows its BPM, its tap and its sync sources, and none of that is here. Under Onsets and Manual the beat is sent and not used.',
  'Every change of audio is a new instance from time 0, as Restart is: a fresh sheet, and the track from its start.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order Assemble() and InitGL join the pieces in, and the repository’s verify script fails if a character differs, if a uniform is set by a call its GLSL type does not take, or if one is never set. At start-up the page also requires each of the five stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line, adds the ES precision defaults and drops the word precise from the two declarations in sinSeries that carry it (GLSL ES 3.00 has no precise; it asks the compiler not to reassociate the angle’s range reduction, and without it a compiler may, in the last bits), and the page checks it changed nothing else. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points.',
  'The palette and the two backward maps are RGBA32F render targets, as in the plugin, which WebGL2 renders into only with EXT_color_buffer_float: the page stops without it rather than fall back to 8 bits. They are read with texelFetch, so no float filtering is needed, and the plugin reads nothing back from the GPU.',
  'The plugin draws into a framebuffer of the page’s, as a host’s, and the page copies it to the canvas, premultiplying on the way: the plugin’s output is straight colour with alpha, and Suminagashi’s clear drops are alpha 0, which shows the backdrop. That copy is the page’s GLSL, not the plugin’s.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime, and the audio is fed up to it. The plugin votes against the wall clock on whether those are seconds or milliseconds; so that a paused page stands still, each new instance — page load, a change of audio and Restart — is handed five frames about a millisecond apart before the first one shown, which settles the vote, with nothing fed during them. The plugin’s clock then runs a few milliseconds ahead of the page’s (about 5 ms in headless Chrome on a Mac); every drop and stroke is timed from it, so the picture does not move by it. Paused, a moved control redraws the frame without feeding or moving anything. A Trigger pressed while paused lays its drop with no size yet, and it grows when the clock moves. Step advances the clock 1/60 s.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. The plugin times every trigger inside the frame and every stroke on its own clock; its harness measures 24, 60 and 144 fps giving the same picture.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sqrt, exp and sin are musl’s, not Apple’s, so the op list’s doubles can differ from the plugin built for a Mac in the last digits (the drops’ places are PCG hashes, integer arithmetic, and cannot), and earshot’s analysis of the same audio can place an onset differently in the last digits. What reaches the GPU is the ops as float, as in the plugin, rasterised by this browser’s GPU, not the Mac’s.',
  'The clip is matest’s colour card, drawn in the page into an 8-bit texture, or one of the kit’s generated clips, or your own image or video. The kit hands over premultiplied colour, and the plugin premultiplies its input itself; the page’s clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them, and an image of your own with transparency would be premultiplied twice at its edges. In Arena the effect gets the composition’s own frame; here its whole texture is used, unpadded.',
  'The photosensitivity note at the top is the page’s, not the plugin’s; in embed mode (?embed=1, the output as a video source) it is not shown. No default was changed for it. The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Nothing here is measured against Resolume: this page is checked against the plugin’s harness, not against Arena. The plugin’s release checks loaded it into Arena, on a Mac and on Windows, and drove its controls there; Onsets on Resolume’s own FFT has not yet been seen to trigger in a host, and the plugin has never opened a live audio device. Its harness, matest, drives the same class headlessly on a synthetic clock, and reads every claim out of the rendered picture: areas through six stacked tools kept to 2.1e-4, a drop’s displacement within 2.8e-4 px of the closed form, a comb drawn back restoring every pixel to 9.8e-4 px, and 20 wrong models and 27 one-character mutants caught. That harness, not this page, is the reason to believe the ink. The patterns’ depths and the drops’ sizes are chosen; no source gives them as fractions of a tray.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Marbling',
  pluginId: 'MA01',
  kind: 'effect',
  tagline:
    'Paper marbling for Resolume: your clip’s colours dropped as ink on water and combed into the patterns marblers make — stone, suminagashi, gelgit, chevron, nonpareil, bouquet, French curl — one step on each drum hit, beat or press. Every stroke is a closed-form map of the water onto itself that moves ink and never thins it, so no ink is mixed or blurred and every edge stays sharp however many strokes cross it.',
  repo: REPO,

  blurb:
    'It is the plugin’s own C++ — the plugin class, the op list and the patterns’ recipes, its audio input and earshot under it — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: the audio is the plugin’s harness’s own track, or a file of yours, fed through earshot’s feed() as a device’s samples would be, never a microphone and never played aloud, so Audio Input, the live-device path and Onsets on Resolume’s FFT are not here; the beat is a tempo the page sends, not Resolume’s bar; the clip is a generated card. This page is checked against the plugin’s harness, not against Resolume.',

  // RGBA32F render targets: the palette and the backward maps.
  needFloat: true,
  // Suminagashi's clear drops are alpha 0, and Mix below 1 over them is
  // partly transparent: show what is under.
  showBackdrop: true,

  // All opaque: the kit's 'alpha' clip is left out because the kit's
  // premultiplied colour is premultiplied again by the plugin (see the notes
  // at the foot).
  sources: ['card', 'scene', 'bars', 'grid', 'detail', 'spot', 'ramp'],

  params: PARAMS,
  presets: kitPresets(),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The buttons, the host row and the lines under the picture.
//===========================================================================

if (mounted) {
  const params = mounted.params;

  // Copy link carries the host row (a file cannot travel in a link).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    for (const p of EVENTS) q.delete(p.id);
    if (audio.id !== 'track' && audio.id !== 'file') q.set('audio', audio.id);
    if (host.bpm !== 120) q.set('bpm', String(host.bpm));
    return q;
  };

  if (!embed) {
    // The kit's note says every declared parameter is here; three are not.
    const note = document.querySelector('.inspector__note');
    if (note) {
      note.textContent += ' Left out here: Audio Input (the device menu: no audio device exists in the browser), the Audio buffer (Resolume’s FFT; the host row under the picture stands in for both) and the About block. Trigger and Paper are the plugin’s event controls: buttons that send the press and the release. Pattern and the drops’ and comb’s controls act on the next trigger, as in the plugin: what is already on the water stays as it was.';
    }

    // FF_TYPE_EVENT: a button that sends the press and the release, then a
    // redraw (paused, the frame is drawn again at the same time). (fountain's.)
    const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
      ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);
    for (const p of EVENTS) {
      const row = rowOf(p);
      const toggle = row?.querySelector('.prow__toggle');
      if (!toggle) continue;
      const press = document.createElement('button');
      press.type = 'button';
      press.className = 'prow__toggle';
      press.id = `ma-${p.id}`;
      press.style.textAlign = 'center';
      press.textContent = p.name;
      press.title = p.hint ?? '';
      press.addEventListener('click', () => {
        renderer?.press(p.name);
        mounted.redraw();
      });
      toggle.replaceWith(press);
    }

    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, control, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the control with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  control.style.maxWidth = '100%';
  control.style.minWidth = '0';
  wrap.append(span, control);
  return wrap;
}

/** Restart the page's clock: a new instance from time 0, as Restart does. */
function restartClock() {
  audio.epoch += 1;
  mounted.state.time = 0;
  mounted.state.frameIndex = 0;
  mounted.redraw();
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'marbling-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const select = document.createElement('select');
  select.className = 'transport__select';
  select.id = 'marbling-audio';
  for (const a of AUDIO) {
    const option = new Option(a.name, a.id);
    option.title = a.hint;
    if (a.id === 'file') option.disabled = true;
    select.append(option);
  }
  select.value = audio.id;
  select.addEventListener('change', () => {
    audio.id = select.value;
    restartClock();
  });

  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  tempo.id = 'marbling-bpm';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.bpm = Number(tempo.value);
    mounted.redraw();
  });

  const pick = document.createElement('label');
  pick.className = 'btn transport__file';
  pick.id = 'marbling-audio-file-label';
  pick.textContent = 'Audio file…';
  pick.title = 'Choose a sound file on this machine. The browser decodes it here and the page feeds it to the plugin through feed(); it is not uploaded and not played aloud.';
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = 'audio/*';
  input.id = 'marbling-audio-file';
  input.hidden = true;
  pick.append(input);
  const note = document.createElement('span');
  note.className = 'transport__label';
  note.id = 'marbling-audio-note';
  note.setAttribute('role', 'status');
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    note.textContent = `Decoding ${file.name}…`;
    try {
      audio.file = await decodeFile(file);
      const fileOption = [...select.options].find((o) => o.value === 'file');
      fileOption.disabled = false;
      fileOption.textContent = `Your file: ${file.name}`;
      audio.id = 'file';
      select.value = 'file';
      note.textContent = `${file.name}: ${audio.file.seconds.toFixed(1)} s, ${audio.file.channels === 1 ? 'mono, on both channels' : `${audio.file.channels} channels, the first two`}, at 48 kHz, looped. It stays in this page.`;
      restartClock();
    } catch (error) {
      note.textContent = `That file could not be decoded: ${error.message ?? error}`;
    }
  });

  row.append(
    tag,
    field('Audio', select, 'What reaches the plugin in place of an audio input, for Onsets. The plugin’s Audio Input menu and the live-device path are not in the browser: the page feeds earshot’s feed() as the plugin’s harness does. Nothing is played aloud. Changing it restarts the clock (a new instance, a fresh sheet).'),
    field('Tempo', tempo, 'The host’s bar for Beat: SetBeatInfo every frame, as matest --beat sends it. Each quarter of the bar is a trigger under Beat.'),
    pick,
    note,
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: the plugin's own state, through the accessors
// its harness reads. They report; they measure nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status ma-status';
  line.id = 'marbling-status';
  line.setAttribute('aria-live', 'off');
  const log = document.createElement('p');
  log.className = 'stage__status ma-log';
  log.id = 'marbling-log';
  log.setAttribute('aria-live', 'off');
  stage.append(line, log);

  const params = mounted.params;
  const patternOf = () => PARAMS.find((p) => p.name === 'Pattern').elements[Math.round(params.get('pattern'))];
  const triggerOn = () => PARAMS.find((p) => p.name === 'Trigger On').elements[Math.round(params.get('triggeron'))];

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    let where;
    if (s.stage === 'drops') where = `the drops (${s.bathTriggers} trigger${s.bathTriggers === 1 ? '' : 's'} on this sheet)`;
    else if (s.stage === 'strokes') where = `the strokes, ${s.strokeStep} of ${s.recipe} done`;
    else where = s.still ? 'finished and still' : 'finished, the last stroke still moving';
    const listening = triggerOn() === 'Onsets' ? '' : `; not listened to under ${triggerOn()}`;
    const feedText = s.source === 'silence'
      ? `nothing fed (earshot ${s.state}${listening})`
      : `${s.source === 'file' ? 'your file' : 'the music'} through feed(), ${(s.fed / FEED_RATE).toFixed(2)} s so far (earshot ${s.state}${listening})`;
    const next = `${patternOf()} on ${triggerOn()}${triggerOn() === 'Beat' ? ` at ${s.bpm} BPM` : ''}: ${where}`
      + ` · ${s.ops} op${s.ops === 1 ? '' : 's'} traced per pixel, ${s.bakes} baked into the map since this instance started`
      + ` · ${s.triggers} trigger${s.triggers === 1 ? '' : 's'} and ${s.sheets} fresh sheet${s.sheets === 1 ? '' : 's'}`
      + ` · ${feedText}`
      + ` · the plugin’s clock at ${s.clock.toFixed(2)} s`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) marbling: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against matest reads the
// declarations, the host values and the frames through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, hostValue, kitValues, audio, host, restartClock };
