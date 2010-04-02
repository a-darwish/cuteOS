#ifndef _SCHED_H
#define _SCHED_H

/*
 * Scheduler and kernel threads
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <tests.h>
#include <proc.h>

struct proc *schedule(void);

void sched_init(void);
void kthread_create(void (*func)(void));

#if	SCHED_TESTS

void sched_run_tests(void);

#else

static void __unused sched_run_tests(void) { }

#endif

#endif /* _SCHED_H */
