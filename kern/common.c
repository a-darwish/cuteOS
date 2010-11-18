/*
 * Common kernel methods
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <idt.h>

/*
 * Protected (for now) by disabling local interrupts.
 *
 * Disabling interrupts is an _omnipotent_ locking mechanism
 * from a single-CPU perspective.
 */
static char buf[256];

/*
 * Quickly disable system interrupts upon entrance! Now the
 * kernel is in an inconsistent state, just gracefully stop
 * the machine and halt.
 *
 * An interesting case faced from not disabling interrupts
 * early on (disabling them at the function end instead) was
 * having other threads getting scheduled between printk()
 * and disabling interrupts, scrolling-away the caller panic
 * message and losing information FOREVER.
 *
 * FIXME: Handle the multi-core case. Worry about this when
 * we're able to schedule threads on secondary CPUs.
 */
void __no_return panic(const char *fmt, ...)
{
	va_list args;
	int n;

	/* NOTE! Do not put anything above this */
	local_irq_disable();

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);

	buf[n] = 0;
	printk("\nPANIC: %s", buf);

	halt();
}
