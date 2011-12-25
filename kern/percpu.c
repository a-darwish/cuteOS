/*
 * Per-CPU Data Area Init
 *
 * Copyright (C) 2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The real deal is in the headers: this is only init & test-cases.
 */

#include <kernel.h>
#include <x86.h>
#include <percpu.h>
#include <sched.h>
#include <tests.h>

/*
 * Initialize the calling CPU's per-CPU area.
 */
void percpu_area_init(enum cpu_type t)
{
	if (t == BOOTSTRAP)
		set_gs(BOOTSTRAP_PERCPU_AREA);

	/* else, we're on a secondary core where %gs
	 * is already set-up by the trampoline. */

	percpu_set(self, get_gs());
	sched_percpu_area_init();
}


/*
 * Below code is NOT meant to run: it will corrupt kernel state!
 *
 * Only use it for inspecting per-CPU accessors assembly output
 * at different GCC optimization levels, especially -O3 and -Os.
 */
#if	PERCPU_TESTS

#include <string.h>
#include <stdint.h>

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

	memcpy(str1, str2, 100);
	memcpy(str1+100, str2+100, 100);

	if (current->pid == 0)
		current->pid = 0x55;
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

/*
 * Blackbox testing of the per-CPU accessors.
 */
void percpu_run_tests(void)
{
	int id;
	uintptr_t self, gs;

	id = percpu_get(apic_id);
	self = percpu_get(self);
	gs = get_gs();

	printk("_PerCPU#%d: area address: self = 0x%lx, %%gs = 0x%lx\n",
		id, self, gs);
	if (self != gs)
		panic("_PerCPU#%d: self reference '0x%lx' != %%gs", id, self);

	*percpu_addr(x64) = 0x6464646464646464;
	percpu_set(x32, 0x32323232);
	percpu_set(x16, 0x1616);
	percpu_set(x8, 0x8);

	printk("_PerCPU#%d: x64 address = 0x%lx, val = 0x%lx\n",
	       id, percpu_addr(x64), percpu_get(x64));
	printk("_PerCPU#%d: x32 address = 0x%lx, val = 0x%x\n",
	       id, percpu_addr(x32), percpu_get(x32));
	printk("_PerCPU#%d: x16 address = 0x%lx, val = 0x%x\n",
	       id, percpu_addr(x16), percpu_get(x16));
	printk("_PerCPU#%d: x8  address = 0x%lx, val = 0x%x\n",
	       id, percpu_addr(x8 ), percpu_get(x8 ));
}

#endif /* PERCPU_TESTS */
