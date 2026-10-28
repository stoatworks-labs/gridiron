"""The page's list of the demo logo pack must be the folder.

    python3 demo/tools/check_pack.py     exit 1 if they disagree

Called from `tools/verify.sh`.

demo/logos is two things at once: the folder the release copies into the
download as "demo logos", and the page's default wall. A static host has no
directory listing, so demo/plugin.js names the files in DEMO_PACK, and a list
kept beside a folder drifts from it. A logo added to the pack and not to the
list is missing from the page's wall without a word; one removed from the pack
and left in the list is a 404 that empties the page's About line into an
error. This compares the two, in the plugin's own order (sorted by name, byte
for byte, as ScanFolder sorts), and fails on any difference.

It also fails if anything in demo/logos is something the plugin would not
read (an extension it does not take): that file would ship in the download
and be silently ignored by both. Dotfiles are left out on both sides, as the
plugin leaves them out, and the release's `cp demo/logos/*` does not match
them -- so a .DS_Store that Finder drops in a checkout is not a failure.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
EXTENSIONS = {"png", "jpg", "jpeg", "gif", "bmp", "tga", "psd", "svg"}


def main():
    folder = sorted((n for n in os.listdir(os.path.join(REPO, "demo", "logos")) if not n.startswith(".")),
                    key=lambda n: n.encode())
    with open(os.path.join(REPO, "demo", "plugin.js")) as handle:
        js = handle.read()
    match = re.search(r"^const DEMO_PACK = \[\n(.*?)^\];$", js, re.S | re.M)
    if match is None:
        print("FAIL  DEMO_PACK not found in demo/plugin.js")
        return 1
    listed = re.findall(r"^\s*'([^']+)',$", match.group(1), re.M)

    problems = 0
    for name in folder:
        if name.rsplit(".", 1)[-1].lower() not in EXTENSIONS:
            print(f"FAIL  demo/logos/{name} is not something the plugin reads")
            problems += 1
    if listed != folder:
        problems += 1
        missing = [n for n in folder if n not in listed]
        extra = [n for n in listed if n not in folder]
        print("FAIL  DEMO_PACK in demo/plugin.js is not demo/logos, sorted by name")
        if missing:
            print(f"        in the folder, not the list: {', '.join(missing)}")
        if extra:
            print(f"        in the list, not the folder: {', '.join(extra)}")
        if not missing and not extra:
            print("        same files, different order (it must be the byte order ScanFolder sorts by)")
    if problems:
        return 1
    print(f"ok    DEMO_PACK lists the {len(folder)} files in demo/logos, in the plugin's order")
    return 0


if __name__ == "__main__":
    sys.exit(main())
