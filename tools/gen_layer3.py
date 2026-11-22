#!/usr/bin/env python3
# ambmpega.library's tools: libmad's Layer III IMDCT for long blocks
# (third_party/libmad/layer3.c: imdct36 by fastsdct, sdctII and dctIV, and
# III_imdct_l's normal window) as 68k assembler for both builds
# (gen68k.py), src/layer3_68k.s:
#
#   void mpga_imdct36(mad_fixed_t const X[18], mad_fixed_t z[36]);
#     imdct36: the 36 outputs, unwindowed (start and stop blocks, which
#     layer3.c then windows in C);
#   void mpga_imdct_l0(mad_fixed_t const X[18], mad_fixed_t z[36]);
#     imdct36 and the normal window (block type 0) together.
#
# (The alias reduction was tried the same way: 0.9% of a 68020's cycles,
# 1.7% of the 68060 build's instructions; not kept.)
#
# The operations are the C's, in its order, the constants read from it:
# the same samples (make test compares them with the host's).
#
#   gen_layer3.py third_party/libmad/layer3.c > src/layer3_68k.s
#
# (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
# (GPL-2.0-or-later, Underbit Technologies).
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen68k

src = open(sys.argv[1]).read()


def func(name):
    i = src.index("void %s(" % name)
    return src[i:src.index("\n}\n", i)]


def table(text, name, n):
    m = re.search(r"\b%s\[%d\]\s*=\s*\{(.*?)\};" % (re.escape(name), n), text, re.S)
    t = re.sub(r"/\*.*?\*/", "", m.group(1))
    v = [(-1 if sg == "-" else 1) * int(x, 16) for sg, x in re.findall(r"([+-]?)\s*MAD_F\((0x[0-9a-f]+)\)", t)]
    assert len(v) == n, (name, len(v))
    return v


# fastsdct's constants (c6 is negative)
fs = func("fastsdct")
C = {}
for m in re.finditer(r"\b(c\d)\s*=\s*(-?)MAD_F\((0x[0-9a-f]+)\)", fs):
    C[m.group(1)] = (-1 if m.group(2) else 1) * int(m.group(3), 16)
assert len(C) == 7
scale9 = table(func("sdctII"), "scale", 9)
scale18 = table(func("dctIV"), "scale", 18)
window_l = table(src, "window_l", 36)


def fastsdct(p, x):
    """libmad's fastsdct: x[9] -> y[0], y[2], ... y[16] (9 values)"""
    a0 = p.add(x[3], x[5])
    a1 = p.sub(x[3], x[5])
    a2 = p.add(x[6], x[2])
    a3 = p.sub(x[6], x[2])
    a4 = p.add(x[1], x[7])
    a5 = p.sub(x[1], x[7])
    a6 = p.add(x[8], x[0])
    a7 = p.sub(x[8], x[0])
    a8 = p.add(a0, a2)
    a9 = p.sub(a0, a2)
    a10 = p.sub(a0, a6)
    a11 = p.sub(a2, a6)
    a12 = p.add(a8, a6)
    a13 = p.sub(a1, a3)
    a14 = p.add(a13, a7)
    a15 = p.add(a3, a7)
    a16 = p.sub(a1, a7)
    a17 = p.add(a1, a3)
    m0 = p.mul(a17, -C["c3"])
    m1 = p.mul(a16, -C["c0"])
    m2 = p.mul(a15, -C["c4"])
    m3 = p.mul(a14, -C["c1"])
    m4 = p.mul(a5, -C["c1"])
    m5 = p.mul(a11, -C["c6"])
    m6 = p.mul(a10, -C["c5"])
    m7 = p.mul(a9, -C["c2"])
    a18 = p.add(x[4], a4)
    a19 = p.sub(p.dbl(x[4]), a4)
    a20 = p.add(a19, m5)
    a21 = p.sub(a19, m5)
    a22 = p.add(a19, m6)
    a23 = p.add(m4, m2)
    a24 = p.sub(m4, m2)
    a25 = p.add(m4, m1)
    return [p.add(a18, a12), p.sub(m0, a25), p.sub(m7, a20), m3, p.sub(a21, m6),
            p.sub(a24, m1), p.sub(a12, p.dbl(a18)), p.add(a23, m0), p.add(a22, m7)]


def sdctII(p, x):
    X = [None] * 18
    tmp = [p.add(x[i], x[17 - i]) for i in range(9)]
    for k, v in enumerate(fastsdct(p, tmp)):
        X[2 * k] = v
    tmp = [p.mul(p.sub(x[i], x[17 - i]), scale9[i]) for i in range(9)]
    for k, v in enumerate(fastsdct(p, tmp)):
        X[2 * k + 1] = v
    for i in (3, 11):
        for j in (0, 2, 4, 6):
            X[i + j] = p.sub(X[i + j], X[i + j - 2])
    return X


def dctIV(p, y):
    tmp = [p.mul(y[i], scale18[i]) for i in range(18)]
    X = sdctII(p, tmp)
    X[0] = p.half(X[0])
    for k in range(1, 18):
        X[k] = p.sub(p.half(X[k]), X[k - 1])
    return X


def imdct36(p, x):
    tmp = dctIV(p, x)
    y = [None] * 36
    for i in range(9):
        y[i] = tmp[9 + i]
    for i in range(9, 27):
        y[i] = p.neg(tmp[36 - (9 + i) - 1])
    for i in range(27, 36):
        y[i] = p.neg(tmp[i - 27])
    return y


def build(window):
    p = gen68k.Prog()
    X = ["%d(a0)" % (4 * k) for k in range(18)]
    y = imdct36(p, X)
    for i in range(36):
        v = p.mul(y[i], window_l[i]) if window else y[i]
        p.store(v, "%d(a1)" % (4 * i))
    return p.ops


def prologue(base):
    return ["\tmove.l\t%d(sp),a0\t\t| X" % base,
            "\tmove.l\t%d(sp),a1\t\t| z" % (base + 4)]


print("""| ambmpega.library: libmad's Layer III IMDCT for long blocks
| (third_party/libmad/layer3.c: imdct36, and III_imdct_l's normal window)
| in 68k assembler, written by tools/gen_layer3.py from the C's operations
| and constants: do not edit; make src/layer3_68k.s writes it again.
| MPGA060 0: ambmpega.library (FPM_64BIT, a MULS.L 32 x 32 -> 64 a
| product); 1: ambmpega060.library (FPM_68060, fixed.h's three 32-bit
| products). The same samples as the C.
|
| void mpga_imdct36(mad_fixed_t const X[18], mad_fixed_t z[36])
| void mpga_imdct_l0(mad_fixed_t const X[18], mad_fixed_t z[36])
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
| (GPL-2.0-or-later, Underbit Technologies).
""")
for name, window in (("mpga_imdct36", False), ("mpga_imdct_l0", True)):
    print("\t.globl\t_%s\n_%s:" % (name, name))
    print(gen68k.function(name, build(window), "d2-d7/a2-a6", prologue, ["a2", "a3", "a4", "a5", "a6"]))
