/*
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Real-mode memory map:
 *
 *          segment:offset                physical
 *  0x0000:0x0000 -> 0x0030:0x00ff  (0x00000 -> 0x003ff) real mode interrupt vector table
 *  0x0040:0x0000 -> 0x0040:0x00ff  (0x00400 -> 0x004ff) bios data area, 256 bytes
 *  0x0050:0x0000 -> 0x9000:0xfbff  (0x00500 -> 0x9fbff) free memory, 630K
 *  0x9000:0xfc00 -> 0x9000:0xffff  (0x9fc00 -> 0x9ffff) usually Extended BIOS Data Area
 *  0xa000:0x0000 -> 0xb000:0xffff  (0xa0000 -> 0xbffff) VGA frame buffer
 *  0xc000:0x0000 -> 0xc000:0x7fff  (0xc0000 -> 0xc7fff) video bios, 32K
 *  0xc000:0x8000 -> 0xe000:0xffff  (0xc8000 -> 0xeffff) empty
 *  0xf000:0x0000 -> 0xf000:0xffff  (0xf0000 -> 0xfffff) motherboard bios, 64K
 *
 * The bootsector is loaded inside the 630K free memory region:
 *
 *  0x07c0:0x0000 -> 0x07e0:0x0000  (0x07c00 -> 0x07e00) bootsector, 512 bytes
 */

/* The 'valid boot sector' flag; it must be put at the end of
 * the mbr, or otherwise the bios will happily ignore us */
#define BOOT_SIGNATURE	0xaa55

.code16

/* The text section is relocated by the linker to 0x0000, thus
 * when we send an address of a string, we're actually sending
 * its offset from whatever segment currently setup in the
 * segment registers */
.text
.global _start
_start:	
	movw   $0x07c0, %ax
	movw   %ax, %es
	movw   %ax, %ss
	/* arbitary stack value > bootsector's 512 bytes code */
	movw   $0x300, %sp

	/* INT 0x10, function 0x03 - read current cursor position
	 * input  %bh - display page number
	 * output %dx - %dh: row number, %dl: column number */
	movb   $0x03, %ah
	xorb   %bh, %bh		
	int    $0x10

	/* INT 0x10, function 0x13 - write string
	 * input  %es:bp - pointer to string start
	 * input  %bh    - page number
	 * input  %cx    - length of string
	 * input  %bl    - characters' attribute (*)
	 * input  %dx    - cursor position (see above)
	 * input  %al    - 01 = move cursor */
	movw   $(string), %bp
	movw   $14, %cx
	movb   $0x03, %bl
	movw   $0x1301, %ax
	int    $0x10

	/*
	 * reset hard-disk controllers and recalibrate the
	 * read/write head positions (seek to cylinder 0).
	 *
	 * INT 0x13, function 0x00 - reset hard disk system
	 * input  %dl    - drive number (#zero = 0x80)
	 * output %ah    - error code
	 * output CF     - boolean error flag
	 */
	movb   $0x01, %ah
	movb   $0x80, %dl
	int    $0x13
	xorw   %dx, %dx
	movb   %ah, %dh
	call   print_hex

	jmp    .

/*
 * print %dx value in hexadecimal.
 *
 * INT 0x10, function 0x0e - write teletype to active page
 * input  %al    - character to write
 * input  %bh    - page number
 */
print_hex:
	xorb    %bh, %bh
	movw	$4, %cx		     # 4 hex digits (2bytes)
print_digit:
	/* extract leftmost digit in %al */
	rolw	$4, %dx
	movw	$0x0e0f, %ax
	andb	%dl, %al
	/* transform extracted digit to ascii */
	cmpb    $0x0a, %al
	jl      digit
	addb    $0x07, %al
digit:
	addb    $0x30, %al
	int	$0x10
	loop	print_digit
	ret

string:
	/* 14 bytes */
	.byte   '\r', '\n'
	.ascii "Loading .."
	.byte   '\r', '\n'

.org 510
boot_flag:
	.word  BOOT_SIGNATURE

/*
 * (*) character attribute is an 8 bit value, low 4 bits
 * set fore color, high 4 bits set background color.
 *
 *  HEX    BIN        COLOR
 *
 *  0      0000      black
 *  1      0001      blue
 *  2      0010      green
 *  3      0011      cyan
 *  4      0100      red
 *  5      0101      magenta
 *  6      0110      brown
 *  7      0111      light gray
 *  8      1000      dark gray
 *  9      1001      light blue
 *  A      1010      light green
 *  B      1011      light cyan
 *  C      1100      light red
 *  D      1101      light magenta
 *  E      1110      yellow
 *  F      1111      white
 *
 * ``When writing graphical data to the video data buffer, the BIOS
 * writes the individual color value of each pixel to be painted on
 * the CRT. It addresses the display buffer as a memory map made up
 * of an array of bits. The video subsytem's control circuitry reads
 * each pixel location from the buffer and generates the color and
 * control signals necessary to write the pixel to the correct screen
 * location with the correct color value'' -- System BIOS for IBM PCs,
 * compatibles, and EISA computers, second edition, Phoenix press
 */