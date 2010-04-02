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
 *		%ss		0x68(%rsp)
 *		%rsp		0x60(%rsp)
 *		%rflags		0x58(%rsp)
 *		%cs		0x50(%rsp)
 *		%rip		0x48(%rsp)
 *		   -- Pushed by CPU --
 *		%rax		0x40(%rsp)
 *		%rcx		0x38(%rsp)
 *		%rdx		0x30(%rsp)
 *		%rdi		0x28(%rsp)
 *		%rsi		0x20(%rsp)
 *		%r8		0x18(%rsp)
 *		%r9		0x10(%rsp)
 *		%r10		0x08(%rsp)
 *		%r11		0x00(%rsp)
 *		   -- Pushed by US --
 */

#include <idt.h>
#include <paging.h>
#include <apic.h>
#include <proc.h>

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

	/* Pop the error code, if any */
	movl   %esi, %ecx
	movl   $1, %eax
	shl    %cl, %eax
	andl   $0x27d00, %eax		# error code pushed?
	jz     0f
	popq   %rdx			# error code
0:
	/* Emulate our IRQ stack protocol */
	subq   $(9 * 8), %rsp

	/* Because we pass below values to printk() using
	 * the stack, lets save them to regs first not to
	 * mess up IRQ stack protocol offset calculations */
	movq   IRQCTX_RFLAGS(%rsp), %r15
	movq   IRQCTX_RSP(%rsp), %r14

	/* printk(): %rsi, %rdx are already saved above */
	movq   $idt_msg, %rdi
	movq   IRQCTX_CS(%rsp), %rcx
	movq   IRQCTX_RIP(%rsp), %r8
	movq   IRQCTX_SS(%rsp), %r9
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
 * Clock ticks handler - Context Switching:
 *
 * We have 3 classes of registers, each one handled differently:
 *
 * - ABI mandated scratch registers:
 *   %rax, %rdx, %rcx, %rdi, %rsi, %r8, %r9, %r10, %r11
 *   Those regs are already saved upon any IRQ handler entry, so
 *   the called C functions don't clobber them and corrupt state.
 *   We exploit this by not saving them again at the ctxt switch,
 *   they will get updated (popped up from the new process stack)
 *   before returning to interrupted code.
 *
 * - ABI mandated non-scratch registers (PCB):
 *   %rbp, %rbx, %r12, %r13, %r14, %r15, %rsp (special case)
 *   If used, C functions will save those first on their stack.
 *   No one _permanently_ saves  those for us, so we  explicitly
 *   save and restore 'em to current and from new proc PCB block.
 *
 * - Registers implicitly saved by the CPU:
 *   %ss, %rsp, %rflags, %cs, %rip
 *   Before  invoking IRQ handlers, the CPU  pushes those for us,
 *   which represents the values of the _interrupted_ code path.
 *   Those are the most important regs, since this is where the
 *   CPU pops the IP and stack of the once-interrupted, possibly
 *   now newly-scheduled, code path again. [1]
 *
 * What about those 2 redundant (%rsp)s?
 *
 *   Those are NOT redundant! The rsp stored at the PCB represents
 *   the stack where the scratch and CPU-pushed registers were
 *   previously saved. It's basically the stack pointer saved
 *   directly at `1:'; we restore it at the same point, not to pop
 *   the wrong elements. [1]
 *
 *   The %rsp at the 'IRQ context' represents the stack value of
 *   the _interrupted_ code path. This is the value the CPU will
 *   use for the new code running after the handler finishes.
 *
 *   This second stack holds no significant values for us, context
 *   switching code, but holds the values pushed by the to-bo-res-
 *   umed code path (which is possibly a new schedulable entity).
 *
 * [1] The stack may have been aritificially created at kthread
 * creation time.
 *
 * NOTE! Only clobber scratch registers here.
 */
.globl timer_handler
timer_handler:
	PUSH_REGS

	/* Pick the new process to run */
	call   schedule
	movq   %rax, %rdi

	movq   current, %rax

	/* Save scratch regs to current->pcb */
	addq   $PD_PCB, %rax
	movq   %rbp, PCB_RBP(%rax)
	movq   %rbx, PCB_RBX(%rax)
	movq   %r12, PCB_R12(%rax)
	movq   %r13, PCB_R13(%rax)
	movq   %r14, PCB_R14(%rax)
	movq   %r15, PCB_R15(%rax)
1:	movq   %rsp, PCB_RSP(%rax)

	/* current = next */
	movq   %rdi, current

	/* Restore scratch from the new PCB */
	addq   $PD_PCB, %rdi
	movq   PCB_RBP(%rdi), %rbp
	movq   PCB_RBX(%rdi), %rbx
	movq   PCB_R12(%rdi), %r12
	movq   PCB_R13(%rdi), %r13
	movq   PCB_R14(%rdi), %r14
	movq   PCB_R15(%rdi), %r15
	movq   PCB_RSP(%rdi), %rsp

	/*
	 * Voila! we're the new process!
	 *
	 * - We're now running with the new stack
	 * - Non-scratch regs have ve been updated from the PCB.
	 * - Scratch regs won't be updated till the end of the
	 *   handler (values will be popped from the new stack)
	 * - %rsp, %rip, and %rflags will be updated by `iret';
	 *   CPU will implicitly pop those from the new stack.
	 */

	jmp    irq_end

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
