/*
 * From 32-bit mode, call given real-mode function, THEN RETURN BACK!
 *
 * Copyright (C) 2009-2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * A key fact of the pmode->rmode switch and returning back is the
 * vaguely documented 16-bit protected mode. Check attached documentation
 * for further analysis.
 *
 * All 16-bit pmode and rmode code is run with a PMODE16_START 386 & 8086
 * [%cs, %ds] bases. The passed rmode function is executed from the well-
 * known offset:
 *
 *      `__rmode_function_offset'
 *
 * out of these segments base. Such rmode function is also executed from
 * a different place than where it's originally linked at, so be position
 * independent and avoid the use of relocatables in its code! A symbol in
 * the passed function can be internally dereferenced using:
 *
 *	`$SYMBOL - $rmode_code_start + __rmode_function_offset'
 *
 * while an absolute address can normally be derefernced using its offset
 * from the setup data segment:
 *
 *	`$ABSOLUTE_ADDRESS - PMODE16_START'
 *
 * Luckily, 'rmcall.h' now includes macros for all of the above formulae.
 */

#include <segment.h>
#include "rmcall.h"
#include "rmcommon.h"

.code32

/*
 * Switch to rmode and execute passed 16-bit method.
 * Switch-back to pmode-32 afterwards, then return to the
 * caller using a regular 32-bit 'ret'.
 *
 * @First param: rmode function code start address
 * @Second param: rmode function code end address
 */
	.globl  rmcall
	.type   rmcall, @function
rmcall:
	cli

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

	/* Save as much state as possible before switching
	 * to real-mode and calling BIOS services: on some
	 * laptops, BIOS corrupted the GDT and EFLAGS!! */
	sidt   pmode32_idtdesc
	pushfl
	popl   pmode32_flags
	movl   %esp, pmode32_esp

	/* Switch to the pmode16 code wich will handle
	 * jumping us to 8086 real-mode. */
	ljmp   $KERNEL_CS16, $0x0

pmode32_continue:
	/* We're back! Restore state! */
	movw   $KERNEL_DS, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %fs
	movw   %ax, %gs
	movw   %ax, %ss

	movl   pmode32_esp, %esp
	lidt   pmode32_idtdesc
	pushl  pmode32_flags
	popfl
	lgdt   global_gdt

	ret
	.size  rmcall, . - rmcall

.code16

/*
 * 16-bit protected mode entry (PE=1, %cs D=0)
 *
 * Just as the trampoline code, this code is executed from
 * a different place than where it's linked to; do not use
 * relocatable symbols: see comments on top of trampoline.S
 */
	.align 16
	.globl pmode16
pmode16:
	/* We're still in pmode (PE=1), so we can still change
	 * a segment cache by loading its GDT selector value */
	movw   $KERNEL_DS16, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %fs
	movw   %ax, %gs
	movw   %ax, %ss
	movw   $STACK_OFFSET, %sp

	/* BIOS interrupt handlers entries */
	lidt   (rmode_idtr - pmode16)

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
	movw   $STACK_OFFSET, %sp

	sti

	/* We're ready; call rmode method */
	movw  $(pmode16_end - pmode16), %ax
	call  *%ax

	/* NOTE! NOTE! NOTE!
	 *
	 * On some Sony VAIO (AMI Aptio v1.23) and Toshiba
	 * laptops, BIOS EDD code enable interrupts -- and
	 * leave em enabled! -- regardless of the previous
	 * EFLAGS.IF state.
	 *
	 * That BIOS bug was found after a horrible debugg-
	 * ing session of an endless cycle of reboots. The
	 * enabled interrupts caused random faults allover
	 * the rmode->pmode16->pmode32 switch process :-( */
	cli

	/* Reload %ds and the GDTR: some machines mess
	 * with the GDT after real-mode EDD BIOS calls. */
	movw   $(PMODE16_START >> 4), %ax
	movw   %ax, %ds
	lgdt   rmode_gdtr - pmode16

	/* Set the protected mode PE flag */
	movl   %cr0, %eax
	orl    $0x1, %eax
	movl   %eax, %cr0

	/* Return to pmode32; mission accomplished! */
	DATA32 ljmp $KERNEL_CS, $pmode32_continue

/*
 * Real-mode {I,G}DTR
 */
	.equ   RIDT_BASE, 0x0		# PC architecture defined
	.equ   RIDT_ENTRY_SIZE, 0x4	# 8086 defined
	.equ   RIDT_ENTRIES, 0x100	# 8086, 286, 386+ defined
rmode_idtr:
	.word  RIDT_ENTRIES * RIDT_ENTRY_SIZE - 1
	.long  RIDT_BASE

rmode_gdtr:
	.word  global_gdt_size - 1	# limit
	.long  global_gdt		# 16-bit mode: 3-byte base

	/* 'pmode16_end' should be nicely aligned: we copy
	 * the passed rmode function here and then call it */
	.align 16
pmode16_end:
	.equ   __rmode_function_offset, pmode16_end - pmode16
	.globl __rmode_function_offset

.data

/*
 * State before the pmode->rmode switch
 */
pmode32_esp:
	.long  0x0
pmode32_flags:
	.quad  0x0
pmode32_idtdesc:
	.word  0x0
	.long  0x0
