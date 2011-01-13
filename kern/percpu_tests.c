/*
 * Per-CPU Data Area test-cases
 *
 * Copyright (C) 2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * NOTE! This code is NOT meant to run: it will corrupt kernel state.
 *
 * Only use this for inspecting the per-CPU accessors assembly output at
 * different GCC optimization levels, especially -O3 and -Os.
 */

#include <percpu.h>
#include <string.h>
#include <stdint.h>
#include <tests.h>

#if PERCPU_TESTS

void percpu_inspect_current(void);
uint64_t percpu_inspect_size(void);
uint64_t percpu_inspect_caching(void);
uint64_t percpu_inspect_order(void);

/*
 * We tell GCC to cache 'current' in registers as much as possible.
 * Check the disassembly of this code to see if it really does so.
 */
static char *str1, *str2;
void percpu_inspect_current(void)
{
	current->pid = 0x00;
	current->state = 0x11;
	current->runtime = 0x22;
	current->enter_runqueue_ts = 0x33;
	current->spinlock_count = 0x44;

	memcpy(str1, str2, 100);
	memcpy(str1+100, str2+100, 100);

	if (current->pid == 0) {
		current->pid = 0x55;
		current->spinlock_count = 0x66;
	}
}

/*
 * Check the disassembly output of below commands and assure
 * that GCC sizes the resuling opcodes correctly ('mov/q/w/l/b').
 */
uint64_t percpu_inspect_size(void)
{
	uint64_t x64;

	x64 = percpu_get(x64);
	x64 = percpu_get(x32);
	x64 = percpu_get(x16);
	x64 = percpu_get(x8);

	percpu_set(x64, 0x1111);
	percpu_set(x32, 5);
	percpu_set(x16, 0xdead);
	percpu_set(x8, 0xff);

	return x64;
}

/*
 * Check the disassembly and assure that GCC does not cache a
 * read result in registers for other reads.
 */
uint64_t percpu_inspect_caching(void)
{
	uint32_t x32;

	x32 = percpu_get(x32);
	x32 = percpu_get(x32);
	x32 = percpu_get(x32);
	x32 = percpu_get(x32);
	x32 = percpu_get(x32);

	return x32;
}

/*
 * Check below code disassembly to assure that GCC does not
 * re-order data-dependent reads and writes.
 */
static uint64_t u, v, w, x, y, z;
uint64_t percpu_inspect_order(void)
{
	u = percpu_get(x64);
	percpu_set(x64, 0xdead);
	v = percpu_get(x64);
	percpu_set(x64, 0xbeef);
	w = percpu_get(x64);
	percpu_set(x64, 0xcafe);
	x = percpu_get(x64);
	percpu_set(x64, 0xbabe);
	y = percpu_get(x64);
	z = percpu_get(x16);

	return z;
}

#endif /* PERCPU_TESTS */
