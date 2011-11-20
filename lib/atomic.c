/*
 * Atomic accessors
 *
 * Copyright (C) 2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Compiler memory barriers are added to the "test_and_{set,add,..}"
 * accessors: they're mostly used in locking loops where global memory
 * state is expected to change by a different thread.
 */

#include <kernel.h>
#include <stdint.h>
#include <atomic.h>

/*
 * Atomically execute:
 *	old = *val & 0x1; *val |= 0x1;
 *	return old;
 */
uint8_t atomic_bit_test_and_set(uint32_t *val)
{
	uint8_t ret;

	asm volatile (
		"LOCK bts $0, %0;"
		"     setc    %1;"
		: "+m" (*val), "=qm" (ret)
		:
		: "cc", "memory");

	return ret;
}
