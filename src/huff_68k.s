| ambmpega.library: libmad's Layer III Huffman decoding of big_values
| (third_party/libmad/layer3.c, III_huffdecode's first loop) in 68k
| assembler, for both builds (no multiplies here). The same decisions, bits
| and values as the C: the same refills at the same points, the same
| checks and errors, the same requantize cache. The C's bitcache holds
| cachesz bits at its bottom; here they are kept at the top of d2 (left
| justified, zeros below), so a table index is one BFEXTU and consuming n
| bits one LSL. Refills read whole bytes where the C's mad_bit_read would
| (the bit pointer on a byte boundary, a multiple of 8 bits), and call
| mad_bit_read otherwise.
|
| enum mad_error mpga_huff_big(struct mpga_huff *h)	(layer3.c's struct)
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE); after libmad's C
| (GPL-2.0-or-later, Underbit Technologies).

	.set	H_BITCACHE, 0
	.set	H_CACHESZ, 4
	.set	H_BITSLEFT, 8
	.set	H_FAKEBITS, 12
	.set	H_XRPTR, 16
	.set	H_SFBOUND, 20
	.set	H_SFBWIDTH, 24
	.set	H_EXPPTR, 28
	.set	H_EXP, 32
	.set	H_PEEK, 36
	.set	H_BIGVALUES, 40
	.set	H_RCOUNT, 44
	.set	H_R1COUNT, 48
	.set	H_ENTRY, 52
	.set	H_REQ, 64

	.set	ERR_BADHUFFTABLE, 0x0237
	.set	ERR_BADHUFFDATA, 0x0238

| the frame: reqcache[16] at 0(a5), region at 64, the pair at 68, linbits 72
	.set	F_REGION, 64
	.set	F_PAIR, 68
	.set	F_LINBITS, 72
	.set	FRAME, 76

| registers: d2 the bits (left justified), d3 cachesz, d4 big_values,
| d5 32 - startbits, d6 bits_left - fakebits, d7 reqhits; a2 xrptr, a3 sfbound,
| a4 the table, a5 the frame, a6 h

	.text
	.globl	_mpga_huff_big
_mpga_huff_big:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-FRAME(sp),sp
	move.l	sp,a5
	move.l	FRAME+48(sp),a6
	clr.l	F_REGION(a5)
	move.l	H_CACHESZ(a6),d3
	move.l	H_BITCACHE(a6),d2
	moveq	#32,d0
	sub.l	d3,d0
	lsl.l	d0,d2			| (cachesz 0: a shift of 32, 0)
	move.l	H_BITSLEFT(a6),d6
	sub.l	H_FAKEBITS(a6),d6
	move.l	H_XRPTR(a6),a2
	move.l	H_SFBOUND(a6),a3
	move.l	H_BIGVALUES(a6),d4
	moveq	#0,d7
	move.l	H_ENTRY(a6),a0
	bsr	settable

| while (big_values-- && cachesz + bits_left - fakebits > 0)
loop:
	subq.l	#1,d4
	bcs	done
	move.l	d3,d0
	add.l	d6,d0
	ble	done
	cmpa.l	a3,a2
	beq	sfb
gotsfb:
	moveq	#21,d0
	cmp.l	d0,d3
	blt	refill
gotbits:
| hcod: the table walk (a shift, not BFEXTU: table 0's startbits is 0,
| and a BFEXTU of width 0 takes 32 bits)
	move.l	d2,d0
	lsr.l	d5,d0
	add.l	d0,d0
	move.w	(a4,d0.l),d1
	bpl	walk
final:
	move.w	d1,d0
	rol.w	#4,d0
	and.l	#7,d0			| hlen
	lsl.l	d0,d2
	sub.l	d0,d3
	cmp.l	H_FAKEBITS(a6),d3	| cachesz < fakebits: data overrun
	blt	errdata
	move.w	d1,d0
	and.w	#0x0ff0,d0
	beq	zeros			| x and y both 0
	move.w	d1,F_PAIR(a5)
| x
	move.w	d1,d0
	lsr.w	#8,d0
	and.l	#15,d0
	beq	xzero
	cmp.w	#15,d0
	beq	xlin
xcached:
	btst	d0,d7
	beq	xmiss
	move.l	(a5,d0.l*4),d0
xsign:
	move.l	d3,d1
	sub.l	H_FAKEBITS(a6),d1
	ble	errdata			| cachesz - fakebits < 1
	add.l	d2,d2
	bcc	1f
	neg.l	d0
1:	subq.l	#1,d3
	move.l	d0,(a2)+
| y
y:
	move.w	F_PAIR(a5),d0
	lsr.w	#4,d0
	and.l	#15,d0
	beq	yzero
	cmp.w	#15,d0
	beq	ylin
ycached:
	btst	d0,d7
	beq	ymiss
	move.l	(a5,d0.l*4),d0
ysign:
	move.l	d3,d1
	sub.l	H_FAKEBITS(a6),d1
	ble	errdata
	add.l	d2,d2
	bcc	1f
	neg.l	d0
1:	subq.l	#1,d3
	move.l	d0,(a2)+
	bra	loop

zeros:
	clr.l	(a2)+
	clr.l	(a2)+
	bra	loop
xzero:
	clr.l	(a2)+
	bra	y
yzero:
	clr.l	(a2)+
	bra	loop

| a value's requantized, not yet in the cache: requantize, keep
xmiss:
	bset	d0,d7
	move.l	d0,-(sp)
	bsr	requant
	move.l	(sp)+,d1
	move.l	d0,(a5,d1.l*4)
	bra	xsign
ymiss:
	bset	d0,d7
	move.l	d0,-(sp)
	bsr	requant
	move.l	(sp)+,d1
	move.l	d0,(a5,d1.l*4)
	bra	ysign

| 15 with linbits: linbits more bits added, requantized (not cached)
xlin:
	move.l	F_LINBITS(a5),d1
	beq	xcached			| (no linbits: 15 is a value like any)
	addq.l	#2,d1			| cachesz < linbits + 2: 16 more bits
	cmp.l	d1,d3
	bge	1f
	moveq	#16,d1
	bsr	fill
1:	bsr	linbits
	bra	xsign
ylin:
	move.l	F_LINBITS(a5),d1
	beq	ycached
	addq.l	#1,d1			| cachesz < linbits + 1
	cmp.l	d1,d3
	bge	1f
	moveq	#16,d1
	bsr	fill
1:	bsr	linbits
	bra	ysign

| d0 = requantize(15 + the next linbits bits, exp), or an overrun
linbits:
	move.l	F_LINBITS(a5),d1
	move.l	d3,d0
	sub.l	H_FAKEBITS(a6),d0
	cmp.l	d1,d0
	bcs	errdata			| cachesz - fakebits < linbits (unsigned)
	bfextu	d2{#0:d1},d0
	lsl.l	d1,d2
	sub.l	d1,d3
	add.l	#15,d0
	bra	requant

| d0 = h->requantize(d0, exp)
requant:
	move.l	H_EXP(a6),-(sp)
	move.l	d0,-(sp)
	move.l	H_REQ(a6),a0
	jsr	(a0)
	addq.l	#8,sp
	rts

| the table walk, the pair not final: d1 the pair
walk:
	moveq	#32,d0
	sub.l	d5,d0			| clumpsz = startbits
1:	lsl.l	d0,d2
	sub.l	d0,d3			| cachesz -= clumpsz
	move.w	d1,d0
	rol.w	#4,d0
	and.l	#7,d0			| clumpsz = pair->ptr.bits
	move.l	d0,a0
	and.l	#0x0fff,d1		| pair->ptr.offset
	bfextu	d2{#0:d0},d0
	add.l	d1,d0
	add.l	d0,d0
	move.w	(a4,d0.l),d1
	move.l	a0,d0
	tst.w	d1
	bpl	1b
	bra	final

| the scale factor band's end: the next band, the next region's table,
| the next exponent
sfb:
	move.l	H_SFBWIDTH(a6),a0
	moveq	#0,d0
	move.b	(a0)+,d0
	move.l	a0,H_SFBWIDTH(a6)
	lea	(a3,d0.l*4),a3
	subq.l	#1,H_RCOUNT(a6)
	bne	2f
	tst.l	F_REGION(a5)
	bne	1f
	move.l	H_R1COUNT(a6),H_RCOUNT(a6)
	bra	3f
1:	clr.l	H_RCOUNT(a6)
3:	addq.l	#1,F_REGION(a5)
	move.l	F_REGION(a5),d0
	move.l	H_ENTRY(a6,d0.l*4),a0
	bsr	settable
	move.l	a4,d0
	beq	errtable
2:	move.l	H_EXPPTR(a6),a0
	move.l	(a0)+,d0
	move.l	a0,H_EXPPTR(a6)
	cmp.l	H_EXP(a6),d0
	beq	gotsfb
	move.l	d0,H_EXP(a6)
	moveq	#0,d7			| reqhits = 0
	bra	gotsfb

| a0 the struct hufftable: its table, linbits and startbits
settable:
	move.l	(a0),a4
	moveq	#0,d0
	move.w	4(a0),d0
	move.l	d0,F_LINBITS(a5)
	moveq	#32,d5
	sub.w	6(a0),d5		| 32 - startbits
	rts

| cachesz < 21: (31 - cachesz) & ~7 bits more, then zeros to 21 if the
| data has run out (fakebits)
refill:
	moveq	#31,d1
	sub.l	d3,d1
	and.l	#-8,d1
	bsr	fill
	moveq	#21,d0
	cmp.l	d0,d3
	bge	gotbits
	sub.l	d3,d0
	add.l	d0,d3
	add.l	d0,H_FAKEBITS(a6)
	sub.l	d0,d6
	bra	gotbits

| d1 bits more (no more than bits_left) below the cachesz there are;
| d0, d1, a0, a1 lost
fill:
	move.l	H_BITSLEFT(a6),d0
	cmp.l	d1,d0
	bcc	1f
	move.l	d0,d1			| bits = bits_left
1:	sub.l	d1,H_BITSLEFT(a6)
	sub.l	d1,d6
	tst.l	d1
	beq	9f
	move.l	H_PEEK(a6),a0
	cmp.w	#8,6(a0)		| peek.left == CHAR_BIT
	bne	slow
	move.l	d1,d0
	and.w	#7,d0
	bne	slow
	move.l	(a0),a1			| whole bytes
	move.l	d1,d0
	lsr.w	#3,d0
	subq.w	#1,d0
	move.l	d1,-(sp)
	moveq	#0,d1
2:	lsl.l	#8,d1
	move.b	(a1)+,d1
	dbf	d0,2b
	move.l	a1,(a0)
	move.l	d1,d0
	move.l	(sp)+,d1
	bra	have
slow:
	move.l	d1,-(sp)
	move.l	d1,-(sp)
	move.l	a0,-(sp)
	jsr	_mad_bit_read
	addq.l	#8,sp
	move.l	(sp)+,d1
have:
	add.l	d1,d3			| cachesz += bits
	moveq	#32,d1
	sub.l	d3,d1
	lsl.l	d1,d0
	or.l	d0,d2
9:	rts

done:
	moveq	#32,d0
	sub.l	d3,d0
	lsr.l	d0,d2			| the C's bitcache: right justified
	move.l	d2,H_BITCACHE(a6)
	move.l	d3,H_CACHESZ(a6)
	move.l	a2,H_XRPTR(a6)
	move.l	a3,H_SFBOUND(a6)
	moveq	#0,d0
out:
	lea	FRAME(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
errdata:
	move.l	a5,sp
	move.l	#ERR_BADHUFFDATA,d0
	bra	out
errtable:
	move.l	a5,sp
	move.l	#ERR_BADHUFFTABLE,d0
	bra	out
