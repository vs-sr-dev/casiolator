OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

SECTIONS
{
	. = 0x0e000000;
	.header : { KEEP(*(.header)) }
	. = 0x0e000480;
	.text : { KEEP(*(.entry)) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); }
	.data : { *(.data .data.*) . = ALIGN(4); }
	__rom_last = . - 2;
	/DISCARD/ : { *(.bss .bss.* COMMON .comment) }
}
