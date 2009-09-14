/*
 * 64-bit startup code
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

.text
.code32

/*
 * 32-bit kernel entry point. We run with the only pre-condition
 * of being loaded within a flat-memory model.
 */
.globl startup_32
startup_32:
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

	/* Load a 64-bit (L=1) code segment */
	lgdt   gdt

	/*
	 * Identity map the first physical 2MBs using a single 2MB
	 * page. We should map more once the kernel get larger; kernel
	 * size shouldn't exceed one megabyte for now.
	 */

	/* Zero the entire init pagetables area */
	xorl   %eax, %eax
	movl   $init_pgt, %edi
	movl   $(INITPGT_AREA / 4), %ecx
	rep    stosl

	/* pml4 entry: P = R/W = U/S = 1 PDP base = pml4 + 0x1000 */
	movl   $(init_pgt + 0x1007), init_pgt

	/* pdp entry: P = R/W = U/S = 1 PD base = pml4 + 0x2000 */
	movl   $(init_pgt + 0x2007), (init_pgt + 0x1000)

	/* pd entry: P = R/W = 1, U/S = 0, 2MB page flag: bit7 = 1,
	 * page base = 0x0 */
	movl   $0x083, (init_pgt + 0x2000)

	/* Final step: store PML4 base adderss */
	movl   $init_pgt, %eax
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

	/* clear prefetch and jump to 64bit code */
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

#define VIRTUAL_START     0xffffffff80000000
#define VIRTUAL(address)  (VIRTUAL_START + address)

.code64
startup_64:
	/* Reset the stack */
	movq   $stack_end, %rsp

	/*
	 * We already have the first physical 2MBs identity mapped;
	 * setup page tables for running the kernel beginning from
	 * virtual address 0xffff80100000.
	 *
	 * Map the 768M 0xffff80000000-0xffffb0000000 region to
	 * to physical addresses 0x0000-0x30000000 using 2MB pages.
	 */

	/* pml4 entry. We only need one entry for 0b111111111, which
	 * will be typically located at the very end of the table.
	 * P = R/W = U/S = 1 as usual. For PDP base, we use the same
	 * PDP table we used for identity mapping */
	movq   $(init_pgt + 0x1007), (init_pgt + 511 * 8)

	/* pdp entry. We also only need a single entry for 0b111111110
	 * which is one entry before the end of PDP. P = R/W = U/S = 1
	 * PD base for the entry is a new table */
	movq   $(init_pgt + 0x3007), (init_pgt + 0x1000 + 510 * 8)

	/* OK, now we setup the new PD table that is going to do all
	 * the work. We map entry 0 to page 0, entry 1 to page 1 and
	 * so on till the end of the table. P = R/W = 1, U/S = 0,
	 * bit7 (2MB page flage) = 1 */
	movq   $(init_pgt + 0x3000), %rbx
	xorq   %rcx, %rcx		# first page num = 0x0
1:	movq   %rcx, %rax
	shl    $21, %rax		# page address = page-num<<21
	addq   $0x083, %rax		# flags: P, R/W, 2M
	movq   %rax, (%rbx)
	addq   $8, %rbx			# next PDE
	incq   %rcx			# next page
	cmpq   $(init_pgt + 0x4000), %rbx
	jne    1b

	/* Done; apply the page tables */
	movq   $init_pgt, %rax
	movq   %rax, %cr3

	/* Run using high half virtual addresses from now on */
	movq   $VIRTUAL(1f), %rax
	jmpq   *%rax
1:

	/* Viola! */
	jmp    .

.data

/*
 * 64-bit GDT and GDT descriptor. Elements ignored in long-mode
 * are not set: we won't support compatibility mode by design
 */
.align 8
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

.bss

/*
 * Booting stack
 */
.align 8
	.equ   STACK_SIZE, 0x4000
stack:
	.fill  STACK_SIZE, 1, 0
stack_end:

/*
 * Boot Page tables. Shouldn't this be a BSS section relocated
 * by a linker script to save executable size?
 */
	.equ   PAGE_SIZE, 4096
	.equ   INITPGT_PGCNT, 4
	.equ   INITPGT_AREA, INITPGT_PGCNT * PAGE_SIZE

.balign 4096
init_pgt:
	.fill  INITPGT_AREA, 1, 0
