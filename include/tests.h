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

#define		STRING_TESTS		0
#define		PRINTK_TESTS		0
#define		VM_TESTS		0
#define		PAGEALLOC_TESTS		0
#define		KMALLOC_TESTS		0
#define		PIT_TESTS		0
#define		APIC_TESTS		0
#define		SCHED_TESTS		0

#if	(SCHED_TESTS == 1) && (PIT_TESTS == 1)

#error	"Can't run scheduler test cases with the PIT ones:\
	PIT tests invalidate scheduler timer ticks setup"

#endif

#endif /* _TESTS_H */
