/*
 * Local APIC configuration
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <segment.h>
#include <msr.h>
#include <apic.h>

void *apic_baseaddr;

void apic_init(void)
{
	uint64_t val;

	apic_baseaddr = (char *)APIC_VRBASE;
	msr_apicbase_setaddr(APIC_PHBASE);
	msr_apicbase_enable();

	val = apic_read(APIC_SPIV);
	val |= APIC_SPIV_ENABLE;
	apic_write(APIC_SPIV, val);

	printk("APIC: bootstrap core local apic enabled\n");
}
