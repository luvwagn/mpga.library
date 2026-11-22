#!/usr/bin/env python3
# ambmpega.library's tools: libmad's dct32() (third_party/libmad/synth.c,
# OPT_SSO) as 68k assembler, written by a program from the C itself, for
# both builds (src/dct32_68k.s):
#
#   MPGA060 0 (ambmpega.library, FPM_64BIT): each MUL(x, c) one MULS.L
#     32 x 32 -> 64 by c << 4 (the high register is the result) or, where
#     c << 4 would not fit, by c << 3 and the pair doubled (ADD, ADDX):
#     (x c) >> 28 to the bit;
#   MPGA060 1 (ambmpega060.library, FPM_68060): fixed.h's three products,
#     (xh ch << 4) + (xh cl >> 12) + (xl ch >> 12), to the bit.
#
# The same sums, differences and SHIFT()s as the C, in its order, so the
# samples are the C's (make test compares them with the host's).
# Registers: the values in d0-d7, spilled to a3-a6, then to the stack,
# the one used furthest ahead first.
#
#   gen_dct32.py third_party/libmad/synth.c > src/dct32_68k.s
#
# (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
# (GPL-2.0-or-later, Underbit Technologies).
import ast
import re
import sys

src = open(sys.argv[1]).read()

# the costab values (the non-OPT_DCTO ones: 28 fractional bits)
body = src[src.index("void dct32("):]
costab = {}
dcto_else = body[body.index("# else"):body.index("# endif")]
for m in re.finditer(r"define\s+costab(\d+)\s+MAD_F\((0x[0-9a-f]+)\)", dcto_else):
    costab["costab" + m.group(1)] = int(m.group(2), 16)
assert len(costab) == 31

# the statements, from the first to the totals comment
code = body[body.index("t0   = in[0]"):body.index("/*\n   * Totals")]
code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
code = re.sub(r"\bin\[\s*(\d+)\]", r"IN(\1)", code)
code = re.sub(r"\b(hi|lo)\[\s*(\d+)\]\[slot\]\s*=", r"\1_\2 =", code)
stmts = [s.strip() for s in code.replace("\n", " ").split(";") if s.strip()]

# ---- to three-address operations
ops = []          # (dst, op, args)
ntmp = [0]


def tmp():
    ntmp[0] += 1
    return "_%d" % ntmp[0]


def walk(e):
    if isinstance(e, ast.Name):
        return e.id
    if isinstance(e, ast.Call):
        f = e.func.id
        if f == "IN":
            return "in%d" % e.args[0].value
        if f == "MUL":
            a = walk(e.args[0])
            d = tmp()
            ops.append((d, "mul", [a, costab[e.args[1].id]]))
            return d
        if f == "SHIFT":
            a = walk(e.args[0])
            d = tmp()
            ops.append((d, "shift", [a]))
            return d
        raise ValueError(f)
    if isinstance(e, ast.BinOp):
        if isinstance(e.op, ast.Mult):
            assert isinstance(e.right, ast.Constant) and e.right.value == 2
            a = walk(e.left)
            d = tmp()
            ops.append((d, "dbl", [a]))
            return d
        a, b = walk(e.left), walk(e.right)
        d = tmp()
        ops.append((d, "add" if isinstance(e.op, ast.Add) else "sub", [a, b]))
        return d
    raise ValueError(ast.dump(e))


outs = {}
for s in stmts:
    tgt, expr = s.split("=", 1)
    tgt = tgt.strip()
    v = walk(ast.parse(expr.strip(), mode="eval").body)
    if tgt.startswith(("hi_", "lo_")):
        ops.append((None, "store", [v, tgt]))
    else:
        ops.append((tgt, "copy", [v]))

# a copy only renames: its destination is the value itself
alias = {}
flat = []
for d, op, a in ops:
    a = [alias.get(x, x) if isinstance(x, str) else x for x in a]
    if op == "copy":
        alias[d] = a[0]
        continue
    flat.append((d, op, a))
ops = flat

# to gen68k's operations: in[k] read where it is (a0), hi and lo stored
# at this slot's column (a2, a1)
sys.path.insert(0, __import__("os").path.dirname(__file__))
import gen68k

ops = [(d, {"shift": "sso"}.get(op, op),
        [("%d(a0)" % (4 * int(x[2:])) if isinstance(x, str) and x.startswith("in") else
          "%d(%s)" % (32 * int(x.split("_")[1]), "a2" if x.startswith("hi") else "a1")
          if isinstance(x, str) and x.startswith(("hi_", "lo_")) else x) for x in a])
       for d, op, a in ops]


def prologue(base):
    return ["\tmove.l\t%d(sp),a0\t\t| in" % base,
            "\tmove.l\t%d(sp),d0\t\t| slot" % (base + 4),
            "\tlsl.l\t#2,d0",
            "\tmove.l\t%d(sp),a1\t\t| lo" % (base + 8),
            "\tmove.l\t%d(sp),a2\t\t| hi" % (base + 12),
            "\tadd.l\td0,a1",
            "\tadd.l\td0,a2"]


hdr = """| ambmpega.library: libmad's dct32() (third_party/libmad/synth.c, OPT_SSO)
| in 68k assembler, written by tools/gen_dct32.py from the C itself: do not
| edit; make src/dct32_68k.s writes it again. MPGA060 0: ambmpega.library
| (FPM_64BIT, a MULS.L 32 x 32 -> 64 a product); 1: ambmpega060.library
| (FPM_68060, fixed.h's three 32-bit products). The same samples as the C.
|
| void mpga_dct32(mad_fixed_t const in[32], unsigned int slot,
|                 mad_fixed_t lo[16][8], mad_fixed_t hi[16][8])
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
| (GPL-2.0-or-later, Underbit Technologies).

\t.globl\t_mpga_dct32
_mpga_dct32:
"""
print(hdr, end="")
print(gen68k.function("dct32", ops, "d2-d7/a2-a6", prologue, ["a3", "a4", "a5", "a6"]), end="")
