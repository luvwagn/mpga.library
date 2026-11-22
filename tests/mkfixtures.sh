#!/bin/sh
# Makes ambmpega.library's test files (tests/fixtures/) from synthetic
# sound (a sweep, tones and noise; nothing recorded), three seconds each:
# MPEG-1 Layer III at 44.1 kHz (128 kbit/s joint stereo, an ID3v2 tag and
# LAME's Info frame) and at 48 kHz (VBR, a Xing table of contents),
# MPEG-2 Layer III at 22.05 kHz mono, MPEG-2.5 Layer III at 11.025 kHz,
# Layer II at 44.1 kHz stereo and 48 kHz mono; and Layer III at 44.1 kHz
# whose channels are nearly alike, with bursts, so that LAME codes it in
# mid/side stereo with some short blocks. Needs ffmpeg with LAME. The
# files are in git; this is how they were made. (c) 2026 Brian King, MIT.
set -e
out=${1:-tests/fixtures}
mkdir -p "$out"
src="aevalsrc=0.4*sin(2*PI*(100+2500*t)*t)+0.1*sin(2*PI*440*t)|0.3*sin(2*PI*(15000-4500*t)*t)+0.2*(random(0)-0.5)+0.1*sin(2*PI*55*t):s=48000:d=3"
ff() { ffmpeg -v error -y -f lavfi -i "$src" "$@"; }
ff -ar 44100 -c:a libmp3lame -b:a 128k -metadata title="ambmpega test" -id3v2_version 3 "$out/l3_44_js128.mp3"
ff -ar 48000 -c:a libmp3lame -q:a 4 -write_xing 1 "$out/l3_48_vbr.mp3"
ff -ar 22050 -ac 1 -c:a libmp3lame -b:a 40k "$out/l3_22_mono.mp3"
ff -ar 11025 -c:a libmp3lame -b:a 24k "$out/l3_11_mpeg25.mp3"
ff -ar 44100 -c:a mp2 -b:a 192k "$out/l2_44_192.mp2"
ff -ar 48000 -ac 1 -c:a mp2 -b:a 96k "$out/l2_48_mono.mp2"
ms="aevalsrc=0.4*sin(2*PI*(100+2500*t)*t)+0.25*sin(2*PI*7000*t)*gt(mod(t\\,0.5)\\,0.47)+0.1*sin(2*PI*440*t)|0.4*sin(2*PI*(100+2500*t)*t)+0.25*sin(2*PI*7000*t)*gt(mod(t\\,0.5)\\,0.47)+0.1*sin(2*PI*660*t):s=48000:d=3"
ffmpeg -v error -y -f lavfi -i "$ms" -ar 44100 -c:a libmp3lame -b:a 128k "$out/l3_44_ms.mp3"
