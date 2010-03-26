/*
 * 64-bit IDT table and interrupt handlers
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Stack contents upon entrance of an exception or IRQ handler, assuming
 * any generated exception error-code popped up:
 *
 *		%ss		0x20(%rsp)
 *		%rsp		0x18(%rsp)
 *		%rflags		0x10(%rsp)
 *		%cs		0x8 (%rsp)
 *		%rip		0x0 (%rsp)
 *		  -- (Stack top) --
 */

#include <idt.h>
#include <paging.h>
#include <apic.h>

.code64
.text

/*
 * Exception handlers
 */

/*
 * Those "stub" handlers catch the vector number using their
 * position in the entries table and then jump to the real
 * fallback idt handler.
 *
 * NOTE! don't change this code without changing the stub
 * code size macro at idt.h
 */
.globl idt_exception_stubs
idt_exception_stubs:
        i = 0
	.rept  EXCEPTION_GATES
	movq   $i, %rsi
	jmp    default_exception_handler
	i = i + 1
	.endr

/*
 * The default/fallback interrupt handler; print a verbose
 * error message and halt
 *
 * Exception vectors that return error code are:
 *
 * 8  - 1 << 8  = 0x100
 * 10 - 1 << 10 = 0x400
 * 11 - 1 << 11 = 0x800
 * ---
 * 12 - 1 << 12 = 0x1000
 * 13 - 1 << 13 = 0x2000
 * 14 - 1 << 14 = 0x4000
 * ---
 * 17 - 1 << 17 = 0x20000
 *
 * which leads to a mask of 0x27d00 for such vector numbers
 */
default_exception_handler:
	movq   $0xdead, %rdx		# No error-code mark
	movl   %esi, %ecx
	movl   $1, %eax
	shl    %cl, %eax
	andl   $0x27d00, %eax		# returns error code?
	jz     0f
	popq   %rdx			# error code
0:
	/* Save those printk() stack params to regs first
	 * not to mess up stack offset calculations */
	movq   0x10(%rsp), %r15		# %rflags
	movq   0x18(%rsp), %r14		# %rsp

	/* printk(): %rsi, %rdx are already saved above */
	movq   $idt_msg, %rdi
	movq   0x8 (%rsp), %rcx		# %cs
	movq   0x0 (%rsp), %r8		# %rip
	movq   0x20(%rsp), %r9		# %ss
	movq   %cr4, %rax
	push   %rax			# %cr4
	movq   %cr3, %rax
	push   %rax			# %cr3
	movq   %cr2, %rax
	push   %rax			# %cr2
	push   %r15			# %rflags
	push   %r14			# %rsp
	cld
	call   printk
1:	hlt
	jmp    1b

idt_msg:
	.ascii "Exception: vector=%d, errcode=0x%lx, %%cs=0x%lx, "
	.ascii "%%rip=0x%lx, %%ss=0x%lx, %%rsp=0x%lx, %%rflags=0x%lx, "
	.ascii "%%cr2=0x%lx, %%cr3=0x%lx, %%cr4=0x%lx \n"
	.byte  0

/*
 * System IRQ handlers
 */

/*
 * Avoid #GPs: setup an 'ignore' handler for any spurious
 * PIC-triggered IRQ (despite PICs being already masked).
 */
.globl PIC_handler
PIC_handler:
	iretq

/*
 * AMD64 ABI indicates that only %rbp, %rbx, and %r12
 * through %r15 need to be perserved by the callee.
 *
 * If any of the above regs get modified by C functions
 * called, they will be saved in the callee stack. Lets
 * save the rest of the regs which won't be saved.
 *
 * The ABI also mandates a cleared direction flag upon C
 * functions entry; clear the flag. No need to save CPU
 * %rflags as the CPU core automatically does so for us.
 */
#define PUSH_REGS		\
	pushq  %rax;		\
	pushq  %rcx;		\
	pushq  %rdx;		\
	pushq  %rdi;		\
	pushq  %rsi;		\
	pushq  %r8 ;		\
	pushq  %r9 ;		\
	pushq  %r10;		\
	pushq  %r11;		\
	cld

#define RESTORE_REGS		\
	popq   %r11;		\
	popq   %r10;		\
	popq   %r9 ;		\
	popq   %r8 ;		\
	popq   %rsi;		\
	popq   %rdi;		\
	popq   %rdx;		\
	popq   %rcx;		\
	popq   %rax

/*
 * Where every IRQ handler ends ..
 *
 * Send EOI to local APIC; 'Fixed' interrupts must notify
 * their completion to the local APIC before returning
 * from the handler, so the local APIC can dispatch other
 * IRQs pending at its IRR register.
 *
 * NOTE-1! APIC MMIO must have been explicitly mapped
 * NOTE-2! The EOI memory-mapped register is only 32-bit
 * NOTE-3! Transform this to C once we have an IRQ model
 * NOTE-4! Don't clobber any reg not saved at PUSH_REGS
 */
irq_end:
	movq   $(VIRTUAL(APIC_PHBASE) + APIC_EOI), %rax
	movl   $0,(%rax)
	RESTORE_REGS
	iretq

/*
 * PS/2 keyboard interrupt handler stub.
 */
.globl kb_handler
kb_handler:
	PUSH_REGS
	call   __kb_handler
	jmp    irq_end

.data

/*
 * 64-bit IDT descriptor
 *
 * IDT base is linked to -2GB, but we want to have our
 * base in the VIRTUAL() mapping form. Thus, we find
 * the IDT base physical address, and VIRTUAL()ize it.
 */
.globl idtdesc
idtdesc:
	.word  idt_end - idt		# limit
	.quad  VIRTUAL(KTEXT_PHYS(idt))	# base

.bss

/*
 * IDT Table
 *
 * Size = 0x100 entries * 0x10 size = 0x1000 bytes
 *
 * The IDT should be aligned on an 16-byte boundary
 * to maximize performance of cache line fills.
 */
.align 16
.globl idt
idt:
	.skip  IDT_GATES * 16
idt_end:
