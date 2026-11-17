| ambmpega.library's output for the 68020 and later, written by hand:
| the synthesised samples (libmad's fixed point, 28 fractional bits) to
| what the program's output wants, as src/mpga.c's C does it (the same
| bytes: make test compares them with the host's). Each sample rounded to
| 16 bits and clipped (the clipped ones counted), its range followed for
| the peak, then written.
|
|   void mpga_put16(const mad_fixed_t *x, WORD *d, ULONG n, ULONG step, struct MpgaRange *r)
|       16 bits, step bytes apart (2, or 4 for interleaved stereo)
|   void mpga_put8(const mad_fixed_t *x, BYTE *d, ULONG n, struct MpgaRange *r)
|       their high bytes (Paula)
|   void mpga_put14(const mad_fixed_t *x, BYTE *hi, UBYTE *lo, ULONG n, struct MpgaRange *r)
|       their high bytes, and their next 6 bits (Paula's 14 bits)
| struct MpgaRange { LONG max, min; ULONG clipped; }: the largest and
| smallest rounded sample (in libmad's scale), and the clipped, added to.
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).

	.set	ONE, 0x10000000		| MAD_F_ONE
	.set	ROUND, 0x1000		| half a 16-bit step

| d0 = the next sample, rounded and clipped (d5 ROUND, d6 ONE - 1, d7
| -ONE; d3 max, d4 min; the clipped counted at 8(a3)); a0 the samples
	.macro	FETCH
	move.l	(a0)+,d0
	add.l	d5,d0
	cmp.l	d6,d0
	bgt.s	.Lhi\@
	cmp.l	d7,d0
	blt.s	.Llo\@
.Lin\@:
	cmp.l	d3,d0
	ble.s	.Lmx\@
	move.l	d0,d3
.Lmx\@:
	cmp.l	d4,d0
	bge.s	.Lok\@
	move.l	d0,d4
	bra.s	.Lok\@
.Lhi\@:	move.l	d6,d0
	addq.l	#1,8(a3)
	bra.s	.Lin\@
.Llo\@:	move.l	d7,d0
	addq.l	#1,8(a3)
	bra.s	.Lin\@
.Lok\@:
	.endm

| the range's registers from r (at a3), and back
	.macro	RANGE_IN
	move.l	#ROUND,d5
	move.l	#ONE-1,d6
	move.l	#-ONE,d7
	movem.l	(a3),d3/d4
	.endm
	.macro	RANGE_OUT
	movem.l	d3/d4,(a3)
	.endm

	.globl	_mpga_put16
_mpga_put16:
	movem.l	d2-d7/a2-a4,-(sp)
	movem.l	40(sp),a0/a1		| x, d
	movem.l	48(sp),d1/d2/a3		| n, step, r
	RANGE_IN
	moveq	#13,d2			| (step kept in a4)
	move.l	52(sp),a4
	subq.l	#1,d1
	bmi.s	9f
1:	FETCH
	asr.l	d2,d0
	move.w	d0,(a1)
	add.l	a4,a1
	dbf	d1,1b
	clr.w	d1			| (more than 65536)
	subq.l	#1,d1
	bpl.s	1b
9:	RANGE_OUT
	movem.l	(sp)+,d2-d7/a2-a4
	rts

	.globl	_mpga_put8
_mpga_put8:
	movem.l	d2-d7/a2-a4,-(sp)
	movem.l	40(sp),a0/a1		| x, d
	move.l	48(sp),d1		| n
	move.l	52(sp),a3		| r
	RANGE_IN
	moveq	#21,d2
	subq.l	#1,d1
	bmi.s	9f
1:	FETCH
	asr.l	d2,d0
	move.b	d0,(a1)+
	dbf	d1,1b
	clr.w	d1
	subq.l	#1,d1
	bpl.s	1b
9:	RANGE_OUT
	movem.l	(sp)+,d2-d7/a2-a4
	rts

	.globl	_mpga_put14
_mpga_put14:
	movem.l	d2-d7/a2-a4,-(sp)
	movem.l	40(sp),a0/a1/a4		| x, hi, lo
	move.l	52(sp),d1		| n
	move.l	56(sp),a3		| r
	RANGE_IN
	moveq	#21,d2
	subq.l	#1,d1
	bmi.s	9f
1:	FETCH
	move.l	d0,a2
	asr.l	d2,d0
	move.b	d0,(a1)+		| the high byte
	move.l	a2,d0
	asr.l	#8,d0
	asr.l	#7,d0
	and.b	#0x3f,d0
	move.b	d0,(a4)+		| its next 6 bits
	dbf	d1,1b
	clr.w	d1
	subq.l	#1,d1
	bpl.s	1b
9:	RANGE_OUT
	movem.l	(sp)+,d2-d7/a2-a4
	rts
