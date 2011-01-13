#ifndef _SCHED_CONF_H
#define _SCHED_CONF_H

/*
 * Scheduler Configuration
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * COM1: log the main scheduler operations?
 */
#define		SCHED_TRACE		0

/*
 * COM1: print scheduling statistics?
 */
#define		SCHED_STATS		0

/*
 * COM1: print stats (if enabled) each @SCHED_STATS_RATE ticks.
 */
#define		SCHED_STATS_RATE	HZ

struct runqueue;

#if SCHED_TRACE
#define	sched_dbg(fmt, ...)		prints(fmt, ##__VA_ARGS__)
static void rq_dump(struct runqueue *rq);
#else
#define sched_dbg(fmt, ...)		{ }
static void rq_dump(struct runqueue __unused *rq) { }
#endif /* SCHED_TRACE */

#if SCHED_STATS
static void print_sched_stats(void);
#else
static void print_sched_stats(void)	{ }
#endif /* SCHED_STATS */

#endif /* _SCHED_CONF_H */
