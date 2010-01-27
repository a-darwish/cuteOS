#ifndef _KERNEL_H
#define _KERNEL_H

/*
 * Common methods and definitions
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdarg.h>
#include <stdint.h>

/*
 * C99
 */
#define NULL	((void *)0)
enum {
	true  = 1,
	false = 0,
};

/*
 * GCC extensions shorthands
 */

#define __packed	__attribute__((packed))
#define __unused	__attribute__((__unused__))
#define __used		__attribute__((__used__))
#define __error(msg)	__attribute__((error(msg)))
#define __likely(exp)	__builtin_expect((exp), 1)
#define __unlikely(exp)	__builtin_expect((exp), 0)
#define __no_inline	__attribute__((noinline))

/* Mark the 'always_inline' attributed function as C99
 * 'inline' cause the attribute by itself is worthless.
 * It's "for functions declared inline" -- GCC manual */
#define __always_inline	inline __attribute__((always_inline))

/* Suppress GCC's "var used uninitialized" */
#define __uninitialized(x)	(x) = (x)

/*
 * Semi type-safe min macro using GNU extensions
 */
#define min(x, y) ({		    \
        typeof(x) _m1 = (x);	    \
	typeof(y) _m2 = (y);	    \
	(void) (&_m1 == &_m2);	    \
	_m1 < _m2 ? _m1 : _m2; })

#define offsetof(type, elem)	((uint64_t) &((type *) 0)->elem)

#define ARRAY_SIZE(array)	(sizeof(array) / sizeof(array[0]))

/*
 * In a binary system, a value 'x' is said to be n-byte
 * aligned when 'n' is a power of the radix 2, and x is
 * a multiple of 'n' bytes.
 *
 * A n-byte-aligned value has at least a log2n number of
 * least-significant zeroes.
 *
 * Return given x value 'n'-aligned.
 *
 * Using two's complement, rounding = (x & (typeof(x))-n)
 */
#define round_down(x, n)	(x & ~(typeof(x))(n - 1))
#define round_up(x, n)		(((x - 1) | (typeof(x))(n - 1)) + 1)

/*
 * Check if given 'x' value is 'n'-aligned
 * 'n' must be power of the radix 2; see round_up()
 */
#define __mask(x, n)		((typeof(x))((n) - 1))
#define is_aligned(x, n)	(((x) & __mask(x, n)) == 0)

/*
 * Main kernel print methods
 */
int vsnprintf(char *buf, int size, const char *fmt, va_list args);
void printk(const char *fmt, ...);
void putc(char c);

/*
 * Critical failures
 */
void panic(const char *fmt, ...);

#define assert(condition)					\
	do {							\
		if (__unlikely(!(condition)))			\
			panic("%s:%d - !(" #condition ")\n",	\
			      __FILE__, __LINE__);		\
	} while (0);

/*
 * Compile-time assert for constant-folded expressions
 *
 * We would've been better using GCC's error(msg) attribute,
 * but it doesn't work with my current GCC build :(.
 */
void __unused __undefined_method(void);
#define compiler_assert(condition)				\
	do {							\
		if (!(condition))				\
			__undefined_method();			\
	} while (0);

#endif /* _KERNEL_H */
