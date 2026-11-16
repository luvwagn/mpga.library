| amcempega.library's synthesis window for the 68020 and later, written
| by hand: libmad's synth_full (third_party/libmad/synth.c, built with
| OPT_SSO) after its 32-point DCT, the same sums of the same 32-bit
| products, so the same samples (make test compares them with the host's
| C). Each product one MULS.L, each fixed-point value read once for the
| two samples it is in.
|
| void mpga_window_full(const mad_fixed_t (*fe)[8], const mad_fixed_t (*fx)[8],
|                       const mad_fixed_t (*fo)[8], const mad_fixed_t (*D)[32],
|                       unsigned pe, unsigned po, mad_fixed_t *pcm)
| One slot's 32 samples into pcm[0..31]: fe, fx and fo the filter's rows
| (16 rows of 8), D the window (17 rows of 32), pe even and po odd the
| phase's offsets into a row of D.
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
| (GPL-2.0-or-later, Underbit Technologies).

| a sample of the even half: d6 += f[k] * D[pe + o] (D at a3), with o
| libmad's 0, 14, 12, ... 2 for k = 0 ... 7
	.macro	EVEN	f, k, o
	move.l	\k*4(\f),d0
	muls.l	\o*4(a3),d0
	add.l	d0,d6
	.endm
| ... and of the odd half: d6 -= f[k] * D[po + o] (D at a2)
	.macro	ODD	f, k, o
	move.l	\k*4(\f),d0
	muls.l	\o*4(a2),d0
	sub.l	d0,d6
	.endm

| both samples of subband sb: fe's row k at a0, fo's at a1, D's row at
| a2 (+po), a3 (+pe), a4 (+15-pe), a5 (+15-po): d6 the one at sb, d7 the
| one at 32 - sb
	.macro	PAIR	k, o
	move.l	\k*4(a0),d0
	move.l	d0,d1
	muls.l	\o*4(a3),d0
	add.l	d0,d6
	muls.l	\k*8(a4),d1
	add.l	d1,d7
	move.l	\k*4(a1),d0
	move.l	d0,d1
	muls.l	\o*4(a2),d0
	sub.l	d0,d6
	muls.l	\k*8(a5),d1
	add.l	d1,d7
	.endm

	.globl	_mpga_window_full
_mpga_window_full:
	movem.l	d2-d7/a2-a6,-(sp)
	movem.l	48(sp),a0/a1/a4/a5	| fe, fx, fo, D
	move.l	64(sp),d4		| pe
	move.l	68(sp),d5		| po
	move.l	72(sp),a6		| pcm
	lsl.l	#2,d4
	lsl.l	#2,d5
	lea	(a5,d4.l),a3		| D[0] + pe
	lea	(a5,d5.l),a2		| D[0] + po

	| sample 0: fe's row 0 even, fx's row 0 odd
	moveq	#0,d6
	EVEN	a0, 0, 0
	EVEN	a0, 1, 14
	EVEN	a0, 2, 12
	EVEN	a0, 3, 10
	EVEN	a0, 4, 8
	EVEN	a0, 5, 6
	EVEN	a0, 6, 4
	EVEN	a0, 7, 2
	ODD	a1, 0, 0
	ODD	a1, 1, 14
	ODD	a1, 2, 12
	ODD	a1, 3, 10
	ODD	a1, 4, 8
	ODD	a1, 5, 6
	ODD	a1, 6, 4
	ODD	a1, 7, 2
	asr.l	#2,d6
	move.l	d6,(a6)+

	| subbands 1-15: fe's row sb, fo's row sb - 1, D's row sb
	move.l	a4,a1			| fo
	moveq	#15*4,d0
	sub.l	d4,d0
	lea	(a5,d0.l),a4		| D[0] + 15 - pe
	moveq	#15*4,d0
	sub.l	d5,d0
	lea	(a5,d0.l),a5		| D[0] + 15 - po
	moveq	#30*4,d3		| pcm[32 - sb] - pcm[sb], bytes
	moveq	#15-1,d2
1:	lea	32(a0),a0
	lea	128(a2),a2
	lea	128(a3),a3
	lea	128(a4),a4
	lea	128(a5),a5
	moveq	#0,d6
	moveq	#0,d7
	PAIR	0, 0
	PAIR	1, 14
	PAIR	2, 12
	PAIR	3, 10
	PAIR	4, 8
	PAIR	5, 6
	PAIR	6, 4
	PAIR	7, 2
	asr.l	#2,d7
	move.l	d7,(a6,d3.l)		| pcm[32 - sb]
	asr.l	#2,d6
	move.l	d6,(a6)+		| pcm[sb]
	subq.l	#8,d3
	lea	32(a1),a1
	dbf	d2,1b

	| sample 16: fo's row 15 (where a1 is now), D's row 16
	lea	128(a2),a2
	moveq	#0,d6
	ODD	a1, 0, 0
	ODD	a1, 1, 14
	ODD	a1, 2, 12
	ODD	a1, 3, 10
	ODD	a1, 4, 8
	ODD	a1, 5, 6
	ODD	a1, 6, 4
	ODD	a1, 7, 2
	asr.l	#2,d6			| (-sum) >> 2: d6 is the sum negated already
	move.l	d6,(a6)

	movem.l	(sp)+,d2-d7/a2-a6
	rts
