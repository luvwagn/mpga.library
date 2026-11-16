/* utility/hooks.h's struct Hook, for a host build (tests/mpga_host.c). */
#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H
#include <exec/types.h>
struct Hook {
    struct Hook *h_Succ, *h_Pred;   /* (its MinNode) */
    ULONG (*h_Entry)();
    ULONG (*h_SubEntry)();
    APTR h_Data;
};
#endif
