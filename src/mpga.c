/*
 * ambmpega.library: MPEG audio (MPEG-1, MPEG-2 and MPEG-2.5; Layers I,
 * II and III) decoded to 16-bit PCM by libmad (third_party/libmad),
 * behind mpega.library's functions (include/libraries/mpega.h): a program
 * written for mpega.library works unchanged, only the name it opens
 * differs. Made for Ambiance, which can choose it or LIBS:mpega.library.
 *
 * A stream: MPEGA_open (the file named, or a program's bitstream hook),
 * MPEGA_decode_frame for each frame's samples, MPEGA_seek and MPEGA_time
 * in milliseconds, MPEGA_scale its loudness, MPEGA_close. Each stream is
 * used by one task at a time; libmad's working stack is the stream's own
 * (16 KB: its Layer III decoding has 9 KB of arrays on the stack), so a
 * caller with a small stack is safe.
 *
 * (c) 2026 Brian King. ambmpega.library is free software under the GNU
 * General Public License, version 3 or (at your option) any later version
 * (LICENSE); libmad inside is GPL-2.0-or-later
 * (third_party/libmad/COPYING).
 */
#include "config.h"
#include <string.h>
#include "global.h"
#include "synth.h"
#include <libraries/mpega.h>

#ifdef __amigaos__
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "stabs.h"

#ifndef MPGA_STATIC                 /* (MPGA_STATIC: built into a program, make prof) */
const char LibName[] = MPGA_LIBNAME;
const char LibIdString[] = MPGA_LIBID;
static const char __attribute__((used)) LibVerTag[] = "$VER: " MPGA_LIBID;
const UWORD LibVersion = 1;
const UWORD LibRevision = 2;

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
#endif

void *mpga_malloc(size_t n)
{
    return AllocVec(n, MEMF_ANY);
}

void *mpga_calloc(size_t n, size_t m)
{
    return AllocVec(n * m, MEMF_ANY | MEMF_CLEAR);
}

void mpga_free(void *p)
{
    if (p)
        FreeVec(p);
}

/* timer.c's mad_timer_string, which nothing here calls, is all that would
 * want sprintf: built with -Dsprintf=mpga_nosprintf (Makefile) */
int mpga_nosprintf(char *s, const char *f, ...)
{
    (void)f;
    *s = 0;
    return 0;
}

/* src/stack.s: f(arg) on the stack sss describes (exec's StackSwap); a
 * hook called as CallHookPkt calls it */
LONG mpga_onstack(LONG (*f)(APTR), APTR arg, struct StackSwapStruct *sss);
ULONG mpga_callhook(struct Hook *h, APTR object, APTR message);
#define STACK_SIZE  16384
#define ALLOC(n)    AllocVec(n, MEMF_ANY | MEMF_CLEAR)
#define FREE(p)     FreeVec(p)
#else
/* on the host (tests/mpga_host.c): plain functions, stdio for files */
#include <stdio.h>
#include <stdlib.h>
#define ALLOC(n)    calloc(1, n)
#define FREE(p)     free(p)
#endif

#define DEF_BUFFER  16384           /* bytes read at a time */
#define MAX_SKIP    64              /* damaged frames passed over in one call */

struct Mpga {
    MPEGA_STREAM pub;               /* first: what the program reads */
    struct Hook *hook;              /* the program's, or NULL */
    APTR handle;                    /* its OPEN's */
#ifdef __amigaos__
    BPTR fh;                        /* the file's, without a hook */
    struct StackSwapStruct sss;
    UBYTE *stack;
#else
    FILE *fp;
#endif
    ULONG size;                     /* the stream's bytes, 0 if not known */
    ULONG start;                    /* the first frame's offset (past an ID3v2 tag) */
    ULONG pos;                      /* the stream's offset of buf + len */
    UBYTE *buf;
    ULONG bufsize;                  /* buf's room for data (MAD_BUFFER_GUARD more after) */
    ULONG len;                      /* bytes in buf */
    BOOL eof;                       /* the stream has ended (its guard bytes in buf) */
    BOOL skip_info;                 /* the first frame is a Xing/Info frame: not sound */
    BOOL check;                     /* MPEGA_CTRL's check_mpeg */
    UBYTE layer, out_ch, div, pad;
    LONG scale;                     /* 100: as decoded */
    mad_fixed_t gain;               /* scale's, MAD_F_ONE for 100 */
    ULONG out_samples;              /* given out, at dec_frequency */
    ULONG xing_frames, xing_bytes;  /* the VBR header's (0 if none) */
    UBYTE toc[100];
    BOOL has_toc;
    struct mad_stream ms;
    struct mad_frame mf;
    struct mad_synth sy;
    /* a call's arguments, for the function run on the stream's own stack */
    WORD **pcm;
    struct MpgaOut *out;            /* MPGA_decode_to's, else MPEGA_decode_frame's pcm */
    ULONG arg;
    ULONG rng;                      /* the dither's noise */
};

/* ---- the stream's bytes: the program's hook, or the file */
static LONG access_call(struct Mpga *m, MPEGA_ACCESS *a)
{
#ifdef __amigaos__
    return (LONG)mpga_callhook(m->hook, m->handle, a);
#else
    return ((LONG (*)(struct Hook *, APTR, MPEGA_ACCESS *))m->hook->h_Entry)(m->hook, m->handle, a);
#endif
}

static BOOL bs_open(struct Mpga *m, char *name)
{
    if (m->hook) {
        MPEGA_ACCESS a;
        memset(&a, 0, sizeof(a));
        a.func = MPEGA_BSFUNC_OPEN;
        a.data.open.stream_name = name;
        a.data.open.buffer_size = (LONG)m->bufsize;
        m->handle = NULL;
        m->handle = (APTR)access_call(m, &a);
        m->size = a.data.open.stream_size > 0 ? (ULONG)a.data.open.stream_size : 0;
        return m->handle != NULL;
    }
#ifdef __amigaos__
    if (!name || !(m->fh = Open((CONST_STRPTR)name, MODE_OLDFILE)))
        return FALSE;
    if (Seek(m->fh, 0, OFFSET_END) >= 0) {
        LONG n = Seek(m->fh, 0, OFFSET_BEGINNING);
        m->size = n > 0 ? (ULONG)n : 0;
    }
    return TRUE;
#else
    if (!name || !(m->fp = fopen(name, "rb")))
        return FALSE;
    fseek(m->fp, 0, SEEK_END);
    m->size = (ULONG)ftell(m->fp);
    fseek(m->fp, 0, SEEK_SET);
    return TRUE;
#endif
}

static void bs_close(struct Mpga *m)
{
    if (m->hook) {
        if (m->handle) {
            MPEGA_ACCESS a;
            memset(&a, 0, sizeof(a));
            a.func = MPEGA_BSFUNC_CLOSE;
            access_call(m, &a);
            m->handle = NULL;
        }
        return;
    }
#ifdef __amigaos__
    if (m->fh)
        Close(m->fh);
    m->fh = 0;
#else
    if (m->fp)
        fclose(m->fp);
    m->fp = NULL;
#endif
}

static LONG bs_read(struct Mpga *m, UBYTE *to, LONG n)
{
    LONG got;
    if (m->hook) {
        MPEGA_ACCESS a;
        memset(&a, 0, sizeof(a));
        a.func = MPEGA_BSFUNC_READ;
        a.data.read.buffer = to;
        a.data.read.num_bytes = n;
        got = access_call(m, &a);
    } else {
#ifdef __amigaos__
        got = Read(m->fh, to, n);
#else
        got = (LONG)fread(to, 1, (size_t)n, m->fp);
#endif
    }
    if (got < 0)
        got = 0;
    if (got > n)
        got = n;
    return got;
}

static BOOL bs_seek(struct Mpga *m, ULONG to)
{
    if (m->hook) {
        MPEGA_ACCESS a;
        memset(&a, 0, sizeof(a));
        a.func = MPEGA_BSFUNC_SEEK;
        a.data.seek.abs_byte_seek_pos = (LONG)to;
        return access_call(m, &a) == 0;
    }
#ifdef __amigaos__
    return Seek(m->fh, (LONG)to, OFFSET_BEGINNING) >= 0;
#else
    return fseek(m->fp, (long)to, SEEK_SET) == 0;
#endif
}

/* buf filled up behind what libmad has not taken yet (from next_frame);
 * at the stream's end, MAD_BUFFER_GUARD zeros after its last byte, so
 * libmad decodes the last frame. FALSE: nothing more to give it. */
static BOOL fill(struct Mpga *m)
{
    ULONG keep = 0, want;
    LONG got;
    if (m->eof)
        return FALSE;
    if (m->ms.buffer && m->ms.next_frame) {
        keep = (ULONG)(m->ms.bufend - m->ms.next_frame);
        if (keep && m->ms.next_frame != m->buf)
            memmove(m->buf, m->ms.next_frame, keep);
    } else
        keep = m->len;              /* (the start: what MPEGA_open read already) */
    m->len = keep;
    want = m->bufsize - keep;
    if (want == 0) {                /* a frame bigger than the buffer: free format */
        m->len = 0;                 /* (given up: libmad looks for the next) */
        want = m->bufsize;
    }
    got = bs_read(m, m->buf + m->len, (LONG)want);
    m->len += (ULONG)got;
    m->pos += (ULONG)got;
    if (got == 0) {
        memset(m->buf + m->len, 0, MAD_BUFFER_GUARD);
        m->len += MAD_BUFFER_GUARD;
        m->eof = TRUE;
    }
    mad_stream_buffer(&m->ms, m->buf, m->len);
    return TRUE;
}

/* ---- the first frame: what kind of stream this is */

/* bytes of an ID3v2 tag at p (10 more if it has a footer), or 0 */
static ULONG id3v2_size(const UBYTE *p, ULONG n)
{
    if (n < 10 || p[0] != 'I' || p[1] != 'D' || p[2] != '3' || p[3] == 0xff || p[4] == 0xff ||
        ((p[6] | p[7] | p[8] | p[9]) & 0x80))
        return 0;
    return 10 + (((ULONG)p[6] << 21) | ((ULONG)p[7] << 14) | ((ULONG)p[8] << 7) | p[9]) +
           ((p[5] & 0x10) ? 10 : 0);
}

static ULONG be32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

/* A Xing or Info header in the Layer III frame at f (its header h):
 * its frames, bytes and table of contents. TRUE if there is one. */
static BOOL xing(struct Mpga *m, const UBYTE *f, ULONG avail, const struct mad_header *h)
{
    ULONG off, flags;
    const UBYTE *x;
    int mono = h->mode == MAD_MODE_SINGLE_CHANNEL;
    if (h->layer != MAD_LAYER_III)
        return FALSE;
    if (h->flags & MAD_FLAG_LSF_EXT)
        off = mono ? 9 : 17;
    else
        off = mono ? 17 : 32;
    off += 4 + ((h->flags & MAD_FLAG_PROTECTION) ? 2 : 0);
    if (off + 120 > avail)
        return FALSE;
    x = f + off;
    if (memcmp(x, "Xing", 4) && memcmp(x, "Info", 4))
        return FALSE;
    flags = be32(x + 4);
    x += 8;
    if (flags & 1) {
        m->xing_frames = be32(x);
        x += 4;
    }
    if (flags & 2) {
        m->xing_bytes = be32(x);
        x += 4;
    }
    if (flags & 4) {
        memcpy(m->toc, x, 100);
        m->has_toc = TRUE;
    }
    return TRUE;
}

static ULONG frame_samples(const struct mad_header *h)
{
    return 32 * MAD_NSBSAMPLES(h);
}

/* libmad's options for the rate asked for: at a half or a quarter of it,
 * the half-rate synthesis, and Layer III's subbands above what is heard
 * left out (MPGA_OPTION_QUARTER: third_party/libmad/CHANGES.mpga) */
static int mad_options(const struct Mpga *m)
{
    return m->div == 4 ? MAD_OPTION_HALFSAMPLERATE | MPGA_OPTION_QUARTER :
           m->div == 2 ? MAD_OPTION_HALFSAMPLERATE : 0;
}

/* the decoding's settings for this stream, as the program asked */
static void settings(struct Mpga *m, const MPEGA_CTRL *c)
{
    const MPEGA_LAYER *l = m->layer == 3 ? &c->layer_3 : &c->layer_1_2;
    const MPEGA_OUTPUT *o;
    int div;
    m->out_ch = (m->pub.channels == 2 && !l->force_mono) ? 2 : 1;
    o = m->out_ch == 2 ? &l->stereo : &l->mono;
    div = o->freq_div;
    if (div != 1 && div != 2 && div != 4) {
        LONG max = o->freq_max > 0 ? o->freq_max : 48000;
        div = 1;
        while (div < 4 && m->pub.frequency / div > max)
            div <<= 1;
    }
    m->div = (UBYTE)div;
    m->pub.dec_channels = m->out_ch;
    m->pub.dec_quality = o->quality < 0 ? 0 : o->quality > 2 ? 2 : o->quality;
    /* below the highest: the synthesis window's products 16-bit (MULS.W) */
    m->sy.mpga_window16 = m->pub.dec_quality < MPEGA_QUALITY_HIGH;
    m->pub.dec_frequency = m->pub.frequency / div;
    m->ms.options = mad_options(m);
}

/* MPEGA_open's work, on the stream's stack: TRUE if it is MPEG audio */
static LONG open_work(APTR arg)
{
    struct Mpga *m = arg;
    struct mad_header h, h2;
    struct mad_stream probe;
    ULONG skip, tries = 0;
    LONG got;

    /* the first block; an ID3v2 tag passed over */
    got = bs_read(m, m->buf, (LONG)m->bufsize);
    m->len = (ULONG)got;
    m->pos = (ULONG)got;
    if (got < 4)
        return FALSE;
    skip = id3v2_size(m->buf, m->len);
    if (skip) {
        if (skip < m->len) {
            memmove(m->buf, m->buf + skip, m->len - skip);
            m->len -= skip;
        } else {
            if (!bs_seek(m, skip)) {        /* (read past it, then) */
                ULONG left = skip - m->len;
                while (left) {
                    got = bs_read(m, m->buf, (LONG)(left < m->bufsize ? left : m->bufsize));
                    if (got <= 0)
                        return FALSE;
                    left -= (ULONG)got;
                }
            }
            m->pos = skip;
            got = bs_read(m, m->buf, (LONG)m->bufsize);
            m->len = got > 0 ? (ULONG)got : 0;
            m->pos += m->len;
        }
    }

    /* its first frame's header (libmad checks that another follows it) */
    for (;;) {
        mad_stream_init(&probe);
        mad_stream_buffer(&probe, m->buf, m->len);
        mad_header_init(&h);
        while (mad_header_decode(&h, &probe)) {
            if (probe.error == MAD_ERROR_BUFLEN || !MAD_RECOVERABLE(probe.error))
                break;
        }
        if (!probe.error || probe.error != MAD_ERROR_BUFLEN || tries++ > 0)
            break;
        /* (not in the first block: the next one) */
        if (m->len > 4096) {
            memmove(m->buf, m->buf + m->len - 4096, 4096);
            m->len = 4096;
        }
        got = bs_read(m, m->buf + m->len, (LONG)(m->bufsize - m->len));
        if (got <= 0)
            break;
        m->len += (ULONG)got;
        m->pos += (ULONG)got;
    }
    if (probe.error) {
        mad_stream_finish(&probe);
        return FALSE;
    }
    m->start = m->pos - m->len + (ULONG)(probe.this_frame - m->buf);
    m->skip_info = xing(m, probe.this_frame, (ULONG)(m->buf + m->len - probe.this_frame), &h);
    /* the next frame's header: with check_mpeg, of the same kind (libmad
     * has already seen a header where this frame ends); after an Info
     * frame, the one that tells the stream's bitrate and mode */
    h2 = h;
    if (!mad_header_decode(&h2, &probe)) {
        if (m->check && (h2.layer != h.layer || h2.samplerate != h.samplerate)) {
            mad_stream_finish(&probe);
            return FALSE;
        }
        if (m->skip_info)
            h = h2;                     /* (its bitrate and mode: the sound's first frame's) */
    } else if (m->check && probe.error != MAD_ERROR_BUFLEN) {
        mad_stream_finish(&probe);
        return FALSE;
    }
    mad_stream_finish(&probe);

    m->layer = (UBYTE)h.layer;
    m->pub.norm = (h.flags & (MAD_FLAG_LSF_EXT | MAD_FLAG_MPEG_2_5_EXT)) ? 2 : 1;
    m->pub.layer = (WORD)h.layer;
    m->pub.mode = h.mode == MAD_MODE_STEREO ? MPEGA_MODE_STEREO :
                  h.mode == MAD_MODE_JOINT_STEREO ? MPEGA_MODE_J_STEREO :
                  h.mode == MAD_MODE_DUAL_CHANNEL ? MPEGA_MODE_DUAL : MPEGA_MODE_MONO;
    m->pub.bitrate = (WORD)(h.bitrate / 1000);
    m->pub.frequency = (LONG)h.samplerate;
    m->pub.channels = h.mode == MAD_MODE_SINGLE_CHANNEL ? 1 : 2;
    m->pub.private_bit = (h.private_bits & MAD_PRIVATE_HEADER) ? 1 : 0;
    m->pub.copyright = (h.flags & MAD_FLAG_COPYRIGHT) ? 1 : 0;
    m->pub.original = (h.flags & MAD_FLAG_ORIGINAL) ? 1 : 0;
    if (m->xing_frames && h.samplerate)
        m->pub.ms_duration = (ULONG)(((unsigned long long)m->xing_frames * frame_samples(&h) * 1000) /
                                     h.samplerate);
    else if (m->size > m->start && h.bitrate)
        m->pub.ms_duration = (ULONG)(((unsigned long long)(m->size - m->start) * 8000) / h.bitrate);

    /* libmad from the first frame on (what is in buf already) */
    memmove(m->buf, m->buf + (m->start - (m->pos - m->len)), m->pos - m->start);
    m->len = m->pos - m->start;
    mad_stream_buffer(&m->ms, m->buf, m->len);
    return TRUE;
}

/* ---- the samples */

/* The synthesised frame at the rate asked for (a half-rate synthesis
 * halved again for freq_div 4) and its loudness, in place: n samples a
 * channel. */
static ULONG prepare(struct Mpga *m)
{
    struct mad_pcm *p = &m->sy.pcm;
    ULONG n = p->length, i, ch, nch = p->channels;
    mad_fixed_t g = m->gain;
    for (ch = 0; ch < nch; ch++) {
        mad_fixed_t *x = p->samples[ch];
        if (m->div == 4) {
            for (i = 0; i < n / 2; i++)
                x[i] = (x[2 * i] >> 1) + (x[2 * i + 1] >> 1);
        }
        if (g != MAD_F_ONE)
            for (i = 0; i < (m->div == 4 ? n / 2 : n); i++)
                x[i] = mad_f_mul(x[i], g);
    }
    return m->div == 4 ? n / 2 : n;
}

/* ---- the writers: n samples rounded to 16 bits and clipped (counted),
 * their range followed for the peak, then written. In 68k assembler
 * (src/out_68k.s) on the Amiga, the same bytes. */
struct MpgaRange {
    LONG max, min;                  /* the rounded samples', libmad's scale */
    ULONG clipped;
};

#ifdef MPGA_OUT_ASM
void mpga_put16(const mad_fixed_t *x, WORD *d, ULONG n, ULONG step, struct MpgaRange *r);
void mpga_put8(const mad_fixed_t *x, BYTE *d, ULONG n, struct MpgaRange *r);
void mpga_put14(const mad_fixed_t *x, BYTE *hi, UBYTE *lo, ULONG n, struct MpgaRange *r);
#else
static inline mad_fixed_t rounded(mad_fixed_t x, struct MpgaRange *r)
{
    x += 1L << (MAD_F_FRACBITS - 16);
    if (x > MAD_F_ONE - 1) {
        x = MAD_F_ONE - 1;
        r->clipped++;
    } else if (x < -MAD_F_ONE) {
        x = -MAD_F_ONE;
        r->clipped++;
    }
    if (x > r->max)
        r->max = x;
    if (x < r->min)
        r->min = x;
    return x;
}

static void mpga_put16(const mad_fixed_t *x, WORD *d, ULONG n, ULONG step, struct MpgaRange *r)
{
    while (n--) {
        *d = (WORD)(rounded(*x++, r) >> (MAD_F_FRACBITS + 1 - 16));
        d = (WORD *)((UBYTE *)d + step);
    }
}

static void mpga_put8(const mad_fixed_t *x, BYTE *d, ULONG n, struct MpgaRange *r)
{
    while (n--)
        *d++ = (BYTE)(rounded(*x++, r) >> (MAD_F_FRACBITS + 1 - 8));
}

static void mpga_put14(const mad_fixed_t *x, BYTE *hi, UBYTE *lo, ULONG n, struct MpgaRange *r)
{
    while (n--) {
        mad_fixed_t v = rounded(*x++, r);
        *hi++ = (BYTE)(v >> (MAD_F_FRACBITS + 1 - 8));
        *lo++ = (UBYTE)((v >> (MAD_F_FRACBITS + 1 - 14)) & 0x3f);
    }
}
#endif

static void range_init(struct MpgaRange *r)
{
    r->max = -MAD_F_ONE;
    r->min = MAD_F_ONE - 1;
    r->clipped = 0;
}

/* the range's peak: the largest size of a 16-bit sample in it */
static UWORD range_peak(const struct MpgaRange *r)
{
    LONG hi = r->max >> (MAD_F_FRACBITS + 1 - 16), lo = r->min >> (MAD_F_FRACBITS + 1 - 16);
    LONG p = hi > -lo ? hi : -lo;
    if (p < 0)
        p = 0;
    return (UWORD)(p > 32767 ? 32767 : p);
}

/* the frame as mpega.library gives it: a WORD buffer each channel */
static LONG output(struct Mpga *m, WORD **pcm)
{
    const struct mad_pcm *p = &m->sy.pcm;
    ULONG n = prepare(m), ch;
    struct MpgaRange r;
    range_init(&r);
    for (ch = 0; ch < m->out_ch; ch++)
        mpga_put16(p->samples[p->channels > 1 ? ch : 0], pcm[ch], n, 2, &r);
    m->out_samples += n;
    return (LONG)n;
}

/* ---- MPGA_decode_to's formats: each written at its ring's index, in
 * runs that do not wrap */

/* the next of the stream's dither noise: a uniform 0 to 255 */
static inline ULONG rnd8(struct Mpga *m)
{
    m->rng = m->rng * 1664525UL + 1013904223UL;
    return m->rng >> 24;
}

static void put8d(struct Mpga *m, const mad_fixed_t *x, BYTE *d, ULONG n, struct MpgaRange *r)
{
    while (n--) {
        mad_fixed_t v = *x++ + (1L << (MAD_F_FRACBITS - 16));
        LONG w;
        if (v > MAD_F_ONE - 1) {
            v = MAD_F_ONE - 1;
            r->clipped++;
        } else if (v < -MAD_F_ONE) {
            v = -MAD_F_ONE;
            r->clipped++;
        }
        if (v > r->max)
            r->max = v;
        if (v < r->min)
            r->min = v;
        w = v >> (MAD_F_FRACBITS + 1 - 16);
        w += (LONG)rnd8(m) - (LONG)rnd8(m) + 128;   /* triangular dither, an 8-bit step; rounded */
        if (w > 32767)
            w = 32767;
        else if (w < -32768)
            w = -32768;
        *d++ = (BYTE)(w >> 8);
    }
}

static LONG output_to(struct Mpga *m, struct MpgaOut *o)
{
    const struct mad_pcm *p = &m->sy.pcm;
    ULONG n = prepare(m), ch, mask = o->mo_Mask, size = mask + 1;
    ULONG clip = 0;
    for (ch = 0; ch < m->out_ch; ch++) {
        const mad_fixed_t *x = p->samples[p->channels > 1 ? ch : 0];
        ULONG left = n, at = o->mo_Index, k;
        struct MpgaRange r;
        range_init(&r);
        if (o->mo_Format == MPGA_FMT_S16I) {
            /* the index counts sample frames; the ring, WORDs */
            ULONG step = m->out_ch, pos = (at * step + ch) & mask;
            WORD *b = o->mo_Buffer[0];
            while (left) {
                k = size ? (mask - pos) / step + 1 : left;
                if (k > left)
                    k = left;
                mpga_put16(x, b + pos, k, step * 2, &r);
                x += k;
                left -= k;
                pos = (pos + k * step) & mask;
            }
        } else {
            while (left) {
                ULONG pos = at & mask;
                k = size ? size - pos : left;
                if (k > left)
                    k = left;
                switch (o->mo_Format) {
                case MPGA_FMT_S16:
                    mpga_put16(x, (WORD *)o->mo_Buffer[ch] + pos, k, 2, &r);
                    break;
                case MPGA_FMT_S8:
                    mpga_put8(x, (BYTE *)o->mo_Buffer[ch] + pos, k, &r);
                    break;
                case MPGA_FMT_S8D:
                    put8d(m, x, (BYTE *)o->mo_Buffer[ch] + pos, k, &r);
                    break;
                default:                    /* MPGA_FMT_S14 */
                    mpga_put14(x, (BYTE *)o->mo_Buffer[ch] + pos, (UBYTE *)o->mo_Buffer[2 + ch] + pos,
                               k, &r);
                    break;
                }
                x += k;
                at += k;
                left -= k;
            }
        }
        o->mo_Peak[ch] = range_peak(&r);
        clip += r.clipped;
    }
    if (m->out_ch == 1)
        o->mo_Peak[1] = o->mo_Peak[0];
    o->mo_Index += n;
    o->mo_Clipped += clip;
    m->out_samples += n;
    return (LONG)n;
}

/* Before the synthesis, what the output does not need left out of it.
 * Mono from stereo: the two channels' subband samples averaged into one
 * (the synthesis is linear), so that only one channel is synthesised.
 * At a half or a quarter of the rate: the subbands above it zero (Layer
 * III's are already: libmad left them out), so that the half-rate
 * synthesis has nothing above it to alias. */
static void reduce(struct Mpga *m)
{
    struct mad_frame *f = &m->mf;
    unsigned int ns = MAD_NSBSAMPLES(&f->header), s, sb, top;
    if (m->out_ch == 1 && f->header.mode != MAD_MODE_SINGLE_CHANNEL) {
        for (s = 0; s < ns; s++) {
            mad_fixed_t *l = f->sbsample[0][s];
            const mad_fixed_t *r = f->sbsample[1][s];
            for (sb = 0; sb < 32; sb++)
                l[sb] = (l[sb] >> 1) + (r[sb] >> 1);
        }
        f->header.mode = MAD_MODE_SINGLE_CHANNEL;   /* (one channel synthesised) */
    }
    top = m->div == 4 ? 8 : m->div == 2 ? 16 : 32;
    if (top < 32 && f->header.layer != MAD_LAYER_III) {
        unsigned int ch, nch = MAD_NCHANNELS(&f->header);
        for (ch = 0; ch < nch; ch++)
            for (s = 0; s < ns; s++)
                memset(&f->sbsample[ch][s][top], 0, (32 - top) * sizeof(mad_fixed_t));
    }
}

/* MPEGA_decode_frame's work, on the stream's stack */
static LONG decode_work(APTR arg)
{
    struct Mpga *m = arg;
    int bad = 0;
    if (!m->ms.buffer && !fill(m))
        return MPEGA_ERR_EOF;
    for (;;) {
        if (mad_frame_decode(&m->mf, &m->ms)) {
            if (m->ms.error == MAD_ERROR_BUFLEN) {
                if (!fill(m))
                    return MPEGA_ERR_EOF;
                continue;
            }
            if (MAD_RECOVERABLE(m->ms.error)) {
                /* a damaged frame, or the reservoir a seek left behind:
                 * the next one */
                if (++bad > MAX_SKIP)
                    return MPEGA_ERR_BADFRAME;
                continue;
            }
            return MPEGA_ERR_BADFRAME;
        }
        if (m->skip_info) {             /* the Xing/Info frame: not sound */
            m->skip_info = FALSE;
            continue;
        }
        break;
    }
    reduce(m);
    mad_synth_frame(&m->sy, &m->mf);
    return m->out ? output_to(m, m->out) : output(m, m->pcm);
}

/* MPEGA_seek's work: libmad again from the frame at ms */
static LONG seek_work(APTR arg)
{
    struct Mpga *m = arg;
    ULONG ms = m->arg, to, dur = m->pub.ms_duration;
    if (dur && ms >= dur)
        return MPEGA_ERR_EOF;
    if (m->has_toc && dur) {
        /* the VBR header's table: where each per cent of the time starts */
        ULONG bytes = m->xing_bytes ? m->xing_bytes : m->size - m->start;
        ULONG pc = (ULONG)(((unsigned long long)ms * 100) / dur), a, b, frac;
        if (pc > 99)
            pc = 99;
        a = m->toc[pc];
        b = pc < 99 ? m->toc[pc + 1] : 256;
        frac = (ULONG)(((unsigned long long)ms * 100 * 256) / dur) - pc * 256;   /* (of 256) */
        to = m->start + (ULONG)(((unsigned long long)(a * 256 + (b - a) * frac) * bytes) / (256 * 256));
    } else if (m->pub.bitrate)
        to = m->start + (ULONG)(((unsigned long long)ms * m->pub.bitrate) / 8);
    else
        return MPEGA_ERR_BADVALUE;
    if (m->size && to >= m->size)
        return MPEGA_ERR_EOF;
    if (!bs_seek(m, to))
        return MPEGA_ERR_BADVALUE;
    m->pos = to;
    m->len = 0;
    m->eof = FALSE;
    mad_stream_finish(&m->ms);
    mad_stream_init(&m->ms);
    m->ms.options = mad_options(m);
    mad_frame_mute(&m->mf);
    mad_synth_mute(&m->sy);
    m->skip_info = FALSE;
    m->out_samples = (ULONG)(((unsigned long long)ms * m->pub.dec_frequency) / 1000);
    return 0;
}

static LONG run(struct Mpga *m, LONG (*f)(APTR))
{
#ifdef __amigaos__
    return mpga_onstack(f, m, &m->sss);
#else
    return f(m);
#endif
}

static void freeit(struct Mpga *m)
{
    mad_synth_finish(&m->sy);
    mad_frame_finish(&m->mf);
    mad_stream_finish(&m->ms);
    if (m->buf)
        FREE(m->buf);
#ifdef __amigaos__
    if (m->stack)
        FREE(m->stack);
#endif
    FREE(m);
}

/* ---- mpega.library's functions */

MPEGA_STREAM *MPEGA_open(char *name, MPEGA_CTRL *ctrl)
{
    struct Mpga *m;
    if (!ctrl)
        return NULL;
    if (!(m = ALLOC(sizeof(*m))))
        return NULL;
    mad_stream_init(&m->ms);
    mad_frame_init(&m->mf);
    mad_synth_init(&m->sy);
    m->pub.handle = m;
    m->hook = ctrl->bs_access;
    m->check = ctrl->check_mpeg != 0;
    m->bufsize = ctrl->stream_buffer_size >= 4096 ? (ULONG)ctrl->stream_buffer_size & ~3UL : DEF_BUFFER;
    m->scale = 100;
    m->gain = MAD_F_ONE;
    m->rng = 0x2545F491UL;
    if (!(m->buf = ALLOC(m->bufsize + MAD_BUFFER_GUARD))) {
        freeit(m);
        return NULL;
    }
#ifdef __amigaos__
    if (!(m->stack = ALLOC(STACK_SIZE))) {
        freeit(m);
        return NULL;
    }
    m->sss.stk_Lower = m->stack;
    m->sss.stk_Upper = (ULONG)m->stack + STACK_SIZE;
    m->sss.stk_Pointer = (APTR)(m->sss.stk_Upper);
#endif
    if (!bs_open(m, name)) {
        freeit(m);
        return NULL;
    }
    if (!run(m, open_work)) {
        bs_close(m);
        freeit(m);
        return NULL;
    }
    settings(m, ctrl);
    return &m->pub;
}

void MPEGA_close(MPEGA_STREAM *s)
{
    struct Mpga *m = (struct Mpga *)s;
    if (!m)
        return;
    bs_close(m);
    freeit(m);
}

LONG MPEGA_decode_frame(MPEGA_STREAM *s, WORD **pcm)
{
    struct Mpga *m = (struct Mpga *)s;
    if (!m || !pcm || !pcm[0] || (m->out_ch > 1 && !pcm[1]))
        return MPEGA_ERR_BADVALUE;
    m->pcm = pcm;
    m->out = NULL;
    return run(m, decode_work);
}

/* ambmpega.library's own: the next frame straight into the program's
 * buffers, in the format its output wants */
LONG MPGA_decode_to(MPEGA_STREAM *s, struct MpgaOut *o)
{
    struct Mpga *m = (struct Mpga *)s;
    if (!m || !o || o->mo_Format > MPGA_FMT_S14)
        return MPEGA_ERR_BADVALUE;
    /* the buffers the format writes: one a channel (S14: two), or one */
    if (!o->mo_Buffer[0] || (o->mo_Format != MPGA_FMT_S16I && m->out_ch > 1 && !o->mo_Buffer[1]) ||
        (o->mo_Format == MPGA_FMT_S14 && (!o->mo_Buffer[2] || (m->out_ch > 1 && !o->mo_Buffer[3]))))
        return MPEGA_ERR_BADVALUE;
    m->out = o;
    return run(m, decode_work);
}

LONG MPEGA_seek(MPEGA_STREAM *s, ULONG ms)
{
    struct Mpga *m = (struct Mpga *)s;
    if (!m)
        return MPEGA_ERR_BADVALUE;
    m->arg = ms;
    return run(m, seek_work);
}

LONG MPEGA_time(MPEGA_STREAM *s, ULONG *ms)
{
    struct Mpga *m = (struct Mpga *)s;
    ULONG f;
    if (!m || !ms)
        return MPEGA_ERR_BADVALUE;
    f = (ULONG)m->pub.dec_frequency;
    *ms = f ? (m->out_samples / f) * 1000 + ((m->out_samples % f) * 1000) / f : 0;
    return 0;
}

/* a valid header's four bytes at p */
static BOOL header_ok(const UBYTE *p)
{
    return p[0] == 0xff && (p[1] & 0xe0) == 0xe0 &&
           ((p[1] >> 3) & 3) != 1 &&            /* version: not reserved */
           ((p[1] >> 1) & 3) != 0 &&            /* layer: not reserved */
           ((p[2] >> 4) & 15) != 15 &&          /* bitrate: not "bad" */
           ((p[2] >> 2) & 3) != 3 &&            /* sample rate: not reserved */
           (p[3] & 3) != 2;                     /* emphasis: not reserved */
}

LONG MPEGA_find_sync(BYTE *buffer, LONG size)
{
    const UBYTE *b = (const UBYTE *)buffer;
    LONG i;
    if (!b)
        return MPEGA_ERR_BADVALUE;
    for (i = 0; i + 4 <= size; i++)
        if (header_ok(b + i))
            return i;
    return MPEGA_ERR_NO_SYNC;
}

LONG MPEGA_scale(MPEGA_STREAM *s, LONG percent)
{
    struct Mpga *m = (struct Mpga *)s;
    if (!m || percent < 0 || percent > 799)
        return MPEGA_ERR_BADVALUE;
    m->scale = percent;
    m->gain = (mad_fixed_t)(((long long)MAD_F_ONE * percent) / 100);
    return 0;
}

#if defined(__amigaos__) && !defined(MPGA_STATIC)
/* ---- the library */
int __UserLibInit(struct Library *lib)
{
    (void)lib;
    SysBase = *(struct ExecBase *volatile *)4L;
    if (!(DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37)))
        return 1;
    return 0;
}

void __UserLibCleanup(void)
{
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
}

/* the jump table, in the .fd's order (from bias 30) */
ADDTABL_2(MPEGA_open, a0, a1);
ADDTABL_1(MPEGA_close, a0);
ADDTABL_2(MPEGA_decode_frame, a0, a1);
ADDTABL_2(MPEGA_seek, a0, d0);
ADDTABL_2(MPEGA_time, a0, a1);
ADDTABL_2(MPEGA_find_sync, a0, d0);
ADDTABL_2(MPEGA_scale, a0, d0);
ADDTABL_2(MPGA_decode_to, a0, a1);         /* (ambmpega.library's own, after mpega's) */
ADDTABL_END();
#endif
