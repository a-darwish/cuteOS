/*
 * 64-bit bootstrap code
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <segment.h>
#include <paging.h>

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

	/* Get what we need from real-mode BIOS services
	 * before switching to long mode */
	call   e820

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
	lgdt   physical_gdtdesc

	/*
	 * Identity map the first physical 2MBs using a single 2MB
	 * page. We should map more once the kernel get larger; kernel
	 * size shouldn't exceed one megabyte for now.
	 */

	/* Zero the entire init pagetables area */
	xorl   %eax, %eax
	movl   $init_pml4, %edi
	movl   $((init_pgt_end - init_pml4) / 4), %ecx
	rep    stosl

	/* pml4 entry 0: P = R/W = U/S = 1 */
	movl   $(init_pml3 + 0x007), init_pml4

	/* pml3 entry 0: P = R/W = U/S = 1 */
	movl   $(init_pml2 + 0x007), init_pml3

	/* pml2 entry 0: P = R/W = 1, U/S = 0, 2MB
	 * page flag: bit7 = 1, page base = 0x0 */
	movl   $0x083, init_pml2

	/* Final step: store PML4 base adderss */
	movl   $init_pml4, %eax
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
	ljmp   $KERNEL_CS, $startup_64

/*
 * Error handling for 32-bit code
 */

early_error:
	hlt
	jmp    early_error

/*
 * This x86 CPU has no 64-bit support; inform our
 * unlucky user and halt
 */
no_longmode:
	/* Blank screen */
	movl   $0xb8000, %edi		# VGA base
	movl   $0x0f200f20, %eax	# white color + ' '
	movl   $((80 * 25 * 2) / 4), %ecx
	rep    stosl

	/* Write our message */
	movl   $0xb80a0, %edi		# VGA base + 1 row
	movl   $no64_start, %esi
	movl   $(no64_end - no64_start), %ecx
	rep    movsb

	jmp    early_error

no64_start:
	/* Message string + VGA color; little-endian */
	.byte  'N', 0x0f, 'O', 0x0f, ' ', 0x0f, '6', 0x0f, \
	       '4', 0x0f, '-', 0x0f, 'b', 0x0f, 'i', 0x0f, \
	       't', 0x0f, ' ', 0x0f, 'C', 0x0f, 'P', 0x0f, \
	       'U', 0x0f, ' ', 0x0f, 's', 0x0f, 'u', 0x0f, \
	       'p', 0x0f, 'p', 0x0f, 'o', 0x0f, 'r', 0x0f, \
	       't', 0x0f, ' ', 0x0f, '-', 0x0f, ' ', 0x0f, \
	       'h', 0x0f, 'a', 0x0f, 'l', 0x0f, 't', 0x0f, \
	       'i', 0x0f, 'n', 0x0f, 'g', 0x0f, ' ', 0x0f
no64_end:

/*
 * Verify we're running on an amd64-compatible cpu
 */
.type check_cpu64, @function
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
	/* Reset the stack */
	movq   $stack_end, %rsp

	/*
	 * 64-bit virtual memory - temporal boot page tables:
	 *
	 *		Base kernel on virtual -2GB
	 *
	 * Full 64-bit addresses poses the challenge of exploding
	 * the kernel size, especially at function calls, far jumps,
	 * and global data references.
	 *
	 * The AMD64 architecture does not even allow ops to encode
	 * immediate 64-bit operands. So we have an 8-byte overhead,
	 * and the additional cost of extra ops.
	 *
	 * A solution would be using the pure 32-bit 0->4GB virtual
	 * range for the kernel, but we leave those for user space.
	 *
	 * Using -2GB as a base empowers the compiler to directly
	 * encode symbolic references as 32-bit values, and let the
	 * CPU implicitly sign-extended those to 64-bits due to the
	 * REX.W prefix. Thus, we use the top 64-bit address space,
	 * but with 32-bit size effeciency! Check below example:
	 *
	 * Passing global strings to memcpy:
	 * 48 c7 c6 80 24 8b 80    mov    $0xffffffff808b2480,%rsi
	 * 48 c7 c7 60 d8 10 80    mov    $0xffffffff8010d860,%rdi
	 * e8 f4 39 00 00          callq  ffffffff8010bb00 <memcpy>
	 *
	 * To avoid the size-overhead of 64-bit offsets in function
	 * calls, we limit the kernel text to a 32-bit virtual area.
	 * Thus, an x86-64 callq will only consume a 4-byte offset:
	 * relative space between next op and the destination label.
	 *
	 * To get kernel globals values (instead of just their add-
	 * resses), we'll use %rip-relative addressing within + or
	 * - 2GB limit, avoiding the size-overhead of referencing a
	 * full 8-byte address.
	 *
	 * All of this was in fact designed by the SysV AMD64 ABI
	 * developers, and is called the 'kernel code model'. We
	 * use this code  model, and let userspace use the 'small'
	 * or the 'medium' one (NT userspace defaults to medium).
	 *
	 * We already have the first physical 2MBs identity mapped;
	 * Map the 1-GByte  0xffffffff80000000 - 0xffffffffc0000000
	 * region to physical addresses 0x0 upwards.
	 */

	/* PML4 entry. A pml4 entry can map a 512-GByte space:
	 * we only need one entry for index 0b111111111 = 511,
	 * at the very end of the table. P = R/W = U/S = 1 */
	movq   $(init_pml3 + 0x007), (init_pml4 + 511 * 8)

	/* PML3 entry. A pml3 entry can map a 1-GByte space: we
	 * only need one entry for index 0b111111110 = 510, one
	 * entry before the last. P = R/W = U/S = 1 */
	movq   $(init_pml2 + 0x007), (init_pml3 + 510 * 8)

	/* PML2 entries. Each entry maps a 2-MByte region leading
	 * to 1-GByte in total. We map entry 0 to page 0, entry 1
	 * to page 1 (1:1 maps) till the end of the table.
	 * P = R/W = 1, U/S = 0, bit7 (2MB page flage) = 1 */
	movq   $(init_pml2), %rbx	# entries pointer
	xorq   %rcx, %rcx		# first page-num = 0x0
1:	movq   %rcx, %rax
	shl    $21, %rax		# page addr = page-num<<21
	addq   $0x083, %rax		# flags: P, R/W, 2MB
	movq   %rax, (%rbx)
	addq   $8, %rbx			# next PDE
	incq   %rcx			# next phys. page
	cmpq   $(init_pml2 + PAGE_SIZE), %rbx
	jne    1b

	/*
	 * Second map - entire physical memory
	 *
	 * We have the luxury of a very large address space, thus
	 * we map the entire physical memory below the -2GB mark.
	 * This simplifies kernel code, as all phys. addressses
	 * become directly accessible.
	 *
	 * Map the 1-GB 0xffff800000000000 - 0xffff800040000000
	 * region to physical 0x0 upwards.
	 *
	 * These are just temp tables; permanent tables will be
	 * built later, after inspecting our machine memory map.
	 */

	/* pml4 entry; index = 0b100000000 = 256 */
	movq   $(init_pml3 + 0x007), (init_pml4 + 256 * 8)

	/* pml3 entry; index = 0b000000000 = 0 */
	movq   $(init_pml2 + 0x007), init_pml3

	/* Done; apply the page tables */
	movq   $init_pml4, %rax
	movq   %rax, %cr3

	/* Run using virtual addresses from now on ..
	 * All addresses will be based on 0xffff800000000000
	 * except %rip and %rsp, where they'll be -2GB based */
	addq   $KTEXT_PAGE_OFFSET, %rsp
	lgdt   virtual_gdtdesc
	movq   $KTEXT_VIRTUAL(1f), %rax
	jmpq   *%rax
1:

	/* Voila! we're now virtual :) */
	call   zap_identity

	/* Note!! don't use %rip-relave ops like 'call' here.
	 * We're now using a virtual %rip, different from the
	 * physical one we're linked to; use absolute address */
	movq   $kernel_start, %rax
	jmpq   *%rax

/*
 * Avoid physical-addresses-dependent code and let NULL
 * pointers segfault: zero identity mappings entries
 */
.type zap_identity, @function
zap_identity:
	movq   $0, KTEXT_VIRTUAL(init_pml4)
	ret

.data

/*
 * 64-bit GDT. Elements ignored in long-mode are not
 * set: we won't support compatibility mode by design
 */
.align 16
gdt:
	.quad  0x0000000000000000

	/* long mode Code segment */
	.long  0x00000000		# base/limit; ignored in LM
	.byte  0x00, 0x98		# P=1, DPL=0, C/conforming=0
	.byte  0xa0, 0x00		# L=1, D=0, base[24-31] ignored

	/* FIXME: for some reason, `iretq' triggers a #GP with
	 * error=16, indicating a misbheving data segment, if DS's
	 * `Present' or `Writable' bits are not set. Set those bits
	 * till we know why iretq logic needs this useless segment */
	.long  0x00000000		# ignored
	.word  0x9200			# Writable=1, Present=1
	.word  0x0000			# ignored

gdt_end:

/*
 * The GDT descriptor to use in the case of no paging or
 * when using identity mappings.
 */
physical_gdtdesc:
	.word  gdt_end - gdt            # limit
	.long  gdt			# Base

/*
 * Interrupt handlers processor logic re-fetch the code segment
 * from the GDT instead of depending on the value in the
 * segment cache descriptors, thus the need of a GDT descriptor
 * with a virtual address of the GDT base.
 */
virtual_gdtdesc:
	.word  gdt_end - gdt            # limit
	.quad  VIRTUAL(gdt)		# Base

.bss

/*
 * Booting stack
 */
.align 8
	.equ   STACK_SIZE, 0x4000
stack:
	.skip  STACK_SIZE
stack_end:

/*
 * Boot Page tables
 */
	.equ   PAGE_SIZE, 0x1000

.balign PAGE_SIZE
init_pml4:
	.skip  PAGE_SIZE

init_pml3:
	.skip  PAGE_SIZE

/* Page Directory of one-to-one mappings
 * between entry number and physical pages */
.globl init_pml2
init_pml2:
	.skip  PAGE_SIZE

init_pgt_end:
