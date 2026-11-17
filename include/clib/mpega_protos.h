/* Automatically generated header (sfdc 1.13)! Do not edit! */

#ifndef CLIB_MPEGA_PROTOS_H
#define CLIB_MPEGA_PROTOS_H

/*
**   $VER: mpega_protos.h 1.0 (15.11.2026)
**
**   C prototypes. For use with 32 bit integers only.
**
**   Copyright (c) 2001 Amiga, Inc.
**       All Rights Reserved
*/

#include <libraries/mpega.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* mpega.library's functions (its .fd: bias 30), as ambmpega.library offers */
/* them: MPEG audio decoded to 16-bit PCM. The structures and constants are */
/* in <libraries/mpega.h>. This file and the headers made from it: (c) 2026 */
/* Brian King, MIT. The library itself is GPL-3.0-or-later (libmad inside). */
MPEGA_STREAM * MPEGA_open(char * stream_name, MPEGA_CTRL * ctrl);
VOID MPEGA_close(MPEGA_STREAM * mpega_stream);
LONG MPEGA_decode_frame(MPEGA_STREAM * mpega_stream, WORD ** pcm);
LONG MPEGA_seek(MPEGA_STREAM * mpega_stream, ULONG ms_time_position);
LONG MPEGA_time(MPEGA_STREAM * mpega_stream, ULONG * ms_time_position);
LONG MPEGA_find_sync(BYTE * buffer, LONG buffer_size);
LONG MPEGA_scale(MPEGA_STREAM * mpega_stream, LONG scale_percent);

/* ambmpega.library's own, from V1 (not in mpega.library) */
LONG MPGA_decode_to(MPEGA_STREAM * mpega_stream, struct MpgaOut * out);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLIB_MPEGA_PROTOS_H */
