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

/* Our 'runque': check-out anytime, but never leave */
#define PROC_ARRAY_LEN	150
static int proc_total = 1;
static struct proc *proc_arr[PROC_ARRAY_LEN];

/*
 * Return next process to schedule
 * FIXME: Avoid interrupting threads holding locks
 */
static int cur_proc;
struct proc *schedule(void)
{
	cur_proc = (cur_proc + 1) % proc_total;

	return proc_arr[cur_proc];
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

	/* A placeholder, for now */
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
	assert(proc_total != PROC_ARRAY_LEN);
	proc_arr[proc_total++] = proc;
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
	proc_arr[0] = current;

	/*
	 * Setup the timer ticks handler
	 *
	 * It's likely that the PIT will trigger before we enable
	 * interrupts, but even if this was the case, the vector
	 * will get 'latched' in the bootstrap local APIC IRR
	 * register and get serviced once interrupts are enabled.
	 */
	vector = TICKS_IRQ_VECTOR;
	set_intr_gate(vector, timer_handler);
	ioapic_setup_isairq(0, vector);

	/*
	 * We can program the PIT as one-shot and re-arm it in the
	 * handler, or let it trigger IRQs monotonically. The arm
	 * method sounds a bit risky: if a single edge trigger got
	 * lost, the entire kernel will halt.
	 */
	pit_monotonic(10);
}

#if	SCHED_TESTS

#include <vga.h>

static void __no_return loop_print(char ch, int color)
{
	while (true) {
		putc_colored(ch, color);
		for (int i = 0; i < 0xffff; i++)
			cpu_pause();
	}
}

static void __no_return test1(void) { loop_print('A', VGA_BLUE); }
static void __no_return test2(void) { loop_print('B', VGA_GREEN); }
static void __no_return test3(void) { loop_print('C', VGA_CYAN); }
static void __no_return test4(void) { loop_print('D', VGA_RED); }
static void __no_return test5(void) { loop_print('E', VGA_MAGNETA); }
static void __no_return test6(void) { loop_print('F', VGA_GRAY); }
static void __no_return test7(void) { loop_print('G', VGA_YELLOW); }

void __no_return sched_run_tests(void)
{
	for (int i = 0; i < 15; i++) {
		kthread_create(test1);
		kthread_create(test2);
		kthread_create(test3);
		kthread_create(test4);
		kthread_create(test5);
		kthread_create(test6);
		kthread_create(test7);
	}

	loop_print('H', VGA_WHITE);
}

#endif /* SCHED_TESTS */
