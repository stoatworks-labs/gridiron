"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the four GLSL bodies of `source/Shaders.cpp`: the wall
pass's vertex and fragment shaders and the composite pass's pair. That is two
copies of the same text, and two copies drift -- quietly, because a wall drawn
by a *plausible* shader looks exactly like one drawn by the right one. The whole
claim of the page is that it runs the plugin's own shaders, so the claim needs
something enforcing it. `gdtest_render` drives the real plugin class and has
never heard of this page, and verify.sh's glslc step compiles the C++ copies
and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each body out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. Each gridiron shader is one raw string that
carries its own `#version 410 core`, so there is no assembly beyond pairing a
vertex stage with a fragment stage; that pairing is checked too, on both sides
(Render.cpp's two Compile calls, plugin.js's two Programs).

It fails when Shaders.cpp grows a body this script does not list (a pass the
page would silently lack).

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write escapes one as \\`. The wall
fragment shader has two, in a comment. This undoes that escape and REJECTS any
other backslash or any `${` on the JS side; there are none in the C++ bodies,
so either could only be somebody hiding a difference.

`--write` produces shaders.js from the C++ by exactly that escape, so the copy
is spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half. The folder rules and trim (Library.cpp),
the atlas plan (AtlasPlan.cpp), the arrangement (Fill.cpp), the layout
(Layout.cpp, Mat4.h, Hash.h), the mappings (Controls.h) and the CPU half of
GridironPlugin::ProcessOpenGL are a hand translation in demo/plugin.js.
demo/tools/crosscheck/, which verify.sh also runs, holds the atlas plan, Fill
and Layout to the C++ number for number. When you change one of those, change the port too.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there.
SHADERS = [
    ("WALL_VERTEX", "kWallVertexShader"),
    ("WALL_FRAGMENT", "kWallFragmentShader"),
    ("COMPOSITE_VERTEX", "kCompositeVertexShader"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragmentShader"),
]

# Which vertex stage goes with which fragment stage, on each side. Each entry is
# (file, text that must appear in it verbatim). A change to either is a change
# to what runs.
ASSEMBLY = [
    ("source/Render.cpp", "mWall.Compile( kWallVertexShader, kWallFragmentShader )"),
    ("source/Render.cpp", "mComposite.Compile( kCompositeVertexShader, kCompositeFragmentShader )"),
    ("demo/plugin.js", "new Program(gl, S.WALL_VERTEX, S.WALL_FRAGMENT, 'wall', { attribs: {} })"),
    ("demo/plugin.js", "new Program(gl, S.COMPOSITE_VERTEX, S.COMPOSITE_FRAGMENT, 'composite', { attribs: {} })"),
]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def from_cpp(source, symbol):
    """The body of one `const char* const kName = R"( ... )";`, or None."""
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def cpp_pieces(source):
    """Every raw-string body Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


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


def write(cpp):
    out = [HEADER]
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check(cpp, js):
    problems = 0

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

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
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    for path, text in ASSEMBLY:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    pairing: {path} still says {text[:64]}")
        else:
            print(f"FAIL  pairing: {path} no longer says: {text}")
            problems += 1
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); a pairing or unlisted-shader failure needs a reader")
        return 1
    print(f"all {len(SHADERS)} shaders and their pairing are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
