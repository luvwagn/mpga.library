/* The few exec/types.h names a host build (tests/ac3_test.c) needs. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef int16_t BOOL;
typedef void *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
#define VOID void
#ifndef CONST
#define CONST const
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#endif
