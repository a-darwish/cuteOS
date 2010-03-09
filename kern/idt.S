/*
 * 64-bit IDT table and handlers
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <idt.h>
#include <paging.h>

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
 * 8  - 1<<8  = 0x100
 * 10 - 1<<10 = 0x400
 * 11 - 1<<11 = 0x800
 * ---
 * 12 - 1<<12 = 0x1000
 * 13 - 1<<13 = 0x2000
 * 14 - 1<<14 = 0x4000
 * ---
 * 17 - 1<<17 = 0x20000
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
0:	movq   24(%rsp), %r8		# %rsp
	movq   16(%rsp), %r9		# %rflags
	movq   0(%rsp), %rcx            # %rip
	movq   $idt_msg, %rdi
	movq   %cr4, %rax
	push   %rax			# %cr4
	movq   %cr3, %rax
	push   %rax			# %cr3
	movq   %cr2, %rax
	push   %rax			# %cr2
	call   printk
	addq   $0x18, %rsp
1:	hlt
	jmp    1b

idt_msg:
	.ascii "Exception: vector=%d, errcode=0x%lx, %%rip=0x%lx, "
	.ascii "%%rsp=0x%lx, %%rflags=0x%lx, %%cr2=0x%lx, %%cr3=0x%lx, "
	.ascii "%%cr4=0x%lx \n"
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
 * PS/2 keyboard interrupt handler stub.
 *
 * AMD64 ABI indicates that only %rbp, %rbx and %r12 through
 * %r15 needs to be perserved by the callee. FIXME: to be
 * transformed to a generic IRQ handling stub once needed.
 */
.globl kb_handler
kb_handler:
	pushq  %rax
	pushq  %rcx
	pushq  %rdx
	pushq  %rdi
	pushq  %rsi
	pushq  %r8
	pushq  %r9
	pushq  %r10
	pushq  %r11
	call   __kb_handler
	popq   %r11
	popq   %r10
	popq   %r9
	popq   %r8
	popq   %rsi
	popq   %rdi
	popq   %rdx
	popq   %rcx
	popq   %rax
	iretq

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
 */
.align 16
.globl idt
idt:
	.skip  IDT_GATES * 16
idt_end:
