#!/usr/bin/env python3
# ambmpega.library's tools (mpegv.library's): a 68k program's functions,
# static ones too, for tools/prof.py. The Amiga's linker keeps only the global symbols; each
# object's own (nm) are placed by where the link map put its code.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   syms.py program.map object.o ... > program.syms
import re
import subprocess
import sys

where = {}
for line in open(sys.argv[1]):
    m = re.match(r"\s*\.text\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$", line)
    if m:
        where[m.group(3)] = int(m.group(1), 16)
for obj in sys.argv[2:]:
    if obj not in where:
        continue
    out = subprocess.run(["m68k-amigaos-nm", obj], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = re.match(r"([0-9a-f]+) ([tT]) (\S+)$", line)
        if m:
            print("%08x %s %s" % (where[obj] + int(m.group(1), 16), m.group(2), m.group(3)))
# the rest (the C library's): the program's own global symbols
out = subprocess.run(["m68k-amigaos-nm", sys.argv[1][:-4]], capture_output=True, text=True).stdout
print(out, end="")
