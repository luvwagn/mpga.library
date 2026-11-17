| ambmpega.library's synthesis window for the 68020 and later, written
| by hand: libmad's synth_full and synth_half (third_party/libmad/synth.c,
| built with OPT_SSO) after their 32-point DCT. Each filter value is read
| once for the two samples it is in.
|
| The 32-bit window (MPEGA_QUALITY_HIGH): the same sums of the same 32-bit
| products as libmad's C (one MULS.L each), so the same samples (make test
| compares them with the host's C).
|
| The 16-bit window (medium and low): each filter value (16 fractional
| bits) shifted to 13 and taken as a word, each window value (14
| fractional bits, which fits a word) its low word, one MULS.W each: a
| third fewer cycles on a 68020 or 68030, the filter's last 3 bits lost.
|
| void mpga_window_full(const mad_fixed_t (*fe)[8], const mad_fixed_t (*fx)[8],
|                       const mad_fixed_t (*fo)[8], const mad_fixed_t (*D)[32],
|                       unsigned pe, unsigned po, mad_fixed_t *pcm)
| One slot's 32 samples into pcm[0..31]: fe, fx and fo the filter's rows
| (16 rows of 8), D the window (17 rows of 32), pe even and po odd the
| phase's offsets into a row of D. mpga_window_half: its 16 (the even
| subbands', for half the rate) into pcm[0..15]. mpga_window_full16 and
| mpga_window_half16: the same with the 16-bit window.
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
| (GPL-2.0-or-later, Underbit Technologies).

	.set	FSHIFT, 3		| the 16-bit window: the filter's 16 bits to 13
	.set	OSHIFT16, 1		| ... and the sums' 27 fractional bits to 28

| a filter value f[k] into d0, as the window wants it
	.macro	LOADF	w, f, k
	move.l	\k*4(\f),d0
	.if	\w
	asr.l	#FSHIFT,d0
	.endif
	.endm

| d0 *= the window's value at off(An)
	.macro	MULD	w, off, an
	.if	\w
	muls.w	\off+2(\an),d0
	.else
	muls.l	\off(\an),d0
	.endif
	.endm
	.macro	MULD1	w, off, an
	.if	\w
	muls.w	\off+2(\an),d1
	.else
	muls.l	\off(\an),d1
	.endif
	.endm

| a sample of the even half: d6 += f[k] * D[pe + o] (D at a3), with o
| libmad's 0, 14, 12, ... 2 for k = 0 ... 7
	.macro	EVEN	w, f, k, o
	LOADF	\w, \f, \k
	MULD	\w, \o*4, a3
	add.l	d0,d6
	.endm
| ... and of the odd half: d6 -= f[k] * D[po + o] (D at a2)
	.macro	ODD	w, f, k, o
	LOADF	\w, \f, \k
	MULD	\w, \o*4, a2
	sub.l	d0,d6
	.endm

| both samples of subband sb: fe's row k at a0, fo's at a1, D's row at
| a2 (+po), a3 (+pe), a4 (+15-pe), a5 (+15-po): d6 the one at sb, d7 the
| one at 32 - sb
	.macro	PAIR	w, k, o
	LOADF	\w, a0, \k
	move.l	d0,d1
	MULD	\w, \o*4, a3
	add.l	d0,d6
	MULD1	\w, \k*8, a4
	add.l	d1,d7
	LOADF	\w, a1, \k
	move.l	d0,d1
	MULD	\w, \o*4, a2
	sub.l	d0,d6
	MULD1	\w, \k*8, a5
	add.l	d1,d7
	.endm

| a sum (d6 or d7) as a sample: libmad's >> 2 for the 32-bit window, the
| 16-bit one's 27 fractional bits to 28
	.macro	OUT	w, d
	.if	\w
	asl.l	#OSHIFT16,\d
	.else
	asr.l	#2,\d
	.endif
	.endm

| the window for one slot: step 1 (all 32 samples) or 2 (the even
| subbands' 16), w 0 (MULS.L) or 1 (MULS.W)
	.macro	WINDOW	name, step, w
	.globl	\name
\name:
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
	EVEN	\w, a0, 0, 0
	EVEN	\w, a0, 1, 14
	EVEN	\w, a0, 2, 12
	EVEN	\w, a0, 3, 10
	EVEN	\w, a0, 4, 8
	EVEN	\w, a0, 5, 6
	EVEN	\w, a0, 6, 4
	EVEN	\w, a0, 7, 2
	ODD	\w, a1, 0, 0
	ODD	\w, a1, 1, 14
	ODD	\w, a1, 2, 12
	ODD	\w, a1, 3, 10
	ODD	\w, a1, 4, 8
	ODD	\w, a1, 5, 6
	ODD	\w, a1, 6, 4
	ODD	\w, a1, 7, 2
	OUT	\w, d6
	move.l	d6,(a6)+

	| subbands step, 2 step ... 15: fe's row sb, fo's row sb - 1, D's row sb
	lea	(\step-1)*32(a4),a1	| fo
	moveq	#15*4,d0
	sub.l	d4,d0
	lea	(a5,d0.l),a4		| D[0] + 15 - pe
	moveq	#15*4,d0
	sub.l	d5,d0
	lea	(a5,d0.l),a5		| D[0] + 15 - po
	moveq	#(32/\step-2)*4,d3	| pcm's at 32 - sb less pcm's at sb, bytes
	moveq	#16/\step-2,d2
1:	lea	\step*32(a0),a0
	lea	\step*128(a2),a2
	lea	\step*128(a3),a3
	lea	\step*128(a4),a4
	lea	\step*128(a5),a5
	moveq	#0,d6
	moveq	#0,d7
	PAIR	\w, 0, 0
	PAIR	\w, 1, 14
	PAIR	\w, 2, 12
	PAIR	\w, 3, 10
	PAIR	\w, 4, 8
	PAIR	\w, 5, 6
	PAIR	\w, 6, 4
	PAIR	\w, 7, 2
	OUT	\w, d7
	move.l	d7,(a6,d3.l)		| at 32 - sb
	OUT	\w, d6
	move.l	d6,(a6)+		| at sb
	subq.l	#8,d3
	lea	\step*32(a1),a1
	dbf	d2,1b

	| sample 16: fo's row 15 (where a1 is now), D's row 16
	lea	\step*128(a2),a2		| (half: the loop stopped at row 14)
	moveq	#0,d6
	ODD	\w, a1, 0, 0
	ODD	\w, a1, 1, 14
	ODD	\w, a1, 2, 12
	ODD	\w, a1, 3, 10
	ODD	\w, a1, 4, 8
	ODD	\w, a1, 5, 6
	ODD	\w, a1, 6, 4
	ODD	\w, a1, 7, 2
	OUT	\w, d6			| (-sum) >> 2: d6 is the sum negated already
	move.l	d6,(a6)

	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.endm

	WINDOW	_mpga_window_full, 1, 0
	WINDOW	_mpga_window_half, 2, 0
	WINDOW	_mpga_window_full16, 1, 1
	WINDOW	_mpga_window_half16, 2, 1
