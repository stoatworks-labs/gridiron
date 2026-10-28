"""The page's frames against the real plugin's: numbers, and pictures to look at.

    python3 compare_frames.py <dir> [--png]

Reads <dir>/plugin.rgba (frames.mm), <dir>/page.rgba (frames.mjs) and
<dir>/frames.json, and prints one line per frame:

  coverage   how far the two agree on WHERE the wall is: of the pixels either
             side lights (alpha > 8/255), the share both light
  mean       mean absolute difference per channel, 0..255, premultiplied
  >32        the share of pixels where some channel differs by more than 32

These are measured and reported, not required. The two are not the same
pipeline and are not expected to be the same pixels: the plugin decodes with
stb_image and draws its SVGs with nanosvg, the page uses the browser's
decoders and SVG renderer; the plugin's mipmaps are Apple's GL driver's, the
page's are ANGLE's (SwiftShader, headless). The PNGs are what settles whether
a difference is one of those or a fault in the port: --png writes
<dir>/frame-NN.png, the plugin on the left and the page on the right.

Needs numpy, and Pillow for --png.
"""
import json
import os
import sys

import numpy as np

W, H = 960, 540


def geometry(folder):
    """The demo pack as each side decodes it: aspect, trim box, trimmed aspect."""
    def read(name):
        rows = {}
        for line in open(os.path.join(folder, name)).read().splitlines():
            if line and not line.startswith("#"):
                parts = line.split()
                rows[parts[0]] = [float(v) for v in parts[1:]]
        return rows
    plugin, page = read("plugin-geometry.txt"), read("page-geometry.txt")
    if list(plugin) != list(page):
        print(f"  the two sides loaded different files, or in a different order: {list(plugin)} / {list(page)}")
        return
    print(f"  the demo pack, decoded: {len(plugin)} logos on both sides, in the same order")
    for name in plugin:
        a, b = plugin[name], page[name]
        box = max(abs(x - y) for x, y in zip(a[1:5], b[1:5]))
        if a[0] != b[0] or box > 0.002 or abs(a[5] - b[5]) > 0.01 * a[5]:
            print(f"    {name:<36} aspect {a[0]:.4f}/{b[0]:.4f}  trimmed {a[5]:.4f}/{b[5]:.4f}  "
                  f"box off by {box:.4f} of the layer  (plugin/page)")
    print("    every other logo: same aspect, trim box within 0.002 of the layer, trimmed aspect within 1%")


def main(argv):
    folder = argv[0]
    frames = json.load(open(os.path.join(folder, "frames.json")))
    size = W * H * 4
    plugin = np.fromfile(os.path.join(folder, "plugin.rgba"), dtype=np.uint8)
    page = np.fromfile(os.path.join(folder, "page.rgba"), dtype=np.uint8)
    if plugin.size != size * len(frames) or page.size != size * len(frames):
        print(f"FAIL expected {len(frames)} frames of {W}x{H}: plugin {plugin.size / size:g}, page {page.size / size:g}")
        return 1

    geometry(folder)

    worst = 0.0
    for i, frame in enumerate(frames):
        a = plugin[i * size:(i + 1) * size].reshape(H, W, 4).astype(np.int16)
        b = page[i * size:(i + 1) * size].reshape(H, W, 4).astype(np.int16)
        lit_a = a[..., 3] > 8
        lit_b = b[..., 3] > 8
        union = np.count_nonzero(lit_a | lit_b)
        both = np.count_nonzero(lit_a & lit_b)
        coverage = both / union if union else 1.0
        diff = np.abs(a - b)
        mean = diff.mean()
        big = np.count_nonzero(diff.max(axis=2) > 32) / (W * H)
        worst = max(worst, mean)
        lit = 100 * np.count_nonzero(lit_a) / (W * H)
        print(f"  {i:2d} {frame['name']:<34} t={frame['time']:<4} plugin lights {lit:5.1f}%  "
              f"coverage {100 * coverage:5.1f}%  mean {mean:5.2f}  >32 {100 * big:5.2f}%")
        if "--png" in argv:
            from PIL import Image
            pair = np.concatenate([a, b], axis=1)[::-1].astype(np.uint8)
            # Over black, as the premultiplied values already are.
            Image.fromarray(pair[..., :3]).save(os.path.join(folder, f"frame-{i:02d}.png"))
    print(f"  worst mean difference {worst:.2f} of 255")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
