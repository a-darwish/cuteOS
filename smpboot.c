/*
 * Multiple-Processor (MP) Initialization
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <smpboot.h>
#include <segment.h>
#include <string.h>
#include <apic.h>

/*
 * Processor descriptors. The number of CPUs and their APIC IDs
 * is gathered from either MP or ACPI MADT tables.
 */
int nr_cpus;
struct cpu_desc cpu_descs[CPUS_MAX];

void smpboot_init(void)
{
	printk("SMP: %d CPUs found\n", nr_cpus);
}
