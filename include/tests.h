/*
 * Testcases switches
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#ifndef _TESTS_H
#define _TESTS_H

#define		LIST_TESTS		0	/* Linked stack/queue tests */
#define		UNROLLED_TESTS		0	/* Unrolled linked list tests */
#define		STRING_TESTS		0	/* Optimized string methods */
#define		PRINTK_TESTS		0	/* printk(fmt, ...) */
#define		PRINTS_TESTS		0	/* prints(fmt, ...) */
#define		VM_TESTS		0	/* Kernel VM mappings */
#define		PAGEALLOC_TESTS		0	/* Page allocator */
#define		KMALLOC_TESTS		0	/* Dynamic memory allocator */
#define		PIT_TESTS		0	/* PIT timer tests */
#define		APIC_TESTS		0	/* Local APIC timer and IPI */
#define		PERCPU_TESTS		0	/* Per-CPU Data Area tests */
#define		ATOMIC_TESTS		0	/* Atomic-accessors */
#define		SCHED_TESTS		0	/* Scheduler tests */
#define		EXT2_TESTS		0	/* File System tests */
#define		FILE_TESTS		0	/* Unix file operations */

#if	(SCHED_TESTS == 1) && (PIT_TESTS == 1)
#error	Cannot run scheduler test cases with the PIT ones:\
	PIT tests invalidate scheduler timer ticks setup.
#endif


#if	(PRINTK_TESTS == 1) && (PRINTS_TESTS == 1)
#error	Cannot simultaneously run printk() VGA tests\
	with the serial-port ones.
#endif


#endif /* _TESTS_H */
