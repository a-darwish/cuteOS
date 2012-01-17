/*
 * SMP trampoline
 *
 * Copyright (C) 2009-2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * NOTE! This code is linked to our long mode kernel. It's copied to
 * SMPBOOT_START directly before starting up the AP cores.
 *
 * We have to completely avoid using relocatable symbols; our linker
 * script rightfully assigns 64bit addresses to all of those symbols,
 * but below code uses only 16 and 32 bit fields. Using relocatables will
 * force the linker to truncate the addresses to fit the R_X86_64_16 and
 * R_X86_64_32 relocation types.
 *
 * To avoid this problem, we point to addresses by calculating the offset
 * of their symbols relative to the code start at `trampoline'. Thus, we're
 * pointing to offsets from whatever segment setup in %CS by the SIPI
 * vector field.
 *
 * For more information on relocations, check the System V ABI AMD64
 * supplement and the ELF specification v1.2.
 */

#include <smpboot.h>
#include <segment.h>
#include <paging.h>
#include <x86.h>

.code16

.text

.globl trampoline
trampoline:
	cli
	movw   %cs, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %ss

	/*
	 * Move to protected mode
	 */

	lidt   (idtdesc - trampoline)	# null
	lgdt   (gdt - trampoline)	# pmode %cs and %ds

	/* Set PE flag */
	movl   %cr0, %eax
	orl    $0x1, %eax
	movl   %eax, %cr0

	jmp    flush_prefetch		# %IP-relative

flush_prefetch:
	/* Setup pmode data segments */
	movw   $(gdt_ds - gdt), %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %ss

	/* Finally, use pmode %cs */
	DATA32 ljmp $0x08, $(SMPBOOT_START + (startup_32 - trampoline))

.code32

startup_32:

	/*
	 * Long mode initialization
	 */

	.equ   MSR_EFER, 0x0c0000080
	.equ   EFER_LME_BIT, 8

	/* Enable PAE */
	movl   %cr4, %eax
	bts    $0x5, %eax
	movl   %eax, %cr4

	/*
	 * NOTE! Page table symbols are absolute (ABS): no
	 * need to substract them from trampoline start.
	 */

	/* Zero page tables area */
	xorl   %eax, %eax
	movl   $identity_pml4, %edi
	movl   $((identity_end - identity_pml4) / 4), %ecx
	rep    stosl

	/* Identity maps: init_pml2 has 1-to-1 mappings between
	 * its entries and physical pages beginning from page 0 */
	movl   $(identity_pml3 + 0x007), identity_pml4
	movl   $(init_pml2 + 0x007), identity_pml3

	movl   $identity_pml4, %eax
	movl   %eax, %cr3

	/* Enable long mode */
	movl   $MSR_EFER, %ecx
	rdmsr
	btsl   $EFER_LME_BIT, %eax
	wrmsr

	/* Activate long mode: enable paging */
	movl   %cr0, %eax
	bts    $31, %eax
	movl   %eax, %cr0

	/* Clear prefetch and jump to 64-bit mode using 64-bit
	 * (conforming) code segment provided in the parameters
	 *
	 * Passed GDTR address is a virtual -128TB based one.
	 * We're in 32-bit mode, so only the least-significant
	 * 4 bytes will get loaded: the desired physical value! */
	movl   $SMPBOOT_PARAMS, %eax
	lgdt   SMPBOOT_GDTR(%eax)
	ljmp   $KERNEL_CS, $(SMPBOOT_START + (startup_64 - trampoline))

.code64

startup_64:
	/* Reload segment caches */
	xorw   %ax, %ax
	movw   %ax, %ds
	movw   %ax, %es
	movw   %ax, %fs
	movw   %ax, %gs
	movw   %ax, %ss

	/* As in head.S, setup temporary -2GB virtual mappings */
	movq   $(identity_pml3 + 0x007), (identity_pml4 + 511 * 8)
	movq   $(init_pml2 + 0x007), (identity_pml3 + 510 * 8)

	/* Virtualize %rip */
	movq   $KTEXT_VIRTUAL(SMPBOOT_START + (1f - trampoline)), %rax
	jmpq   *%rax

1:
	movq   $SMPBOOT_PARAMS, %rdi

	/* All passed addreses are in VIRTUAL() form */
	lidt   SMPBOOT_IDTR(%rdi)
	lgdt   SMPBOOT_GDTR(%rdi)
	movq   SMPBOOT_STACK_PTR(%rdi), %rsp
	movq   SMPBOOT_PERCPU_PTR(%rdi), %rdx

	/* We're no longer physical addresses dependent, use
	 * BSC's page tables which have no identity maps */
	movq   SMPBOOT_CR3(%rdi), %rax
	movq   %rax, %cr3

	/*
	 * Three pre-requisites to enter kernel C code:
	 * - A unique stack (done above)
	 * - A cleared direction flag (x86-64 ABI mandate)
	 * - %gs set to our unique per-CPU area
	 */

	cld

	movl   $MSR_GS_BASE,%ecx
	movl   %edx, %eax
	shrq   $32, %rdx
	wrmsr

	/* Jump to AP cores init code; we're done */
	movq   $secondary_start, %rax
	jmpq   *%rax

/*
 * We can't substract symbols from different ELF sections,
 * so we don't use the data section for below structuers.
 */

.align 16
gdt:
	.word  gdt_end - gdt - 1	# limit
	.long  SMPBOOT_START + (gdt - trampoline)
					# base; pmode flat address
	.word  0x0000			# padding
gdt_cs:
	/* 0x00cf9a000000ffff */
	.word  0xffff			# limit
	.word  0x0000			# base
	.word  0x9a00			# P=1, DPL=00, type=0xa (exec/read)
	.word  0x00cf			# G=1 (4K), D=1, limit[16:19]=0xf
gdt_ds:
	/* 0x00cf92000000ffff */
	.word  0xffff			# limit
	.word  0x0000			# base
	.word  0x9200			# P=1, DPL=00, type=0x2 (read/write)
	.word  0x00cf			# G=1 (4K), B=1, limit[16:19]=0xf
gdt_end:

idtdesc:
	.word  0			# zero limit, force shutdown if NMI
	.long  0			# base, ignored


.globl trampoline_end
trampoline_end:

/*
 * Artificial BSS section: nothing copied to SMPBOOT_START
 * vector after trampoline_end
 *
 * Mandatorily 4K-aligned Page Tables resides after the
 * first trampoline page.
 */

.equ	PAGE_SIZE,	0x1000
.equ	identity_pml4,	SMPBOOT_START + PAGE_SIZE
.equ	identity_pml3,	identity_pml4 + PAGE_SIZE
.equ	identity_end,	identity_pml3 + PAGE_SIZE
