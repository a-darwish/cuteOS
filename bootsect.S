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

/* Real-mode segment where the kernel will be copied to from
 * disk. 512KB (0x10000-0x90000) will be more than enough for
 * the time-being */
#define KERNEL_SEGMENT  0x1000

/* Print messages to screen */
#define MSG(msg)		\
	movw   $msg, %si;  	\
	call   print_string;	\
	call   print_nl
	
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
	movw   %ax, %ds
	/* arbitary stack value > bootsector's 512 bytes code */
	movw   $0x300, %sp

	MSG    (welcome)

	/* Check for enhanced bios services to move beyond the 8GB
	 * limit with LBA, or the 504MB limit with no LBA (CHS).
	 * If no enhanced services exist (a pre-1995 bios), we'll
	 * gracefully exit.
	 *
	 * INT 0x13, function 0x41
	 * input  %bx     - 0x55aa (defined by ebios standard)
	 * input  %dl     - drive number (#zero = 0x80)
	 * output success - carry = 0 && bx = 0xaa55 &&
	 *                  cx bit 0 = 1 (enhanced drive access)
	 * output failure - carry = 1 || bx != 0xaa55 */
	movb   $0x41, %ah
	movb   $0x80, %dl
	movw   $0x55aa, %bx
	int    $0x13
	jc     no_ebios
	cmpw   $0xaa55, %bx
	jne    no_ebios
	shrw   $1, %cx
	jnc    no_ebios

	/* Copy the kernel from disk to its destined starting address
	 * 0x10000. For the sake of simplicity, we'll copy the disk's
	 * first 512KB after the mbr regardless of the real kernel
	 * size. We may optimize later.
	 *
	 * INT 0x13, function 0x42 - extended read sectors
	 * input  %dl     - drive number
	 * input  %ds:si  - pointer to the Disk Address Packet
	 * output %ah     - error code (if any)
	 * output CF      - error flag (true = error) */
	movw   $16, %cx			# 16 * 64 sector = 512K
	movb   $0x80, %dl
	movw   $disk_address_packet, %si
	movw   $KERNEL_SEGMENT, buffer_seg
	movw   $1, sector		# mbr is sector 0
next_sectors:
	movb   $64, sectors_cnt
	movb   $0x42, %ah
	int    $0x13
	jc     disk_error
	addw   $64, sector
	addw   $0x8000, buffer_offset   # 32K copied, 0x8000 bytes
	jnc    same_segment
	addw   $0x1000, buffer_seg	# 64K copied, move to next segment
	movw   $0x0000, buffer_offset
same_segment:
	loop   next_sectors

	jmp    .

/*
 * Print methods
 *
 * INT 0x10, function 0x0e - write teletype to active page
 * input  %al    - character to write
 * input  %bh    - page number
 * input  %bl    - characters' attribute (*)
 */

/* print %dx value in hexadecimal */
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

/* print string pointed by %ds:si */
print_string:
	xorb   %bh, %bh
	movb   $0x0e, %ah
	lodsb
	cmpb   $0, %al
	je     end
	int    $0x10
	jmp    print_string
end:
	ret

/* print newline */
print_nl:
	xorb    %bh, %bh
	movw    $((0x0e << 8) | '\r'), %ax
	int     $0x10
	movb    $'\n', %al
	int     $0x10
	ret

/*
 * Error conditions
 */

disk_error:
	pushw  %ax
	MSG    (disk)
	popw   %dx
	call   print_hex
	call   print_nl
	jmp    halt
no_ebios:
	MSG    (old_bios)
halt:
	MSG    (exiting)
	jmp    .

/*
 * Disk Address Packet
 */

disk_address_packet:
/* packet size in bytes */
packet_size:
	.byte 0x10
unused:
	.byte 0x00
/* number of blocks to transfer [1 - 127] */
sectors_cnt:
	.byte 0x00
unused2:
	.byte 0x00
/* read/write buffer address */
buffer_offset:
	.word 0x00
buffer_seg:
	.word 0x00
/* logical sector number */
sector:
	.quad 0x00

/*
 * Strings
 */

disk:
	.asciz "Error reading disk's sectors"
old_bios:
	.asciz "A BIOS from the dawn of time!"
exiting:
	.asciz "EXITING"
welcome:
	.asciz "Loading .."

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