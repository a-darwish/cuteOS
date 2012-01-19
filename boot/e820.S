/*
 * BIOS E820H - Query System Address Map service.
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Check the ACPI spec v4.0, Chapter 14, "System Address Map Interfaces"
 * and critical comments on top of e820.h for further details.
 *
 * To query the BIOS, we switch back to 8086 real mode. A key fact of this
 * switch and return back is the vaguely documented 16-bit protected mode.
 * Check attached documentation for further analysis.
 *
 * NOTE! This code assumes being run from kernel head in 32-bit protected
 * mode, with interrupts disabled, and with undefined IDTR (no setup IDT).
 */

#include <segment.h>
#include <e820.h>

.text

.code32

/*
 * Get all what we need from real-mode BIOS services
 * before switching to long mode.
 */
.globl e820
	.type  e820, @function
e820:
	/* Send the rmode method desired to be executed to
	 * the 'switch to rmode' function; it will call
	 * the passed method on our behalf and return! */
	pushl  $get_mmap_end
	pushl  $get_mmap
	call   pmode32_to_real
	addl   $8, %esp
	ret

.code16

/*
 * Get system memory map through the E820 BIOS ACPI service
 * and store the result in the system structure described at
 * e820.h
 *
 * NOTE! ACPI specifies a minimum e820 entry size of 20 bytes;
 * it does NOT specify a max. Nonetheless, "Sony VAIO" BIOSes
 * discard any E820h call with an entry size > 24.  Let it be.
 */
	.type  get_mmap, @function
	.equ   e820_sig, E820_BIOS_SIG
	.equ   init_sig, E820_INIT_SIG
	.equ   entry_sz, 24		# ACPI-v4/SonyVaio
	.equ   width_sz, 4		# width field size
	.equ   error_sz, 4		# error field size
	.equ   chksm_sz, 4		# chksm field size
	.equ   end_sz,   4		# end mark size
	.equ   max_entry, E820_PHYS_MAX - \
		(entry_sz+width_sz+end_sz+error_sz+chksm_sz)
get_mmap:
	xorw   %ax, %ax
	movw   %ax, %es
	movw   %ax, %ds
	movl   $E820_PHYS_BASE, %edi	# buffer start

	/* First write our start signature */
	movl   $init_sig, %es:(%di)
	addl   $4, %edi			# signature size

	/* Iteratively call bios service */
	xorl   %ecx, %ecx		# previous entry size
	xorl   %ebx, %ebx		# continuation; 0 on start
1:
	addl   %ecx, %edi		# previous entry size
	addl   $width_sz, %edi		# size field width
	cmpl   $max_entry, %edi		# no more space?
	jge    e820_full		# buffer full
	movl   $entry_sz, %ecx		# max supported entry size
	movl   $e820_sig, %edx		# 'SMAP'
	movl   $0xe820, %eax		# bios function E280h
	pushl  %edi			# may get trashed
	int    $0x15
	popl   %edi

	/* Result sanity checks */
	cmpl   $e820_sig, %eax		# verify bios revision
	jne    e820_none		# e820 not supported :(
	cmpl   $0x14, %ecx		# 20 bytes minimum (acpi)
	jl     e820_bug			# bios bug
	jc     e820_err			# general error
	cmpl   $entry_sz, %ecx		# max entry size exceeded?
	jg     e820_huge

	/* Store returned entry size */
	movl   %ecx, %es:-width_sz(%di)

	/* Last entry? */
	testl  %ebx, %ebx
	je     e820_done		# no more entries

	/* Read next entry */
	jmp    1b

	/* Choose error code (%eax) */
e820_full:
	movl   $E820_BUF_FULL, %eax
	jmp    end
e820_none:
	movl   $E820_NOT_SUPP, %eax
	jmp    end
e820_err:
	movl   $E820_ERROR, %eax
	jmp    end
e820_bug:
	movl   $E820_BIOS_BUG, %eax
	jmp    end
e820_huge:
	movl   $E820_HUGE_ENTRY, %eax
	jmp    end
e820_done:
	movl   $E820_SUCCESS, %eax
	addl   %ecx, %es:(%di)
	addl   $end_sz, %es:(%di)
	jmp    end

	/* Error code stored at %eax; %edi
	 * points to 'err code' field start */
end:	movl   $E820_END, %es:-end_sz(%di)
	movl   %eax, %es:(%di)

	/* Finally, calculate checksum */
	movl   $E820_PHYS_BASE, %esi
	addl   $error_sz, %edi
	call   get_checksum
	movl   %eax, %es:(%di)

	/* Restore segments state */
	movw   %cs, %ax
	movw   %ax, %ds
	movw   %ax, %es
	ret

/*
 * Calculate a memory area checksum where %esi is
 * buffer start and %edi points to buffer end.
 * NOTE! %ds must have a 0 base value
 */
	.type  get_checksum, @function
get_checksum:
	xorl   %eax, %eax
1:	cmpl   %esi, %edi
	je     2f
	movzbl (%esi), %ebx
	addl   %ebx, %eax
	incl   %esi
	jmp    1b
2:	ret

get_mmap_end:

/*
 * 32-bit protected mode to real-mode switch ..
 */

/* Where the 16-bit protected mode code and data is
 * going to be copied to and executed from */
#define PMODE16_START	0x6000

.code32

/*
 * Switch to rmode and execute passed 16-bit method.
 * Switch-back to pmode32 afterwards, then return to the
 * caller using a regular 32-bit 'ret'.
 *
 * @First param: rmode function code start address
 * @Second param: rmode function code end address
 */
	.type   pmode32_to_real, @function
pmode32_to_real:
	cli

	/* Save our gdtr, so pmode16 code can jump back
	 * to pmode32 using the same setup gdt */
	sgdt   pmode32_gdtr

	/* Copy pmode16 switch code and data below 1MB */
	movl   $pmode16, %esi
	movl   $PMODE16_START, %edi
	movl   $(pmode16_end - pmode16), %ecx
	rep    movsb

	/* Copy rmode func directly below pmode16 code.
	 * %edi (dst) is already setup from above movsb */
	movl   4(%esp), %esi		# src
	movl   8(%esp), %ecx
	subl   %esi, %ecx		# len
	rep    movsb

	/* Load 16-bit code and data segments. Those
	 * segments are also applicable to rmode */
	lgdt   pmode16_gdt

	/* Save state and switch to pmode16 code wich
	 * will handle jumping us to real-mode */
	pusha
	movl   %esp, pmode32_esp
	ljmp   $KERNEL_CS, $0x0

pmode32_continue:
	/* We're back! restore state; gdtr and %cs
	 * have already been restored */
	movw   $KERNEL_DS, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %ss
	movl   pmode32_esp, %esp
	popa
	ret

.code16

/*
 * 16-bit protected mode entry (PE=1, %cs D=0)
 *
 * Just as the trampoline code, this code is executed from
 * a different place than where it's linked to; do not use
 * relocatable symbols: see comments on top of trampoline.S
 */
pmode16:
	/* We're still in pmode (PE=1), so we can still change
	 * a segment cache by loading its GDT selector value */
	movw   $0x10, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %fs
	movw   %ax, %gs
	movw   %ax, %ss

	/* Switch to canonical rmode: clear PE */
	movl   %cr0, %eax
	andl   $~0x1, %eax
	movl   %eax, %cr0

	/* Flush prefetch; setup 8086 code segment */
	ljmp   $(PMODE16_START >> 4), $(rmode - pmode16)

/*
 * Canoncial real mode entry; no relocatables
 * PE=0, cached %cs D=0, %cs = (base >> 4)
 */
rmode:
	movw   %cs, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %fs
	movw   %ax, %gs
	movw   %ax, %ss
	movw   $0x1000, %sp

	/* BIOS interrupt handlers entries */
	lidt   (rmode_idtr - pmode16)

	/* We're ready; call rmode method */
	movw  $(pmode16_end - pmode16), %ax
	call  *%ax

	/* Set the protected mode PE flag */
	movl   %cr0, %eax
	orl    $0x1, %eax
	movl   %eax, %cr0

	/* Switch back to pmode16 */
	jmp    flushpr

/*
 * 16-bit protected mode re-entry
 */
flushpr:
	/* Restore pmode32 gdt */
	lgdt   (pmode32_gdtr - pmode16)

	/* Return to pmode32; mission accomplished! */
	DATA32 ljmp $KERNEL_CS, $pmode32_continue

/*
 * GDTR to return back to pmode32. Should be set by
 * the pmode32 code _before_ switching to pmode16.
 * NOTE! must be copied with code to PMODE16_START
 */
	.equ   GDTR_SIZE, 6		# 2-byte limit + 4-byte base
pmode32_gdtr:
	.skip  GDTR_SIZE

/*
 * Real-mode IDTR
 */
	.equ   RIDT_BASE, 0x0		# PC architecture defined
	.equ   RIDT_ENTRY_SIZE, 0x4	# 8086 defined
	.equ   RIDT_ENTRIES, 0x100	# 8086, 286, 386+ defined
rmode_idtr:
	.word  RIDT_ENTRIES * RIDT_ENTRY_SIZE - 1
	.long  RIDT_BASE

pmode16_end:

.data

/*
 * Pmode16 and rmode applicable segment descs.
 */
pmode16_gdt:
	/* gdtr */
	.word  (pmode16_gdt_end - pmode16_gdt - 1)
	.long  pmode16_gdt		# base
	.word  0x0000			# padding

	/* 16-bit code segment; default operand size and
	 * address size is 16 bits, limit is 64K bytes */
	.word  0xffff			# limit
	.word  PMODE16_START		# base
	.word  0x9a00			# P=1, DPL=00, type=0xA (execute/read)
	.word  0x0000			# G=0 (byte), D=0 (16-bit)

	/* 16-bit data segment */
	.word  0xffff			# limit
	.word  PMODE16_START		# base
	.word  0x9200			# P=1, DPL=00, type=0x2 (read/write)
	.word  0x0000			# G=0 (byte), D=0 (16-bit)
pmode16_gdt_end:

.bss

/*
 * 32-bit protected mode stack pointer
 */
pmode32_esp:
	.long  0x0
