| amcempega.library's stack switch: LONG mpga_onstack(LONG (*f)(APTR),
| APTR arg, struct StackSwapStruct *sss) calls f(arg) on the stack sss
| describes, through exec's StackSwap (so the task's tc_SPLower and
| tc_SPUpper say where it is), and swaps back after. libmad's Layer III
| decoding keeps 9 KB of arrays on the stack; a program calling
| MPEGA_decode_frame may have 4 KB. Written here, not in C: gcc may put
| off popping a call's arguments until after the second StackSwap.
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).
	.globl	_mpga_onstack
_mpga_onstack:
	movem.l	d2-d3/a2-a3/a6,-(sp)
	move.l	24(sp),a2		| f
	move.l	28(sp),a3		| arg
	move.l	32(sp),d2		| sss
	move.l	4.w,a6
	move.l	d2,a0
	jsr	-732(a6)		| StackSwap: onto the stream's stack
	move.l	a3,-(sp)
	jsr	(a2)			| (d2, a2, a3: kept by the C it calls)
	addq.l	#4,sp
	move.l	d0,d3
	move.l	d2,a0
	move.l	4.w,a6
	jsr	-732(a6)		| StackSwap: back
	move.l	d3,d0
	movem.l	(sp)+,d2-d3/a2-a3/a6
	rts

| ULONG mpga_callhook(struct Hook *h, APTR object, APTR message): the
| program's bitstream hook called as CallHookPkt would (h in a0, object
| in a2, message in a1, through h_Entry), without utility.library.
	.globl	_mpga_callhook
_mpga_callhook:
	movem.l	a2-a3,-(sp)
	movem.l	12(sp),a0/a2		| h, object
	move.l	20(sp),a1		| message
	move.l	8(a0),a3		| h_Entry
	jsr	(a3)
	movem.l	(sp)+,a2-a3
	rts
