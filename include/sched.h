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

/*
 * Threads priority boundaries
 */
#define		MIN_PRIO		0
#define		MAX_PRIO		19

struct proc;

enum cpu_type {
	BOOTSTRAP,
	SECONDARY,
};
void schedulify_this_code_path(enum cpu_type);

void sched_init(void);
struct proc *sched_tick(void);

void sched_enqueue(struct proc *);
void kthread_create(void (*func)(void));

#if	SCHED_TESTS
void sched_run_tests(void);
#else
static void __unused sched_run_tests(void) { }
#endif

#endif /* _SCHED_H */
