/*
 * 64-bit startup code
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <segment.h>
#include <paging.h>
#include <apic.h>
#include <ioapic.h>

.text
.code32

/*
 * 32-bit kernel entry point. We run with the only pre-condition
 * of being loaded within a flat-memory model.
 */
.globl startup_32
startup_32:
	/* Relocate kernel code away from our bss section.
	 * See the linker script for more details */
	.equ   KERNEL_SIZE, 0x80000
	movl   $__headbss_start, %esi
	movl   $__phys_start, %edi
	movl   $(KERNEL_SIZE / 4), %ecx
	rep    movsl

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
	/* Reset the stack */
	movq   $stack_end, %rsp

	/*
	 * We already have the first physical 2MBs identity mapped;
	 * setup page tables for running the kernel beginning from
	 * higher half virtual address 0xffff80100000.
	 *
	 * Map the 768M 0xffff80000000-0xffffb0000000 region to
	 * to physical addresses 0x0000-0x30000000 using 2MB pages.
	 */

	/* pml4 entry. We only need one entry for 0b111111111, which
	 * will be typically located at the very end of the table.
	 * P = R/W = U/S = 1 as usual. */
	movq   $(init_pml3 + 0x007), (init_pml4 + 511 * 8)

	/* pml3 entry. We also only need a single entry for 0b111111110
	 * which is one entry before the end of PML3. P = R/W = U/S = 1 */
	movq   $(init_pml2 + 0x007), (init_pml3 + 510 * 8)

	/* OK, now we setup the new PD table that is going to do all
	 * the work. We map entry 0 to page 0, entry 1 to page 1 and
	 * so on till the end of the table. P = R/W = 1, U/S = 0,
	 * bit7 (2MB page flage) = 1 */
	movq   $(init_pml2), %rbx
	xorq   %rcx, %rcx		# first page num = 0x0
1:	movq   %rcx, %rax
	shl    $21, %rax		# page address = page-num<<21
	addq   $0x083, %rax		# flags: P, R/W, 2M
	movq   %rax, (%rbx)
	addq   $8, %rbx			# next PDE
	incq   %rcx			# next page
	cmpq   $(init_pml2 + PAGE_SIZE), %rbx
	jne    1b

	/* Done; apply the page tables */
	movq   $init_pml4, %rax
	movq   %rax, %cr3

	/* Run using high half virtual addresses from now on */
	leaq   VIRTUAL_START(%rsp), %rsp
	lgdt   virtual_gdtdesc
	movq   $VIRTUAL(1f), %rax
	jmpq   *%rax
1:

	/* Viola! */
	call   map_apic
	call   zap_identity
	movq   $kernel_start, %rax
	jmpq   *%rax

/*
 * Map local and i/o apic registers i/o mapped at *_phbase
 * to the top 4MB of the address space. Add a 0x1FF entry
 * to the init PML3 table pointing to the special APIC Page
 * Directory. Apic space must be strong uncacheable (UC).
 */
.type map_apic, @function
map_apic:
	movq   $(apic_pml2 + 0x007), (init_pml3 + 511 * 8)
	movl   $(APIC_PHBASE + 0x09b), (apic_pml2 + 511 * 8)
	movl   $(IOAPIC_PHBASE + 0x09b), (apic_pml2 + 510 * 8)
	ret

/*
 * Use virtual addresses from now on; low memory identity
 * mappings can be zapped at any time.
 */

/*
 * Avoid physical addresses dependent code and let NULL
 * pointers segfault: zero the identity-mapping page table
 * entries. Thanks to 2MB pages, only three of 'em exist.
 */
.type zap_identity, @function
zap_identity:
	movq   $0, VIRTUAL(init_pml4)
	movq   $0, (VIRTUAL(init_pml3))
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

/*
 * APIC mappings Page Directory (PML2)
 */
.balign PAGE_SIZE
apic_pml2:
	.skip  PAGE_SIZE
