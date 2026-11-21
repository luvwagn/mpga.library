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

# each value's uses (op indices)
uses = {}
for i, (d, op, a) in enumerate(ops):
    for x in a[:1] if op == "mul" else a:
        if isinstance(x, str) and not x.startswith(("in", "hi_", "lo_")):
            uses.setdefault(x, []).append(i)


def next_use(v, i):
    for u in uses.get(v, []):
        if u > i:
            return u
    return 1 << 30


def emit(variant):
    DREGS = ["d%d" % i for i in range(8)]
    AREGS = ["a3", "a4", "a5", "a6"]
    reg = {}                 # value -> data register
    home = {}                # value -> a3-a6 or a stack slot (a copy)
    held = {}                # location -> value (or "(busy)")
    nslots = [0]
    freeslots = []
    out = []
    END = 1 << 30

    def dying(v, i):
        return next_use(v, i) == END

    def operand(v):
        if v.startswith("in"):
            return "%d(a0)" % (4 * int(v[2:]))
        return reg[v] if v in reg else home[v]

    def drop_reg(r):
        v = held.pop(r)
        if reg.get(v) == r:
            del reg[v]

    def new_home():
        for r in AREGS:
            if r not in held:
                return r
        if freeslots:
            return freeslots.pop()
        nslots[0] += 1
        return "SLOT%d(sp)" % (nslots[0] - 1)

    def get_dreg(i, keep=()):
        for r in DREGS:
            if r not in held:
                return r
        cands = [r for r in DREGS if held[r] != "(busy)" and held[r] not in keep]
        # the one used furthest ahead; of those, one already with a home
        r = max(cands, key=lambda r: (next_use(held[r], i), held[r] in home))
        v = held[r]
        if v not in home:
            h = new_home()
            out.append("\tmove.l\t%s,%s" % (r, h))
            home[v] = h
            held[h] = v
        drop_reg(r)
        return r

    def kill(v, i):
        """v's last use was op i: its locations are free"""
        if not isinstance(v, str) or v.startswith(("in", "hi_", "lo_")) or not dying(v, i):
            return
        if v in reg:
            drop_reg(reg[v])
        if v in home:
            h = home.pop(v)
            held.pop(h, None)
            if h.endswith("(sp)"):
                freeslots.append(h)

    def take(v, i, keep=()):
        """a data register holding v's value that may be overwritten"""
        if not v.startswith("in") and dying(v, i) and v in reg:
            r = reg[v]
            drop_reg(r)
            return r
        r = get_dreg(i, keep)
        out.append("\tmove.l\t%s,%s" % (operand(v), r))
        return r

    def place(v, r):
        reg[v] = r
        held[r] = v

    for i, (d, op, a) in enumerate(ops):
        if op in ("add", "sub"):
            x, y = a
            if op == "add" and not y.startswith("in") and y in reg and dying(y, i) \
                    and not (not x.startswith("in") and x in reg and dying(x, i)):
                x, y = y, x      # (the dying one in a data register takes the sum)
            r = take(x, i, keep=(y,))
            held[r] = "(busy)"
            out.append("\t%s.l\t%s,%s" % (op, operand(y), r))
            del held[r]
            kill(x, i)
            kill(y, i)
            place(d, r)
        elif op == "dbl":
            r = take(a[0], i)
            out.append("\tadd.l\t%s,%s" % (r, r))
            kill(a[0], i)
            place(d, r)
        elif op == "mul":
            x, c = a
            r = take(x, i)
            held[r] = "(busy)"
            if variant == 0:
                h = get_dreg(i)
                if (c << 4) < (1 << 31):
                    out.append("\tmuls.l\t#%d,%s:%s" % (c << 4, h, r))
                else:
                    out.append("\tmuls.l\t#%d,%s:%s" % (c << 3, h, r))
                    out.append("\tadd.l\t%s,%s" % (r, r))
                    out.append("\taddx.l\t%s,%s" % (h, h))
                del held[r]
                kill(x, i)
                place(d, h)
            else:
                ch, cl = c >> 16, c & 0xffff
                assert 0 <= ch < 4096
                hx = get_dreg(i)
                held[hx] = "(busy)"
                t = get_dreg(i)
                # hx = xh, r = xl, t = xh cl >> 12
                out.append("\tmove.l\t%s,%s" % (r, hx))
                out.append("\tswap\t%s" % hx)
                out.append("\text.l\t%s" % hx)
                out.append("\tand.l\t#65535,%s" % r)
                out.append("\tmulu.w\t#%d,%s" % (ch, r))
                out.append("\tasr.l\t#8,%s" % r)
                out.append("\tasr.l\t#4,%s" % r)
                out.append("\tmove.l\t%s,%s" % (hx, t))
                if cl < 32768:
                    out.append("\tmuls.w\t#%d,%s" % (cl, t))
                else:
                    out.append("\tmuls.l\t#%d,%s" % (cl, t))
                out.append("\tasr.l\t#8,%s" % t)
                out.append("\tasr.l\t#4,%s" % t)
                out.append("\tmuls.w\t#%d,%s" % (ch, hx))
                out.append("\tlsl.l\t#4,%s" % hx)
                out.append("\tadd.l\t%s,%s" % (t, hx))
                out.append("\tadd.l\t%s,%s" % (r, hx))
                del held[r]
                del held[hx]
                kill(x, i)
                place(d, hx)
        elif op == "shift":
            r = take(a[0], i)
            out.append("\tadd.l\t#2048,%s" % r)
            out.append("\tasr.l\t#8,%s" % r)
            out.append("\tasr.l\t#4,%s" % r)
            kill(a[0], i)
            place(d, r)
        elif op == "store":
            v, tgt = a
            half, k = tgt.split("_")
            out.append("\tmove.l\t%s,%d(%s)" % (operand(v), 32 * int(k), "a2" if half == "hi" else "a1"))
            kill(v, i)
        else:
            raise ValueError(op)
    return out, nslots[0]


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
for variant in (0, 1):
    code, n = emit(variant)
    print("\t.if\tMPGA060 == %d" % variant)
    frame = 4 * n
    print("\tmovem.l\td2-d7/a2-a6,-(sp)")
    if frame:
        print("\tlea\t-%d(sp),sp" % frame)
    base = 44 + frame
    print("\tmove.l\t%d(sp),a0\t\t| in" % (base + 4))
    print("\tmove.l\t%d(sp),d0\t\t| slot" % (base + 8))
    print("\tlsl.l\t#2,d0")
    print("\tmove.l\t%d(sp),a1\t\t| lo" % (base + 12))
    print("\tmove.l\t%d(sp),a2\t\t| hi" % (base + 16))
    print("\tadd.l\td0,a1")
    print("\tadd.l\td0,a2")
    for l in code:
        print(re.sub(r"SLOT(\d+)\(sp\)", lambda m: "%d(sp)" % (4 * int(m.group(1))), l))
    if frame:
        print("\tlea\t%d(sp),sp" % frame)
    print("\tmovem.l\t(sp)+,d2-d7/a2-a6")
    print("\trts")
    print("\t.endif")
    sys.stderr.write("variant %d: %d instructions, %d stack slots\n" % (variant, len(code), n))
