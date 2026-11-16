# amcempega.library

MPEG audio decoded to 16-bit PCM for any Amiga program (68020 or better,
no FPU needed), behind **mpega.library's functions**: a program written for
mpega.library works unchanged; only the name it opens differs
(`AMCEMPEGANAME`, "amcempega.library"). Made for AmigaMCE, a media centre
for the Amiga, which keeps it in its own `Libs` and can use it or whichever
`mpega.library` is in `LIBS:` (Stéphane Tavenard's, a libmad clone, or the
ZZ9000's, which decodes on the card).

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

Not yet: the 32-point DCT and Layer III's IMDCT in 68k assembler; a 68060
build without 64-bit multiplies.

## Using it

```c
#include <proto/mpega.h>
struct Library *MPEGABase = OpenLibrary(AMCEMPEGANAME, AMCEMPEGAVERSION);

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
make            build/amcempega.library
make test       the fixtures (tests/mkfixtures.sh made them from synthetic
                sound) decoded eight ways each on this computer and through
                the library on a 68020 under vamos with an 8 KB stack: the
                samples' CRCs alike, and as tests/expected.txt
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

| | 44.1 kHz stereo | 22 kHz | mono 22 kHz |
|---|---|---|---|
| amcempega.library, high | 4.92 / 4.36 M | 3.27 / 3.16 M | 2.44 / 2.31 M |
| amcempega.library, medium | 4.46 / 3.90 M | 3.05 / 2.93 M | 2.33 / 2.20 M |
| mpega.library 2.4 (Tavenard, integer) | 4.07 / 2.92 M | 2.37 M | 1.42 M |

At medium quality the synthesis window is still 42% of the time (its
36,288 products a frame), the 32-point DCT 17%: where the work goes next.
