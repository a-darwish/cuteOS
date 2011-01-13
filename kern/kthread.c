/*
 * Kernel threads
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <proc.h>
#include <sched.h>
#include <x86.h>
#include <segment.h>
#include <paging.h>
#include <kmalloc.h>

/*
 * Create a new kernel thread running given function
 * code, and attach it to the runqueue.
 *
 * NOTE! given function must never exit!
 */
void kthread_create(void (*func)(void))
{
	struct proc *proc;
	struct irq_ctx *irq_ctx;
	char *stack;

	proc = kmalloc(sizeof(*proc));
	proc_init(proc);

	/* New thread stack, moving down */
	stack = kmalloc(STACK_SIZE);
	stack = stack + STACK_SIZE;

	/* Reserve space for our IRQ stack protocol */
	irq_ctx = (struct irq_ctx *)(stack - sizeof(*irq_ctx));
	irq_ctx_init(irq_ctx);

	/*
	 * Values for the code to-be-executed once scheduled.
	 * They will get popped and used automatically by the
	 * processor at ticks handler `iretq'.
	 *
	 * Set to-be-executed code's %rsp to the top of the
	 * newly allocated stack since this new code doesn't
	 * care about the values currently 'pushed'; only
	 * the ctontext switching code does.
	 */
	irq_ctx->cs = KERNEL_CS;
	irq_ctx->rip = (uintptr_t)func;
	irq_ctx->ss = 0;
	irq_ctx->rsp = (uintptr_t)stack;
	irq_ctx->rflags = default_rflags().raw;

	/* For context switching code, which runs at the
	 * ticks handler context, give a stack that respects
	 * our IRQ stack protocol */
	proc->pcb.rsp = (uintptr_t)irq_ctx;

	/* Push the now completed proc to the runqueu */
	sched_enqueue(proc);
}
