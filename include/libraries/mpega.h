/*
 * mpega.library's interface, as amcempega.library offers it: MPEG audio
 * (MPEG-1, MPEG-2 and MPEG-2.5; Layers I, II and III) decoded to 16-bit
 * PCM. The same functions, structures and constants as Stephane Tavenard's
 * mpega.library 2 (its documentation, mpega.doc), so a program written for
 * it works unchanged with either library: only the name it opens differs.
 *
 * This header, mpga_lib.sfd and the clib, inline and proto headers made
 * from it are MIT: (c) 2026 Brian King. amcempega.library itself is
 * GPL-3.0-or-later (libmad inside): see README.md.
 */
#ifndef LIBRARIES_MPEGA_H
#define LIBRARIES_MPEGA_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef UTILITY_HOOKS_H
#include <utility/hooks.h>
#endif

#define AMCEMPEGANAME       "amcempega.library"
#define AMCEMPEGAVERSION    1
#define MPEGA_VERSION       2       /* mpega.library's own, for its programs */

/* Decoding qualities (MPEGA_OUTPUT's quality) */
#define MPEGA_QUALITY_LOW    0
#define MPEGA_QUALITY_MEDIUM 1
#define MPEGA_QUALITY_HIGH   2

/*
 * A bitstream hook (MPEGA_CTRL's bs_access) is called with the hook in a0,
 * the handle its MPEGA_BSFUNC_OPEN returned in a2 (NULL for that call) and
 * an MPEGA_ACCESS in a1:
 *   MPEGA_BSFUNC_OPEN   open the stream named data.open.stream_name; reads
 *                       come in blocks of data.open.buffer_size; set
 *                       data.open.stream_size (bytes, 0 if not known).
 *                       Returns a handle, or 0 if it failed.
 *   MPEGA_BSFUNC_CLOSE  returns 0 if all right
 *   MPEGA_BSFUNC_READ   data.read.num_bytes into data.read.buffer: returns
 *                       the bytes read, 0 at the end
 *   MPEGA_BSFUNC_SEEK   to byte data.seek.abs_byte_seek_pos: returns 0 if
 *                       all right
 */
#define MPEGA_BSFUNC_OPEN  0
#define MPEGA_BSFUNC_CLOSE 1
#define MPEGA_BSFUNC_READ  2
#define MPEGA_BSFUNC_SEEK  3

typedef struct {
    LONG func;                      /* MPEGA_BSFUNC_ */
    union {
        struct {
            char *stream_name;      /* in */
            LONG buffer_size;       /* in */
            LONG stream_size;       /* out */
        } open;
        struct {
            void *buffer;           /* in/out */
            LONG num_bytes;         /* in */
        } read;
        struct {
            LONG abs_byte_seek_pos; /* in */
        } seek;
    } data;
} MPEGA_ACCESS;

/* The output for streams of one kind (mono, or stereo) */
typedef struct {
    WORD freq_div;                  /* 1, 2 or 4; 0: the least that gives at most freq_max */
    WORD quality;                   /* MPEGA_QUALITY_ */
    LONG freq_max;                  /* for freq_div 0 */
} MPEGA_OUTPUT;

/* One layer's (or two's) settings */
typedef struct {
    WORD force_mono;                /* 1: stereo decoded as mono */
    MPEGA_OUTPUT mono;
    MPEGA_OUTPUT stereo;
} MPEGA_LAYER;

typedef struct {
    struct Hook *bs_access;         /* NULL: the file named, read by dos.library */
    MPEGA_LAYER layer_1_2;          /* Layers I and II */
    MPEGA_LAYER layer_3;            /* Layer III */
    WORD check_mpeg;                /* 1: MPEGA_open makes sure it is MPEG audio */
    LONG stream_buffer_size;        /* bytes read at a time (a multiple of 4; 0: the default) */
} MPEGA_CTRL;

/* MPEGA_STREAM's mode */
#define MPEGA_MODE_STEREO   0
#define MPEGA_MODE_J_STEREO 1
#define MPEGA_MODE_DUAL     2
#define MPEGA_MODE_MONO     3

/* An open stream (read only) */
typedef struct {
    WORD norm;                      /* 1 or 2 (MPEG-2.5: 2) */
    WORD layer;                     /* 1 to 3 */
    WORD mode;                      /* MPEGA_MODE_ */
    WORD bitrate;                   /* kbit/s (VBR: the first frame's) */
    LONG frequency;                 /* Hz */
    WORD channels;                  /* 1 or 2 */
    ULONG ms_duration;              /* the stream's length, ms (0 if not known) */
    WORD private_bit;
    WORD copyright;
    WORD original;
    WORD dec_channels;              /* decoded to: 1 or 2 */
    WORD dec_quality;
    LONG dec_frequency;             /* Hz */
    void *handle;                   /* the library's */
} MPEGA_STREAM;

#define MPEGA_MAX_CHANNELS 2
#define MPEGA_PCM_SIZE     1152     /* MPEGA_decode_frame's most samples a channel */

/* Errors (negative) */
#define MPEGA_ERR_NONE     0
#define MPEGA_ERR_BASE     0
#define MPEGA_ERR_EOF      (MPEGA_ERR_BASE - 1)
#define MPEGA_ERR_BADFRAME (MPEGA_ERR_BASE - 2)
#define MPEGA_ERR_MEM      (MPEGA_ERR_BASE - 3)
#define MPEGA_ERR_NO_SYNC  (MPEGA_ERR_BASE - 4)
#define MPEGA_ERR_BADVALUE (MPEGA_ERR_BASE - 5)

#endif /* LIBRARIES_MPEGA_H */
