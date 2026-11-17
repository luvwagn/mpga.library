/*
 * ambmpega.library's test: each file named decoded through mpega's
 * functions (on the host: src/mpga.c built in; on the 68k: the library
 * opened from PROGDIR:Libs), in several ways, and the samples' CRC-32
 * printed (big-endian words, left and right interleaved) with the
 * stream's details. The host's lines and the 68k's must match each other
 * and tests/expected.txt.
 *
 *   mpga_test [-w out.raw] [-n frames] [-way n] file...
 *
 * Each file: as it is (the bitstream hook, from memory), 22 kHz, 11 kHz,
 * mono, mono at 22 kHz, at half loudness, through dos.library (the file's
 * name), from a second in (MPEGA_seek, MPEGA_time), and through
 * MPGA_decode_to into small rings in each of its formats. -w writes the
 * first way's samples (for tools/snr.py); -n decodes only that many
 * frames each way; -way only the nth way (0 to 7: make prof).
 *
 * (c) 2026 Brian King, MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <libraries/mpega.h>
#if defined(__amigaos__) && !defined(MPGA_STATIC)
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/mpega.h>
struct Library *MPEGABase;
#ifndef MPGA_TEST_LIB
#define MPGA_TEST_LIB AMBMPEGANAME
#endif
#else
MPEGA_STREAM *MPEGA_open(char *, MPEGA_CTRL *);
void MPEGA_close(MPEGA_STREAM *);
LONG MPEGA_decode_frame(MPEGA_STREAM *, WORD **);
LONG MPEGA_seek(MPEGA_STREAM *, ULONG);
LONG MPEGA_time(MPEGA_STREAM *, ULONG *);
LONG MPEGA_find_sync(BYTE *, LONG);
LONG MPEGA_scale(MPEGA_STREAM *, LONG);
LONG MPGA_decode_to(MPEGA_STREAM *, struct MpgaOut *);
#endif

static UBYTE *data;
static LONG data_len, data_pos;

#ifdef __amigaos__
static ULONG bs_func(register struct Hook *h __asm("a0"), register APTR handle __asm("a2"),
                     register MPEGA_ACCESS *a __asm("a1"))
#else
static ULONG bs_func(struct Hook *h, APTR handle, MPEGA_ACCESS *a)
#endif
{
    (void)h;
    (void)handle;
    switch (a->func) {
    case MPEGA_BSFUNC_OPEN:
        data_pos = 0;
        a->data.open.stream_size = data_len;
        return 1;
    case MPEGA_BSFUNC_CLOSE:
        return 0;
    case MPEGA_BSFUNC_READ: {
        LONG n = a->data.read.num_bytes;
        if (n > data_len - data_pos)
            n = data_len - data_pos;
        if (n > 0) {
            memcpy(a->data.read.buffer, data + data_pos, n);
            data_pos += n;
        }
        return n < 0 ? 0 : n;
    }
    case MPEGA_BSFUNC_SEEK:
        if (a->data.seek.abs_byte_seek_pos < 0 || a->data.seek.abs_byte_seek_pos > data_len)
            return 1;
        data_pos = a->data.seek.abs_byte_seek_pos;
        return 0;
    }
    return 0;
}

static ULONG crc_tab[256];

static void crc_init(void)
{
    ULONG i, j, c;
    for (i = 0; i < 256; i++) {
        for (c = i, j = 0; j < 8; j++)
            c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crc_tab[i] = c;
    }
}

static ULONG crc_words(ULONG c, const WORD *w, ULONG n)
{
    c = ~c;
    while (n--) {
        UBYTE b[2];
        b[0] = (UBYTE)((UWORD)*w >> 8);
        b[1] = (UBYTE)*w++;
        c = crc_tab[(c ^ b[0]) & 0xff] ^ (c >> 8);
        c = crc_tab[(c ^ b[1]) & 0xff] ^ (c >> 8);
    }
    return ~c;
}

static const char *base(const char *p)
{
    const char *s = strrchr(p, '/'), *t = strrchr(p, ':');
    if (t > s)
        s = t;
    return s ? s + 1 : p;
}

enum { AS_IS, HALF, QUARTER, MONO, MONO_HALF, LOUD50, BYNAME, SEEK,
       TO_S16, TO_S16I, TO_S8, TO_S8D, TO_S14, TO_S14_MONO, MEDIUM, MEDIUM_HALF, WAYS };
static const char *const way_name[WAYS] = {
    "as it is", "22 kHz", "11 kHz", "mono", "mono 22 kHz", "half loudness", "by name", "from 1 s",
    "to 16-bit rings", "to AHI's 16-bit stereo", "to Paula's 8 bits", "to 8 bits dithered, 3 times as loud",
    "to Paula's 14 bits", "to 14 bits, mono", "medium quality", "medium quality, 22 kHz"
};

#define RING 4096                   /* (MPGA_decode_to's rings: small, so they wrap) */

static ULONG crc_bytes(ULONG c, const UBYTE *p, ULONG n)
{
    c = ~c;
    while (n--)
        c = crc_tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

/* The n samples a channel MPGA_decode_to wrote from at into its rings,
 * into the CRC: each sample's bytes, channel after channel (S16, S16I as
 * the words MPEGA_decode_frame gives, so the same CRC) */
static ULONG ring_crc(ULONG c, const struct MpgaOut *o, ULONG at, ULONG n, int ch)
{
    ULONG i;
    int k;
    for (i = 0; i < n; i++, at++) {
        UBYTE b[8];
        int nb = 0;
        for (k = 0; k < (o->mo_Format == MPGA_FMT_S16I ? 1 : 2); k++) {
            int c2 = ch > 1 ? k : 0;
            switch (o->mo_Format) {
            case MPGA_FMT_S16: {
                WORD v = ((WORD *)o->mo_Buffer[c2])[at & (RING - 1)];
                b[nb++] = (UBYTE)((UWORD)v >> 8);
                b[nb++] = (UBYTE)v;
                break;
            }
            case MPGA_FMT_S16I: {
                int j;
                for (j = 0; j < ch; j++) {
                    WORD v = ((WORD *)o->mo_Buffer[0])[(at * ch + j) & (2 * RING - 1)];
                    b[nb++] = (UBYTE)((UWORD)v >> 8);
                    b[nb++] = (UBYTE)v;
                }
                if (ch == 1) {
                    b[nb] = b[nb - 2];
                    b[nb + 1] = b[nb - 1];
                    nb += 2;
                }
                break;
            }
            case MPGA_FMT_S14:
                b[nb++] = ((UBYTE *)o->mo_Buffer[c2])[at & (RING - 1)];
                b[nb++] = ((UBYTE *)o->mo_Buffer[2 + c2])[at & (RING - 1)];
                break;
            default:
                b[nb++] = ((UBYTE *)o->mo_Buffer[c2])[at & (RING - 1)];
                break;
            }
        }
        c = crc_bytes(c, b, nb);
    }
    return c;
}

static int one(const char *file, int way, LONG frames, FILE *w)
{
    static WORD l[MPEGA_PCM_SIZE], r[MPEGA_PCM_SIZE], out[MPEGA_PCM_SIZE * 2];
    WORD *pcm[2];
    struct Hook hook;
    MPEGA_CTRL c;
    MPEGA_STREAM *s;
    LONG n, i, nf = 0, err = 0;
    ULONG samples = 0, crc = 0, t = 0;
    WORD div = way == HALF || way == MONO_HALF || way == MEDIUM_HALF ? 2 : way == QUARTER ? 4 : 1;
    WORD q = way == MEDIUM || way == MEDIUM_HALF ? MPEGA_QUALITY_MEDIUM : MPEGA_QUALITY_HIGH;
    WORD mono = way == MONO || way == MONO_HALF || way == TO_S14_MONO;
    static WORD r16[2][RING], ri[2 * RING];
    static UBYTE r8[4][RING];
    struct MpgaOut o;
    UWORD peak = 0;

    pcm[0] = l;
    pcm[1] = r;
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (ULONG (*)())bs_func;
    memset(&c, 0, sizeof(c));
    c.bs_access = way == BYNAME ? NULL : &hook;
    c.layer_1_2.mono.freq_div = c.layer_1_2.stereo.freq_div = div;
    c.layer_3.mono.freq_div = c.layer_3.stereo.freq_div = div;
    c.layer_1_2.mono.quality = c.layer_1_2.stereo.quality = q;
    c.layer_3.mono.quality = c.layer_3.stereo.quality = q;
    c.layer_1_2.force_mono = c.layer_3.force_mono = mono;
    c.check_mpeg = 1;
    if (!(s = MPEGA_open((char *)file, &c))) {
        printf("%s, %s: not opened\n", base(file), way_name[way]);
        return 1;
    }
    if (way == AS_IS)
        printf("%s: MPEG-%d layer %d, mode %d, %ld Hz, %d kbit/s, %d channels, %lu ms\n", base(file),
               s->norm, s->layer, s->mode, (long)s->frequency, s->bitrate, s->channels,
               (unsigned long)s->ms_duration);
    if (way == LOUD50)
        MPEGA_scale(s, 50);
    if (way == TO_S8D)                  /* (clipped: counted, the dither kept in range) */
        MPEGA_scale(s, 300);
    if (way == SEEK && (n = MPEGA_seek(s, 1000)) < 0)
        err = n;
    memset(&o, 0, sizeof(o));
    o.mo_Mask = RING - 1;
    o.mo_Index = RING - 100;        /* (each ring wraps in the first frame) */
    switch (way) {
    case TO_S16:
        o.mo_Format = MPGA_FMT_S16;
        o.mo_Buffer[0] = r16[0];
        o.mo_Buffer[1] = r16[1];
        break;
    case TO_S16I:
        o.mo_Format = MPGA_FMT_S16I;
        o.mo_Buffer[0] = ri;
        o.mo_Mask = 2 * RING - 1;
        break;
    case TO_S8:
    case TO_S8D:
    case TO_S14:
    case TO_S14_MONO:
        o.mo_Format = way == TO_S8 ? MPGA_FMT_S8 : way == TO_S8D ? MPGA_FMT_S8D : MPGA_FMT_S14;
        for (i = 0; i < 4; i++)
            o.mo_Buffer[i] = r8[i];
        break;
    }
    while (!err && nf < frames) {
        if (way >= TO_S16 && way <= TO_S14_MONO) {
            ULONG at = o.mo_Index;
            n = MPGA_decode_to(s, &o);
            if (n > 0) {
                crc = ring_crc(crc, &o, at, (ULONG)n, s->dec_channels);
                if (o.mo_Peak[0] > peak)
                    peak = o.mo_Peak[0];
                if (o.mo_Peak[1] > peak)
                    peak = o.mo_Peak[1];
                nf++;
                samples += n;
                continue;
            }
        } else
            n = MPEGA_decode_frame(s, pcm);
        if (n == MPEGA_ERR_EOF)
            break;
        if (n < 0) {
            err = n;
            break;
        }
        nf++;
        for (i = 0; i < n; i++) {
            out[2 * i] = l[i];
            out[2 * i + 1] = s->dec_channels > 1 ? r[i] : l[i];
        }
        crc = crc_words(crc, out, n * 2);
        if (w) {                    /* (big-endian, whatever this computer's order) */
            static UBYTE be[MPEGA_PCM_SIZE * 4];
            for (i = 0; i < n * 2; i++) {
                be[2 * i] = (UBYTE)((UWORD)out[i] >> 8);
                be[2 * i + 1] = (UBYTE)out[i];
            }
            fwrite(be, 2, n * 2, w);
        }
        samples += n;
    }
    MPEGA_time(s, &t);
    printf("%s, %s: %ld Hz, %d channels: %ld frames, %lu samples, %lu ms, crc %08lx%s", base(file),
           way_name[way], (long)s->dec_frequency, s->dec_channels, (long)nf, (unsigned long)samples,
           (unsigned long)t, (unsigned long)crc, err ? " (error)" : "");
    if (way >= TO_S16 && way <= TO_S14_MONO)
        printf(", peak %u, %lu clipped", (unsigned)peak, (unsigned long)o.mo_Clipped);
    printf("\n");
    MPEGA_close(s);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *w = NULL;
    LONG frames = 0x7fffffff;
    int i, way, only = -1;

    crc_init();
#ifdef MPGA_STATIC
    {   /* (make prof: where the program's code is, for tools/prof.py) */
        FILE *b = fopen("prof.base", "w");
        if (b) {
            fprintf(b, "code at %08lx\n", (unsigned long)main);
            fclose(b);
        }
    }
#endif
#if defined(__amigaos__) && !defined(MPGA_STATIC)
    if (!(MPEGABase = OpenLibrary((CONST_STRPTR)"PROGDIR:Libs/" MPGA_TEST_LIB, AMBMPEGAVERSION))) {
        printf("no %s\n", MPGA_TEST_LIB);
        return 20;
    }
#endif
    for (i = 1; i < argc; i++) {
        FILE *f;
        if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            w = fopen(argv[++i], "wb");
            continue;
        }
        if (!strcmp(argv[i], "-way") && i + 1 < argc) {
            only = atol(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            frames = atol(argv[++i]);
            continue;
        }
        if (!(f = fopen(argv[i], "rb"))) {
            printf("%s: not there\n", argv[i]);
            continue;
        }
        fseek(f, 0, SEEK_END);
        data_len = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc(data_len + 1);
        fread(data, 1, data_len, f);
        fclose(f);
        printf("%s: find_sync %ld\n", base(argv[i]), (long)MPEGA_find_sync((BYTE *)data, data_len));
        for (way = 0; way < WAYS; way++) {
            if (only >= 0 && way != only)
                continue;
            BOOL wr = only >= 0 ? way == only : way == AS_IS;      /* (-w: that way's samples) */
            one(argv[i], way, frames, wr ? w : NULL);
            if (w && wr) {
                fclose(w);
                w = NULL;
            }
        }
        free(data);
    }
#if defined(__amigaos__) && !defined(MPGA_STATIC)
    CloseLibrary(MPEGABase);
#endif
    return 0;
}
