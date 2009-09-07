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
 *  0x0050:0x0000 -> 0x8000:0xffff  (0x00500 -> 0x8ffff) free memory
 *  0x9000:0x0000 -> 0x9000:0xffff  (0x90000 -> 0x9ffff) usually Extended BIOS Data Area
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

/* Print messages to screen */
#define MSG(msg)		\
	movw   $msg, %si;  	\
	call   print_string
	
.code16

/* The text section is relocated by the linker to 0x0000, thus
 * when we send an address of a string, we're actually sending
 * its offset from whatever segment currently setup in the
 * code segment register */
.text
.global _start
_start:
	/* All BIOSes load the mbr to the same 0x07c00 physical
	 * address. The difference is that some load it using the
	 * 0x07c0:0x00 scheme while others load it as 0x00:0x7c00.
	 * For certainity, we force our own realmode code segment
	 * value using a far jump */
	.equ   MBR_SEG, 0x07c0
	ljmp   $MBR_SEG, $real_start

real_start:
	/* We want to run the actual kernel beginning from the 512KB
	 * [0x00000-0x7ffff] range. The bios loads the mbr at
	 * [0x07c00-0x07dff] which is inside the destined kernel range.
	 * This makes kernel loading impossible without relocation as
	 * the mbr code which does the copying will overwrite itself
	 * in the process. To avoid this, move the bootsector code out
	 * of the way to physical address 0x80500 */
	.equ   DST_SEG, 0x8050
	movw   $MBR_SEG, %ax
	movw   %ax, %ds			# %ds -> %es (si = di = 0)
	movw   $DST_SEG, %ax
	movw   %ax, %es
	xorw   %si, %si
	xorw   %di, %di
	movw   $256, %cx
	rep    movsw
	ljmp   $DST_SEG, $relocated

relocated:
	movw   $DST_SEG, %ax
	movw   %ax, %es
	movw   %ax, %ss
	movw   %ax, %ds
	movw   %ax, %es
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
	 * at 0x00500. For simplicity, we'll copy the disk's first
	 * 1024 sectors post the mbr to ram regardless of the real
	 * kernel size; we may optimize later. The kernel will be in
	 * the range [0x00500-0x804ff].
	 *
	 * Note that we currently copy to 0x500 and not 0x00 as the
	 * latter will invalidate the bios and real-mode ivt which
	 * are still needed to copy the disk sectors to RAM. Once
	 * we're sure we won't need the bios anymore, we'll copy the
	 * kernel to 0x0, enable protected mode, and run it.
	 *
	 * INT 0x13, function 0x42 - extended read sectors
	 * input  %dl     - drive number
	 * input  %ds:si  - pointer to the Disk Address Packet
	 * output %ah     - error code (if any)
	 * output CF      - error flag (true = error) */
	.equ   KERNEL_SEGMENT, 0x0050
	.equ   KERNEL_SEGMENT_END, 0x8050
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
	addw   $0x8000, buffer_offset   # 32K copied; 0x8000 bytes
	jnc    same_segment
	addw   $0x1000, buffer_seg	# 64K copied, move to next segment
	movw   $0x0000, buffer_offset
same_segment:
	loop   next_sectors

	/* Now we prepare moving to protected mode
	 *
	 * we disable interrupts cause the interrupt table format
	 * for protected mode and real-mode differs. It is inevitable
	 * that if IDTR selects an interrupt table, it  will have the
	 * wrong format at some time which can lead to unpredictable
	 * behvaior (horrors)
	 *
	 * The processor will still attempt to access the interrupt
	 * table if an exception or nonmaskable interrupt (NMI) occurs;
	 * change the limit value in the IDTR to zero, which will force
	 * a shutdown if an exception or a malice NMI occurs: already
	 * unrecoverable events at this stage. */
	cli
	lidt   idtdesc			# null
	lgdt   gdt			# pmode cs and ds descriptors

	/* We copy the kernel to physical address 0x0 cause this will
	 * be the protected mode code segment base. This will
	 * completely strike out the bios and the real-mode IVT, but
	 * it should be fine as we already have interrupts disabled */
	cld
	movw   $0x00000, %ax
start_copy:
	movw   %ax, %es			# %ds -> %es
	/* dst always prcedes src $(KERN_SEGMENT<<4) bytes */
	addw   $KERNEL_SEGMENT, %ax
	movw   %ax, %ds
	cmpw   $KERNEL_SEGMENT_END, %ax	# 512KB copied?
	je     end_copy
	xorw   %si, %si
	xorw   %di, %di
	movw   $0x8000, %cx
	/*  move to the next 0x1000<<4 (64K) bytes */
	addw   $(0x1000 - KERNEL_SEGMENT), %ax
	rep    movsw
	jmp    start_copy

end_copy:

	/* Enable the A20. The story goes that IBM, to achive backward
	 * compatibility with the 8086s, used a three-state multiplexer
	 * to gate the A20 line output from its 80286 CPUs before it
	 * reached the rest of the machine. A tri-state mux can either
	 * produce 0, efficively killing the a20 line, or go to a high
	 * impedence state where it just acts as a wire. They used an
	 * unused pin in the keyboard controller to control that mux */
	call   enable_a20

	/* Set the protected mode PE flag */
	movl   %cr0, %eax
	orl    $0x1, %eax
	movl   %eax, %cr0

	/* Flush the processor's instruction prefetch queue by
	 * executing a jump instruction. The 80386 fetches and decodes
	 * instructions and addresses before they are used; however,
	 * after a change into protected mode, the prefetched
	 * instruction information is no longer valid. A JMP forces
	 * the processor to discard the invalid information.
	 *
	 * Note that a short jump to a code32 block won't work. It's
	 * either a short jump to a code16 block, or a long jump to
	 * code32 block where the %cs selector is updated. A short jump
	 * is ideal cause it's the same in real mode and 16-bit
	 * protected mode. It's also the suggested method in Intel
	 * documents. Thanks to bcos@#osdev for this valuable 'short
	 * jump' info */
	jmp    flush_prefetch

flush_prefetch:
	/* Setup the data segments and the stack pointer. Note that
	 * the segment descriptors index is 8bytes aligned, thus
	 * Intel uses the least significant 3 bits in the segment
	 * selector to decode the DPL and the TI bits */
	movw   $(gdt_ds - gdt), %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %ss

	/* GAS should know that $(gdt_cs - gdt) which equals 0x08 *is*
	 * an absoulte code segment value, but it doesn't */
	DATA32 ljmp $0x08, $0

/*
 * A20 line methods
 *
 * There's usually a controller in the keyboard itself and
 * a motherboard keyboard controller responsible for buffering,
 * data validity checking, and translation form Kscan codes
 * provided by the in-keyboard controller to scan codes.
 *
 * On AT+ machines, ports 0x60 and 0x64 communicate with the
 * motherboard keyboard controller. What's relative to us is
 * port 0x64 which is used to read status from the
 * motherboard keyboard controller and to send commands to it
 *
 * Port 0x64 input:
 *
 * bit1: If clear, the motherboard controller's input buffer
 * is empty. A write can be made to port 0x60 or port 0x64.
 * If set, the motherboard controller's input buffer is full.
 * Until the motherboard controller has emptied the buffer,
 * no writes should occur to ports 0x60/0x64 or the command
 * will be lost.
 *
 * bit0: If clear, the motherboard controller's output buffer
 * is empty. A read from port 0x60 will not be valid.
 * If set, the motherboard controller's output buffer has
 * a byte available. Use port 0x60 to read that byte.
 *
 * Port 0x64 output:
 * (send commands to motherboard's keyboard controller)
 *
 * 0xD0: Read the controller's state/output-port. The byte can
 * be read by reading from 0x60 afterwards.
 * bit1: if set, A20 line is on. The opposite if clear.
 *
 * 0xD1: The next byte written to 0x60 is transfered to the
 * controller's state/output-port
 * same bit values semantics as the read state command
 */

	.equ   KCOMM, 0x64		# controller command io port
	.equ   KDATA, 0x60		# controller data io port
	.equ   KCOMM_GETSTATE, 0xD0
	.equ   KCOMM_SETSTATE, 0xD1

enable_a20:
	/* First we read the controller state and save it. To avoid
	 * affecting other system functions, we want to keep the
	 * other state bits intact while setting the A20 bit */
	call   kpoll_empty
	movb   $KCOMM_GETSTATE, %al
	outb   $KCOMM
	call   kpoll_output		# wait for output
	xorw   %ax, %ax
	inb    $KDATA
	pushw  %ax
	/* Then we write the new state with the A20 bit set */
	call   kpoll_empty
	movb   $KCOMM_SETSTATE, %al
	outb   $KCOMM
	call   kpoll_empty
	popw   %ax
	orw    $0x02, %ax
	outb   $KDATA
	ret				# done (hopefully)

/* loop till the motherboard controller's input buffer
 * is empty. You can write to 0x64 or 0x60 afterwards */
kpoll_empty:
	xorw   %ax, %ax
	inb    $KCOMM
	btw    $1, %ax
	jc     kpoll_empty
	ret

/* loop till the motherboard controller's output buffer
 * has an available byte to read from 0x60 */
kpoll_output:
	xorw   %ax, %ax
	inb    $KCOMM
	btw    $0, %ax
	jnc    kpoll_output
	ret

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
	xorb   %bh, %bh
	movw   $4, %cx		     # 4 hex digits (2bytes)
print_digit:
	/* extract leftmost digit in %al */
	rolw   $4, %dx
	movw   $0x0e0f, %ax
	andb   %dl, %al
	/* transform extracted digit to ascii */
	cmpb   $0x0a, %al
	jl     digit
	addb   $0x07, %al
digit:
	addb   $0x30, %al
	int    $0x10
	loop   print_digit
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

/*
 * Error conditions
 */

disk_error:
	pushw  %ax
	MSG    (disk)
	popw   %dx
	call   print_hex
	jmp    halt
no_ebios:
	MSG    (old_bios)
halt:
	MSG    (error)
	jmp    .

/*
 * Disk Address Packet
 */

disk_address_packet:
/* packet size in bytes */
packet_size:
	.byte  0x10
unused:
	.byte  0x00
/* number of blocks to transfer [1 - 127] */
sectors_cnt:
	.byte  0x00
unused2:
	.byte  0x00
/* read/write buffer address */
buffer_offset:
	.word  0x00
buffer_seg:
	.word  0x00
/* logical sector number */
sector:
	.quad  0x00

/*
 * Protected mode GDT
 *
 * Refer to attached Intel i686 documentation, vol. 3:
 * 2.4 Memory-management registers
 * 3.4.3 Segment descriptors
 * 3.4.3.1 Code and data segment descriptor types
 */

/* As said by linux x86 boot code, there are machines which are
 * known not to boot with the GDT being 8-byte unaligned. Intel
 * also recommends 16 byte alignment, so let it be */
.align 4
gdt:
	/* since the null descriptor values are not used, we
	 * save the gdt table descriptor here at its place */
	.word  gdt_end - gdt	# limit: size of gdt table in bytes
	.long  DST_SEG<<4 + gdt	# NOTE! protected mode flat address
	.word  0x0000		# padding
gdt_cs:
	/* code segment descriptor */
	.word  0xffff		# limit = 0xfffff
	.word  0x0000		# base address = 0x00
	.byte  0x00, 0x9A	# P=1, DPL=00, S=1, TYPE=0xA (execute/read)
	.byte  0xcf, 0x00	# granularity=4K, D=1
gdt_ds:
	/* data segment descriptor */
	.word  0xffff		# limit = 0xfffff
	.word  0x0000		# base address = 0x00
	.byte  0x00, 0x92	# P=1, DPL=00, S=1, TYPE=0x2 (read/write)
	.byte  0xcf, 0x00	# granularity=4K, B=1
gdt_end:

idtdesc:
	.word  0
	.long  0

/*
 * Strings
 */

disk:
	.asciz "Disk "
old_bios:
	.asciz "Old BIOS"
error:
	.asciz " error; EXITING"
welcome:
	.asciz "Loading..\r\n"

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

