! Casiolator Loopy sound test: cartridge header, vectors and entry point
! (header layout from kasami's Loopy-Tools "ROM Structure.md")

	.section .header, "ax"
	.long	0x0e000080		! first word after the header
	.long	__rom_last		! last word of the ROM
	.long	0			! checksum, filled in by checksum.py
	.long	0xffffffff
	.long	0x02ffffff		! no SRAM
	.long	0x02fffffe
	.long	0xfffffffe
	.ascii	"CASIOLATOR SOUND TEST"
	.balign	0x80, 0xff

	! vector table: every exception and interrupt spins in place
	.rept	116
	.long	_hang
	.endr

	.balign	4
_hang:
	bra	_hang
	nop

	! the BIOS jumps to 0x0e000480
	.section .entry, "ax"
	.global	_start
_start:
	mov.l	stack, r15
	mov.l	vectors, r0
	ldc	r0, vbr
	mov.l	main, r0
	jsr	@r0
	nop
1:	bra	1b
	nop
	.balign	4
stack:	.long	0x09080000
vectors: .long	0x0e000080
main:	.long	_main
