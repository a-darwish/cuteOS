#ifndef _SCHED_H
#define _SCHED_H

/*
 * Thread Scheduling
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <tests.h>
#include <stdint.h>

/*
 * System clock ticks per second
 */
#define HZ			250

/*
 * A thread round-robin slice in number of ticks
 */
#define RR_INTERVAL		2

/*
 * Threads priority boundaries
 */
#define		MIN_PRIO	0
#define		MAX_PRIO	19

/*
 * Priorities only affect latency, not CPU usage.
 */
#define DEFAULT_PRIO		10
#define UNDEF_PRIO		-1
#define VALID_PRIO(prio)			\
	(MIN_PRIO <= (prio) && (prio) <= MAX_PRIO)

/*
 * The runqueue: a bucket array holding heads of
 * the lists connecting threads of equal priority.
 */
struct runqueue {
	struct list_node head[MAX_PRIO + 1];
};

static inline void rq_init(struct runqueue *rq)
{
	for (int i = MIN_PRIO; i <= MAX_PRIO; i++)
		list_init(&rq->head[i]);
}

static inline int rq_get_highest_prio(struct runqueue *rq)
{
	for (int i = MAX_PRIO; i >= MIN_PRIO; i--)
		if (!list_empty(&rq->head[i]))
			return i;

	return UNDEF_PRIO;
}

static inline bool rq_empty(struct runqueue *rq)
{
	if (rq_get_highest_prio(rq) == UNDEF_PRIO)
		return true;

	return false;
}

/*
 * Per-CPU area scheduling elements
 *
 * NOTE! Always disable interrupts before accessing any
 * of these elements outside of the timer IRQ context.
 */
struct percpu_sched {
	volatile clock_t sys_ticks;

	struct runqueue rrq[2];
	struct runqueue *rq_active;
	struct runqueue *rq_expired;

	struct list_node just_queued;

	int current_prio;

	int just_queued_turn;
};

struct proc;
enum cpu_type {
	BOOTSTRAP,
	SECONDARY,
};

void sched_percpu_area_init(void);
void schedulify_this_code_path(enum cpu_type);
void sched_init(void);

void sched_enqueue(struct proc *);
struct proc *sched_tick(void);	/* Avoid GCC warning */

void kthread_create(void (* __no_return func)(void));
uint64_t kthread_alloc_pid(void);

#if	SCHED_TESTS
void sched_run_tests(void);
void smpboot_run_tests(void);
void __no_return loop_print(char ch, int color);
#else
static void __unused sched_run_tests(void) { }
static void __unused smpboot_run_tests(void) { }
#endif

#endif /* _SCHED_H */
