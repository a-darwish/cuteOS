/*
 * Load Ramdisk.
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Send our real-mode method, which loads ramdisk using BIOS EDD, to the
 * "rmcall" function; "rmcall" will switch to real-mode, call the passed
 * method on our behalf, then return back!
 *
 * The python build script creates the final disk image by:
 * o extending the kernel image size to 512-KB
 * o appending a header containing signature and ramdisk size
 * o appending the ramdisk image
 *
 * Thus load the first sector after kernel image (disk sector 0x400) to
 * RAM, parse the script-built header to get the ramdisk number of sec-
 * tors and load them.
 */

#include <paging.h>
#include "rmcall.h"
#include "rmcommon.h"

.code32

/*
 * Ramdisk header format:
 * o start signature, "CUTE-STA", 8-bytes
 * o ramdisk size in number of 512-byte sectors, 4-bytes
 * o ramdisk length (minus the header) in bytes, 4-bytes
 * o end signature, "CUTE-END", 8-bytes
 */
.equ start_sigl, ('C' | 'U' << (1*8) | 'T' << (2*8) | 'E' << (3*8))	# low
.equ start_sigh, ('-' | 'S' << (1*8) | 'T' << (2*8) | 'A' << (3*8))	# high
.equ end_sigl,   ('C' | 'U' << (1*8) | 'T' << (2*8) | 'E' << (3*8))	# low
.equ end_sigh,   ('-' | 'E' << (1*8) | 'N' << (2*8) | 'D' << (3*8))	# high

	.globl load_ramdisk
	.type  load_ramdisk, @function
/*
 * Three local variables:
 *
 * @destination_mem: Final destination of just loaded ramdisk sector
 * @first_sector:    A flag which is only set when loading the first
 *                   ramdisk sector.
 * @sectors_count:   Number of ramdisk sectors yet to be loaded.
 */
#define        destination_mem  -0x04(%ebp)
#define        first_sector     -0x08(%ebp)
#define        sectors_count    -0x0c(%ebp)
load_ramdisk:
	push   %ebp
	movl   %esp, %ebp
	subl   $12, %esp		# 3 local vars

	movl   $0x400, RCODE_SECTOR_LBA
	movl   $0x1, RCODE_FIRST_CALL
	movl   $1, first_sector
	movl   $KTEXT_PHYS(__kernel_end), destination_mem
next_sec:
	movl   %ecx, sectors_count	# Don't trash the loop counter!
	pushl  $loadfs16_end
	pushl  $loadfs16
	call   rmcall
	addl   $8, %esp

	/* If we're the 1st sector, do the necessary signature
	 * checking, then parse the number of ramdisk sectors */
	cmpl   $1, first_sector
	jne    1f
	cmpl   $start_sigl, (RCODE_BUFFER)
	jne    bad_signature
	cmpl   $start_sigh, (RCODE_BUFFER + 4)
	jne    bad_signature
	pushl  (RCODE_BUFFER + 8)
	popl   sectors_count
	cmpl   $end_sigl, (RCODE_BUFFER + 16)
	jne    bad_signature
	cmpl   $end_sigh, (RCODE_BUFFER + 20)
	jne    bad_signature
	movl   $0, first_sector
1:
	/* Copy just loaded buffer to its final destination */
	movl   $RCODE_BUFFER, %esi
	movl   destination_mem, %edi
	movl   $(512 / 4), %ecx
	rep    movsl

	/* Loop increment part */
	movl   %edi, destination_mem	# Next sector's final dest
	incl   RCODE_SECTOR_LBA		# Next ramdisk LBA
	movl   sectors_count, %ecx	# Restore the loop counter!
	loop   next_sec

	movl   %ebp, %esp
	popl   %ebp
	ret
	.size load_ramdisk, . - load_ramdisk

bad_signature:
	movl   $badsig_message, %esi
	movl   $badsig_end - badsig_message, %ecx
	call   vga_print
1:	hlt
	jmp    1b
badsig_message:
	/* Message string + VGA color; little-endian */
	.byte  'B', 0x0f, 'a', 0x0f, 'd', 0x0f, ' ', 0x0f, \
	       'r', 0x0f, 'a', 0x0f, 'm', 0x0f, 'd', 0x0f, \
	       'i', 0x0f, 's', 0x0f, 'k', 0x0f, ' ', 0x0f, \
	       's', 0x0f, 'i', 0x0f, 'g', 0x0f, 'n', 0x0f, \
	       'a', 0x0f, 't', 0x0f, 'u', 0x0f, 'r', 0x0f, \
	       'e', 0x0f, '.', 0x0f, ' ', 0x0f, ' ', 0x0f
badsig_end:

.code16

#include "rmcommon.h"

loadfs16:
	/* Only for the first sector, print a "Loading: " message */
	cmpl   $1, ABS_ADDR(RCODE_FIRST_CALL)
	jne    1f
	movw   $REL_ADDR(loading_msg, loadfs16), %si
	call   print_string
	movl   $0, ABS_ADDR(RCODE_FIRST_CALL)
1:
	/* We wouldn't be here if EDD extensions are not present, but
	 * in case some funky BIOS developer put EDD init code in the
	 * "Check Extensions Present" service, call it before reading
	 * any disk sector using EDD's "Extended Read". It also vali-
	 * dates the passed BIOS drive number. */
	EDD_CHECK_EXTENSIONS_PRESENT(ABS_ADDR(RCODE_DRIVE_NUMBER), no_ebios)

	/* INT 0x13, function 0x42 - extended read sectors
	 * input  %dl     - drive number (provided by bios in %dl)
	 * input  %ds:si  - pointer to the Disk Address Packet
	 * output %ah     - error code (if any)
	 * output CF      - error flag (true = error) */
	movl   ABS_ADDR(RCODE_SECTOR_LBA), %eax
	movl   %eax, REL_ADDR(disk_address_packet + 0x8, loadfs16)
	movw   $REL_ADDR(disk_address_packet, loadfs16), %si
	movb   ABS_ADDR(RCODE_DRIVE_NUMBER), %dl
	movb   $0x42, %ah
	int    $0x13
	jc     disk_error

	/* A ".": indicator of a succesfully-loaded sector */
	movw   $REL_ADDR(dot_char, loadfs16), %si
	call   print_string
	ret

disk_address_packet:
	.byte  0x10			# Packet size in bytes
	.byte  0x00			# Unused
	.byte  0x01			# Number of blocks to transfer [1 - 127]
	.byte  0x00			# Unused
	.word  RCODE_BUFFER		# Buffer address
	.word  0x0000			# Buffer sgment
	.quad  0x0000000000000000	# Logical sector number(dynamically set)

no_ebios:
	movw   $REL_ADDR(noebios_msg, loadfs16), %si
	call   print_string
	jmp    1f

disk_error:
	pushw  %ax			# INT 0x13 error code
	movw   $REL_ADDR(loading_err, loadfs16), %si
	call   print_string
	popw   %dx
	call   print_hex

1:	hlt
	jmp    1b

	PUT_PRINT_METHODS()

loading_msg:
	.asciz "Loading Ramdisk (each dot = a 512-byte sector): "
loading_err:
	.asciz "\r\nError Loading Ramdisk; %ax = 0x"
noebios_msg:
	.asciz "\r\nBIOS EDD extensions not present."
dot_char:
	.asciz "."

loadfs16_end:
