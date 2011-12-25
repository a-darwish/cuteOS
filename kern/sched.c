/*
 * Uniprocessor scheduling
 *
 * Copyright (C) 2010-2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * For a detailed analysis of general-purpose scheduling, along with a
 * nice historical context, check the research collected in the Cute
 * 'references' tree.
 *
 * This is a multi-level feedback queue with strict fairness; its core
 * ideas are taken from CTSS [classical MLFQ dynamics], Linux O(1) [the
 * two runqueues design], FreeBSD ULE, and Staircase Deadline [fairness]
 * scheduling.
 *
 * I've tried a Solaris/SVR4-style MLFQ earlier, where it just boosted
 * threads priorities starving for a second or more. These schedulers
 * parameter table was scarily heuristic and full of voo-doo constants.
 * Optimizing these constants for certain tests kept introducing corner
 * cases for others; it was like a never-ending game of cat & mouse.
 *
 * Such voo-doos would've been partially acceptable if the designer had
 * the luxury of different machines and workloads to test against,
 * something I OBVIOUSLY lack. So in a search for more theoretically-
 * sound algorithms, I sticked with a strict-fairness design.
 *
 * FIXME: Get highest queue priority with runnable threads in O(1).
 */

#include <kernel.h>
#include <percpu.h>
#include <list.h>
#include <serial.h>
#include <idt.h>
#include <apic.h>
#include <ioapic.h>
#include <pit.h>
#include <vectors.h>
#include <proc.h>
#include <kmalloc.h>
#include <sched.h>
#include <conf_sched.h>
#include <tests.h>

/*
 * Initialize all scheduler globals (runqueues + book-keeping).
 * Every core has its own unique version of such memory.
 *
 * NOTE! Disable interrupts for any code accessing such globals
 * outside of the timer IRQ context.  Check include/percpu.h
 */
void sched_percpu_area_init(void)
{

/* CPU ticks counter; incremented for every tick */
	PS->sys_ticks = 0;

/*
 * A multi-level feedback queue with strict fairness:
 *
 * A classical problem with MLFQs is starvation: a number of threads
 * can influx the high priority queues, starving the low-priority ones
 * for a considerable time (if not infinitely).
 *
 * To handle this, we add fairness to the design. If a thread finishes
 * its entire slice, it's put into the 'expired' queue  with decreased
 * priority.
 *
 * No thread in the expired queue can run till all other tasks in the
 * 'active' queue had a chance to run. Once the active queue get
 * emptied, we swap it with the expired one, and rerun the algorithm.
 * Thus, we have a starvation upper bound = N * RR_INTERVAL, where N =
 * # of runnable threads.
 *
 * If a task slept during its interval, it's popped from the runqueue.
 * After wakeup, it re-runs with same old priority up to end of the
 * _remaining_ part of its slice. [*]
 *
 * If a runqueue swap occurred during thread sleep, its slice usage is
 * reset after wake-up and it's also put in a priority higher than the
 * new active queue's default one. [+]
 *
 * Above design rationale is to maintain low-latency for interactive
 * threads while providing strict fair treatment, runtime-wise, to all
 * jobs in the system.
 *
 * [*] After wakeup, such task will immediately preempt the currently
 * running thread, or wait one RR_INTERVAL at max.
 *
 * [+] If we put such thread in the new rq with its original old prio-
 * rity, it'd be punished heavily (latency-wise) for its sleep. This
 * is especially true for low-priority tasks where the chance of a rq
 * swap during their sleep is high.
 */
	PS->rq_active = &PS->rrq[0];
	PS->rq_expired = &PS->rrq[1];
	rq_init(PS->rq_active);
	rq_init(PS->rq_expired);

/*
 * If we just allowed new runnable threads to get added to the active
 * rq, we can starve its lower-prio threads. If we added them to the
 * expired rq instead, we will fsck up their response time up to
 * N*RR_INTERVAL ms, where N = # of runnable threads in the active rq.
 *
 * As a middle solution to this, we add them to a special list, and
 * alternate dispatching between this list and the active runqueue.
 *
 * Once the active rq get emptied and swapped, we move all the tasks
 * of that list to the new active runqueue at the head of its default
 * priority list.
 *
 * Thus, the new scheduler starvation upper bound = 2*N*RR_INTERVAL
 * ms, where N = number of runnable threads in the active rq.
 */
	list_init(&PS->just_queued);
	PS->just_queued_turn = 1;
}

/*
 * Statically allocate the booting-thread descriptor: ‘current’
 * must be available in all contexts, including early boot.
 */
struct proc swapper;

/*
 * @ENQ_NORMAL: Enqueue given thread as if it's a newly created
 * thread in the system (clear its state).
 *
 * @ENQ_RETURN: Return a thread to its original queue priority.
 * Don't mess with ANY of its CPU usage counters state. This is
 * for threads preempted by higher-priroty tasks.
 */
enum enqueue_type {
	ENQ_NORMAL,
	ENQ_RETURN,
};

static void __rq_add_proc(struct runqueue *rq, struct proc *proc, int prio,
			    enum enqueue_type type)
{
	assert(VALID_PRIO(prio));

	proc->enter_runqueue_ts = PS->sys_ticks;
	proc->state = TD_RUNNABLE;

	switch(type) {
	case ENQ_NORMAL:
		proc->runtime = 0;
		list_add_tail(&rq->head[prio], &proc->pnode);
		break;
	case ENQ_RETURN:
		list_add(&rq->head[prio], &proc->pnode);
		break;
	default:
		assert(false);
	}
}

static void rq_add_proc(struct runqueue *rq, struct proc *proc, int prio)
{
	__rq_add_proc(rq, proc, prio, ENQ_NORMAL);
}

static void rq_return_proc(struct runqueue *rq, struct proc *proc, int prio)
{
	__rq_add_proc(rq, proc, prio, ENQ_RETURN);
}


/*
 * @@@ Scheduling proper: @@@
 */

void sched_enqueue(struct proc *proc)
{
	union x86_rflags flags;

	flags = local_irq_disable_save();

	proc->enter_runqueue_ts = PS->sys_ticks;
	proc->state = TD_RUNNABLE;
	proc->runtime = 0;

	list_add_tail(&PS->just_queued, &proc->pnode);

	local_irq_restore(flags);
	sched_dbg("@@ T%d added\n", proc->pid);
}

/*
 * Dispatch the most suitable thread from the runqueues.
 * Alternate dispatching between the active runqueue and
 * the just_queued list.
 *
 * Return NULL if all relevant queues are empty.
 */
static struct proc *dispatch_runnable_proc(int *ret_prio)
{
	struct proc *proc, *spare;
	int h_prio;

	if (PS->just_queued_turn && !list_empty(&PS->just_queued)) {
		PS->just_queued_turn = 0;

		proc = list_entry(PS->just_queued.next, struct proc, pnode);
		list_del(&proc->pnode);
		*ret_prio = DEFAULT_PRIO;
		return proc;
	}

	if (rq_empty(PS->rq_active)) {
		rq_dump(PS->rq_expired);
		swap(PS->rq_active, PS->rq_expired);

		/* FIXME: this can be done in O(1) */
		list_for_each_safe(&PS->just_queued, proc, spare, pnode) {
			list_del(&proc->pnode);
			rq_add_proc(PS->rq_active, proc, DEFAULT_PRIO);
		}
		rq_dump(PS->rq_active);
	}

	/* The active rq is still empty even after swap and
	 * popping the just_queued threads? System is idle! */
	if (rq_empty(PS->rq_active)) {
		*ret_prio = UNDEF_PRIO;
		return NULL;
	}

	/* It's now guaranteed: a thread from the runqueues
	 * will get scheduled; try 'just_queued' next time. */
	PS->just_queued_turn = 1;

	h_prio = rq_get_highest_prio(PS->rq_active);
	assert(VALID_PRIO(h_prio));
	assert(!list_empty(&PS->rq_active->head[h_prio]));

	proc = list_entry(PS->rq_active->head[h_prio].next, struct proc,  pnode);
	list_del(&proc->pnode);
	*ret_prio = h_prio;
	return proc;
}

/*
 * Preempt current thread using given new one.
 * New thread should NOT be in ANY runqueue.
 */
static struct proc *preempt(struct proc *new_proc, int new_prio)
{
	assert(new_proc != current);
	assert(list_empty(&new_proc->pnode));
	assert(new_proc->state == TD_RUNNABLE);

	assert(VALID_PRIO(new_prio));
	PS->current_prio = new_prio;

	new_proc->state = TD_ONCPU;
	new_proc->stats.dispatch_count++;
	new_proc->stats.rqwait_overall += PS->sys_ticks -
		new_proc->enter_runqueue_ts;

	sched_dbg("dispatching T%d\n", new_proc->pid);
	return new_proc;
}

/*
 * Our scheduler, it gets invoked HZ times per second.
 */
struct proc *sched_tick(void)
{
	struct proc *new_proc;
	int new_prio;

	PS->sys_ticks++;
	current->runtime++;

	assert(current->state == TD_ONCPU);
	assert(VALID_PRIO(PS->current_prio));

	current->stats.runtime_overall++;
	current->stats.prio_map[PS->current_prio]++;

	if (PS->sys_ticks % SCHED_STATS_RATE == 0)
		print_sched_stats();

	/*
	 * Only switch queues after finishing the slice, not to introduce
	 * fairness regression for last task standing in the active queue.
	 */
	if (current->runtime >= RR_INTERVAL) {
		current->stats.preempt_slice_end++;

		new_proc = dispatch_runnable_proc(&new_prio);
		if (new_proc == NULL)
			return current;

		PS->current_prio = max(MIN_PRIO, PS->current_prio - 1);
		rq_add_proc(PS->rq_expired, current, PS->current_prio);
		return preempt(new_proc, new_prio);
	}

	/*
	 * If a higher priority task appeared, it must be an old sleeping
	 * thread that has just woken up; dispatch it.
	 * FIXME: what about the just_queued threads response time?
	 */
	new_prio = rq_get_highest_prio(PS->rq_active);
	if (new_prio > PS->current_prio) {
		current->stats.preempt_high_prio++;
		panic("Sleep support in not yet in the kernel; how "
		      "did we reach here?");

		new_proc = list_entry(PS->rq_active->head[new_prio].next,
				      struct proc, pnode);
		list_del(&new_proc->pnode);

		rq_return_proc(PS->rq_active, current, PS->current_prio);
		return preempt(new_proc, new_prio);
	}

	/*
	 * No higher priority tasks in the horizon, and our slice usage
	 * is not yet complete. Peace of cake, continue running.
	 */
	return current;
}

/*
 * Let current CPU-init code path be a schedulable entity.
 *
 * Once the scheduler's timer starts ticking, every code path
 * should be in the context of a Schedulable Entity so it can,
 * once interrupted, get later executed. All cores (bootstrap
 * and secondary) initialization path should call this method.
 *
 * A unique 'current' descriptor, representing the initializa-
 * tion path, should be already allocated.
 */
void schedulify_this_code_path(enum cpu_type t)
{
	/* 'Current' is a per-CPU structure */
	percpu_area_init(t);

	/*
	 * We tell GCC to cache 'current' as much as possible since
	 * it does not change for the lifetime of a thread, even if
	 * that thread moved to another CPU.
	 *
	 * Thus GCC usually dereferences %gs:0 and cache the result
	 * ('current' address) in a general-purpose register before
	 * executing _any_ of the original function code.
	 *
	 * But in this case, getting 'current' address before
	 * initializing the per-CPU area will just return a garbage
	 * value (invalid/un-initialized %gs); thus the barrier.
	 */
	barrier();

	proc_init(current);
	current->state = TD_ONCPU;
	PS->current_prio = DEFAULT_PRIO;
}

void sched_init(void)
{
	extern void ticks_handler(void);
	uint8_t vector;

	pcb_validate_offsets();

	/*
	 * Setup the timer ticks handler
	 *
	 * It's likely that the PIT will trigger before we enable
	 * interrupts, but even if this was the case, the vector
	 * will get 'latched' in the bootstrap local APIC IRR
	 * register and get serviced once interrupts are enabled.
	 */
	vector = TICKS_IRQ_VECTOR;
	set_intr_gate(vector, ticks_handler);
	ioapic_setup_isairq(0, vector, IRQ_BROADCAST);

	/*
	 * We can program the PIT as one-shot and re-arm it in the
	 * handler, or let it trigger IRQs monotonically. The arm
	 * method sounds a bit risky: if a single edge trigger got
	 * lost, the entire kernel will halt.
	 */
	pit_monotonic(1000 / HZ);
}


/*
 * @@@ Statistics: @@@
 */

#if SCHED_STATS

#include <spinlock.h>
spinlock_t printstats_lock = SPIN_UNLOCKED();

static void print_proc_stats(struct proc *proc, int prio)
{
	uint dispatch_count;
	clock_t rqwait_overall;

	dispatch_count = proc->stats.dispatch_count;
	dispatch_count = max(1u, dispatch_count);

	rqwait_overall = proc->stats.rqwait_overall;
	if (proc != current)
		rqwait_overall += PS->sys_ticks - proc->enter_runqueue_ts;

	prints("%lu:%d:%lu:%lu:%lu:%lu:%u:%u ", proc->pid, prio,
	       proc->stats.runtime_overall,
	       proc->stats.runtime_overall / dispatch_count,
	       rqwait_overall,
	       rqwait_overall / dispatch_count,
	       proc->stats.preempt_high_prio,
	       proc->stats.preempt_slice_end);
}

static void print_sched_stats(void)
{
	struct proc *proc;

	spin_lock(&printstats_lock);

	prints("%lu ", PS->sys_ticks);
	print_proc_stats(current, PS->current_prio);
	for (int i = MIN_PRIO; i <= MAX_PRIO; i++) {
		list_for_each(&PS->rq_active->head[i], proc, pnode)
			print_proc_stats(proc, i);
		list_for_each(&PS->rq_expired->head[i], proc, pnode)
			print_proc_stats(proc, i);
	}
	list_for_each(&PS->just_queued, proc, pnode)
		print_proc_stats(proc, DEFAULT_PRIO);
	prints("\n");

	spin_unlock(&printstats_lock);
}
#endif /* SCHED_STATS */

/*
 * @@@ Tracing: @@@
 */

#if SCHED_TRACE
static void rq_dump(struct runqueue *rq)
{
	struct proc *proc;
	const char *name;

	name = (rq == rq_active) ? "active" : "expired";
	prints("Dumping %s table:\n", name);
	for (int i = MAX_PRIO; i >= MIN_PRIO; i--)
		if (!list_empty(&rq->head[i]))
			list_for_each(&rq->head[i], proc, pnode)
				prints("%lu ", proc->pid);
	prints("\n");
}
#endif	/* !SCHED_TRACE */


/*
 * @@@ Testing: @@@
 */

#if SCHED_TESTS
#include <vga.h>

void __no_return loop_print(char ch, int color)
{
	while (true) {
		putc_colored(ch, color);
		for (int i = 0; i < 0xffff; i++)
			cpu_pause();
	}
}

static void __no_return test0(void) { loop_print('A', VGA_LIGHT_BLUE); }
static void __no_return test1(void) { loop_print('B', VGA_LIGHT_BLUE); }
static void __no_return test2(void) { loop_print('C', VGA_LIGHT_BLUE); }
static void __no_return test3(void) { loop_print('D', VGA_LIGHT_CYAN); }
static void __no_return test4(void) { loop_print('E', VGA_LIGHT_CYAN); }
static void __no_return test5(void) { loop_print('F', VGA_LIGHT_CYAN); }

void sched_run_tests(void)
{
	for (int i = 0; i < 20; i++) {
		kthread_create(test0);
		kthread_create(test1);
		kthread_create(test2);
		kthread_create(test3);
		kthread_create(test4);
		kthread_create(test5);
	}
}
#endif /* SCHED_TESTS */
