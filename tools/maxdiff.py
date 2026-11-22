#!/usr/bin/env python3
# The largest difference between two decodes of the same file (16-bit
# big-endian samples, as mpga_test -w writes them), and how many samples
# differ at all; exit 1 if any differs by more than the limit. Plain
# Python, no numpy (the release build's container has none).
# (c) 2026 Brian King, MIT.
#   maxdiff.py a.raw b.raw limit
import array
import sys


def samples(path):
    a = array.array('h')
    a.frombytes(open(path, 'rb').read())
    if sys.byteorder == 'little':
        a.byteswap()
    return a


a, b = samples(sys.argv[1]), samples(sys.argv[2])
if len(a) != len(b):
    print("%d samples against %d" % (len(a), len(b)))
    sys.exit(1)
big = differ = 0
for x, y in zip(a, b):
    if x != y:
        differ += 1
        big = max(big, abs(x - y))
print("largest difference %d, %d of %d samples differ" % (big, differ, len(a)))
sys.exit(0 if big <= int(sys.argv[3]) else 1)
