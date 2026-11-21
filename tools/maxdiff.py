#!/usr/bin/env python3
# The largest difference between two decodes of the same file (16-bit
# big-endian samples, as mpga_test -w writes them), and how many samples
# differ at all; exit 1 if any differs by more than the limit.
# (c) 2026 Brian King, MIT.
#   maxdiff.py a.raw b.raw limit
import sys
import numpy as np
a = np.frombuffer(open(sys.argv[1], 'rb').read(), dtype='>i2').astype(np.int32)
b = np.frombuffer(open(sys.argv[2], 'rb').read(), dtype='>i2').astype(np.int32)
if len(a) != len(b):
    print("%d samples against %d" % (len(a), len(b)))
    sys.exit(1)
d = np.abs(a - b)
print("largest difference %d, %d of %d samples differ" % (d.max(initial=0), int((d > 0).sum()), len(a)))
sys.exit(0 if d.max(initial=0) <= int(sys.argv[3]) else 1)
