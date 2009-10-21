#ifndef KERNEL_H
#define KERNEL_H

/*
 * Common methods and definitions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdarg.h>

/*
 * C99
 */
#define NULL ((void *)0)
enum {
	true = 1,
	false = 0,
};

/*
 * GCC extensions shorthands
 */
#define __unused	__attribute__((__unused__))
#define __used		__attribute__((__used__))

/*
 * Semi type-safe min macro using GNU extensions
 */
#define min(x, y) ({		    \
        typeof(x) _m1 = (x);	    \
	typeof(y) _m2 = (y);	    \
	(void) (&_m1 == &_m2);	    \
	_m1 < _m2 ? _m1 : _m2; })

/*
 * Main kernel print methods
 */
int vsnprintf(char *buf, int size, const char *fmt, va_list args);
void printk(const char *fmt, ...);

/*
 * Critical failures
 */
void panic(const char *fmt, ...);
#define assert(condition)					\
	do {							\
		if (!(condition))				\
			panic("%s:%d - !(" #condition ")\n",	\
			      __FILE__, __LINE__);		\
	} while (0);

#endif
