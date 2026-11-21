#!/usr/bin/env python3
# ambmpega.library's tools (mpegv.library's): instructions executed (not cycles), by function,
# under vamos: what an interpreting emulator (Amiberry without its JIT)
# and a 68060 (about an instruction a clock) pay for, where a 68020's
# cycles (prof.py) weigh multiplies and shifts heavily.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   prof_instr.py program.syms -- [vamos arguments] program [its arguments]
#
# Run with amitools' Python. Slow (a Python call an instruction): a few
# pictures.
import atexit
import bisect
import collections
import os
import re
import sys

mapfile = sys.argv[1]
args = sys.argv[sys.argv.index("--") + 1:]

from amitools.vamos.machine import machine as mm

counts = collections.Counter()
hooked = [False]
_execute = mm.Machine.execute


def hook(*a):
    counts[a[0] if a else 0] += 1


def execute(self, max_cycles=1000):
    if not hooked[0]:
        self.cpu.set_instr_hook_callback(hook)
        hooked[0] = True
    return _execute(self, max_cycles)


mm.Machine.execute = execute

syms = []
for line in open(mapfile):
    m = re.match(r"([0-9a-f]+) [tT] _?([A-Za-z_][A-Za-z0-9_.]*)\s*$", line)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
offs = [s[0] for s in syms]
main_off = dict((n, o) for o, n in syms).get("main")


def report():
    base = None
    try:
        m = re.search(r"code at ([0-9a-fA-F]+)", open("prof.base").read())
        if m and main_off is not None:
            base = int(m.group(1), 16) - main_off
    except OSError:
        pass
    out = sys.__stdout__
    if base is None:
        print("no 'code at' line from the program", file=out)
        return
    per = collections.Counter()
    for pc, c in counts.items():
        off = pc - base
        i = bisect.bisect_right(offs, off) - 1
        name = syms[i][1] if 0 <= i < len(syms) and 0 <= off < offs[-1] + 65536 else "(outside)"
        per[name] += c
    t = sum(counts.values()) or 1
    print("total %d instructions" % t, file=out)
    for name, c in per.most_common(40):
        print("%6.2f%% %12d  %s" % (100.0 * c / t, c, name), file=out)
    # MPEG_PROF_FUNC=name: that function's instructions, each with its
    # count (offsets in the program's code, for objdump -d)
    fn = os.environ.get("MPEG_PROF_FUNC")
    if fn:
        print("in %s:" % fn, file=out)
        for pc, c in sorted(counts.items()):
            off = pc - base
            i = bisect.bisect_right(offs, off) - 1
            if 0 <= i < len(syms) and syms[i][1] == fn:
                print("%08x %10d" % (off, c), file=out)


atexit.register(report)

from amitools.tools.vamos import main

sys.argv = ["vamos"] + args
sys.exit(main())
