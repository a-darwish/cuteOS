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

/*
 * Atomically execute:
 *	return *val++;
 */
uint64_t atomic_inc(uint64_t *val)
{
	uint64_t i = 1;

	asm volatile (
		"LOCK xaddq %0, %1"
		: "+r"(i), "+m" (*val)
		:
		: "cc");

	return i;
}


#if    ATOMIC_TESTS

void atomic_run_tests(void)
{
	printk("_Atomic: 0 -> 99 should be printed:\n");
	for (uint64_t i = 0; i < 100; atomic_inc(&i))
		printk("%d ", i);
	putc('\n');

	printk("_Atomic: 0xfffffffffffffff0 - 0xffffffffffffffff should "
	       "be printed:\n");
	for (uint64_t i = -0x10; i != 0; atomic_inc(&i))
		printk("0x%lx ", i);
	putc('\n');
}

#endif /* ATOMIC_TESTS */
