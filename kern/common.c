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
 * FIXME: fix the SMP case once primitive SMP
 * code is ready
 */
void __no_return panic(const char *fmt, ...)
{
	va_list args;
	char buf[256];
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);

	buf[n] = 0;
	printk("\nPANIC: %s", buf);

	local_irq_disable();
	halt();
}
