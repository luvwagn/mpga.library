# ambmpega.library

MPEG audio decoded to 16-bit PCM for any Amiga program (68020 or better,
no FPU needed), behind **mpega.library's functions**: a program written for
mpega.library works unchanged; only the name it opens differs
(`AMBMPEGANAME`, "ambmpega.library"). Made for Ambiance, an Amiga Media
Center application, which keeps it in its own `Libs` and can use it or
whichever `mpega.library` is in `LIBS:` (Stéphane Tavenard's, a libmad
clone, or the ZZ9000's, which decodes on the card).

- MPEG-1, MPEG-2 (16, 22.05, 24 kHz) and MPEG-2.5 (8, 11.025, 12 kHz);
  Layers I, II and III; stereo, joint stereo, dual channel and mono;
  free-format bitrates.
- Xing and Info headers: a VBR file's length and its seeking table; the
  Info frame itself is not played. ID3v2 tags are passed over.
- mpega's settings: each layer's output rate divided by 2 or 4 (or as
  `freq_max` allows), stereo decoded as mono, the loudness (`MPEGA_scale`),
  seeking and the time in milliseconds, the program's own bitstream hook or
  a file by name.
- The decoder is [libmad](https://www.underbit.com/products/mad/) 0.15.1b
  (`third_party/libmad`), with Debian's security fixes, each multiply one
  `MULS.L` 32 x 32 -> 64: within one or two steps of 16 bits of a
  floating-point decoder (about 83 dB; Tavenard's mpega 2.4 is 34 dB for
  MP3, 56 dB for Layer II). Medium and low quality: the synthesis window's
  products 16-bit (`MULS.W`), about a tenth faster at about 70 dB.
- Its working stack is the stream's own (16 KB): a program calling it from
  a task with a 4 KB stack is safe.
- Its own `MPGA_decode_to` (after mpega's functions): each frame straight
  into the program's ring buffers in the format its output wants: 16-bit,
  16-bit interleaved for AHI, 8-bit for Paula (dithered or not), or
  Paula's 14 bits, with each channel's peak and the samples clipped.

- **Two builds:** `ambmpega.library` for any 68020 or later, and
  `ambmpega060.library` for a 68060, which has no `MULS.L` 32 x 32 -> 64:
  68060.library would emulate each one, about 19,700 a frame of 128 kbit/s
  MP3. There each product is made from three 32-bit multiplies, at most
  2^-24 of full scale below the 64-bit one: the same 83 dB, the samples
  within a step of the other build's. A program can ship both and open the
  68060's only where `AttnFlags` says 68060, or time both there, as
  Ambiance does: an emulator set to a 68060 may do the 64-bit multiply
  itself (Amiberry does, even with unimplemented instructions trapping).

Not yet: the 32-point DCT and Layer III's IMDCT in 68k assembler.

## Why not mpega.library?

It is not for speed: Stéphane Tavenard's mpega.library 2.4 (1999) is
still the faster decoder in every mode measured (see Speed). It is fast
because its arithmetic is rough:

- **Accuracy.** mpega 2.4 is 34 dB from a floating-point decoder on MP3
  at its best quality (56 dB on Layer II), and 12 dB with stereo decoded
  as mono. ambmpega.library is 83 dB at high quality, in stereo or mono
  alike (its mono against the float decoder's left and right averaged),
  70 dB at medium.
- **A name of its own.** The ZZ9000's driver installer (2.9.1) copies its
  own mpega.library over `LIBS:mpega.library` with no backup, so a program
  that needs a particular decoder cannot count on that name.
- **No copy afterwards.** `MPGA_decode_to` writes each frame straight into
  the program's ring buffers in its output's format, counting peaks and
  clipping as it goes; with mpega's functions each frame is decoded into
  mpega's buffers, then copied and converted.
- **Source and tests.** libmad with Debian's security fixes, its own
  16 KB stack, a test suite that runs the 68k build against the host build,
  and a licence that lets it be published and maintained in the open.

Against the other libmad-based mpega.library clones, which are as
accurate, it is the faster at 22 kHz and in mono: they decode every
subband and channel and throw the extra away, so their 22 kHz and mono
save almost nothing, while ambmpega.library leaves that work out.

## Using it

```c
#include <proto/mpega.h>
struct Library *MPEGABase = OpenLibrary(AMBMPEGANAME, AMBMPEGAVERSION);

MPEGA_CTRL ctrl = { 0 };            /* NULL bs_access: the file, by name */
ctrl.layer_1_2.stereo.freq_div = ctrl.layer_3.stereo.freq_div = 1;
ctrl.layer_1_2.mono.freq_div = ctrl.layer_3.mono.freq_div = 1;
ctrl.layer_1_2.stereo.quality = ctrl.layer_3.stereo.quality = MPEGA_QUALITY_HIGH;
MPEGA_STREAM *s = MPEGA_open("song.mp3", &ctrl);
WORD l[MPEGA_PCM_SIZE], r[MPEGA_PCM_SIZE], *pcm[2] = { l, r };
LONG n;
while ((n = MPEGA_decode_frame(s, pcm)) >= 0)
    /* n samples a channel at s->dec_frequency, s->dec_channels of them */;
MPEGA_close(s);
```

`mpga.doc` describes every function; the structures are in
`include/libraries/mpega.h`.

## Licence

GPL-3.0-or-later (`LICENSE`): libmad is GPL-2.0-or-later
(`third_party/libmad/COPYING`; what is changed in it:
`third_party/libmad/CHANGES.mpga`). The interface (`include/`,
`mpga_lib.sfd`, `mpga.doc`) and the test are MIT and contain nothing of
libmad's, so any program may be built against it; a program that opens the
library at run time is not bound by its licence.

## Building

```
make            build/ambmpega.library and build/ambmpega060.library
make test       the fixtures (tests/mkfixtures.sh made them from synthetic
                sound) decoded eight ways each on this computer and through
                the library on a 68020 under vamos with an 8 KB stack: the
                samples' CRCs alike, and as tests/expected.txt; the
                same for the 68060's build (on a 68040 under vamos,
                tests/expected060.txt), its samples within a step of the
                68020's, and no 64-bit multiply or divide in it
make snr        each fixture against ffmpeg's floating-point decoder
make LIBNAME=mpega.library
                the same library under mpega.library's own name
```

Needs [AmigaPorts' m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc)
16.2 (rc24 or later; the continuation of bebbo's amiga-gcc) with libnix, a
C compiler for the build computer, and for the tests amitools' vamos (and
ffmpeg and numpy for `make snr`).

## Speed

A 68020's cycles a frame (26.12 ms of sound at 44.1 kHz), as vamos's
Musashi core counts them, for 10 s of music at 128 kbit/s stereo MP3
(and, after the slash, Layer II at 224 kbit/s):

| | 44.1 kHz stereo | 22 kHz | mono 22 kHz | SNR (MP3) |
|---|---|---|---|---|
| ambmpega.library, high | 4.92 / 4.36 M | 3.27 / 3.16 M | 2.44 / 2.31 M | 83 dB |
| ambmpega.library, medium | 4.46 / 3.90 M | 3.05 / 2.93 M | 2.33 / 2.20 M | 70 dB |
| mpega.library 2.4 (Tavenard, integer) | **4.07 / 2.92 M** | **2.37 M** | **1.42 M** | 34 dB |
| mpega080 2.6.3 (libmad), 68020 build | 4.23 / 3.97 M | 4.24 M | 4.07 M | 80 dB |
| AmiKit mpega.library 3.1 (libmad), `-hifi` | 5.91 / 5.38 M | 5.71 M | 5.56 M | 80 dB |

SNR is against ffmpeg's floating-point decoder (`make snr`; ffmpeg's own
fixed-point decoder scores 86 dB). mpega 2.4's 020, 040 and 060 integer
builds are the same code; its FPU builds cannot run under vamos.

What these figures are not:

- **Real hardware.** Musashi models no caches or wait states; a real
  68030 or 68060, or an emulator's JIT, will give other times, though
  probably the same order.
- **Real time on a 68020/030.** A frame lasts 26.12 ms, so 4.46 M cycles
  a frame is about 170 M 68020 cycles a second: full-rate stereo MP3 is
  beyond a 68020 or 68030 with any of these decoders, and at 22 kHz mono
  mpega 2.4 (about 54 M a second) has the real edge on those machines.
- **A 68060.** `ambmpega.library` uses `MULS.L` 32 x 32 -> 64 at both
  qualities, about 19,700 a frame, which a 68060 lacks; 68060.library
  emulates each one, so on a 68060 it is slow. `ambmpega060.library` is
  the one for it: 0.86 M instructions a frame against 0.57 M (vamos counts
  them), with no traps. Not yet timed on a real 68060.

At medium quality the synthesis window is still 42% of the time (its
36,288 products a frame), the 32-point DCT 17%: where the work goes next.
