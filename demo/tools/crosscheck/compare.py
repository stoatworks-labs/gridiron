"""Compare printer.cpp's lines with run.mjs's.

    python3 compare.py CPP.txt JS.txt [--tolerance T] [--report]

Every token that is not a float -- a line's tag, a logo index, a schedule
cell, a count, the schedule's note -- must be identical. A float (eight hex
digits, the bits of an IEEE single) may differ from the C++ by at most
T * max(1, |value|) (default 0: identical bits). Prints one summary line --
how many floats differ at all, by how many units in the last place at worst,
and by how much -- and, on failure, the first few differences. Exit 1 on
failure.

Why a tolerance exists at all: JavaScript's Math.sin, cos, tan and pow are
V8's, the plugin's sinf, cosf, tanf and powf are Apple's libm, and the two
disagree in the last bit for some arguments. That reaches the cube's and the
camera's matrices and twinkle's brightness. Nothing integer can differ: the
schedule, every cell's logo, hero and edge flags and frame index are compared
exactly.

--report never fails: it counts the floats beyond the tolerance instead, for
a comparison that is measured rather than required (the arm64 build).
"""
import struct
import sys


def ulps(a, b):
    def ordered(h):
        u = int(h, 16)
        return 0x80000000 - (u & 0x7FFFFFFF) if u & 0x80000000 else 0x80000000 + u
    return abs(ordered(a) - ordered(b))


def value(h):
    return struct.unpack(">f", bytes.fromhex(h))[0]


def is_float(token):
    return len(token) == 8 and all(c in "0123456789abcdef" for c in token)


def main(argv):
    tolerance = 0.0
    if "--tolerance" in argv:
        tolerance = float(argv[argv.index("--tolerance") + 1])
    left = open(argv[0]).read().splitlines()
    right = open(argv[1]).read().splitlines()

    problems = []
    if len(left) != len(right):
        problems.append(f"line counts differ: C++ {len(left)}, js {len(right)}")

    floats = differing = worst = 0
    worst_abs = 0.0
    schedules = cells = 0
    for number, (a, b) in enumerate(zip(left, right), start=1):
        if a.startswith("S "):
            schedules += 1
        if a.startswith("C "):
            cells += 1
        ta, tb = a.split(" "), b.split(" ")
        if len(ta) != len(tb) or ta[0] != tb[0]:
            problems.append(f"line {number}: shape differs\n    C++: {a[:160]}\n    js : {b[:160]}")
            continue
        if ta[0] in ("S", "T", "L") and a != b:
            problems.append(f"line {number}: {ta[0]} differs\n    C++: {a[:160]}\n    js : {b[:160]}")
            continue
        for x, y in zip(ta, tb):
            if is_float(x) and is_float(y):
                floats += 1
                if x != y:
                    differing += 1
                    d = ulps(x, y)
                    gap = abs(value(x) - value(y))
                    # In ulps only where that means something: away from zero
                    # and on the same side of it.
                    if abs(value(x)) > 1e-3 and (value(x) > 0) == (value(y) > 0):
                        worst = max(worst, d)
                    worst_abs = max(worst_abs, gap)
                    if gap > tolerance * max(1.0, abs(value(x))):
                        problems.append(f"line {number}: {x} vs {y} ({d} ulps, {gap:.3g} apart)")
            elif x != y:
                problems.append(f"line {number}: {x!r} vs {y!r}\n    C++: {a[:160]}\n    js : {b[:160]}")
                break
        if len(problems) > 12 and "--report" not in argv:
            break

    summary = (f"{len(left)} lines, {schedules} schedules, {cells} cells, {floats:,} floats; "
               f"{differing:,} differ: at most {worst} ulp (values above 0.001) and {worst_abs:.3g} absolute")
    if "--report" in argv:
        beyond = [p for p in problems if " apart)" in p]
        other = [p for p in problems if " apart)" not in p]
        print(summary)
        if beyond:
            print(f"{len(beyond)} float(s) beyond {tolerance:g} on {len({p.split(':')[0] for p in beyond})} line(s), e.g. {beyond[0]}")
        for p in other[:3]:
            print(p)
        return 0
    if problems:
        print(f"FAIL {summary}")
        for p in problems[:12]:
            print(f"  {p}")
        return 1
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
