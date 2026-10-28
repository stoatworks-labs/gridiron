#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, against the plugin's C++.
#
#     demo/tools/crosscheck/crosscheck.sh                 the port; exit 1 on a difference
#     demo/tools/crosscheck/crosscheck.sh --frames [DIR]  and the pictures, reported
#
# 1. THE PORT (run by tools/verify.sh). printer.cpp drives the plugin's own
#    Fill.cpp, Layout.cpp and Controls.h -- GL-free -- through scenario.mjs:
#    seven folders of logos (the demo pack's shapes among them, animated GIFs
#    among them), every grid from 1x1 to 32x32, all five modes, every fit,
#    walk, edge scope, hero and fill rule, at times out to a thousand seconds
#    and outputs from portrait to wide; and AtlasPlan.cpp's layers and logo
#    geometry for 25 folders against a range of GPU limits and the frame
#    budget. run.mjs puts demo/port.js through the same. Everything integer
#    must be identical -- every plan's layers and note, every schedule, every
#    cell's logo, span, hero, edge and frame -- and every float within a
#    millionth (relative, or absolute below 1). The floats are not required to
#    be identical because JavaScript's Math.sin, cos, tan and pow are V8's and
#    the plugin's sinf, cosf, tanf and powf are Apple's libm, which differ in
#    the last bit for some arguments.
#
#    The C++ is built -ffp-contract=off, the x86_64 slice's arithmetic: Apple
#    clang fuses a*b+c on arm64, which JavaScript cannot, so the plugin's two
#    slices round differently from each other. That build is compared too,
#    within a hundredth. Up to v0.2.3 the two slices disagreed by a whole unit
#    on the cube -- a wordmark across two cells had its centre exactly on a
#    slice boundary and each build rounded it to a different slice -- and a
#    hundredth is what holds that fixed: a cell on the wrong slice is off by at
#    least a cell. What still differs is Twinkle's brightness and Fade's
#    opacity, sines of arguments in the thousands by a thousand seconds, by up
#    to 0.004.
#
# 2. THE FRAMES (--frames; needs Google Chrome). frames.mm drives the REAL
#    plugin class -- stb_image, nanosvg, the atlas, the shaders -- in a headless
#    GL 4.1 context on the demo pack, and frames.mjs drives the page in
#    headless Chrome through the same nineteen settings and times. Reported,
#    not required: they are not the same decoders or the same GL, so they are
#    not expected to be the same pixels. compare_frames.py prints, per frame,
#    how far the two agree on where the wall is and by how much the pixels
#    differ, and writes side-by-side PNGs (plugin left, page right) to DIR.
#    The page is served from demo/ on 127.0.0.1:${PORT:-8716}.
#
# Needs node, a C++ compiler and the FFGL submodule; --frames also numpy and
# Pillow. Writes nothing in the repository.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }

tmp="$(mktemp -d)"
server=""
cleanup() {
	[[ -n "$server" ]] && kill "$server" 2>/dev/null || true
	rm -rf "$tmp"
}
trap cleanup EXIT

#---------------------------------------------------------------------------
# 1. The port.
#---------------------------------------------------------------------------
# AtlasPlan needs Library's Image, and Image needs Library.cpp and SvgLib.cpp
# (stb_image and nanosvg) to link, though planning decodes nothing.
port_sources=( "$REPO/source/Fill.cpp" "$REPO/source/Layout.cpp" "$REPO/source/AtlasPlan.cpp"
               "$REPO/source/Library.cpp" "$REPO/source/SvgLib.cpp" )
port_includes=( -I "$REPO/source" -I "$REPO/external/stb" -I "$REPO/external/nanosvg" )
"$CXX" -std=c++17 -O2 -ffp-contract=off -w "${port_includes[@]}" "$HERE/printer.cpp" "${port_sources[@]}" -o "$tmp/printer-strict"
"$CXX" -std=c++17 -O2 -w "${port_includes[@]}" "$HERE/printer.cpp" "${port_sources[@]}" -o "$tmp/printer-native"

node "$HERE/scenario.mjs" > "$tmp/scenario.txt"
"$tmp/printer-strict" < "$tmp/scenario.txt" > "$tmp/cpp.txt"
"$tmp/printer-native" < "$tmp/scenario.txt" > "$tmp/cpp-native.txt"
node "$HERE/run.mjs" < "$tmp/scenario.txt" > "$tmp/js.txt"

if ! out=$( python3 "$HERE/compare.py" "$tmp/cpp.txt" "$tmp/js.txt" --tolerance 1e-6 ); then
	echo "FAIL the JavaScript port differs from the plugin's Fill/Layout/Controls (contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the port against the plugin's C++ (contraction off, the x86_64 slice's arithmetic): $out"

if ! out=$( python3 "$HERE/compare.py" "$tmp/cpp-native.txt" "$tmp/js.txt" --tolerance 1e-2 ); then
	echo "FAIL the plugin as built for arm64 differs from the x86_64 arithmetic by more than a hundredth"
	echo "     (a cube cell turning with a different slice on one build than on the other?):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the port against the plugin as built for arm64 (fused multiply-adds), within a hundredth: $out"

[[ "${1:-}" == "--frames" ]] || exit 0

#---------------------------------------------------------------------------
# 2. The frames.
#---------------------------------------------------------------------------
out_dir="${2:-$tmp/frames}"
mkdir -p "$out_dir"
[[ -f "$REPO/external/ffgl/CMakeLists.txt" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
plugin_sources=()
for s in Clock SvgLib Diag Library Fill Layout AtlasPlan Atlas Shaders Render Gridiron; do plugin_sources+=( "$REPO/source/$s.cpp" ); done
"$CXX" -std=c++17 -O2 -DGL_SILENCE_DEPRECATION -DGRIDIRON_VERSION="\"$version\"" \
	-I "$REPO/source" -I "$REPO/external/stb" -I "$REPO/external/nanosvg" -I "$REPO/external/ffgl/source/lib" \
	-x objective-c++ "$HERE/frames.mm" -x c++ "${plugin_sources[@]}" "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" \
	-framework OpenGL -framework AppKit -w -o "$tmp/frames"

python3 -m http.server "${PORT:-8716}" --bind 127.0.0.1 --directory "$REPO/demo" >/dev/null 2>&1 &
server=$!
sleep 1

node "$HERE/frames.mjs" "http://127.0.0.1:${PORT:-8716}/" "$out_dir"
# HOME moved so the plugin's log (Diag.cpp) goes to the scratch folder, not
# to ~/Library/Logs/gridiron where a real session's would be.
mkdir -p "$tmp/home"
HOME="$tmp/home" "$tmp/frames" "$REPO/demo/logos" geometry > "$out_dir/plugin-geometry.txt"
HOME="$tmp/home" "$tmp/frames" "$REPO/demo/logos" 960 540 < "$out_dir/frames.txt" > "$out_dir/plugin.rgba"
python3 "$HERE/compare_frames.py" "$out_dir" --png
echo "     side by side, plugin left and page right: $out_dir/frame-NN.png"
