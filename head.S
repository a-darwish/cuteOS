/*
 * 64-bit startup code
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

.code32

/*
 * 32-bit kernel entry point. We run with the only pre-condition
 * of being loaded within a flat-memory model.
 */
.globl _start
_start:
	/* Setup a boot stack and assure amd64 support */
	movl   $stack_end, %esp
	call   check_cpu64
	testl  %eax, %eax
	jz     no_longmode

	/*
	 *  Long mode initialization
	 */

	/* Enable the 64-bit page translation table entries by setting
	 * PAE. This is required before activating long mode */
	.equ   CR4_PAE, 5
	movl   %cr4, %eax
	bts    $CR4_PAE, %eax
	movl   %eax, %cr4

	/* Load a 64-bit(L=1) code segment */
	lgdt   gdt

	/*
	 * Identity-map the first 8MB region (0x000000-0x800000)
	 * CRITICAL: pgtable must be 4KB page aligned.
	 */

	/* Zero the entire page tables area; processor won't tolerate
	 * any garbage there (P bit leading to pagefaults, etc) */
	xorl   %eax, %eax
	movl   $pgtable, %edi
	movl   $(PGTABLES_AREA/4), %ecx
	rep    stosl

	/* Single PML4 entry for 0x0; P=1, R/W=1, U/S=1, PWT=0,
	 * PDP base = pgtable + 0x1000 */
	movl   $(pgtable + 0x1007), pgtable

	/* Single PDP entry for 0x0; P=1, R/W=1, U/S=1, PWT=0
	 * PD base = pgtable + 0x2000 */
	movl   $(pgtable + 0x1000), %ebx
	movl   $(pgtable + 0x2007), (%ebx)

	/* 4 Page Directory (PD) entries for range (0b0-0b0100)
	 * P=1, R/W=1, U/S=1, PWT=0.
	 * PT base = pgtable + (0x3000, 0x4000, ..., 0x6000) */
	movl   $(pgtable + 0x2000), %ebx
	movl   $(pgtable + 0x3007), %edx
	movl   $4, %ecx			# 4 PD entries
1:	movl   %edx, (%ebx)
	addl   $0x1000, %edx		# next Page Table
	addl   $8, %ebx			# next PDE
	decl   %ecx
	jnz    1b

	/* Set the 4 Page Tables pointed by the 4 PDEs above. The
	 * first entry of the first Page Table based at pgtable+0x3000
	 * point to page 0x0; second entry points to page 0x1, and so on
	 * spanning the four tables and covering the 8MB range */
	movl   $(pgtable + 0x3000), %ebx
	xorl   %ecx, %ecx
1:	movl   %ecx, %eax
	shl    $12, %eax		# page address = page-num<<12
	addl   $0x003, %eax		# P=1, R/W=1, U/S=0
	movl   %eax, (%ebx)
	addl   $8, %ebx			# next PTE
	incl   %ecx			# next page number
	cmpl   $2048, %ecx		# 4 tables * 512 entries = 2048
	jne    1b

	/* Final step: store PML4 base adderss */
	movl   $pgtable, %eax
	movl   %eax, %cr3

	/* Enable long mode */
	.equ   MSR_EFER, 0x0c0000080
	.equ   EFER_LME, 8
	movl   $MSR_EFER, %ecx
	rdmsr
	btsl   $EFER_LME, %eax
	wrmsr

	/* Activate long mode: enable paging */
	movl   %cr0, %eax
	bts    $31, %eax
	movl   %eax, %cr0

	ljmp   $0x08, $startup_64

no_longmode:
	/* we should print something here once the VGA
	 * code is done */
	jmp    .

/*
 * Verify we're running on an amd64-compatible cpu
 */
check_cpu64:
	/* Check if the CPUID instruction is supported to avoid an
	 * invalid-opcode exception (#UD) in older cpus. The ability
	 * to modify the eflags register ID flag indicates support */
	pushfl
	popl   %eax
	movl   %eax, %ebx
	xorl   $0x00200000, %eax	# toggle bit 21 (ID)
	pushl  %eax
	popfl				# save modified eflags
	pushfl
	popl   %eax
	cmpl   %eax, %ebx		# no change apparent?
	jz     no_amd64

	/* Check if extended CPUID functions exist. It must be
	 * supported on 64bit-capable processors */
	movl   $0x80000000, %eax
	cpuid
	cmpl   $0x80000001, %eax
	jb     no_amd64

	/* Finally, check for longmode availability */
	.equ   X86_FEATURE_LM, 29
	movl   $0x80000001, %eax
	cpuid
	btl    $X86_FEATURE_LM, %edx
	jnc    no_amd64

	/* Results time */
	movl   $0x1, %eax		# We're amd64!
	ret
no_amd64:
	xorl   %eax, %eax
	ret

/*
 * 64-bit code entry!
 */
.code64
startup_64:
	movq   $0xdeadbeefdeadbeef, %rax
	jmp    .

/*
 * 64-bit GDT and GDT descriptor. Elements ignored in long-mode
 * are not set: we won't support compatibility mode by design
 */
.align 4
gdt:
	/* GDT descriptor in place of NULL */
	.word  gdt_end - gdt            # limit
	.long  gdt			# Base
	.word  0x0000			# padding

	/* long mode Code segment */
	.long  0x00000000		# base/limit; ignored in LM
	.byte  0x00, 0x98		# P=1, DPL=0, C/conforming=0
	.byte  0xa0, 0x00		# L=1, D=0, base[24-31] ignored

	/* Data segment */
	.quad  0x0000000000000000	# null; %ds is ignored in LM
gdt_end:

/*
 * Booting stack
 */
	.equ   STACK_SIZE, 0x2000
stack:
	.fill  STACK_SIZE, 1, 0
stack_end:

/*
 * Boot Page tables. Shouldn't this be a BSS section relocated
 * by a linker script to save executable size?
 */
	.equ  PAGE_SIZE, 0x1000
	.equ  PGTABLES_CNT, 7		# 4 PT + 1 PD + 1PDP + 1PML4
	.equ  PGTABLES_AREA, PAGE_SIZE * PGTABLES_CNT

.balign 4096
pgtable:
	.fill PGTABLES_AREA, 1, 0
