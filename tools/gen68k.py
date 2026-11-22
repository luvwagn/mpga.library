#!/usr/bin/env python3
# ambmpega.library's tools: the back end of gen_dct32.py and gen_layer3.py.
# Straight-line fixed-point code (libmad's, as a list of operations) to
# 68k assembler, for both builds:
#
#   variant 0 (ambmpega.library, FPM_64BIT): mad_f_mul(x, c) by a
#     constant as one MULS.L 32 x 32 -> 64 by c << s (s the most of 4 that
#     fits) and the pair shifted left 4 - s times (ADD, ADDX): the high
#     register is bits 28 to 59 of x c, as FPM_64BIT's cast keeps;
#   variant 1 (ambmpega060.library, FPM_68060): fixed.h's three products,
#     (xh ch << 4) + (xh cl >> 12) + (xl ch >> 12), to the bit.
#
# Operations (dst, op, args); a value is a name, or a memory operand
# ("4(a0)") read where it is:
#   add, sub (a - b), neg, dbl (a * 2), half (a / 2, C's: toward zero),
#   mul (a, constant), sso (libmad's OPT_SSO SHIFT: (a + 2048) >> 12),
#   store (value, memory operand), zero (memory operand).
# Values live in d0-d7, are spilled to the address registers given (a
# copy kept), then to the stack, the one used furthest ahead first.
#
# (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).


def is_mem(v):
    return isinstance(v, str) and "(" in v


def emit(ops, variant, spill_aregs):
    END = 1 << 30
    uses = {}
    for i, (d, op, a) in enumerate(ops):
        for x in (a[:1] if op in ("mul", "store") else a):
            if isinstance(x, str) and not is_mem(x):
                uses.setdefault(x, []).append(i)

    def next_use(v, i):
        for u in uses.get(v, []):
            if u > i:
                return u
        return END

    DREGS = ["d%d" % i for i in range(8)]
    reg, home, held = {}, {}, {}
    nslots = [0]
    freeslots = []
    out = []

    def dying(v, i):
        return next_use(v, i) == END

    def operand(v):
        if is_mem(v):
            return v
        return reg[v] if v in reg else home[v]

    def drop_reg(r):
        v = held.pop(r)
        if reg.get(v) == r:
            del reg[v]

    def new_home():
        for r in spill_aregs:
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
        if not isinstance(v, str) or is_mem(v) or not dying(v, i):
            return
        if v in reg:
            drop_reg(reg[v])
        if v in home:
            h = home.pop(v)
            held.pop(h, None)
            if h.endswith("(sp)"):
                freeslots.append(h)

    def take(v, i, keep=()):
        """a data register with v's value that may be overwritten"""
        if not is_mem(v) and dying(v, i) and v in reg:
            r = reg[v]
            drop_reg(r)
            return r
        r = get_dreg(i, keep)
        out.append("\tmove.l\t%s,%s" % (operand(v), r))
        return r

    def place(v, r):
        reg[v] = r
        held[r] = v

    def mul(r, c, i):
        """r * c >> 28 (FPM's mad_f_mul), r consumed; the register holding it"""
        assert -(1 << 31) <= c < (1 << 31)
        if variant == 0:
            h = get_dreg(i)
            s = 4
            while not (-(1 << 31) <= (c << s) < (1 << 31)):
                s -= 1
            out.append("\tmuls.l\t#%d,%s:%s" % (c << s, h, r))
            for _ in range(4 - s):
                out.append("\tadd.l\t%s,%s" % (r, r))
                out.append("\taddx.l\t%s,%s" % (h, h))
            return h
        ch, cl = c >> 16, c & 0xffff
        assert -32768 <= ch < 32768
        hx = get_dreg(i)
        held[hx] = "(busy)"
        t = get_dreg(i)
        # hx = xh, r = xl, t = xh cl >> 12
        out.append("\tmove.l\t%s,%s" % (r, hx))
        out.append("\tswap\t%s" % hx)
        out.append("\text.l\t%s" % hx)
        out.append("\tand.l\t#65535,%s" % r)
        if ch >= 0:
            out.append("\tmulu.w\t#%d,%s" % (ch, r))
        else:
            out.append("\tmuls.l\t#%d,%s" % (ch, r))
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
        del held[hx]
        return hx

    for i, (d, op, a) in enumerate(ops):
        if op in ("add", "sub"):
            x, y = a
            if op == "add" and not is_mem(y) and y in reg and dying(y, i) \
                    and not (not is_mem(x) and x in reg and dying(x, i)):
                x, y = y, x      # (the dying one in a data register takes the sum)
            r = take(x, i, keep=(y,))
            held[r] = "(busy)"
            out.append("\t%s.l\t%s,%s" % (op, operand(y), r))
            del held[r]
            kill(x, i)
            kill(y, i)
            place(d, r)
        elif op in ("dbl", "neg", "half", "sso"):
            r = take(a[0], i)
            if op == "dbl":
                out.append("\tadd.l\t%s,%s" % (r, r))
            elif op == "neg":
                out.append("\tneg.l\t%s" % r)
            elif op == "half":
                out.append("\ttst.l\t%s" % r)
                out.append("\tbpl.s\t1f")
                out.append("\taddq.l\t#1,%s" % r)
                out.append("1:")
                out.append("\tasr.l\t#1,%s" % r)
            else:
                out.append("\tadd.l\t#2048,%s" % r)
                out.append("\tasr.l\t#8,%s" % r)
                out.append("\tasr.l\t#4,%s" % r)
            kill(a[0], i)
            place(d, r)
        elif op == "mul":
            x, c = a
            r = take(x, i)
            held[r] = "(busy)"
            h = mul(r, c, i)
            del held[r]
            kill(x, i)
            place(d, h)
        elif op == "store":
            v, m = a
            out.append("\tmove.l\t%s,%s" % (operand(v), m))
            kill(v, i)
        elif op == "zero":
            out.append("\tclr.l\t%s" % a[0])
        else:
            raise ValueError(op)
    return out, nslots[0]


class Prog:
    """operations built by calls: each returns its value's name"""

    def __init__(self):
        self.ops = []
        self.n = 0

    def _op(self, op, *a):
        self.n += 1
        d = "_%d" % self.n
        self.ops.append((d, op, list(a)))
        return d

    def add(self, a, b): return self._op("add", a, b)
    def sub(self, a, b): return self._op("sub", a, b)
    def neg(self, a): return self._op("neg", a)
    def dbl(self, a): return self._op("dbl", a)
    def half(self, a): return self._op("half", a)
    def mul(self, a, c): return self._op("mul", a, c)
    def sso(self, a): return self._op("sso", a)

    def store(self, v, m):
        self.ops.append((None, "store", [v, m]))

    def zero(self, m):
        self.ops.append((None, "zero", [m]))


def function(name, ops, saves, prologue, spill_aregs, variants=(0, 1)):
    """the function's text: each variant under .if MPGA060 == n"""
    import re
    text = []
    for variant in variants:
        code, n = emit(ops, variant, spill_aregs)
        text.append("\t.if\tMPGA060 == %d" % variant)
        frame = 4 * n
        nsave = len(expand(saves))
        text.append("\tmovem.l\t%s,-(sp)" % saves)
        if frame:
            text.append("\tlea\t-%d(sp),sp" % frame)
        for l in prologue(4 * nsave + frame + 4):
            text.append(l)
        for l in code:
            text.append(re.sub(r"SLOT(\d+)\(sp\)", lambda m: "%d(sp)" % (4 * int(m.group(1))), l))
        if frame:
            text.append("\tlea\t%d(sp),sp" % frame)
        text.append("\tmovem.l\t(sp)+,%s" % saves)
        text.append("\trts")
        text.append("\t.endif")
        stats = (variant, len([l for l in code if not l.endswith(":")]), n)
        import sys
        sys.stderr.write("%s variant %d: %d instructions, %d stack slots\n" % ((name,) + stats))
    return "\n".join(text) + "\n"


def expand(saves):
    """the registers in a movem list such as d2-d7/a2-a6"""
    regs = []
    for part in saves.split("/"):
        if "-" in part:
            a, b = part.split("-")
            regs += ["%s%d" % (a[0], k) for k in range(int(a[1]), int(b[1]) + 1)]
        else:
            regs.append(part)
    return regs
