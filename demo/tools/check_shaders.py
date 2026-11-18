"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.
(woofer's script, which is mirrorball's, pinhole's and fountain's, with
carousel's port check, and marbling's pieces, its five-stage assembly, its
`uniform()` helper, matest's music and earshot's sources.)

------------------------------------------------------------------- why

The page at marbling-demo.stoatworks-labs.com carries copies of this
repository, and copies drift quietly: a comb whose tines are a step off, or a
track whose hats moved, still looks like marbling.

1. **The GLSL.** `demo/shaders.js` holds the six raw-string pieces of
   `source/Shaders.cpp` (kQuadVertex, kCommon and the four passes' fragments)
   plus kVersion, AND the order the plugin joins them in: Marbling.cpp's
   InitGL compiles `kVersion + kQuadVertex` as every program's vertex stage
   and Shaders.cpp's `Assemble( pass )` -- kVersion + kCommon + the pass --
   as the fragment stage of each of its four programs (bake, resize, palette,
   display). This script reads that order out of the C++ and writes it into
   shaders.js as `ASSEMBLY`. The page builds its expected text for every
   stage from that table, and at run time REQUIRES the text the compiled
   plugin hands to glShaderSource to equal one of them before the kit's
   port() is applied (demo/wasm/gl_shim.cpp). The pieces are compared exactly
   -- no whitespace normalisation, no comment stripping: a comment updated on
   one side only is drift worth catching. Every `const char* const k... =
   R"(` piece in Shaders.cpp must be one this script knows, and shaders.js
   must be byte for byte what `--write` produces, so nothing else can be
   typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **What the browser compiles instead: the port.** WebGL2 compiles GLSL ES
   3.00, and the demo kit's `port()` (demo/vendor/gl.js, copied unedited from
   stoatworks-backend/resolume-demo/kit) turns each stage into it at load
   time. This script holds that transform to exactly three things on THIS
   plugin's stages and checks that they are the only difference:

     - `#version 410 core` becomes `#version 300 es` (port()'s pattern also
       takes the blank line after it: it eats whitespace to a newline);
     - the ES default precisions (highp float, int and the samplers) are put
       in front;
     - `precise ` is dropped from exactly the declarations PRECISE lists:
       `k` and `y` in kCommon's sinSeries, the range reduction, in every
       fragment stage. ES 3.00 has no `precise`. It stops a compiler fusing or
       reassociating `x - k * ( 2.0 * kPi )`, so the browser may round the
       reduced argument differently from the Mac -- by about |x| 6e-8, the
       series' own budget -- and the page and AGENTS.md say so. A `precise`
       added anywhere else fails here until it is listed, and said.

   port() also sizes `float[](`; marbling's GLSL has none. The transform is
   mirrored here in Python (`port_mirror`), and the mirror is tied to the real
   thing by the hash of port()'s text in the vendored gl.js (PORT_SHA256): a
   kit that changes port() fails this check until the mirror is re-read
   against it. The page checks the same thing at run time on the port's
   actual output (plugin.js, `shaderSource`), and refuses a stage that
   differs.

3. **Every uniform's setter.** The page runs the plugin's own glUniform calls,
   and WebGL2 is stricter about them than a desktop driver is allowed to be
   lenient: a float sent to an `int` or a sampler is INVALID_OPERATION and the
   uniform stays 0, silently, and a sampler nobody sets sits on unit 0.
   Marbling.cpp sets uniforms as `glUniformNx( where( shader, "Name" ), ... )`
   or through its two `uniform( shader, "Name", v )` overloads (int ->
   glUniform1i, float -> glUniform1f). Which overload a call takes is C++'s
   overload resolution, which this script does not do: UNIFORM_ARGS lists
   each such call's argument text and the overload it resolves to, read by
   hand, and an argument whose text changes fails here until it is read
   again. Each uniform the stages declare must be set somewhere, each call
   must set a declared uniform, and each by the setter its GLSL type takes.

4. **matest's music.** The page feeds the plugin matest's own track through
   earshot's feed(). demo/wasm/glue.cpp carries tools/matest/main.cpp's
   `musicAt`, character for character, and Rig::Render's lines that feed it
   (HARNESS_LINES, compared with their indentation stripped; the glue's
   `plugin` and `fedUntil` are references to its instance's).

5. **No device.** No file the page serves may name getUserMedia,
   enumerateDevices or mediaDevices: the page never asks for a microphone or
   lists an audio device, and this keeps it so.

6. **The WebAssembly.** `demo/marbling-core.wasm` is the plugin's C++ and
   earshot's compiled by `demo/tools/build-wasm.sh`, and committed, because
   the deploy has no build step. Building it needs emscripten, which verify.sh
   cannot assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the seven plugin .cpp files, earshot's sources and headers, the
   SDK files, the glue, the shim and the build script), of both outputs, and
   the SDK commit. A source file changed since the last build -- a regenerated
   StoatworksAbout.h or a new earshot snapshot included -- or an output that
   is not the one the build wrote fails here, and the fix is to rerun
   build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js beyond rule 5: the
panel is built from the plugin's own declarations at run time, but its read-out
units, its tooltips, its presets and its layout are the page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("BAKE_FRAGMENT", "kBakeFragment"),
    ("RESIZE_FRAGMENT", "kResizeFragment"),
    ("PALETTE_FRAGMENT", "kPaletteFragment"),
    ("DISPLAY_FRAGMENT", "kDisplayFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# The four programs InitGL compiles, as its `stages[]` table names them, and
# the Pass each assembles.
PROGRAMS = [("bake", "Bake"), ("resize", "Resize"), ("palette", "Palette"), ("display", "Display")]

# The variables each stage declares `precise`, which port() strips. Listed by
# hand on purpose: a new one is a new guarantee the browser drops, and has to
# be added here, to AGENTS.md ("Browser demo") and to the page's disclosure
# together. kCommon's sinSeries declares k and y, in every fragment stage.
PRECISE = {f"{name}_fragment": ["k", "y"] for name, _ in PROGRAMS}

# sha256 of `export function port(...) { ... }` in demo/vendor/gl.js, from its
# first line to the first `}` at column 0. port_mirror() below was written
# against this text (carousel's, the same kit).
PORT_SHA256 = "f104076947f5cd791a38857e82491be22efae881ae3d61d1b0111ee457b25c8d"

PRECISION = {
    "fragment": "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                "precision highp usampler2D;\nprecision highp isampler2D;\n"
                "precision highp sampler2DArray;\nprecision highp usampler2DArray;\n"
                "precision highp isampler2DArray;\n"
                "precision highp sampler3D;\nprecision highp usampler3D;\n"
                "precision highp isampler3D;\n",
    "vertex": "precision highp float;\nprecision highp int;\n",
}

# The setters each GLSL uniform type takes.
SETTER = {
    "sampler2D": {"glUniform1i"},
    "int": {"glUniform1i"},
    "bool": {"glUniform1i"},
    "ivec2": {"glUniform2i"},
    "ivec4": {"glUniform4i"},
    "float": {"glUniform1f"},
    "vec2": {"glUniform2f", "glUniform2fv"},
    "vec3": {"glUniform3f", "glUniform3fv"},
    "vec4": {"glUniform4f", "glUniform4fv"},
}

# Marbling.cpp's `uniform( shader, "Name", <argument> )` calls: the argument's
# text, and the overload C++ picks for it, read by hand (`hooks`, `count` and
# the literals are int; the two casts and the clamp of a float are float).
UNIFORM_ARGS = {
    ("Hooks", "hooks"): "glUniform1i",
    ("InputTexture", "0"): "glUniform1i",
    ("Palette", "1"): "glUniform1i",
    ("Map", "2"): "glUniform1i",
    ("MapIdentity", "bath.MapIsIdentity() ? 1 : 0"): "glUniform1i",
    ("OpCount", "count"): "glUniform1i",
    ("DropClear", "o.clear ? 1 : 0"): "glUniform1i",
    ("BakeLast", "count - 1"): "glUniform1i",
    ("Rescale", "static_cast< float >( static_cast< double >( height ) / static_cast< double >( mapH ) )"): "glUniform1f",
    ("MixAmount", "std::clamp( params[ PT_MIX ], 0.0f, 1.0f )"): "glUniform1f",
}
# ...and the two overloads, as Marbling.cpp defines them.
OVERLOADS = [
    "void MarblingPlugin::uniform( const FFGLShader& shader, const char* name, int v )\n{\n\tglUniform1i( where( shader, name ), v );\n}",
    "void MarblingPlugin::uniform( const FFGLShader& shader, const char* name, float v )\n{\n\tglUniform1f( where( shader, name ), v );\n}",
]

# matest's music, copied into the glue.
HARNESS = ["musicAt"]
HARNESS_LINES = [
    "constexpr int kRate = 48000;",
    # Rig::Render's feed.
    "//Every sample from the last frame's time to this one, once.",
    "const int from = static_cast< int >( std::llround( fedUntil * kRate ) );",
    "const int to   = static_cast< int >( std::llround( t * kRate ) );",
    "if( to > from )",
    "std::vector< float > pcm( static_cast< size_t >( to - from ) * 2 );",
    "for( int k = from; k < to; ++k )",
    "pcm[ static_cast< size_t >( k - from ) * 2 ] = pcm[ static_cast< size_t >( k - from ) * 2 + 1 ] =",
    "static_cast< float >( audio( static_cast< double >( k ) / kRate ) );",
    "plugin.AudioForTest().Feed( pcm.data(), to - from, kRate, 2 );",
    "fedUntil = t;",
]

# What a page that never touches a device must never name.
DEVICE_APIS = ("getUserMedia", "enumerateDevices", "mediaDevices")

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/marbling-core.js", "demo/marbling-core.wasm")
REGISTRATION = ("EffectPlugin.cpp",)
# earshot's device backend, which build-wasm.sh leaves out (EARSHOT_CAPTURE=OFF).
NOT_COMPILED = ("Miniaudio.cpp", "MiniaudioImpl.cpp")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)\\n";', source)
    return None if match is None else match.group(1) + "\n"


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def pieces_of(expression, where):
    """`std::string( kVersion ) + kA + kB` -> [kVersion, kA, kB]."""
    pieces = []
    for term in (t.strip() for t in expression.split("+")):
        m = re.fullmatch(r"(?:std::string\(\s*)?(?:shaders::)?(k\w+)(?:\s*\))?", term)
        if m is None:
            raise Stale(f"{where} joins `{term}`, which is not a piece")
        pieces.append(m.group(1))
    return pieces


def assembly(cpp, plugin_cpp):
    """[(stage name, [JS pieces])]: the vertex stage from InitGL's `vertex`,
    each program's fragment from Shaders.cpp's Assemble(), checked against
    InitGL's stages[] table, which compiles exactly those four programs."""
    vertex = re.search(r"const std::string vertex = (.*?);", plugin_cpp)
    if vertex is None:
        raise Stale("Marbling.cpp's InitGL no longer builds `const std::string vertex = ...;`")
    for needed in ("const std::string fragment = shaders::Assemble( stage.pass );",
                   "stage.shader->Compile( vertex.c_str(), fragment.c_str() )"):
        if needed not in plugin_cpp:
            raise Stale(f"Marbling.cpp's InitGL no longer has `{needed}`")
    if len(re.findall(r"(?:\.|->)Compile\(", plugin_cpp)) != 1:
        raise Stale("Marbling.cpp calls Compile more than once; this script reads one loop over stages[]")
    table = re.search(r"const Stage stages\[\] = \{(.*?)\n\t\};", plugin_cpp, re.S)
    if table is None:
        raise Stale("no `const Stage stages[] = { ... };` in Marbling.cpp's InitGL")
    rows = re.findall(r"\{ &(\w+)Shader, shaders::Pass::(\w+), \"(\w+)\" \}", table.group(1))
    if [(name, p) for _, p, name in rows] != PROGRAMS or any(s != n for s, _, n in rows):
        raise Stale(f"InitGL's stages[] is {rows}, not this script's {PROGRAMS}")

    body = re.search(r"std::string Assemble\( Pass pass \)\s*\{(.*?)\n\}", cpp, re.S)
    if body is None:
        raise Stale("no std::string Assemble( Pass pass ) in source/Shaders.cpp")
    head = re.fullmatch(
        r"\s*const std::string text = (.*?);\s*switch\( pass \)\s*\{(.*?)\}\s*return text;\s*", body.group(1), re.S)
    if head is None:
        raise Stale("Assemble() is not `const std::string text = ...; switch( pass ) { ... } return text;`")
    common = pieces_of(head.group(1), "Assemble()'s text")
    cases = re.findall(r"case Pass::(\w+): return text \+ (k\w+);", head.group(2))
    if len(cases) != len(re.findall(r"\bcase\b", head.group(2))):
        raise Stale("an Assemble() case is not `case Pass::X: return text + kPiece;`")
    by_pass = dict(cases)

    out = []
    symbols = pieces_of(vertex.group(1), "InitGL's vertex")
    for symbol in symbols:
        if symbol not in JS_NAME:
            raise Stale(f"InitGL's vertex uses {symbol}, a piece this script does not copy")
    out.append(("marbling_vertex", [JS_NAME[s] for s in symbols]))
    for name, pass_ in PROGRAMS:
        if pass_ not in by_pass:
            raise Stale(f"Assemble() has no case for Pass::{pass_}")
        symbols = common + [by_pass[pass_]]
        for symbol in symbols:
            if symbol not in JS_NAME:
                raise Stale(f"Assemble( Pass::{pass_} ) uses {symbol}, a piece this script does not copy")
        out.append((f"{name}_fragment", [JS_NAME[s] for s in symbols]))
    if set(by_pass) != set(p for _, p in PROGRAMS):
        raise Stale(f"Assemble() has cases {sorted(by_pass)}; InitGL compiles {[p for _, p in PROGRAMS]}")
    return out


def render(cpp, plugin_cpp):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version[:-1]}\\n';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// Marbling.cpp's InitGL (the vertex stage of every program) and\n"
        "// Shaders.cpp's Assemble() (each program's fragment stage).\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in assembly(cpp, plugin_cpp):
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    out.append(
        "\n// The declarations each stage marks `precise`, which the kit's port()\n"
        "// strips (GLSL ES 3.00 has no `precise`): check_shaders.py's PRECISE.\n"
        "export const PRECISE = {\n"
    )
    for name, _ in assembly(cpp, plugin_cpp):
        out.append(f"  {name}: [{', '.join(repr(v) for v in PRECISE.get(name, []))}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    match = re.search(r"^export const VERSION = '(.*?)\\n';$", js, re.M)
    return None if match is None else match.group(1) + "\n"


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, plugin_cpp, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<24} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<24} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in table:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<24} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in table)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1

    try:
        expected = render(cpp, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The port.
#---------------------------------------------------------------------------

def port_text(gl_js):
    """port()'s own text in the vendored kit: `export function port(` to the
    first `}` at column 0."""
    match = re.search(r"^export function port\(.*?^\}$", gl_js, re.S | re.M)
    return None if match is None else match.group(0) + "\n"


def port_mirror(source, stage):
    """demo/vendor/gl.js's port(), in Python, for the text PORT_SHA256 names:
    the version line out, `precise ` out, `float[](` sized from its
    declaration, `#version 300 es` and the stage's precisions in front."""
    out = re.sub(r"^\s*#version\s+410\s+core\s*\n", "", source, count=1)
    out = re.sub(r"\bprecise\s+", "", out)

    def sized(match):
        decl = re.search(r"\[\s*(\d+)\s*\]\s*=\s*$", out[: match.start()])
        return f"float[{decl.group(1)}](" if decl else "float[]("

    out = re.sub(r"\bfloat\s*\[\s*\]\s*\(", sized, out)
    return "#version 300 es\n" + PRECISION[stage] + out


def check_port(cpp, plugin_cpp, gl_js):
    problems = 0
    text = port_text(gl_js)
    if text is None:
        print("FAIL  no `export function port(` in demo/vendor/gl.js")
        return 1
    got = hashlib.sha256(text.encode()).hexdigest()
    if got != PORT_SHA256:
        print(f"FAIL  demo/vendor/gl.js's port() is not the text this script mirrors (sha256 {got[:12]}, want {PORT_SHA256[:12]}):"
              " read the new port(), bring port_mirror() into line with it, and update PORT_SHA256")
        return 1
    print(f"ok    {'port()':<24} is the text port_mirror() follows (sha256 {got[:12]})")

    try:
        table = assembly(cpp, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return 1
    pieces = dict((name, from_cpp(cpp, symbol)) for name, symbol in SHADERS)
    pieces["VERSION"] = cpp_version(cpp) or ""

    for name, parts in table:
        stage = "vertex" if name.endswith("_vertex") else "fragment"
        source = "".join(pieces[p] for p in parts)
        ported = port_mirror(source, stage)
        head = "#version 300 es\n" + PRECISION[stage]
        if not source.startswith("#version 410 core\n"):
            print(f"FAIL  {name} does not open with `#version 410 core`")
            problems += 1
            continue
        if not ported.startswith(head):
            print(f"FAIL  {name}: the port does not open with `#version 300 es` and the precisions")
            problems += 1
            continue
        before = source[len("#version 410 core\n"):].split("\n")
        # port()'s version pattern ends `core\s*\n`, so it takes the blank
        # lines straight after the version line with it (whitespace only).
        blank = 0
        while len(before) > 1 and before[0].strip() == "":
            before.pop(0)
            blank += 1
        after = ported[len(head):].split("\n")
        if len(before) != len(after):
            print(f"FAIL  {name}: the port has {len(after)} lines after its head, the plugin {len(before)}")
            problems += 1
            continue
        stripped = []
        bad = 0
        for i, (b, a) in enumerate(zip(before, after)):
            if a == b:
                continue
            m = re.fullmatch(r"(\s*)precise (float (\w+) = .*)", b)
            if m and a == m.group(1) + m.group(2):
                stripped.append(m.group(3))
                continue
            bad += 1
            print(f"FAIL  {name}: the port changes line {i + 1} beyond the version line, the precisions and `precise`")
            print(f"          plugin: {b}")
            print(f"          port  : {a}")
        want = PRECISE.get(name, [])
        if bad:
            problems += bad
        elif stripped != want:
            print(f"FAIL  {name}: the port drops `precise` from {stripped or 'nothing'}, PRECISE lists {want or 'nothing'}")
            problems += 1
        else:
            dropped = f", `precise` off {' and '.join(stripped)}" if stripped else ""
            print(f"ok    {'port ' + name:<24} the version line (and {blank} blank), the precisions{dropped}: nothing else")
    for name in sorted(set(PRECISE) - set(n for n, _ in table)):
        print(f"FAIL  PRECISE.{name} is no stage of the plugin's")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The uniforms.
#---------------------------------------------------------------------------

def declared_uniforms(text):
    """{name: type} of every `uniform <type> <Name>[ <n> ];` in a stage's source."""
    out = {}
    for kind, name in re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[[^\]]*\])?\s*;", text, re.M):
        out[name] = kind
    return out


def check_uniforms(cpp, plugin_cpp):
    problems = 0
    try:
        table = assembly(cpp, plugin_cpp)
    except Stale:
        return 0  # already reported
    pieces = {name: from_cpp(cpp, symbol) or "" for name, symbol in SHADERS}
    pieces["VERSION"] = cpp_version(cpp) or ""
    declared = {}
    for stage, parts in table:
        for name, kind in declared_uniforms("".join(pieces[p] for p in parts)).items():
            if declared.setdefault(name, kind) != kind:
                print(f"FAIL  uniform {name} is {declared[name]} in one stage and {kind} in {stage}")
                problems += 1

    for overload in OVERLOADS:
        if overload not in plugin_cpp:
            print(f"FAIL  Marbling.cpp no longer defines `{overload.splitlines()[0]}` as this script reads it")
            problems += 1

    calls = re.findall(r"(glUniform\w+)\(\s*where\(\s*\w+,\s*\"(\w+)\"\s*\)", plugin_cpp)
    if len(re.findall(r"\bwhere\(\s*\w+,\s*\"", plugin_cpp)) != len(calls):
        print("FAIL  Marbling.cpp names a uniform through where() that is not `glUniformNx( where( shader, \"Name\" ), ...`")
        problems += 1
    helper = re.findall(r"(?<![\w:])uniform\(\s*\w+,\s*\"(\w+)\",\s*(.*?)\s*\);", plugin_cpp)
    if len(re.findall(r"(?<![\w:])uniform\(\s*\w+,\s*\"", plugin_cpp)) != len(helper):
        print("FAIL  Marbling.cpp calls uniform( shader, \"Name\", ... ) in a shape this script does not read")
        problems += 1
    for name, argument in helper:
        setter = UNIFORM_ARGS.get((name, argument))
        if setter is None:
            print(f"FAIL  uniform( ..., \"{name}\", {argument} ) is not in UNIFORM_ARGS -- read which overload it takes and list it")
            problems += 1
            continue
        calls.append((setter, name))
    for key in sorted(set(UNIFORM_ARGS) - set(helper)):
        print(f"FAIL  UNIFORM_ARGS lists uniform( ..., \"{key[0]}\", {key[1]} ), which Marbling.cpp no longer makes")
        problems += 1

    set_names = {n for _, n in calls}
    for name in sorted(set(declared) - set_names):
        print(f"FAIL  uniform {declared[name]} {name} is never set (a sampler would sit on unit 0)")
        problems += 1
    for setter, name in calls:
        if name not in declared:
            print(f"FAIL  {setter} sets {name}, which no stage declares")
            problems += 1
        elif setter not in SETTER.get(declared[name], set()):
            print(f"FAIL  {name} is {declared[name]} and is set with {setter}, not {' or '.join(sorted(SETTER.get(declared[name], {'?'})))}")
            problems += 1
    if problems == 0:
        print(f"ok    {'UNIFORMS':<24} all {len(declared)} set, each by its type's setter ({len(calls)} calls)")
    return problems


#---------------------------------------------------------------------------
# matest's music, and no device.
#---------------------------------------------------------------------------

def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at the
    start of a line (its return type first) to the first `}` at the start of
    a line after it."""
    match = re.search(r"^[A-Za-z_][\w:<>&* ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/matest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<24} matches tools/matest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/matest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          matest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    matest_lines = {line.strip() for line in main_cpp.splitlines()}
    glue_lines = {line.strip() for line in glue.splitlines()}
    for line in HARNESS_LINES:
        missing = [where for where, lines in (("tools/matest/main.cpp", matest_lines), ("demo/wasm/glue.cpp", glue_lines)) if line not in lines]
        if missing:
            print(f"FAIL  `{line}` is not in {' or '.join(missing)}")
            problems += 1
    if problems == 0:
        print(f"ok    {'HARNESS LINES':<24} all {len(HARNESS_LINES)} in both (kRate, Rig::Render's feed)")
    return problems


def check_no_devices():
    problems = 0
    demo = os.path.join(REPO, "demo")
    served = []
    for root, dirs, files in os.walk(demo):
        rel = os.path.relpath(root, demo)
        if rel.split(os.sep)[0] in ("tools", "wasm"):
            continue  # not served (.assetsignore), and this file names them
        for name in files:
            if name.endswith((".js", ".html")):
                served.append(os.path.join(root, name))
    for path in sorted(served):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for api in DEVICE_APIS:
            if api in text:
                print(f"FAIL  {os.path.relpath(path, REPO)} names {api}: the page must never ask for or list an audio device")
                problems += 1
    if problems == 0:
        print(f"ok    {'NO DEVICES':<24} none of the {len(served)} served scripts and pages names {', '.join(DEVICE_APIS)}")
    return problems


#---------------------------------------------------------------------------
# The WebAssembly.
#---------------------------------------------------------------------------

def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/marbling-core.wasm was built -- rerun demo/tools/build-wasm.sh")
            problems += 1

    # A source file added since the build is an input the build never saw.
    trees = [("source", REGISTRATION), (os.path.join("external", "earshot", "src"), NOT_COMPILED),
             (os.path.join("external", "earshot", "include", "earshot"), ())]
    for tree, skipped in trees:
        for name in sorted(os.listdir(os.path.join(REPO, tree))):
            if name in skipped:
                continue
            rel = f"{tree}/{name}".replace(os.sep, "/")
            if (name.endswith(".h") or name.endswith(".cpp")) and rel not in recorded:
                print(f"FAIL  {rel} is newer than the .wasm's manifest -- rebuild")
                problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
        elif os.stat(os.path.join(REPO, output)).st_mode & 0o111:
            print(f"FAIL  {output} is executable; it is served as a static file (chmod 644)")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<24} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    plugin_cpp = read("source", "Marbling.cpp")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, plugin_cpp)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, plugin_cpp, js)
    problems += check_port(cpp, plugin_cpp, read("demo", "vendor", "gl.js"))
    problems += check_uniforms(cpp, plugin_cpp)
    problems += check_harness(read("tools", "matest", "main.cpp"), read("demo", "wasm", "glue.cpp"))
    problems += check_no_devices()
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces and the order all five stages join them in are the plugin's,"
          " the port changes only the version line, the precisions and the listed `precise`,"
          " every uniform is set by its type's setter, the glue's music is matest's,"
          " no served file names a device API, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
