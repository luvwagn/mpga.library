/*
 * libmad's config.h for amcempega.library (HAVE_CONFIG_H: global.h
 * includes it first): the sizes of the 68k's types, its fixed-point
 * multiply (FPM_*, from the Makefile), and its memory from exec on the
 * Amiga (asked for only when a stream is opened and closed).
 * Part of amcempega.library, GPL-3.0-or-later.
 */
#ifndef MPGA_CONFIG_H
#define MPGA_CONFIG_H

#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define HAVE_LIMITS_H 1
#define HAVE_ASSERT_H 1
#ifndef NDEBUG
#define NDEBUG 1
#endif

#ifdef __amigaos__
#include <stddef.h>
void *mpga_malloc(size_t n);
void *mpga_calloc(size_t n, size_t m);
void mpga_free(void *p);
#define malloc(n) mpga_malloc(n)
#define calloc(n, m) mpga_calloc(n, m)
#define free(p) mpga_free(p)
#endif

#endif
