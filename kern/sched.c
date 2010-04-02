/*
 * Scheduler barebones
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The context-switching foundation is already set-up, but we only
 * schedule 2 processes so far.
 */

#include <kernel.h>
#include <idt.h>
#include <apic.h>
#include <ioapic.h>
#include <pit.h>
#include <vectors.h>
#include <proc.h>
#include <segment.h>
#include <x86.h>
#include <kmalloc.h>
#include <string.h>
#include <sched.h>
#include <tests.h>

/*
 * At any point in time, this refers to the
 * current process descriptor
 */
struct proc *current;

/* Our 'runque' :) */
static struct proc *next;

/*
 * Return next process to schedule
 * FIXME: Avoid interrupting threads holding locks
 */
struct proc *schedule(void)
{
	struct proc *ret;

	ret = current;
	if (next != NULL) {
		ret = next;
		next = current;
	}

	return ret;
}

/*
 * Create a new kernel thread running given
 * function code, and attach it to the runqueue.
 *
 * NOTE! given function must never exit!
 */
#define STACK_SIZE PAGE_SIZE
void kthread_create(void (*func)(void))
{
	struct proc *proc;
	struct irq_ctx *irq_ctx;
	char *stack;

	proc = kmalloc(sizeof(*proc));
	proc_init(proc);

	proc->pid = 1;

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

	/* Push the now completed proc to the 'runqueu' :) */
	assert(next == NULL);
	next = proc;
}

void timer_handler(void);

void sched_init(void)
{
	uint8_t vector;

	pcb_validate_offsets();

	/* Let our code path be a schedulable entity */
	current = kmalloc(sizeof(*current));
	proc_init(current);
	current->pid = 0;

	/*
	 * Setup the timer ticks handler
	 *
	 * It's likely that the PIT will trigger before we enable
	 * interrupts, but even if this was the case, the vector
	 * will get 'latched' in the bootstrap local APIC IRR
	 * register and get serviced once interrupts are enabled.
	 */
	vector = PIT_IRQ_VECTOR;
	set_intr_gate(vector, timer_handler);
	ioapic_setup_isairq(0, vector);

	/*
	 * We can program the PIT as one-shot and re-arm it in the
	 * handler, or let it trigger IRQs monotonically. The arm
	 * method sounds a bit risky: if a single edge trigger got
	 * lost, the entire kernel will halt.
	 */
	pit_monotonic(52);
}

#if	SCHED_TESTS

#include <vga.h>

static void __no_return test_thread(void)
{
	while (true) {
		putc_colored('B', VGA_LIGHT_GREEN);
	}
}

void __no_return sched_run_tests(void)
{
	kthread_create(test_thread);

	while (true) {
		putc_colored('A', VGA_LIGHT_RED);
	}
}

#endif /* SCHED_TESTS */
