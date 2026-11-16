#!/usr/bin/env python3
# The SNR (dB) of decoded 16-bit stereo samples (big-endian) against a
# reference (little-endian, from ffmpeg), each aligned on the other first
# (decoders differ in the delay they keep). (c) 2026 Brian King, MIT.
#   snr.py test.raw ref.s16le
import sys
import numpy as np
t = np.frombuffer(open(sys.argv[1], 'rb').read(), dtype='>i2').astype(np.float64).reshape(-1, 2)
r = np.frombuffer(open(sys.argv[2], 'rb').read(), dtype='<i2').astype(np.float64).reshape(-1, 2)
best = None
for off in range(-3000, 3001):
    a = t[max(0, off):]
    b = r[max(0, -off):]
    n = min(len(a), len(b), 100000)
    if n < 10000:
        continue
    a = a[2000:n]; b = b[2000:n]
    e = np.sum((a - b) ** 2)
    if best is None or e < best[0]:
        best = (e, off, np.sum(b ** 2), len(a), np.max(np.abs(a - b)))
e, off, s, n, mx = best
print("offset %d samples, SNR %.1f dB, max difference %d (%d samples compared)" % (off, 10 * np.log10(s / max(e, 1e-9)), mx, n))
